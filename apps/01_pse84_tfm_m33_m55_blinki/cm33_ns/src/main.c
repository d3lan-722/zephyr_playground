/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33 non-secure blinky for the kit_pse84_eval.
 * Blinks the red LED (led0) at 1 Hz.
 */

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define BLINK_PERIOD_MS 100

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	printf("CM33-NS blinky on %s\n", CONFIG_BOARD);

	if (!gpio_is_ready_dt(&led)) {
		printf("CM33-NS: led0 not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
		printf("CM33-NS: failed to configure led0\n");
		return 0;
	}

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
