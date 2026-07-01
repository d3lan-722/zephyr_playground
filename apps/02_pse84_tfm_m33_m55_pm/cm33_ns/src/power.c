/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr PM dispatcher for the CM33-NS image.
 *
 * Round 7: all three currently-implemented PM entry points call
 * PDL syspm directly from NS. The NS-side cy_syspm_v4.c is compiled
 * with CY_PDL_SYSPM_ENABLE_SRF_INTEG (auto-defined by cy_syspm_srf.h
 * because at least one of the four CYCFG_PPC_SECURED_{SRSS_MAIN,
 * SRSS_HIB_DATA, PWRMODE_PWRMODE, M55APPCPUSS} bits is 1U). That
 * activates the SRF branch inside each Cy_SysPm_Cpu*Enter*Sleep
 * function, which packs an SRF request and psa_call()s into
 * IFX_EXT_SP; the S-side handler runs the actual SLEEPDEEP+WFI at
 * PC2.  No project-local partition wrap is needed for these APIs.
 *
 * z_pm still exists (see tfm_partitions/z_pm/) but only exposes
 * Z_PM_OP_PING today. It is the placeholder for future ops that
 * PDL DOES NOT SRF-wrap: Cy_SysPm_SetSysDeepSleepMode,
 * Cy_SysPm_SetSOCMEMDeepSleepMode, CM55-side hibernate, Layer-B
 * bias, retention patterns.
 *
 * The SoC default pm_state_set (in soc/infineon/edge/pse84/power.c)
 * is dropped from the build by the application CMakeLists — its
 * PRE_KERNEL_1 SYS_INIT calls Cy_SysPm_SetDeepSleepMode which is
 * NOT SRF-wrapped and bus-faults from NS.
 *
 * Prerequisite for the direct-NS path: CONFIG_IDLE_STACK_SIZE must
 * be large enough for tfm_ns_interface_dispatch's fpu_ctx_full
 * alloca (136 bytes) on top of the pool_allocate + Cy_SysPm_*
 * frames. See prj.conf; 2 KiB works, the Zephyr default 320 bytes
 * does not.
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>

#include "cy_syspm.h"

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

/* PM_STATE_SUSPEND_TO_IDLE (cpu_sleep): SLEEPDEEP=0 + WFI.
 * PDL takes the SRF branch → IFX_EXT_SP → S-side execution. */
static void enter_cpu_sleep(void)
{
	indicator_cpu_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	indicator_cpu_sleep_off();
}

/* PM_STATE_STANDBY substate 1 (cpu_deep_sleep): SLEEPDEEP=1 + WFI
 * with PDL callback + PPU trim fixups executed on the S side. */
static void enter_cpu_deep_sleep(void)
{
	indicator_cpu_deep_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	indicator_cpu_deep_sleep_off();
}

/* PM_STATE_STANDBY substate 2 (system_deep_sleep): same primitive
 * as substate 1 today. Distinct call site so phase 7+ (DS-RAM /
 * DS-OFF, Layer-B bias) can specialise it via z_pm — those steps
 * need Cy_SysPm_SetSysDeepSleepMode et al. which are NOT SRF-wrapped
 * and therefore MUST route through the z_pm partition (or Option D).
 */
static void enter_system_deep_sleep(void)
{
	indicator_system_deep_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
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
