/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO indicators: mode LEDs (red / green) + PM-busy scope trigger
 * (P3.1). See gpio_indicators.h for the module rationale.
 */

#include "gpio_indicators.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

/** @brief Devicetree handle for led0 (red -- LP indicator). */
static const struct gpio_dt_spec led_red =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/** @brief Devicetree handle for led1 (green -- ULP indicator). */
static const struct gpio_dt_spec led_green =
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/** @brief Devicetree handle for the PM-busy scope trigger on P3.1.
 *
 *  Declared in the app-local overlay
 *  (`boards/kit_pse84_eval_pse846gps2dbzc4a_m33.overlay`) as
 *  `pm_signals/pm_busy { gpios = <&gpio_prt3 1 GPIO_ACTIVE_HIGH>; }`
 *  with the alias `pm-busy`.
 */
static const struct gpio_dt_spec pm_busy =
    GPIO_DT_SPEC_GET(DT_ALIAS(pm_busy), gpios);

int gpio_indicators_init(void)
{
	if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green) ||
	    !gpio_is_ready_dt(&pm_busy)) {
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&pm_busy, GPIO_OUTPUT_INACTIVE) < 0) {
		return -EIO;
	}
	return 0;
}

void gpio_indicators_set_mode_leds(int red, int green)
{
	(void)gpio_pin_set_dt(&led_red, red);
	(void)gpio_pin_set_dt(&led_green, green);
}

void gpio_indicators_transition_begin(void)
{
	(void)gpio_pin_set_dt(&pm_busy, 1);
}

void gpio_indicators_transition_end(void)
{
	(void)gpio_pin_set_dt(&pm_busy, 0);
}
