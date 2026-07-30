/*
 * SCB3 Clock Enablement Fix
 *
 * SCB3 (SPI for BGT60TR13C) is in PERI_0_GROUP_1, which requires CLK_HF10.
 * The Zephyr SoC init (soc_pse84_m33_s.c) only explicitly enables CLK_HF3,
 * HF4, and HF11. If the boot ROM doesn't enable CLK_HF10, SCB3 will not
 * function.
 *
 * This module ensures CLK_HF10 is enabled before any SPI/UART in GROUP_1
 * is initialized. It runs at PRE_KERNEL_1 priority 0 (before the clock
 * control driver).
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <cy_sysclk.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(scb3_clock_fix, LOG_LEVEL_INF);

static int ensure_clk_hf10_enabled(void)
{
	bool was_enabled = Cy_SysClk_ClkHfIsEnabled(10U);

	if (!was_enabled) {
		LOG_WRN("CLK_HF10 was NOT enabled! Enabling now for PERI_0_GROUP_1 (SCB2/SCB3)");

		/* Set CLK_HF10 source to CLKPATH0 (same as other HF clocks) */
		cy_en_sysclk_status_t status;

		status = Cy_SysClk_ClkHfSetSource(10U, CY_SYSCLK_CLKHF_IN_CLKPATH0);
		if (status != CY_SYSCLK_SUCCESS) {
			LOG_ERR("ClkHfSetSource(10) failed: %d", status);
			return -EIO;
		}

		status = Cy_SysClk_ClkHfSetDivider(10U, CY_SYSCLK_CLKHF_NO_DIVIDE);
		if (status != CY_SYSCLK_SUCCESS) {
			LOG_ERR("ClkHfSetDivider(10) failed: %d", status);
			return -EIO;
		}

		status = Cy_SysClk_ClkHfEnable(10U);
		if (status != CY_SYSCLK_SUCCESS) {
			LOG_ERR("ClkHfEnable(10) failed: %d", status);
			return -EIO;
		}

		LOG_INF("CLK_HF10 enabled successfully");
	} else {
		LOG_INF("CLK_HF10 was already enabled (freq=%u Hz)",
			Cy_SysClk_ClkHfGetFrequency(10U));
	}

	return 0;
}

/* Run before clock_control driver (PRE_KERNEL_1, priority 0) */
SYS_INIT(ensure_clk_hf10_enabled, PRE_KERNEL_1, 0);
