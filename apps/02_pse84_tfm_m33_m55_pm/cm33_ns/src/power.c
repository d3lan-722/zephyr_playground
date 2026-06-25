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
 */
static void enter_cpu_deep_sleep(void)
{
	indicator_cpu_deep_sleep_on();
	pm_irq_prologue();
	(void)z_pm_cpu_deep_sleep();
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
			enter_cpu_deep_sleep();
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
