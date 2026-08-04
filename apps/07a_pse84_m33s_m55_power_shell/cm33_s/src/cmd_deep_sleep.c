/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief `deep_sleep` shell command -- put CM33 into CPU Deep Sleep
 *        until the next NVIC IRQ (typically a LPTIMER tick).
 *
 * Relationship to `sleep`:
 *
 *   sleep       Cy_SysPm_CpuEnterSleep       -- just WFI. CPU pipeline
 *                                               halts, all clocks and
 *                                               peripherals stay alive.
 *                                               Buck at active voltage.
 *
 *   deep_sleep  Cy_SysPm_CpuEnterDeepSleep   -- SLEEPDEEP bit set, then
 *                                               WFI. Enables buck to drop
 *                                               to 0.70V LP topology, HF
 *                                               clocks to gate, IHO/IMO
 *                                               to stop, BGREF to go LP.
 *                                               Deepest state before
 *                                               DS-RAM / DS-OFF.
 *
 * Per AN237976 the SoC auto-escalates to *System* Deep Sleep when both
 * CPUs are in Deep Sleep AND the PPUs match Table 2. Our project 06
 * has already collapsed the APP-domain PPUs (PD1/APPCPU/APPCPUSS/
 * SOCMEM/U55 all OFF) via pm_boot_optimize, and CM55 never boots, so
 * the SoC sees "all CPUs asleep" as soon as CM33 executes WFI here.
 * The SYS-domain PPUs (MAIN, SRAM0/1, SYSCPU) are left at their
 * boot-time ON state, which is a valid target for both plain Deep
 * Sleep and DS-RAM per Table 2.
 *
 * Wake source: MCWDT0/LPTIMER on PILO, alive across Deep Sleep by
 * design (PILO is a deep-sleep-alive clock). k_msleep(20) at the top
 * of the handler schedules the next Zephyr deadline, and that is
 * what wakes us -- the LPTIMER driver's ISR fires the tick, Cortex-M
 * wakes on the NVIC pending edge. UART RX does NOT wake System Deep
 * Sleep (only SCB0 supports DS wake; our shell UART is on SCB2).
 *
 * Layer-B static prep (SYS_INIT at PRE_KERNEL_2) mirrors the
 * reference tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c
 * ifx_pm_init:
 *
 *   1. BGREF into low-power mode -- lower bandgap reference current
 *      during Deep Sleep.
 *   2. Core buck DS-target: 0.70 V, LP topology, override enabled --
 *      when SLEEPDEEP is asserted the buck reconfigures to this.
 *   3. Stop IHO during Deep Sleep (Cy_SysClk_IhoDeepsleepDisable).
 *      IHO is a 50 MHz internal high-frequency oscillator that feeds
 *      DPLL_LP0 in this project; keeping it running in DS wastes
 *      current since HF clocks are all gated anyway.
 *   4. Stop IMO during Deep Sleep (SRSS_CLK_IMO_CONFIG bit clear).
 *   5. CLK_BAK <- PILO so backup domain (RTC, BREGs) stays clocked.
 *   6. Vote plain DEEPSLEEP (not DS-RAM/OFF) as the System
 *      deep-sleep mode.
 *
 * Layer-A per-entry sequence (cmd_deep_sleep):
 *
 *   1. pm-busy HIGH, banner print, k_msleep(20) TX drain.
 *   2. pm-busy LOW -- WFI window starts.
 *   3. PRIMASK on, BASEPRI off, __DSB.
 *   4. Cy_SysPm_CpuEnterDeepSleep(WAIT_FOR_INTERRUPT) -- sets
 *      SLEEPDEEP, executes WFI.
 *   5. On wake, clear SLEEPDEEP so that any subsequent Zephyr idle
 *      WFI is plain CPU sleep (not another deep sleep from the same
 *      thread).
 *   6. __enable_irq, pm-busy HIGH, print dwell, pm-busy LOW.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/time_units.h>

#include <cmsis_core.h>
#include <cy_pdl.h>
#include <cy_sysclk.h>
#include <cy_syspm.h>
#include <soc.h>

#include <stddef.h>

#include "gpio_indicators.h"

/* ------------------------------------------------------------------
 * Layer-B: one-shot static prep at PRE_KERNEL_2 (before main())
 * ------------------------------------------------------------------ */

/**
 * @brief Put SRSS bandgap reference into low-power mode.
 *
 * BGREF_LPMODE bit trades a small startup-time penalty (irrelevant
 * for our use case -- we wake on LPTIMER tick, not on a sharp
 * transient) for a few tens of µA saved during Deep Sleep.
 */
static void deep_sleep_bgref_lp(void)
{
	SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;
}

/**
 * @brief Configure core buck to drop to 0.70 V / LP topology when
 *        SLEEPDEEP is asserted.
 *
 * Buck has two configurations: "active" (drives HP/LP/ULP mode
 * voltage, high-power low-ripple topology, up to 400 mA load) and
 * "deep-sleep" (this override -- low-power high-ripple topology
 * targeting ~0.70 V for retention loads only).
 *
 * The override bit tells the PMU to switch to the DS config
 * automatically when SLEEPDEEP is set in SCB_SCR. On wake, the PMU
 * switches back to the active config before releasing SLEEPDEEP.
 */
static void deep_sleep_buck_config(void)
{
	Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
	Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
	Cy_SysPm_CoreBuckDpslpEnableOverride(true);
}

/**
 * @brief Stop the internal high-frequency oscillators in Deep Sleep.
 *
 *   IHO -- 50 MHz internal HF oscillator. Feeds DPLL_LP0 (which
 *          feeds HF0 = CM33 core clock). In Deep Sleep HF clocks are
 *          gated, so IHO has no consumers; disabling saves a few
 *          hundred µA on its bias current.
 *
 *   IMO -- 8 MHz internal medium-frequency oscillator. Not used as
 *          a source anywhere in this project (already off in
 *          previous ClkPath configs); the DPSLP_ENABLE bit is
 *          cleared here for completeness.
 *
 * PILO is deliberately NOT touched: it clocks MCWDT0 which is our
 * Zephyr system timer AND the only viable wake source for Deep
 * Sleep. Disabling PILO would make the CPU un-wakeable.
 */
static void deep_sleep_stop_osc(void)
{
	Cy_SysClk_IhoDeepsleepDisable();
	SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;
}

/**
 * @brief Point CLK_BAK at PILO so backup-domain peripherals (RTC,
 *        BREGs) keep ticking through Deep Sleep.
 */
static void deep_sleep_backup_clock(void)
{
	Cy_SysClk_ClkBakSetSource(CY_SYSCLK_BAK_IN_PILO);
}

/**
 * @brief One-shot boot hook. Runs at PRE_KERNEL_2, after
 *        pm_early_mcwdt_reset (PRE_KERNEL_1) and before the shell
 *        thread starts.
 *
 * Sets up the SRSS + PMU + buck + clock knobs so that a subsequent
 * `deep_sleep` shell command achieves its full current-drop
 * potential. Cy_SysPm_SetDeepSleepMode votes plain DEEPSLEEP (not
 * DS-RAM or DS-OFF), matching what CpuEnterDeepSleep does on
 * SLEEPDEEP entry.
 */
static int pm_deep_sleep_init(void)
{
	Cy_SysPm_Init();
	deep_sleep_bgref_lp();
	deep_sleep_buck_config();
	deep_sleep_stop_osc();
	deep_sleep_backup_clock();
	(void)Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
	return 0;
}

SYS_INIT(pm_deep_sleep_init, PRE_KERNEL_2,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

/* ------------------------------------------------------------------
 * Layer-A: shell handler
 * ------------------------------------------------------------------ */

/**
 * @brief Shell handler for `deep_sleep`.
 *
 * Semantics identical to @c cmd_sleep except the entry API is
 * @c Cy_SysPm_CpuEnterDeepSleep (SLEEPDEEP asserted before WFI)
 * and SLEEPDEEP is cleared on wake to keep Zephyr's idle-thread
 * WFI as plain CPU sleep afterwards.
 */
static int cmd_deep_sleep(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	gpio_indicators_transition_begin();

	shell_print(sh, "entering CPU deep sleep -- wakes on LPTIMER tick");

	/* Let the shell TX ring drain before WFI. With mcwdt0 now the
	 * Zephyr system timer, k_msleep(20) really waits 20 ms at
	 * every HP/LP/ULP mode -- comfortable margin for ~60 chars at
	 * 115200 baud (~5 ms actual drain). */
	k_msleep(20);

	gpio_indicators_transition_end();

	uint32_t t_enter = k_cycle_get_32();

	/* PRIMASK on, BASEPRI off -- see cmd_sleep.c file-comment.
	 * The Deep Sleep SLEEPDEEP bit is set by CpuEnterDeepSleep;
	 * we clear it on wake so subsequent Zephyr idle-WFIs from the
	 * same context don't unintentionally deep-sleep. */
	__disable_irq();
	irq_unlock(0);
	__DSB();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	SCB_SCR &= (uint32_t)~SCB_SCR_SLEEPDEEP_Msk;
	__enable_irq();

	uint32_t t_exit = k_cycle_get_32();

	gpio_indicators_transition_begin();

	uint32_t dwell_cyc = t_exit - t_enter;
	uint32_t dwell_us = k_cyc_to_us_floor32(dwell_cyc);

	shell_print(sh, "woke after %u us (%u LPTIMER cycles @ 32768 Hz)",
		    dwell_us, dwell_cyc);

	gpio_indicators_transition_end();
	return 0;
}

SHELL_CMD_REGISTER(deep_sleep, NULL,
		   "Enter CPU Deep Sleep (SLEEPDEEP + WFI) until any NVIC IRQ "
		   "(LPTIMER tick). Pair with `noidle on` to see the "
		   "active-vs-deep-sleep current delta.",
		   cmd_deep_sleep);
