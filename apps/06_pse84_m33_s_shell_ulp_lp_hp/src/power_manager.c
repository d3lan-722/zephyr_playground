/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * HP / LP / ULP switcher for PSE84 CM33-Secure.
 *
 * Follows the golden pattern from Infineon's
 * `mtb-example-psoc-edge-secure-power-management/proj_cm33_s/main.c`:
 * three @c cy_stc_syspm_callback_t hooks (one per target mode) are
 * registered with @c Cy_SysPm_RegisterCallback so that
 * @c Cy_SysPm_SystemEnter{Hp,Lp,Ulp}() automatically retunes PLL0
 * around the voltage step. Doing anything else -- in particular,
 * scaling the CPU only via @c Cy_SysClk_ClkHfSetDivider while
 * leaving PLL0 running at its HP frequency -- hangs the CPU on
 * LP->ULP because the 400 MHz PLL cannot sustain lock at the ULP
 * SoC voltage of 0.7 V. The CPU then loses its instruction clock
 * mid-fetch and the whole system freezes with the RRAM controller
 * step being the visible "last thing that ran" (observed:
 * `<T:LP2ULP:3:before-RRAM_ULP>` was the final marker before
 * introducing this callback-based path).
 *
 * PLL0 target frequencies (per @ref DPLL_FREQ_HP_HZ etc. -- the DT
 * overlay pins CLK_HF0 to DPLL_LP0 / 1, so the DPLL output IS the
 * CM33 core frequency):
 *   HP  : 200 MHz         intermediate LP<->HP  : 75 MHz
 *   LP  :  80 MHz         intermediate LP<->ULP : 41 MHz
 *   ULP :  50 MHz
 *
 * The intermediate frequency is set in the BEFORE_TRANSITION phase
 * of the target-mode callback; the final frequency is set in
 * AFTER_TRANSITION together with @c Cy_RRAM_SetVoltageMode().
 * This ordering matters:
 *
 *   Up-transitions   (ULP->LP, LP->HP, ULP->HP): voltage rises
 *                    first (@c Cy_SysPm_SystemEnter* handles it),
 *                    then PLL is stepped up in AFTER_TRANSITION.
 *   Down-transitions (HP->LP, HP->ULP, LP->ULP): PLL is stepped
 *                    down to the safe intermediate in
 *                    BEFORE_TRANSITION so that when the voltage
 *                    then drops the PLL is already inside the new
 *                    mode's frequency envelope; AFTER_TRANSITION
 *                    then sets the final (per-mode) frequency.
 */

#include "power_manager.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cy_pdl.h"

#include "diag.h"

/* ------------------------------------------------------------------
 * PLL0 target frequencies -- CM33-oriented per AN237976 Table 5.
 *
 * The DPLL_LP0 input on this board is IHO = 50 MHz. This is NOT the
 * 24 MHz value used by the Infineon mtb-example -- their example
 * targets a different reference. Confirmed against the board DT:
 *   dpll_lp0 { FB=28 REF=7 OUT=1  clock-frequency = 200 MHz }
 *   -> input = 200 * 7 / 28 = 50 MHz
 * Using the wrong 24 MHz value here previously produced PLL outputs
 * ~2x the intended target -- observed with the runtime clock probe:
 *   `lp`  target 80 MHz -> measured DPLL_LP0 = 166.666 MHz
 *                       (50 * 10/3, the FB/REF/OUT that
 *                        Cy_SysClk_PllConfigure picks for 80 MHz
 *                        assuming 24 MHz input)
 *   `ulp` target 50 MHz -> Cy_SysClk_PllEnable returned 0x004a0002
 *                       and the PLL stayed bypassed (asked to lock
 *                       at 50*25/3 = 416 MHz at 0.7 V, timed out).
 *
 * The DT overlay pins CLK_HF0 to DPLL_LP0 / 1, so DPLL frequency IS
 * the CM33 core frequency. Targets are the AN237976 Table 5 CM33-max
 * values directly:
 *   HP  : DPLL 200 MHz -> HF0 200 MHz  (CM33 HP  max)
 *   LP  : DPLL  80 MHz -> HF0  80 MHz  (CM33 LP  max)
 *   ULP : DPLL  50 MHz -> HF0  50 MHz  (CM33 ULP max)
 *
 * The 50 MHz ULP target is at the DPLL_LP0's specified min-frequency
 * lock envelope; no PLL lock issues have been observed at 0.7 V core
 * voltage since the baseline was moved down from 400 MHz to 200 MHz
 * (see PLAN_dual_dvfs.md for the history).
 *
 * The vendor-tested transition intermediates below are absolute
 * DPLL-side thresholds tied to the SRAM/RRAM trim window at the
 * target voltage. HP <-> LP: 75 MHz DPLL -> HF0 75 MHz, well
 * inside the "reduce by 82% below 400" rule (implies HF0 <= ~72
 * MHz -- 75 is a hair over, matches the Infineon mtb-example).
 * LP <-> ULP: 41 MHz DPLL -> HF0 41 MHz, inside the "reduce by 66%
 * below 140" rule (implies HF0 <= ~47 MHz).
 * ------------------------------------------------------------------ */
#define DPLL_INPUT_FREQ_HZ (50000000U) /* IHO */
#define DPLL_ENABLE_TIMEOUT_MS (10000U)

#define DPLL_FREQ_HP_HZ (200000000U) /* HF0 /1 -> CM33 200 MHz */
#define DPLL_FREQ_LP_HZ (80000000U)  /* HF0 /1 -> CM33  80 MHz */
#define DPLL_FREQ_ULP_HZ (50000000U) /* HF0 /1 -> CM33  50 MHz */

#define DPLL_FREQ_INTERMEDIATE_LP_HZ (75000000U)  /* HP <-> LP transitions */
#define DPLL_FREQ_INTERMEDIATE_ULP_HZ (41000000U) /* LP <-> ULP transitions */

/*
 * Raw-SCB2 step markers. Route diagnostic markers through diag_trace()
 * so a hang inside the PDL / callback path leaves the last-known step
 * visible on the console even if the Zephyr UART driver is broken by
 * the transition. See src/diag.h for the alive/dead diagnosis matrix.
 */
#define TRACE(msg) diag_trace("<T:" msg ">\n")

/** Current active power mode. Boot leaves the SoC in HP. */
static pm_mode_t s_current_mode = PM_MODE_HP;

/**
 * @brief Sentinel value for @ref s_last_pll_enable_st meaning
 *        "PllEnable was not reached this attempt because
 *         PllConfigure failed first".
 */
#define PM_PLL_ENABLE_NOT_REACHED 0xFFFFFFFFu

/**
 * Latched status from the most recent pm_pll_reconfigure() call, so
 * pm_clock_probe() can print "did the PLL actually re-lock?" without
 * emitting bytes while the SCB baud is transient. Set inside
 * pm_pll_reconfigure() immediately after Cy_SysClk_PllConfigure /
 * PllEnable return; printed later once the SCB is retuned and the
 * console is safe to write.
 *
 * @c s_last_pll_target_hz == 0 means "no PLL retune since boot"
 * (pm_init() does not touch the PLL). See
 * @ref PM_PLL_ENABLE_NOT_REACHED for the s_last_pll_enable_st
 * sentinel.
 */
static uint32_t s_last_pll_target_hz;
static uint32_t s_last_pll_configure_st;
static uint32_t s_last_pll_enable_st = PM_PLL_ENABLE_NOT_REACHED;

/**
 * Console UART handle. The SCB baud divider it caches at boot is
 * only valid for the CLK_HF10 frequency that was live at that time,
 * so we retune it after every mode change (see
 * @ref pm_reconfigure_console_uart).
 */
static const struct device *const console_uart =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/**
 * @brief Re-run @c uart_configure on the console SCB so the Infineon
 *        ifx_cat1 driver recomputes its baud divider against the
 *        currently-live CLK_HF10 frequency.
 *
 * Called from @ref pm_switch_to after every successful mode change
 * (which retuned DPLL_LP0, therefore CLK_HF10). No-op if the console
 * device is not yet ready or its configuration cannot be read back.
 */
static void pm_reconfigure_console_uart(void)
{
	struct uart_config cfg;

	if (!device_is_ready(console_uart)) {
		return;
	}
	if (uart_config_get(console_uart, &cfg) != 0) {
		return;
	}
	(void)uart_configure(console_uart, &cfg);
}

/* ------------------------------------------------------------------
 * PLL retune helper -- centralises Disable / Configure / Enable so
 * the three callbacks below stay readable. During the Disable /
 * Enable window CLK_HF0 falls back to its path-mux fallback (the IMO
 * on PSE84 boards). This is why calling this from RRAM-linked code
 * is safe: the CPU keeps fetching, just at IMO speed, until PLL is
 * relocked.
 *
 * Console-integrity contract
 * ---------------------------
 * The CM33 shell UART (SCB2) is peri-group (0,1) which sources CLK_HF10,
 * which in turn is fed from DPLL_LP0 via the board's path_mux. So
 * DPLL_LP0 IS the SCB2 peripheral clock, and every call into this
 * function invalidates the SCB baud divider that was calibrated for
 * the previous DPLL frequency.
 *
 * Two guarantees are required for the console to stay coherent:
 *
 *   1. Nothing may be in-flight when Cy_SysClk_PllDisable runs. Any
 *      byte still sitting in the SCB2 TX FIFO or shift register will
 *      be clocked out at whatever transient bit-time the SCB sees
 *      during the disable/enable window, producing the garbled
 *      mid-marker output that used to appear during transitions.
 *      We drain the FIFO+SR via @ref diag_trace_flush before
 *      touching the PLL.
 *   2. The SCB baud divider must be recomputed against the new
 *      CLK_HF10 before the next byte is emitted. That retune is
 *      done by @ref pm_reconfigure_console_uart, but NOT here --
 *      calling it from this function means it runs inside the
 *      SysPm critical section (Cy_SysLib_EnterCriticalSection was
 *      taken by Cy_SysPm_SystemEnter{...}), and empirically the
 *      Zephyr ifx_cat1 UART driver's uart_configure() misbehaves in
 *      that context: subsequent bytes on the shell were garbled or
 *      the console froze entirely. Instead the retune runs once at
 *      the END of pm_switch_to() with interrupts re-enabled, which
 *      is reliable.
 *
 * Consequence: NO diagnostic bytes may be emitted between this
 * function's return and pm_switch_to()'s trailing retune -- the
 * baud divider is stale in that window. All TRACE() calls inside
 * the SysPm callbacks have therefore been removed.
 * ------------------------------------------------------------------ */
static cy_en_syspm_status_t pm_pll_reconfigure(uint32_t freq_hz)
{
	cy_stc_pll_config_t cfg = {
	    .inputFreq = DPLL_INPUT_FREQ_HZ,
	    .outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,
	    .outputFreq = freq_hz,
	};
	cy_en_sysclk_status_t st;

	/* Guarantee 1: nothing in flight when PLL drops. */
	diag_trace_flush();

	Cy_SysClk_PllDisable(SRSS_DPLL_LP_0_PATH_NUM);

	st = Cy_SysClk_PllConfigure(SRSS_DPLL_LP_0_PATH_NUM, &cfg);
	if (st != CY_SYSCLK_SUCCESS) {
		s_last_pll_target_hz = freq_hz;
		s_last_pll_configure_st = (uint32_t)st;
		s_last_pll_enable_st = PM_PLL_ENABLE_NOT_REACHED;
		return CY_SYSPM_FAIL;
	}
	st = Cy_SysClk_PllEnable(SRSS_DPLL_LP_0_PATH_NUM,
				 DPLL_ENABLE_TIMEOUT_MS);
	s_last_pll_target_hz = freq_hz;
	s_last_pll_configure_st = 0u;
	s_last_pll_enable_st = (uint32_t)st;
	if (st != CY_SYSCLK_SUCCESS) {
		return CY_SYSPM_FAIL;
	}
	/* Guarantee 2 is intentionally NOT done here -- see contract
	 * comment above. pm_switch_to() will retune the SCB after
	 * Cy_SysPm_SystemEnter* returns and IRQs are re-enabled. */
	return CY_SYSPM_SUCCESS;
}

/* ------------------------------------------------------------------
 * SysPm callbacks. Structure verbatim from mtb-example.
 * @c Cy_SysPm_ExecuteCallback() (called from
 * @c Cy_SysPm_SystemEnter{...}) invokes each in CHECK_READY /
 * BEFORE_TRANSITION / AFTER_TRANSITION phases; we only act in
 * BEFORE and AFTER.
 * ------------------------------------------------------------------ */

static cy_en_syspm_status_t
pm_syspm_hp_cb(cy_stc_syspm_callback_params_t *params,
	       cy_en_syspm_callback_mode_t mode)
{
	ARG_UNUSED(params);

	if (mode == CY_SYSPM_BEFORE_TRANSITION) {
		/* Any -> HP: voltage is about to rise. Take PLL to the
		 * safe intermediate (75 MHz) which is inside both the
		 * source (LP or ULP) and target (HP) envelopes. */
		return pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_LP_HZ);
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		/* Now at HP voltage: RRAM to HP timings, then PLL to
		 * final HP frequency (200 MHz -- CM33 HP spec max). */
		Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_HP);
		return pm_pll_reconfigure(DPLL_FREQ_HP_HZ);
	}
	return CY_SYSPM_SUCCESS;
}

static cy_en_syspm_status_t
pm_syspm_lp_cb(cy_stc_syspm_callback_params_t *params,
	       cy_en_syspm_callback_mode_t mode)
{
	ARG_UNUSED(params);

	if (mode == CY_SYSPM_BEFORE_TRANSITION) {
		if (Cy_SysPm_IsSystemUlp()) {
			/* ULP -> LP up-transition: PLL to ULP-safe 41 MHz
			 * first so the still-running PLL survives the
			 * voltage step (voltage is about to rise, but PLL
			 * needs to be under the ULP ceiling while EnterLp
			 * runs the SRAM-trim / core-buck sequence). */
			return pm_pll_reconfigure(
			    DPLL_FREQ_INTERMEDIATE_ULP_HZ);
		}
		/* HP -> LP down-transition: PLL to 75 MHz -- inside the
		 * LP envelope so the CPU keeps fetching once EnterLp
		 * drops core voltage. */
		return pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_LP_HZ);
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		/* Now at LP voltage: RRAM to LP timings, then PLL to
		 * final LP frequency (80 MHz -- CM33 LP spec max). */
		Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_LP);
		return pm_pll_reconfigure(DPLL_FREQ_LP_HZ);
	}
	return CY_SYSPM_SUCCESS;
}

static cy_en_syspm_status_t
pm_syspm_ulp_cb(cy_stc_syspm_callback_params_t *params,
		cy_en_syspm_callback_mode_t mode)
{
	ARG_UNUSED(params);

	if (mode == CY_SYSPM_BEFORE_TRANSITION) {
		/* Any -> ULP down-transition: PLL must be inside the ULP
		 * envelope BEFORE the voltage drops or the PLL falls out
		 * of lock and the CPU loses its clock. 41 MHz is the
		 * vendor-tested safe transition value. */
		return pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_ULP_HZ);
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_ULP);
		return pm_pll_reconfigure(DPLL_FREQ_ULP_HZ);
	}
	return CY_SYSPM_SUCCESS;
}

static cy_stc_syspm_callback_params_t pm_hp_params = {NULL, NULL};
static cy_stc_syspm_callback_params_t pm_lp_params = {NULL, NULL};
static cy_stc_syspm_callback_params_t pm_ulp_params = {NULL, NULL};

static cy_stc_syspm_callback_t pm_hp_cb = {
    .callback = &pm_syspm_hp_cb,
    .type = CY_SYSPM_HP,
    .skipMode = 0U,
    .callbackParams = &pm_hp_params,
    .prevItm = NULL,
    .nextItm = NULL,
    .order = 0U,
};

static cy_stc_syspm_callback_t pm_lp_cb = {
    .callback = &pm_syspm_lp_cb,
    .type = CY_SYSPM_LP,
    .skipMode = 0U,
    .callbackParams = &pm_lp_params,
    .prevItm = NULL,
    .nextItm = NULL,
    .order = 0U,
};

static cy_stc_syspm_callback_t pm_ulp_cb = {
    .callback = &pm_syspm_ulp_cb,
    .type = CY_SYSPM_ULP,
    .skipMode = 0U,
    .callbackParams = &pm_ulp_params,
    .prevItm = NULL,
    .nextItm = NULL,
    .order = 0U,
};

/* Public API ------------------------------------------------------- */

const char *pm_mode_name(pm_mode_t m)
{
	switch (m) {
	case PM_MODE_HP:
		return "HP  (200 MHz)";
	case PM_MODE_LP:
		return "LP  ( 80 MHz)";
	case PM_MODE_ULP:
		return "ULP ( 50 MHz)";
	default:
		return "UNKNOWN";
	}
}

void pm_init(void)
{
	/* Nothing to reprogram on init. The DT overlay lands us in a
	 * fully-in-spec HP state at boot:
	 *
	 *   DPLL_LP0 = 200 MHz  (overlay clock-frequency, matches
	 *                        DPLL_FREQ_HP_HZ)
	 *   CLK_HF0  = DPLL/1 = 200 MHz  (CM33 HP spec max per
	 *                                 AN237976 Table 5)
	 *   CLK_HF10 = DPLL/4 =  50 MHz  (SCB2 peripheral clock,
	 *                                 baud divider calibrated by
	 *                                 the Zephyr SCB driver at
	 *                                 init against this value)
	 *
	 * Reprogramming the PLL here even to the same target frequency
	 * would still tear down/back up DPLL_LP0 momentarily, which
	 * transiently invalidates CLK_HF10 and hence the SCB baud
	 * divider. The Zephyr UART driver would then need an
	 * uart_configure() call before the next printk to recompute
	 * its divider, and there is nothing in the Zephyr bring-up
	 * sequence that gives us a hook to insert that call between
	 * our PLL change and the next console output. So we would
	 * garble the very next printk for no benefit -- the state was
	 * already correct.
	 *
	 * pm_init() therefore just registers the three SysPm callbacks
	 * and returns. The first `lp` or `ulp` command runs its
	 * callback (which reconfigures DPLL_LP0 to 80 or 50 MHz)
	 * followed by pm_switch_to()'s trailing SCB retune, so the
	 * console stays coherent across the transition.
	 */
	s_current_mode = PM_MODE_HP;

	(void)Cy_SysPm_RegisterCallback(&pm_hp_cb);
	(void)Cy_SysPm_RegisterCallback(&pm_lp_cb);
	(void)Cy_SysPm_RegisterCallback(&pm_ulp_cb);

	SystemCoreClockUpdate();
}

pm_mode_t pm_current_mode(void) { return s_current_mode; }

/**
 * @brief Call the PDL @c Cy_SysPm_SystemEnter* entry point that
 *        matches @p target and emit the raw-SCB "switch:Enter…"
 *        marker on the wire so the last known step is visible if
 *        the CPU hangs mid-transition.
 *
 * Single-purpose helper: dispatch only, no state mutation and no
 * console retune. Keeping this separate from @ref pm_switch_to
 * lets that function focus on the surrounding orchestration
 * (marker, state update, SCB retune, probe).
 *
 * @return @c CY_SYSPM_SUCCESS on success; the PDL's status code
 *         otherwise. Returns @c CY_SYSPM_FAIL for an unknown mode
 *         so the caller can distinguish it from "hardware refused".
 */
static cy_en_syspm_status_t pm_syspm_enter(pm_mode_t target)
{
	switch (target) {
	case PM_MODE_HP:
		TRACE("switch:EnterHp");
		return Cy_SysPm_SystemEnterHp();
	case PM_MODE_LP:
		TRACE("switch:EnterLp");
		return Cy_SysPm_SystemEnterLp();
	case PM_MODE_ULP:
		TRACE("switch:EnterUlp");
		return Cy_SysPm_SystemEnterUlp();
	default:
		return CY_SYSPM_FAIL;
	}
}

int pm_switch_to(pm_mode_t target)
{
	cy_en_syspm_status_t st;

	if (target == s_current_mode) {
		return 0;
	}
	if (target != PM_MODE_HP && target != PM_MODE_LP &&
	    target != PM_MODE_ULP) {
		return -EINVAL;
	}

	st = pm_syspm_enter(target);
	if (st != CY_SYSPM_SUCCESS) {
		TRACE("switch:FAIL");
		printk("[pm] SystemEnter* failed (%d)\n", (int)st);
		return -EIO;
	}

	s_current_mode = target;
	SystemCoreClockUpdate();

	/* Retune the SCB baud divider against the new CLK_HF10. This
	 * MUST run here (with IRQs re-enabled after
	 * Cy_SysPm_SystemEnter* returned) and NOT inside
	 * pm_pll_reconfigure -- calling uart_configure() inside the
	 * SysPm critical section is empirically unsafe on this SCB
	 * driver (subsequent bytes get garbled or the console freezes
	 * entirely). The AFTER callback did not emit any diagnostic
	 * bytes, so the SCB2 TX FIFO is empty here; flush is a cheap
	 * defensive check. */
	diag_trace_flush();
	pm_reconfigure_console_uart();
	TRACE("switch:complete");

	/* Report the actual live clock frequencies so we don't have
	 * to guess from register readbacks. See pm_clock_probe(). */
	pm_clock_probe();
	return 0;
}

/* ------------------------------------------------------------------
 * Hardware clock measurement
 * --------------------------
 * The SoC has dedicated 24-bit counters that let us measure any
 * clock in the system against a fixed reference. IHO (50 MHz,
 * silicon-fixed, always running on PSE84) is the reference because
 * it is independent of the DPLL state we are trying to verify.
 *
 * IMO is NOT used: it is not present on this SoC family (there is
 * no Cy_SysClk_ImoIsEnabled() helper; the only always-on internal
 * high-frequency source is IHO). Confirmed empirically -- passing
 * CY_SYSCLK_MEAS_CLK_IMO returned 0 Hz for every measured clock
 * because counter1 (clocked by IMO) never decremented.
 *
 *   Cy_SysClk_StartClkMeasurementCounters(clock1=IHO, count1=N,
 *                                         clock2=measured)
 *     -> counter1 counts DOWN from N at IHO rate
 *     -> counter2 counts UP at the measured-clock rate
 *   Cy_SysClk_ClkMeasurementCountersDone()
 *     -> true when counter1 hits zero
 *   Cy_SysClk_ClkMeasurementCountersGetFreq(measuredClock=true,
 *                                           refClkFreq=50 MHz)
 *     -> returns measured_freq = counter2 / N * 50e6 Hz
 *
 * count1 sizing:
 *   Measurement wall time = count1 / 50 MHz.
 *   counter2 max = 2^24 - 1 = 16777215.
 *   With DPLL_LP0 at up to 400 MHz counter2 = count1 * (400/50) =
 *   count1 * 8. Choose count1 = 50000 -> wall time 1 ms, counter2
 *   max ~400000, ample headroom.
 *
 * As a safety net we also print Cy_SysClk_ClkHfGetFrequency() --
 * the PDL's computed-from-registers value. If the hardware counter
 * ever bails again, the computed value will still show up so we can
 * still see what's going on.
 * ------------------------------------------------------------------ */

#define PM_PROBE_REF_COUNT 50000u /* 1 ms at IHO 50 MHz */

/**
 * @brief Iteration cap for the polling loop that waits for a clock
 *        measurement to finish. count1 = PM_PROBE_REF_COUNT gives a
 *        1 ms wall time, so 1e6 iterations is roughly a >>100x
 *        overhead safety net -- never taken in practice, only
 *        exists to guarantee the loop can never spin forever if the
 *        counter block wedges.
 */
#define PM_MEAS_SAFETY_ITERS 1000000u

static uint32_t pm_measure_hz(cy_en_meas_clks_t measured)
{
	cy_en_sysclk_status_t st;
	uint32_t safety;

	st = Cy_SysClk_StartClkMeasurementCounters(
	    CY_SYSCLK_MEAS_CLK_IHO, PM_PROBE_REF_COUNT, measured);
	if (st != CY_SYSCLK_SUCCESS) {
		return 0u;
	}

	safety = PM_MEAS_SAFETY_ITERS;
	while (!Cy_SysClk_ClkMeasurementCountersDone() && (--safety != 0u)) {
		/* spin */
	}
	if (safety == 0u) {
		return 0u;
	}
	return Cy_SysClk_ClkMeasurementCountersGetFreq(true,
						       CY_SYSCLK_IHO_FREQ);
}

/**
 * @brief Format a Hz value as "%3u.%03u MHz (%u Hz)" via printk.
 *
 * Explicit signature so both the measured and computed values print
 * in the same format for side-by-side comparison.
 */
static void pm_print_hz(const char *label, uint32_t meas_hz, uint32_t comp_hz)
{
	printk(
	    "[clk] %s meas=%3u.%03u MHz (%9u Hz)  comp=%3u.%03u MHz (%9u Hz)\n",
	    label, meas_hz / 1000000u, (meas_hz / 1000u) % 1000u, meas_hz,
	    comp_hz / 1000000u, (comp_hz / 1000u) % 1000u, comp_hz);
}

void pm_clock_probe(void)
{
	/* Hardware-measured (via the dedicated counter block, IHO ref). */
	uint32_t m_path0 = pm_measure_hz(CY_SYSCLK_MEAS_CLK_PATH0);
	uint32_t m_hf0 = pm_measure_hz(CY_SYSCLK_MEAS_CLK_CLKHF0);
	uint32_t m_hf10 = pm_measure_hz(CY_SYSCLK_MEAS_CLK_CLKHF10);

	/* Computed from register readback (never returns 0 spuriously).
	 * Cy_SysClk_ClkPathGetFrequency(0) reports the DPLL_LP0 output
	 * as the PDL sees it. */
	uint32_t c_path0 = Cy_SysClk_ClkPathGetFrequency(0u);
	uint32_t c_hf0 = Cy_SysClk_ClkHfGetFrequency(0u);
	uint32_t c_hf10 = Cy_SysClk_ClkHfGetFrequency(10u);

	pm_print_hz("DPLL_LP0 ", m_path0, c_path0);
	pm_print_hz("CLK_HF0  ", m_hf0, c_hf0);	  /* CM33 core   */
	pm_print_hz("CLK_HF10 ", m_hf10, c_hf10); /* SCB2 peri   */

	/* Report the return code of the most recent PLL reconfigure so
	 * we can spot "the PLL never came back" failures cheaply. Zero
	 * (CY_SYSCLK_SUCCESS) is good; anything else means the PLL is
	 * likely OFF and the clock tree fell back to the IHO-based
	 * bypass source (25 MHz on HF0, 12.5 MHz on HF10 -- observed on
	 * OpenOCD dumps that showed DPLL_LP0 "OFF,unlocked" after a
	 * mode command). Values of the cy_en_sysclk_status_t enum are
	 * SUCCESS=0, INVALID_STATE=<vendor>, TIMEOUT=<vendor>, etc. */
	if (s_last_pll_target_hz == 0u) {
		printk("[pll] no retune since boot (cybsp/board default)\n");
	} else {
		printk("[pll] last target=%u Hz  Configure=0x%08x  "
		       "Enable=0x%08x\n",
		       s_last_pll_target_hz, s_last_pll_configure_st,
		       s_last_pll_enable_st);
	}
}
