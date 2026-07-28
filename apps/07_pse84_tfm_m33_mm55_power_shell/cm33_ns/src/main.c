/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33 non-secure application bring-up for
 * apps/07_pse84_tfm_m33_mm55_power_shell.
 *
 * Phase C (shell layer): banner + GPIO indicator init + z_pm PING
 * round-trip + power-mode manager init. The following shell commands
 * are registered automatically at link time (via SHELL_CMD_REGISTER):
 *
 *   noidle [on|off]    -- veto WFI in idle thread (see src/cmd_noidle.c)
 *   sleep              -- Cy_SysPm_CpuEnterSleep     (src/cmd_sleep.c)
 *   deep_sleep         -- Cy_SysPm_CpuEnterDeepSleep (src/cmd_deep_sleep.c)
 *   hp / lp / ulp      -- Phase-C STUB, prints "not implemented"
 *   probe              -- Phase-C STUB, prints "not implemented"
 *
 * Phase D wires hp / lp / ulp / probe into a new set of z_pm ops:
 *   - Z_PM_OP_SWITCH_ACTIVE_MODE (mode transition body)
 *   - Z_PM_OP_CLOCK_PROBE (frequency measurement)
 *   - Z_PM_OP_DEEP_SLEEP_BIAS (called from here at boot, lowers the
 *     `deep_sleep` sleep-floor from ~today to the AN237976 Table-2
 *     row target -- see PLAN.md sec. 5.3)
 *   - Z_PM_OP_BOOT_CLOCK_RETUNE (DPLL_LP0 400 MHz -> 200 MHz + HF0
 *     /2 -> /1, called from here at boot to match project 06's
 *     baseline clock tree)
 *
 * At the end of Phase C: shell prompt appears, `sleep`/`deep_sleep`
 * enter their respective states and wake on the LPTIMER tick,
 * `noidle` correctly toggles the idle-thread WFI. `deep_sleep`
 * currently reaches a higher sleep floor than optimal because the
 * BGREF/CoreBuck/IHO/IMO DS-bias registers (all Bucket-C, PC=2 only)
 * still hold TF-M cycfg defaults; Phase D's DEEP_SLEEP_BIAS op fixes
 * this.
 */

#include <stdio.h>

#include <zephyr/kernel.h>

#include "gpio_indicators.h"
#include "power_manager.h"
#include "z_pm_client.h"

int main(void)
{
	printf("CM33-NS power-shell on %s\n", CONFIG_BOARD);

	if (gpio_indicators_init() < 0) {
		printf("gpio_indicators_init failed -- LEDs / pm_busy pin "
		       "will be dark\n");
	}

	/* Prove the z_pm out-of-tree TF-M partition + PSA call
	 * infrastructure is intact end-to-end. Non-fatal: a failure
	 * here does not prevent the shell from coming up, but every
	 * subsequent Phase-D op will also fail. */
	uint32_t cookie = 0;
	psa_status_t st = z_pm_ping(&cookie);

	if (st == PSA_SUCCESS && cookie == Z_PM_PING_COOKIE) {
		printf("z_pm ping ok: cookie=0x%08x\n", cookie);
	} else {
		printf("z_pm ping FAIL: status=%d cookie=0x%08x\n", (int)st,
		       cookie);
	}

	pm_init();
	printf("initial power mode: %s (stub -- Phase D wires the real "
	       "switcher)\n",
	       pm_mode_name(pm_current_mode()));

	return 0;
}
