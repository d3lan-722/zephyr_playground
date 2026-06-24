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

	while (1) {
		indicator_active_on();
		k_busy_wait(BLINK_ON_MS * 1000U);
		indicator_active_off();
		k_msleep(SLEEP_BETWEEN_BLINKS_MS);
	}

	return 0;
}
