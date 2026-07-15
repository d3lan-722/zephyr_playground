/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33-Secure shell-controlled active-power-mode demo -- bring-up.
 *
 * This file only handles application bring-up: print a banner,
 * initialise the power-mode manager, initialise the shell command
 * module. Everything else lives in dedicated modules:
 *
 *   src/power_manager.[ch]  -- HP/LP/ULP transitions, PLL retune,
 *                              clock-measurement probe.
 *   src/shell_cmds.[ch]     -- ulp / lp / hp / probe shell handlers,
 *                              RGB indicator LEDs.
 *   src/diag.[ch]           -- raw-SCB tracing, blue-LED heartbeat
 *                              thread.
 *
 * The Zephyr shell auto-registers commands via SHELL_CMD_REGISTER
 * at link time, so pulling shell_cmds.c into the build is enough
 * to make the commands available -- shell_cmds_init() only puts
 * the indicator LEDs into a known state.
 */

#include <stdio.h>

#include <zephyr/kernel.h>

#include "power_manager.h"
#include "shell_cmds.h"

int main(void)
{
	printf("CM33-S shell-power-mode on %s\n", CONFIG_BOARD);

	if (shell_cmds_init() < 0) {
		printf("CM33-S: shell indicator LEDs not ready\n");
		/* Non-fatal: shell still works, mode indicator is dark. */
	}

	pm_init();

	printf("CM33-S: initial power mode %s\n",
	       pm_mode_name(pm_current_mode()));
	return 0;
}
