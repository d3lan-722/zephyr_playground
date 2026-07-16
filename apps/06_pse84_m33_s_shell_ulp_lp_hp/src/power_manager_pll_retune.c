/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief DVFS strategy: reprogram DPLL_LP0 per mode via SysPm
 *        callbacks. Selected by @c PM_STRATEGY_PLL_RETUNE.
 *
 * Three @c cy_stc_syspm_callback_t hooks (one per target mode)
 * fire on the BEFORE_TRANSITION and AFTER_TRANSITION phases of
 * @c Cy_SysPm_SystemEnter{Hp,Lp,Ulp}. BEFORE takes the PLL to a
 * SRAM-safe intermediate frequency; AFTER sets the final target
 * and retunes the RRAM controller.
 *
 * Scales all clocks derived from DPLL_LP0 with the mode -- CM33
 * lands at the AN237976 spec ceiling for every mode, peripheral
 * clocks drop with it. Cost: ~400-500 ms per transition
 * (two @c Cy_SysClk_PllEnable lock waits + a mandatory SCB baud
 * retune afterwards).
 *
 * See @c PLAN_dual_dvfs.md for the design rationale.
 */

#include "power_manager_internal.h"

#ifdef PM_STRATEGY_PLL_RETUNE

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cy_pdl.h"

#include "diag.h"
#include "pm_phase_log.h"

/* ------------------------------------------------------------------
 * DPLL_LP0 target frequencies. Input is IHO = 50 MHz (see the DT
 * overlay's dpll_lp0 { FB=28 REF=7 OUT=1 clock-frequency=200 MHz }).
 * The overlay pins CLK_HF0 to DPLL_LP0 / 1, so the DPLL output IS
 * the CM33 core frequency; targets match AN237976 Table 5 exactly.
 *
 * Intermediates are absolute DPLL-side thresholds tied to the
 * SRAM/RRAM trim window at the destination voltage. Vendor-tested;
 * see PLAN_dual_dvfs.md.
 * ------------------------------------------------------------------ */
#define DPLL_INPUT_FREQ_HZ (50000000u) /* IHO */
#define DPLL_ENABLE_TIMEOUT_MS (10000u)

#define DPLL_FREQ_HP_HZ (200000000u) /* CM33 HP  spec max */
#define DPLL_FREQ_LP_HZ (80000000u)  /* CM33 LP  spec max */
#define DPLL_FREQ_ULP_HZ (50000000u) /* CM33 ULP spec max */

#define DPLL_FREQ_INTERMEDIATE_LP_HZ (75000000u)  /* HP <-> LP */
#define DPLL_FREQ_INTERMEDIATE_ULP_HZ (41000000u) /* LP <-> ULP */

/* Sentinel for s_last_pll_enable_st meaning "PllEnable was not
 * reached this call -- PllConfigure failed first". */
#define PM_PLL_ENABLE_NOT_REACHED 0xFFFFFFFFu

/**
 * Latched status of the most recent pm_pll_reconfigure() call.
 * Set inside the SysPm critical section; printed later by
 * pm_strategy_probe_status() when the console is safe. Zero target
 * means "no retune since boot".
 */
static uint32_t s_last_pll_target_hz;
static uint32_t s_last_pll_configure_st;
static uint32_t s_last_pll_enable_st = PM_PLL_ENABLE_NOT_REACHED;

/* ------------------------------------------------------------------
 * Per-phase timing capture.
 *
 * The three natural phases of a PLL-retune transition are:
 *   pll_pre  -- BEFORE_TRANSITION cb: retune PLL down to a safe
 *               intermediate before Cy_SysPm_SystemTransition* runs.
 *   volt     -- the actual voltage step inside
 *               Cy_SysPm_SystemTransition* (analog rail change,
 *               state-machine walk).
 *   pll_post -- AFTER_TRANSITION cb: RRAM retune + PLL retune up to
 *               the final target for the new voltage mode.
 *
 * We snapshot k_cycle_get_32() at four points:
 *   t_enter_start  -- before Cy_SysPm_SystemEnter* is called
 *   s_before_end   -- last instruction of the BEFORE_TRANSITION cb
 *   s_after_start  -- first instruction of the AFTER_TRANSITION cb
 *   t_enter_end    -- after Cy_SysPm_SystemEnter* returns
 * and record the three deltas via pm_phase_log_record().
 *
 * Per-phase effective CPU rate for the cycle-to-us conversion:
 *
 *   pll_pre / pll_post: Cy_SysClk_PllDisable() switches CLK_PATH0
 *     to its bypass source before the PLL is reconfigured; on this
 *     board the DPLL_LP0 reference is IHO (50 MHz), so during the
 *     PllEnable lock wait (which dominates the phase) CLK_HF0
 *     runs at ~50 MHz. Small transients at each end where the CPU
 *     is at source / intermediate / target are negligible compared
 *     to the >100 ms lock wait, so 50 MHz is used for both phases.
 *
 *   volt: the CPU is at whatever intermediate the BEFORE callback
 *     set. Direction-dependent -- see intermediate_hz_for().
 *
 * If a strategy path skips a callback phase (never observed in
 * practice but defensive), the sentinel 0 makes pm_phase_log_print
 * emit a zero for the missing phase rather than crash.
 * ------------------------------------------------------------------ */
static uint32_t s_before_end_cyc;
static uint32_t s_after_start_cyc;

/** DPLL_LP0 rate the callback chain parks CLK_HF0 at during the
 *  Cy_SysPm_SystemTransition* voltage step, given source and target
 *  power modes. Matches the intermediate frequency picked in
 *  pm_syspm_{hp,lp,ulp}_cb BEFORE_TRANSITION. */
static uint32_t intermediate_hz_for(pm_mode_t src, pm_mode_t tgt)
{
	if (tgt == PM_MODE_HP) {
		/* pm_syspm_hp_cb BEFORE always picks LP intermediate. */
		return DPLL_FREQ_INTERMEDIATE_LP_HZ;
	}
	if (tgt == PM_MODE_ULP) {
		/* pm_syspm_ulp_cb BEFORE always picks ULP intermediate. */
		return DPLL_FREQ_INTERMEDIATE_ULP_HZ;
	}
	/* tgt == PM_MODE_LP: pm_syspm_lp_cb BEFORE branches on
	 * Cy_SysPm_IsSystemUlp(), which reflects the current source. */
	return (src == PM_MODE_ULP) ? DPLL_FREQ_INTERMEDIATE_ULP_HZ
				    : DPLL_FREQ_INTERMEDIATE_LP_HZ;
}

/**
 * @brief Reprogram DPLL_LP0 to @p freq_hz.
 *
 * Console-integrity contract (called from a SysPm callback, inside
 * the Cy_SysLib critical section):
 *   - Drain SCB2 TX FIFO before Cy_SysClk_PllDisable so no byte is
 *     in flight while CLK_HF10 collapses to its bypass source.
 *   - Do NOT re-run uart_configure() here. The Zephyr SCB driver
 *     misbehaves when reconfigured under a critical section (bytes
 *     get garbled or the console freezes). The SCB retune happens
 *     back in pm_switch_to() with IRQs re-enabled.
 *   - No diagnostic bytes may be emitted between this function's
 *     return and pm_switch_to()'s trailing SCB retune -- the baud
 *     divider is stale in that window.
 *
 * @return CY_SYSPM_SUCCESS on lock or CY_SYSPM_FAIL if either
 *         PllConfigure or PllEnable reported an error.
 */
static cy_en_syspm_status_t pm_pll_reconfigure(uint32_t freq_hz)
{
	cy_stc_pll_config_t cfg = {
	    .inputFreq = DPLL_INPUT_FREQ_HZ,
	    .outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,
	    .outputFreq = freq_hz,
	};
	cy_en_sysclk_status_t st;

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

	return (st == CY_SYSCLK_SUCCESS) ? CY_SYSPM_SUCCESS : CY_SYSPM_FAIL;
}

/* ------------------------------------------------------------------
 * SysPm callbacks. PDL calls each in the CHECK_READY /
 * BEFORE_TRANSITION / AFTER_TRANSITION phases; we act only on the
 * last two. Direction rule:
 *   Down (voltage falls): drop PLL to safe intermediate BEFORE the
 *                         voltage step so the still-running PLL
 *                         survives it, then RRAM + final PLL AFTER.
 *   Up   (voltage rises): same shape -- the intermediate keeps the
 *                         PLL inside the source-mode envelope while
 *                         Cy_SysPm_SystemEnter* raises voltage.
 * ------------------------------------------------------------------ */

static cy_en_syspm_status_t pm_syspm_hp_cb(cy_stc_syspm_callback_params_t *p,
					   cy_en_syspm_callback_mode_t mode)
{
	ARG_UNUSED(p);

	if (mode == CY_SYSPM_BEFORE_TRANSITION) {
		cy_en_syspm_status_t st =
		    pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_LP_HZ);
		s_before_end_cyc = k_cycle_get_32();
		return st;
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		s_after_start_cyc = k_cycle_get_32();
		Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_HP);
		return pm_pll_reconfigure(DPLL_FREQ_HP_HZ);
	}
	return CY_SYSPM_SUCCESS;
}

static cy_en_syspm_status_t pm_syspm_lp_cb(cy_stc_syspm_callback_params_t *p,
					   cy_en_syspm_callback_mode_t mode)
{
	ARG_UNUSED(p);

	if (mode == CY_SYSPM_BEFORE_TRANSITION) {
		cy_en_syspm_status_t st;
		/* Coming up from ULP the PLL must be under the ULP
		 * ceiling; from HP just under the LP ceiling. */
		uint32_t intermediate = Cy_SysPm_IsSystemUlp()
					    ? DPLL_FREQ_INTERMEDIATE_ULP_HZ
					    : DPLL_FREQ_INTERMEDIATE_LP_HZ;
		st = pm_pll_reconfigure(intermediate);
		s_before_end_cyc = k_cycle_get_32();
		return st;
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		s_after_start_cyc = k_cycle_get_32();
		Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_LP);
		return pm_pll_reconfigure(DPLL_FREQ_LP_HZ);
	}
	return CY_SYSPM_SUCCESS;
}

static cy_en_syspm_status_t pm_syspm_ulp_cb(cy_stc_syspm_callback_params_t *p,
					    cy_en_syspm_callback_mode_t mode)
{
	ARG_UNUSED(p);

	if (mode == CY_SYSPM_BEFORE_TRANSITION) {
		cy_en_syspm_status_t st =
		    pm_pll_reconfigure(DPLL_FREQ_INTERMEDIATE_ULP_HZ);
		s_before_end_cyc = k_cycle_get_32();
		return st;
	}
	if (mode == CY_SYSPM_AFTER_TRANSITION) {
		s_after_start_cyc = k_cycle_get_32();
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
    .callbackParams = &pm_hp_params,
};

static cy_stc_syspm_callback_t pm_lp_cb = {
    .callback = &pm_syspm_lp_cb,
    .type = CY_SYSPM_LP,
    .callbackParams = &pm_lp_params,
};

static cy_stc_syspm_callback_t pm_ulp_cb = {
    .callback = &pm_syspm_ulp_cb,
    .type = CY_SYSPM_ULP,
    .callbackParams = &pm_ulp_params,
};

/* ------------------------------------------------------------------
 * Strategy interface (see power_manager_internal.h).
 * ------------------------------------------------------------------ */

void pm_strategy_init(void)
{
	(void)Cy_SysPm_RegisterCallback(&pm_hp_cb);
	(void)Cy_SysPm_RegisterCallback(&pm_lp_cb);
	(void)Cy_SysPm_RegisterCallback(&pm_ulp_cb);
}

int pm_strategy_transition(pm_mode_t source, pm_mode_t target)
{
	/* labels[source_idx][target_idx] -- rows and columns follow the
	 * pm_mode_t enum ordering (ULP=0, LP=1, HP=2). Diagonal entries
	 * are unused because pm_switch_to filters no-op transitions. */
	static const char *const labels[3][3] = {
		/*             target=ULP    target=LP    target=HP  */
		/* source=ULP */ {"ulp2ulp", "ulp2lp",   "ulp2hp"},
		/* source=LP  */ {"lp2ulp",  "lp2lp",    "lp2hp"},
		/* source=HP  */ {"hp2ulp",  "hp2lp",    "hp2hp"},
	};
	uint32_t t_enter_start, t_enter_end;
	cy_en_syspm_status_t st;

	pm_phase_log_reset(labels[source][target]);
	s_before_end_cyc = 0u;
	s_after_start_cyc = 0u;

	t_enter_start = k_cycle_get_32();
	st = pm_syspm_enter(target);
	t_enter_end = k_cycle_get_32();

	if (st != CY_SYSPM_SUCCESS) {
		printk("[pm] SystemEnter* failed (%d)\n", (int)st);
		return -EIO;
	}

	/* Defensive: if a callback phase somehow didn't fire, fall
	 * back to placing the missing boundary at the whole window's
	 * start / end so cycle counts stay non-negative. */
	if (s_before_end_cyc == 0u) {
		s_before_end_cyc = t_enter_start;
	}
	if (s_after_start_cyc == 0u) {
		s_after_start_cyc = t_enter_end;
	}

	pm_phase_log_record("pll_pre", s_before_end_cyc - t_enter_start,
			    DPLL_INPUT_FREQ_HZ);
	pm_phase_log_record("volt", s_after_start_cyc - s_before_end_cyc,
			    intermediate_hz_for(source, target));
	pm_phase_log_record("pll_post", t_enter_end - s_after_start_cyc,
			    DPLL_INPUT_FREQ_HZ);
	return 0;
}

const char *pm_strategy_mode_name(pm_mode_t m)
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

void pm_strategy_probe_status(void)
{
	if (s_last_pll_target_hz == 0u) {
		printk("[pll] no retune since boot (cybsp/board default)\n");
		return;
	}
	printk("[pll] last target=%u Hz  Configure=0x%08x  Enable=0x%08x\n",
	       s_last_pll_target_hz, s_last_pll_configure_st,
	       s_last_pll_enable_st);
}

bool pm_strategy_needs_uart_retune(void) { return true; }

void pm_strategy_print_last_phases(void) { pm_phase_log_print(); }

#endif /* PM_STRATEGY_PLL_RETUNE */
