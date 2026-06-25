/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33 non-secure indicator-loop blinky for the kit_pse84_eval.
 * Phase 1 of the porting plan: drive the green RGB indicator only,
 * no PM core involvement yet.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "indicator.h"
#include "z_pm_client.h"

#define BLINK_ON_MS 200
/* Phase 5 system_deep_sleep window: 1000 ms <= residency < 2000 ms.
 * Expected indicator: magenta (red+blue) between green flashes,
 * dispatched as PM_STATE_STANDBY substate 2.
 */
#define SLEEP_BETWEEN_BLINKS_MS 1500

int main(void)
{
	printf("CM33-NS indicator blinky on %s\n", CONFIG_BOARD);

	indicator_init();

	/* Phase 6 step 1: ping the z_pm secure partition to validate the
	 * out-of-tree partition + PSA call infrastructure end-to-end.
	 * Logs the result once at boot; does not affect the blink loop.
	 */
	uint32_t cookie = 0;
	psa_status_t st = z_pm_ping(&cookie);

	if (st == PSA_SUCCESS && cookie == Z_PM_PING_COOKIE) {
		printf("z_pm ping ok: cookie=0x%08x\n", cookie);
	} else {
		printf("z_pm ping FAIL: status=%d cookie=0x%08x\n", (int)st,
		       cookie);
	}

	while (1) {
		indicator_active_on();
		k_busy_wait(BLINK_ON_MS * 1000U);
		indicator_active_off();
		k_msleep(SLEEP_BETWEEN_BLINKS_MS);
	}

	return 0;
}
