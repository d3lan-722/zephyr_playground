/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief `deep_sleep` shell command -- put CM33-NS into CPU Deep
 *        Sleep until the next NVIC IRQ (typically an LPTIMER tick).
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
 *
 * On this project's CM33-NS build, both entry points are SRF-wrapped
 * by the PDL (`CY_PDL_SYSPM_ENABLE_SRF_INTEG`) -- the NS call packs a
 * request into IFX_EXT_SP and TF-M-S runs the actual SLEEPDEEP + WFI
 * at PC=2 on our behalf. The direct PDL call from NS is therefore
 * the correct dispatch path; no project-local z_pm wrap is needed
 * for CpuEnter{Sleep,DeepSleep}.
 *
 * System-DEEPSLEEP voting: this project's CM55 image (see cm55/)
 * arms Cy_SysPm_SetDeepSleepMode(DEEPSLEEP) at its own boot then
 * parks in an infinite Cy_SysPm_CpuEnterDeepSleep loop with IRQs
 * masked. So the moment CM33-NS executes WFI here, both CPUs have
 * voted DS and the PWRMODE state machine can collapse to a real
 * AN237976 Table-2 DEEPSLEEP row -- the whole reason for the
 * project's dual-core structure. That is the difference vs.
 * project 06, where CM55 never boots and the SoC bottoms out at
 * CPU-DEEPSLEEP (no PPU retention fold, ~62 uA floor).
 *
 * Layer of prep NOT done here yet (Phase D):
 *
 *   Project 06 has a PRE_KERNEL_2 SYS_INIT hook
 *   (pm_deep_sleep_init) that programs SRSS_PWR_CTL2 BGREF_LPMODE,
 *   CoreBuck DS voltage/mode/override, IHO/IMO DS-off, and
 *   Cy_SysPm_SetDeepSleepMode(DEEPSLEEP). Every register touched by
 *   that hook is in the PWRMODE_PWRMODE / SRSS_MAIN PPC regions
 *   which are PC=2 only on this build, so the sequence must go
 *   through the z_pm partition (Z_PM_OP_DEEP_SLEEP_BIAS, plan
 *   sec. 5.3). Phase D adds the op + the client stub + the boot-
 *   time call from main(). Until then, deep_sleep still WORKS --
 *   it correctly enters CPU-DS and wakes on LPTIMER -- but the
 *   sleep-floor current is higher than optimal because the DS
 *   bias registers stay at their TF-M cycfg defaults.
 *
 * Wake source: MCWDT0/LPTIMER on PILO, alive across Deep Sleep by
 * design (PILO is a deep-sleep-alive clock). k_msleep(20) at the top
 * of the handler schedules the next Zephyr deadline, and that is
 * what wakes us. UART RX does NOT wake System Deep Sleep (only SCB0
 * supports DS wake; our shell UART is on SCB2).
 *
 * Per-entry sequence (this file):
 *
 *   1. pm-busy HIGH, banner print, k_msleep(20) TX drain.
 *   2. pm-busy LOW -- WFI window starts.
 *   3. PRIMASK on, BASEPRI off, __DSB.
 *   4. Cy_SysPm_CpuEnterDeepSleep(WAIT_FOR_INTERRUPT) -- SRF-wrapped;
 *      TF-M-S sets SLEEPDEEP, executes WFI at PC=2.
 *   5. On wake, clear SLEEPDEEP so that any subsequent Zephyr idle
 *      WFI is plain CPU sleep (not another deep sleep from the same
 *      thread).
 *   6. __enable_irq, pm-busy HIGH, print dwell, pm-busy LOW.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/time_units.h>

#include <cmsis_core.h>
#include <cy_pdl.h>
#include <cy_syspm.h>

#include <stddef.h>

#include "gpio_indicators.h"

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

	/* Let the shell TX ring drain before WFI. With mcwdt0 as the
	 * Zephyr system timer, k_msleep(20) really waits 20 ms at
	 * every HP/LP/ULP mode -- comfortable margin for ~60 chars at
	 * 115200 baud (~5 ms actual drain). */
	k_msleep(20);

	gpio_indicators_transition_end();

	uint32_t t_enter = k_cycle_get_32();

	/* PRIMASK on, BASEPRI off -- see cmd_sleep.c file-comment. */
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
