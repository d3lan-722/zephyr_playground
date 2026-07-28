/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Preemptive MCWDT0 counter reset at PRE_KERNEL_1.
 *
 * Rationale (from apps/06_pse84_m33_s_shell_ulp_lp_hp/src/pm_boot_optimize.c
 * pm_early_mcwdt_reset, itself lifted from
 * tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c ifx_pm_init):
 *
 *   The Infineon LPTIMER driver (infineon_lp_timer_pdl.c, SYS_INIT
 *   at PRE_KERNEL_2) calls Cy_MCWDT_Init which returns BAD_PARAM
 *   if any counter is already enabled. SE-ROM / RRAM boot leaves
 *   MCWDT0 counters running on cold boot on some builds, so
 *   without a preemptive disable the driver's lptimer_init returns
 *   -EINVAL, the system clock never starts, and k_msleep /
 *   k_busy_wait hang forever (sys_clock_cycle_get_32 reads a
 *   counter that never advances).
 *
 *   SYS_INIT failures do NOT abort boot in Zephyr, so the symptom
 *   is silent: banner prints, then the first sleep-related call
 *   spins forever.
 *
 * On this build the bits happen to be clean at cold boot, so this
 * hook is defensive future-proofing against SoC / cycfg drift.
 * Runs at PRE_KERNEL_1 priority 0 -- earliest possible slot, so
 * the counters are always clean by the time the LPTIMER PDL driver
 * runs at PRE_KERNEL_2.
 */

#include <zephyr/init.h>

#include <soc.h>
#include "cy_pdl.h"
#include "cy_mcwdt.h"

static int boot_mcwdt_reset(void)
{
	Cy_MCWDT_Unlock(MCWDT_STRUCT0);
	Cy_MCWDT_Disable(MCWDT_STRUCT0,
			 CY_MCWDT_CTR0 | CY_MCWDT_CTR1 | CY_MCWDT_CTR2, 100U);
	Cy_MCWDT_ClearInterrupt(MCWDT_STRUCT0,
				CY_MCWDT_CTR0 | CY_MCWDT_CTR1 | CY_MCWDT_CTR2);
	Cy_MCWDT_SetInterruptMask(MCWDT_STRUCT0, 0U);
	return 0;
}

SYS_INIT(boot_mcwdt_reset, PRE_KERNEL_1, 0);
