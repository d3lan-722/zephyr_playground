/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr PM dispatcher for the CM33-NS image.
 *
 * Phase 4 of the porting plan: only the PM_STATE_SUSPEND_TO_IDLE
 * (cpu_sleep) branch is wired to the PDL. The other states call
 * printk and return - they will be filled in in subsequent phases.
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

static void enter_cpu_sleep(void)
{
	indicator_cpu_sleep_on();
	pm_irq_prologue();
	/* Plain Cortex-M33 sleep: SLEEPDEEP=0 + WFI.
	 *
	 * Deliberately NOT using Cy_SysPm_CpuEnterSleep here. With TF-M
	 * enabled + CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y the PDL is built
	 * with CY_PDL_SYSPM_ENABLE_SRF_INTEG, which routes the NS call
	 * through the MTB SRF mailbox into TF-M-S. That path needs the
	 * SRF pool initialised before first use - skipping it produces
	 * an immediate-return / no-wake loop and a watchdog reset.
	 * Plain WFI from NS is fully supported by the M33 and does not
	 * need PDL state.
	 */
	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
	__DSB();
	__WFI();
	indicator_cpu_sleep_off();
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	switch (state) {
	case PM_STATE_SUSPEND_TO_IDLE:
		enter_cpu_sleep();
		break;
	case PM_STATE_STANDBY:
		printk("pm: STANDBY substate %u not implemented yet\n",
		       substate_id);
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
