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
 * PLL0 target frequencies match the mtb-example exactly:
 *   HP  : 400 MHz         intermediate LP<->HP  : 75 MHz
 *   LP  : 120 MHz         intermediate LP<->ULP : 41 MHz
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
 * Table 5 lists two frequency columns per mode: the CM33 core max
 * and the HF-clock (DPLL output) max. On this board the Zephyr DT
 * fixes `clk_hf0 { clock-div = <1> }`, so DPLL_LP0's output IS the
 * CM33 CLK_HF0 with no further division. We therefore target the
 * CM33 core column (200 / 80 / 50 MHz) directly rather than the
 * higher HF/DPLL column (400 / 140 / 50) that the Infineon
 * mtb-example uses (that example expects a /2 HF0 divider applied
 * elsewhere in its BSP, which would land CM33 at 200 MHz too).
 *
 * The vendor-tested transition intermediates are absolute
 * thresholds (not percentages) tied to the SRAM/RRAM trim window
 * at the target voltage, so they carry over unchanged:
 *   HP <-> LP  intermediate: 75 MHz  (must be <= ~72 MHz LP margin)
 *   LP <-> ULP intermediate: 41 MHz  (must be <= ~47 MHz ULP margin)
 *
 * Voltage / current expectations at the CM33 core once switched:
 *   HP  : 0.9 V core, ~<CPU-load> mA @ 200 MHz
 *   LP  : 0.8 V core, ~<CPU-load> mA @  80 MHz
 *   ULP : 0.7 V core, ~<CPU-load> mA @  50 MHz
 * ------------------------------------------------------------------ */
#define DPLL_INPUT_FREQ_HZ (24000000U)
#define DPLL_ENABLE_TIMEOUT_MS (10000U)

#define DPLL_FREQ_HP_HZ (200000000U) /* CM33 HP  max */
#define DPLL_FREQ_LP_HZ (80000000U)  /* CM33 LP  max */
#define DPLL_FREQ_ULP_HZ (50000000U) /* CM33 ULP max */

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
 * Console UART. Re-configured after every mode change so the SCB
 * UART driver recomputes its baud divider against the new peripheral
 * clock (the shell backend caches the divider it last computed).
 */
static const struct device *const console_uart =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

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
 * ------------------------------------------------------------------ */
static cy_en_syspm_status_t pm_pll_reconfigure(uint32_t freq_hz)
{
	cy_stc_pll_config_t cfg = {
	    .inputFreq = DPLL_INPUT_FREQ_HZ,
	    .outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,
	    .outputFreq = freq_hz,
	};
	cy_en_sysclk_status_t st;

	Cy_SysClk_PllDisable(SRSS_DPLL_LP_0_PATH_NUM);

	st = Cy_SysClk_PllConfigure(SRSS_DPLL_LP_0_PATH_NUM, &cfg);
	if (st != CY_SYSCLK_SUCCESS) {
		TRACE("PLL:ConfigureFail");
		return CY_SYSPM_FAIL;
	}
	st = Cy_SysClk_PllEnable(SRSS_DPLL_LP_0_PATH_NUM,
				 DPLL_ENABLE_TIMEOUT_MS);
	if (st != CY_SYSCLK_SUCCESS) {
		TRACE("PLL:EnableFail");
		return CY_SYSPM_FAIL;
	}
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
		TRACE("HP-cb:BEFORE:pll-75MHz");
		return pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_LP_HZ);
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		/* Now at HP voltage: RRAM to HP timings, then PLL to
		 * final HP frequency (200 MHz -- CM33 HP spec max). */
		TRACE("HP-cb:AFTER:rram-HP+pll-200MHz");
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
			TRACE("LP-cb:BEFORE(from-ULP):pll-41MHz");
			return pm_pll_reconfigure(
			    DPLL_FREQ_INTERMEDIATE_ULP_HZ);
		}
		/* HP -> LP down-transition: PLL to 75 MHz -- inside the
		 * LP envelope so the CPU keeps fetching once EnterLp
		 * drops core voltage. */
		TRACE("LP-cb:BEFORE(from-HP):pll-75MHz");
		return pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_LP_HZ);
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		/* Now at LP voltage: RRAM to LP timings, then PLL to
		 * final LP frequency (80 MHz -- CM33 LP spec max). */
		TRACE("LP-cb:AFTER:rram-LP+pll-80MHz");
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
		TRACE("ULP-cb:BEFORE:pll-41MHz");
		return pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_ULP_HZ);
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		TRACE("ULP-cb:AFTER:rram-ULP+pll-50MHz");
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
	/* Zephyr boot (cybsp + board DT) leaves DPLL_LP0 at the board
	 * `clock-frequency` (400 MHz on kit_pse84_eval). That is over
	 * the CM33 HP spec max of 200 MHz per AN237976 Table 5, but
	 * the CPU tolerates it AND -- crucially -- the SCB UART's
	 * baud divider was calibrated by the driver init against that
	 * 400 MHz HF tree. Reprogramming PLL0 here would immediately
	 * garble the console (the driver has no chance to re-run
	 * uart_configure between our PLL change and the next printk).
	 *
	 * So do NOT touch the PLL on init -- just register the three
	 * SysPm callbacks. The very first `lp` / `ulp` / `hp` command
	 * the user issues will run the target-mode callback, which
	 * reprograms PLL0 to the CM33-spec frequency AND triggers
	 * pm_reconfigure_console_uart() at the end of pm_switch_to(),
	 * bringing the shell UART back onto the new peripheral clock.
	 * From that command onwards HP=200/LP=80/ULP=50 MHz and every
	 * transition retunes the console cleanly.
	 *
	 * Trade-off: the *boot* HP state runs at 400 MHz until the
	 * user issues their first mode command. If you want boot HP
	 * to be 200 MHz too, you'd need to (a) call
	 * pm_pll_reconfigure(200M) here AND (b) immediately re-run
	 * uart_configure on the console -- which requires a
	 * device-ready UART before pm_init runs. Not worth it for a
	 * shell demo.
	 */
	s_current_mode = PM_MODE_HP;

	(void)Cy_SysPm_RegisterCallback(&pm_hp_cb);
	(void)Cy_SysPm_RegisterCallback(&pm_lp_cb);
	(void)Cy_SysPm_RegisterCallback(&pm_ulp_cb);

	SystemCoreClockUpdate();
}

pm_mode_t pm_current_mode(void) { return s_current_mode; }

int pm_switch_to(pm_mode_t target)
{
	cy_en_syspm_status_t st;

	if (target == s_current_mode) {
		return 0;
	}

	switch (target) {
	case PM_MODE_HP:
		TRACE("switch:EnterHp");
		st = Cy_SysPm_SystemEnterHp();
		break;
	case PM_MODE_LP:
		TRACE("switch:EnterLp");
		st = Cy_SysPm_SystemEnterLp();
		break;
	case PM_MODE_ULP:
		TRACE("switch:EnterUlp");
		st = Cy_SysPm_SystemEnterUlp();
		break;
	default:
		return -EINVAL;
	}

	if (st != CY_SYSPM_SUCCESS) {
		TRACE("switch:FAIL");
		printk("[pm] SystemEnter* failed (%d)\n", (int)st);
		return -EIO;
	}

	s_current_mode = target;
	TRACE("switch:before-SystemCoreClockUpdate");
	SystemCoreClockUpdate();

	/* SCB UART pclk just changed with the PLL retune; the driver's
	 * baud divider is now wrong for the new pclk. Re-run
	 * uart_configure so the driver recomputes it. */
	TRACE("switch:before-uart-reconfigure");
	pm_reconfigure_console_uart();
	TRACE("switch:complete");
	return 0;
}
