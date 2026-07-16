/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief HP / LP / ULP switcher for PSE84 CM33-Secure -- orchestration.
 *
 * Public API is in @c power_manager.h. The DVFS mechanics live in
 * one of two sibling files, selected at compile time:
 *
 *   PM_STRATEGY_PLL_RETUNE   -> power_manager_pll_retune.c
 *   PM_STRATEGY_HF0_DIVIDER  -> power_manager_hf0_divider.c
 *
 * Both provide the @c pm_strategy_* symbols declared in
 * @c power_manager_internal.h; only one is linked per build.
 * See @c PLAN_dual_dvfs.md for the strategy trade-offs and
 * @c PLAN_pm_refactor.md for the module split.
 *
 * This file owns the shared state (current mode, console UART
 * handle), the raw-SCB @c TRACE macro, the @c pm_syspm_enter
 * voltage-step primitive, the hardware clock-measurement probe,
 * and the four public entry points -- each a thin wrapper that
 * delegates the strategy-specific decision to @c pm_strategy_*.
 */

#include "power_manager.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cy_pdl.h"

#include "diag.h"
#include "gpio_indicators.h"
#include "pm_phase_log.h"
#include "power_manager_internal.h"

/* ==================================================================
 * Shared state and helpers.
 * ================================================================== */

/** Current active power mode; boot state is HP. */
static pm_mode_t s_current_mode = PM_MODE_HP;

/** Console UART handle. The SCB baud divider it caches at boot is
 *  only valid for the CLK_HF10 in effect at that time. */
static const struct device *const console_uart =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/**
 * @brief Re-run @c uart_configure() so the Infineon SCB driver
 *        recomputes its baud divider against the currently-live
 *        @c CLK_HF10. No-op if the device is not ready.
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

/**
 * Raw-SCB2 step markers. Bypass the Zephyr UART driver so a hang
 * mid-transition still leaves the last-known step on the wire.
 * See @c src/diag.h for the alive/dead diagnosis matrix.
 */
#define TRACE(msg) diag_trace("<T:" msg ">\n")

cy_en_syspm_status_t pm_syspm_enter(pm_mode_t target)
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

/* ==================================================================
 * Public API.
 * ================================================================== */

const char *pm_mode_name(pm_mode_t m) { return pm_strategy_mode_name(m); }

pm_mode_t pm_current_mode(void) { return s_current_mode; }

void pm_init(void)
{
	/* The DT overlay lands the SoC in an in-spec HP state at boot
	 * (DPLL_LP0 200 MHz, CLK_HF0 /1, CM33 at the AN237976 HP
	 * ceiling). Nothing to reprogram here -- the strategy's own
	 * init hook does whatever it needs (register SysPm callbacks
	 * for PLL_RETUNE, nothing for HF0_DIVIDER). */
	s_current_mode = PM_MODE_HP;
	pm_strategy_init();
	SystemCoreClockUpdate();
}

int pm_switch_to(pm_mode_t target)
{
	int rc;
	pm_mode_t source;
	uint32_t t_start, t_end, cycles;

	if (target == s_current_mode) {
		return 0;
	}
	if (target != PM_MODE_HP && target != PM_MODE_LP &&
	    target != PM_MODE_ULP) {
		return -EINVAL;
	}

	source = s_current_mode;

	/* P3.1 (pm-busy) is asserted around ONLY the strategy call so
	 * an external instrument (PPK2 D7, scope on P3.1) captures the
	 * DVFS transient itself and nothing else -- no shell prints,
	 * no UART retune, no clock probe. That GPIO pulse width is the
	 * authoritative transition wall time.
	 *
	 * The cycle counters below record CPU work only. They cannot
	 * be trusted as absolute wall time because CLK_HF0 (which
	 * SysTick is sourced from) changes several times inside a
	 * transition -- source rate at entry, IHO bypass while the
	 * PLL is disabled, intermediate rate while the PLL is locked
	 * to it, target rate at exit. Cross-reference these counts
	 * with the PPK2 pulse width via scripts/postprocess.py. */
	t_start = k_cycle_get_32();
	gpio_indicators_transition_begin();
	rc = pm_strategy_transition(source, target);
	gpio_indicators_transition_end();
	t_end = k_cycle_get_32();

	if (rc != 0) {
		TRACE("switch:FAIL");
		return rc;
	}

	s_current_mode = target;
	SystemCoreClockUpdate();

	if (pm_strategy_needs_uart_retune()) {
		/* CLK_HF10 changed -> SCB baud divider is stale. Retune
		 * runs here (IRQs re-enabled after Cy_SysPm_SystemEnter*
		 * returned); the same call inside the SysPm critical
		 * section is empirically unsafe on this driver. */
		diag_trace_flush();
		pm_reconfigure_console_uart();
	}

	cycles = t_end - t_start;
	printk("[pm] transition %s -> %s : %u cycles\n", pm_mode_name(source),
	       pm_mode_name(target), cycles);
	pm_strategy_print_last_phases();

	TRACE("switch:complete");
	pm_clock_probe();
	return 0;
}

/* ==================================================================
 * Hardware clock probe.
 *
 * The SoC has dedicated 24-bit counters that measure any clock
 * against a fixed reference (IHO, 50 MHz, always running). We also
 * print the PDL's computed frequencies alongside so a broken
 * counter is immediately visible.
 * ================================================================== */

#define PM_PROBE_REF_COUNT 50000u /* 1 ms wall time at IHO 50 MHz */
#define PM_MEAS_SAFETY_ITERS                                                   \
	1000000u /* >>100x headroom over the 1 ms window                       \
		  */

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

static void pm_print_hz(const char *label, uint32_t meas_hz, uint32_t comp_hz)
{
	printk(
	    "[clk] %s meas=%3u.%03u MHz (%9u Hz)  comp=%3u.%03u MHz (%9u Hz)\n",
	    label, meas_hz / 1000000u, (meas_hz / 1000u) % 1000u, meas_hz,
	    comp_hz / 1000000u, (comp_hz / 1000u) % 1000u, comp_hz);
}

void pm_clock_probe(void)
{
	uint32_t m_path0 = pm_measure_hz(CY_SYSCLK_MEAS_CLK_PATH0);
	uint32_t m_hf0 = pm_measure_hz(CY_SYSCLK_MEAS_CLK_CLKHF0);
	uint32_t m_hf10 = pm_measure_hz(CY_SYSCLK_MEAS_CLK_CLKHF10);

	uint32_t c_path0 = Cy_SysClk_ClkPathGetFrequency(0u);
	uint32_t c_hf0 = Cy_SysClk_ClkHfGetFrequency(0u);
	uint32_t c_hf10 = Cy_SysClk_ClkHfGetFrequency(10u);

	pm_print_hz("DPLL_LP0 ", m_path0, c_path0);
	pm_print_hz("CLK_HF0  ", m_hf0, c_hf0);	  /* CM33 core   */
	pm_print_hz("CLK_HF10 ", m_hf10, c_hf10); /* SCB2 peri   */

	pm_strategy_probe_status();
}
