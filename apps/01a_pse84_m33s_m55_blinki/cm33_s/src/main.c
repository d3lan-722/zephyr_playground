/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33-Secure blinky for kit_pse84_eval. Blinks the red LED (led0)
 * at 1 Hz after releasing the CM55 core through a local copy of the
 * PSE84 secure boot code (src/pse84_boot_local.c) which returns
 * instead of trapping the CM33 in for(;;).
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "pse84_boot_local.h"

#define BLINK_PERIOD_MS 500

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	app_pse84_cm55_startup();

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
		return 0;
	}

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
