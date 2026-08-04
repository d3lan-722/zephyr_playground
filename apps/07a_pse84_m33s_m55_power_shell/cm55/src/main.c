/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM55 parking image for apps/07a_pse84_m33s_m55_power_shell.
 *
 * Ported from apps/07_pse84_tfm_m33_m55_power_shell/cm55/src/main.c.
 * Structurally identical: arm the CM55-side "plain DEEPSLEEP" mode
 * on the App-domain PPUs, then quiesce SysTick + mask IRQs and loop
 * in Cy_SysPm_CpuEnterDeepSleep so CM55 is a passive DEEPSLEEP
 * requestor for the SoC PWRMODE state machine.
 *
 * Difference vs project 07:
 *   - Project 07 routed Cy_SysPm_SetDeepSleepMode through the SRF
 *     mailbox because the PWRMODE PPU registers were in a PC=2-only
 *     PPC region (TF-M SPE owned it). Here the CM33-S local boot
 *     (app_pse84_cm55_startup -> cy_ppc0_init/cy_ppc1_init) marks
 *     every PPC region PC-all-access, so the direct PDL register
 *     writes on CM55 succeed with no relay.
 */

#include <cmsis_core.h>

#include "cy_device.h"
#include "cy_syspm.h"

/**
 * @brief Arm CM55 as a plain DEEPSLEEP requestor (one-time).
 *
 * Cy_SysPm_SetDeepSleepMode routes to Cy_SysPm_SetAppDeepSleepMode
 * on CM55, which programs the App-domain PPUs (PD1, APPCPUSS,
 * APPCPU) to the AN237976 Table-2 row for the requested mode.
 * Plain CY_SYSPM_MODE_DEEPSLEEP = App PPUs FULL_RETENTION, which is
 * what keeps the CM33-driven cpu_deep_sleep / system_deep_sleep
 * paths in sync with a complete DEEPSLEEP row.
 */
static void cm55_arm_deepsleep_mode(void)
{
	Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
}

static void zephyr_quiesce_for_deepsleep(void)
{
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	__disable_irq();
}

int main(void)
{
	cm55_arm_deepsleep_mode();
	zephyr_quiesce_for_deepsleep();

	for (;;) {
		Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	}

	return 0;
}
