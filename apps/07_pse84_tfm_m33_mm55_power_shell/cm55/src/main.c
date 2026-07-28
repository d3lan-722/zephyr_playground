/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/cache.h>

#include "cy_device.h"
#include "cy_syspm.h"

#include <cmsis_core.h>

/**
 * @brief Arm CM55 as a plain DEEPSLEEP requestor (one-time).
 *
 * On CM55, @c Cy_SysPm_SetDeepSleepMode routes to
 * @c Cy_SysPm_SetAppDeepSleepMode which programs the App-domain PPUs
 * (PD1, APPCPUSS, APPCPU) to the AN237976 Table-2 row for the
 * requested mode. Called via the PDL wrapper (SRF-integrated: the
 * PWRMODE PPU register file is in a PC=2-only PPC region that CM55
 * NS at PC=6 cannot write directly).
 *
 * Programming plain @c CY_SYSPM_MODE_DEEPSLEEP here (App PPUs =
 * FULL_RETENTION) is what keeps the CM33-NS-driven
 * @c cpu_deep_sleep / @c system_deep_sleep paths in sync with a
 * complete AN237976 DEEPSLEEP row when the SRSS state machine
 * folds. Any deeper variant (DS-RAM, DS-OFF) must be programmed
 * per-transition — CM55 is a passive requestor here.
 */
static void cm55_arm_deepsleep_mode(void)
{
	Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
}

/**
 * @brief Stop SysTick + mask IRQs so nothing wakes WFI.
 */
static void zephyr_quiesce_for_deepsleep(void)
{
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	__disable_irq();
}

/**
 * @brief CM55 WFI parking-lot entry point.
 * @return Never returns.
 */
int main(void)
{

	cm55_arm_deepsleep_mode();

	zephyr_quiesce_for_deepsleep();

	for (;;) {
		Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	}

	return 0;
}
