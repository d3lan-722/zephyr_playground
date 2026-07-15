/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33-Secure shell-controlled active-power-mode demo -- bring-up.
 *
 * This file only handles application bring-up: print a banner,
 * initialise the GPIO indicators, initialise the power-mode
 * manager. Everything else lives in dedicated modules:
 *
 *   src/power_manager.[ch]   -- HP/LP/ULP transitions, PLL retune,
 *                               clock-measurement probe.
 *   src/shell_cmds.[ch]      -- ulp / lp / hp / probe shell
 *                               handlers.
 *   src/gpio_indicators.[ch] -- mode-indicator LEDs (led0 red,
 *                               led1 green) + PM-busy scope
 *                               trigger on P3.1.
 *   src/diag.[ch]            -- raw-SCB tracing, blue-LED
 *                               heartbeat thread (led2).
 *
 * The Zephyr shell auto-registers commands via SHELL_CMD_REGISTER
 * at link time, so pulling shell_cmds.c into the build is enough
 * to make the commands available.
 */

#include <stdio.h>

#include <zephyr/kernel.h>

#include "gpio_indicators.h"
#include "power_manager.h"

int main(void)
{
	printf("CM33-S shell-power-mode on %s\n", CONFIG_BOARD);

	if (gpio_indicators_init() < 0) {
		printf("CM33-S: GPIO indicators init failed\n");
		/* Non-fatal: shell + mode transitions still work; only
		 * the visual LED indicators and the P3.1 scope trigger
		 * are dark. */
	}

	pm_init();

	printf("CM33-S: initial power mode %s\n",
	       pm_mode_name(pm_current_mode()));
	return 0;
}
