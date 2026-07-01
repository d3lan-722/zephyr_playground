/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr PM dispatcher for the CM33-NS image.
 *
 * Phase 6: PM_STATE_SUSPEND_TO_IDLE, PM_STATE_STANDBY substate 1
 * (cpu_deep_sleep), and PM_STATE_STANDBY substate 2
 * (system_deep_sleep) all route the actual SLEEPDEEP+WFI through
 * the out-of-tree TF-M partition z_pm. The partition calls PDL
 * Cy_SysPm_Cpu{Enter,Deep}Sleep on the secure side, which has the
 * privilege to touch PWRMODE/SRSS without bus-faulting.
 *
 * The SoC default pm_state_set lives in
 * soc/infineon/edge/pse84/power.c and is dropped from the build by
 * the application CMakeLists (its PRE_KERNEL_1 SYS_INIT bus-faults
 * from NS under TF-M).
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>

/* Direct PDL syspm entry point for the substate-1 diagnostic path
 * below. Provided by the NS-side libmodules_hal_infineon.a. */
#include "cy_syspm.h"

#include "indicator.h"
#include "z_pm_client.h"

/**
 * @brief Switch IRQ masking from BASEPRI to PRIMASK before WFI.
 *
 * Zephyr enters pm_state_set with BASEPRI set by irq_lock(), which
 * masks every maskable interrupt - including the timer that is
 * supposed to wake us. PSE84 / Cortex-M33 only honours wake from
 * standby/sleep when the masking interrupt is signalled with
 * PRIMASK set; BASEPRI keeps the wake source pending forever.
 * Match the SoC default pm_state_set: __disable_irq() sets PRIMASK,
 * irq_unlock(0) clears BASEPRI. pm_state_exit_post_ops clears
 * PRIMASK via __enable_irq().
 */
static inline void pm_irq_prologue(void)
{
	__disable_irq();
	irq_unlock(0);
}

/* PM_STATE_SUSPEND_TO_IDLE (cpu_sleep): partition does SLEEPDEEP=0 + WFI. */
static void enter_cpu_sleep(void)
{
	indicator_cpu_sleep_on();
	pm_irq_prologue();
	(void)z_pm_cpu_sleep();
	indicator_cpu_sleep_off();
}

/* PM_STATE_STANDBY substate 1 (cpu_deep_sleep): partition does
 * Cy_SysPm_CpuEnterDeepSleep (SLEEPDEEP=1 + WFI with PDL fixups).
 * Round-7: temporarily unused while enter_cpu_deep_sleep_direct_pdl()
 * runs the direct-PDL experiment (see pm_state_set below). Kept so
 * the swap-back is a one-liner. */
static void enter_cpu_deep_sleep(void) __unused;
static void enter_cpu_deep_sleep(void)
{
	indicator_cpu_deep_sleep_on();
	pm_irq_prologue();
	(void)z_pm_cpu_deep_sleep();
	indicator_cpu_deep_sleep_off();
}

/* DIAGNOSTIC (round 7): call PDL syspm from NS directly, bypassing
 * z_pm. The NS-side cy_syspm_v4.c IS compiled with
 * CY_PDL_SYSPM_ENABLE_SRF_INTEG (verified by preprocessing), so this
 * *should* take the SRF branch: mtb_srf_pool_allocate -> psa_call
 * (IFX_EXT_SP) -> S-side handler runs Cy_SysPm_CpuEnterDeepSleep at
 * PC2 -> SLEEPDEEP+WFI. Empirically it faults; the goal of this
 * experiment (with CONFIG_TFM_HALT_ON_CORE_PANIC=ON) is to catch
 * the fault with the debugger and find out which register access
 * actually blew up. */
static void enter_cpu_deep_sleep_direct_pdl(void)
{
	indicator_cpu_deep_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	indicator_cpu_deep_sleep_off();
}

/* PM_STATE_STANDBY substate 2 (system_deep_sleep): same primitive as
 * substate 1 today. Distinct op so Phase 7+ (DS-OFF, Layer-B bias)
 * can specialise without disturbing the per-CPU deep-sleep path.
 */
static void enter_system_deep_sleep(void)
{
	indicator_system_deep_sleep_on();
	pm_irq_prologue();
	(void)z_pm_system_deep_sleep();
	indicator_system_deep_sleep_off();
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	switch (state) {
	case PM_STATE_SUSPEND_TO_IDLE:
		enter_cpu_sleep();
		break;
	case PM_STATE_STANDBY:
		switch (substate_id) {
		case 1U:
			/* Round-7 experiment: bypass z_pm and call PDL
			 * syspm from NS directly. Swap back to
			 * enter_cpu_deep_sleep() once diagnosed. */
			enter_cpu_deep_sleep_direct_pdl();
			break;
		case 2U:
			enter_system_deep_sleep();
			break;
		default:
			printk("pm: STANDBY substate %u not implemented\n",
			       substate_id);
			break;
		}
		break;
	case PM_STATE_SUSPEND_TO_RAM:
		printk("pm: SUSPEND_TO_RAM not implemented yet\n");
		break;
	case PM_STATE_SOFT_OFF:
		printk("pm: SOFT_OFF not implemented yet\n");
		break;
	default:
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
	__enable_irq();
}
