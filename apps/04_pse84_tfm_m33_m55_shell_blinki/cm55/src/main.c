/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM55 blinky for the kit_pse84_eval.
 * Blinks the green LED (led1) at ~2 Hz so it can be visually
 * distinguished from the CM33-NS red LED running at 1 Hz.
 */

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define BLINK_PERIOD_MS 100

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

int main(void)
{
	printf("CM55 blinky on %s\n", CONFIG_BOARD);

	if (!gpio_is_ready_dt(&led)) {
		printf("CM55: led1 not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
		printf("CM55: failed to configure led1\n");
		return 0;
	}

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
