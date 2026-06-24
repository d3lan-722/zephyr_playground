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
 * Plan §3.2: CM55 stays in CY_SYSPM_MODE_DEEPSLEEP forever.
 * The CPU initiating a deeper system mode (CM33-NS, for
 * DEEPSLEEP_RAM / DEEPSLEEP_OFF) updates SRSS PWR_CTL.DEEPSLEEP_MODE
 * itself just before its WFI.
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
