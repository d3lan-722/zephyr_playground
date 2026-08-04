/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33-Secure shell-controlled active-power-mode demo -- bring-up.
 *
 * Derived from apps/06_pse84_m33_s_shell_ulp_lp_hp/src/main.c but
 * runs on the 01a dual-core scaffolding: this CM33-S image releases
 * CM55 (which parks in Cy_SysPm_CpuEnterDeepSleep) BEFORE starting
 * the shell. That second DEEPSLEEP requestor is what unblocks the
 * PWRMODE state machine on the `deep_sleep` shell command -- the
 * missing piece from project 06.
 *
 * Structural modules (unchanged from 06):
 *
 *   src/power_manager.[ch]       -- HP/LP/ULP transitions, PLL retune,
 *                                   clock-measurement probe.
 *   src/shell_cmds.[ch]          -- ulp / lp / hp / probe shell handlers.
 *   src/cmd_{noidle,sleep,deep_sleep}.c
 *                                -- noidle veto + WFI + SLEEPDEEP+WFI.
 *   src/gpio_indicators.[ch]     -- mode LEDs (led0 red, led1 green) +
 *                                   PM-busy scope trigger on P3.1.
 *   src/diag.[ch]                -- raw-SCB tracing + blue heartbeat.
 *   src/pm_early_boot.c          -- MCWDT0 reset (LPTIMER prereq).
 *   src/pse84_boot_local.[ch]    -- local copy of pse84_boot.c that
 *                                   releases CM55 and RETURNS, so the
 *                                   Zephyr scheduler stays alive.
 */

#include <stdio.h>

#include <zephyr/kernel.h>

#include "gpio_indicators.h"
#include "power_manager.h"
#include "pse84_boot_local.h"

int main(void)
{
	printf("CM33-S shell-power-mode on %s\n", CONFIG_BOARD);

	app_pse84_cm55_startup();
	printf("CM33-S: CM55 released (parking in deep sleep)\n");

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
