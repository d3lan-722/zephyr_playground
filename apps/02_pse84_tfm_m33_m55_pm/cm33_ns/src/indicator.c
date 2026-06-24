/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief RGB LED indicator implementation (PORT16 P16.5/6/7).
 *
 * led0 = red (P16.7), led1 = green (P16.6), led2 = blue (P16.5)
 * — aliases come from the kit_pse84_eval common DTSI.
 *
 * Plan §6 colour map:
 *   green        = active phase
 *   red          = PM_STATE_SUSPEND_TO_IDLE   (cpu_sleep)
 *   blue         = PM_STATE_STANDBY sub 1     (cpu_deep_sleep)
 *   magenta R+B  = PM_STATE_STANDBY sub 2     (system_deep_sleep)
 *   cyan G+B     = PM_STATE_SUSPEND_TO_RAM    (system_deep_sleep_ram)
 *   white R+G+B  = PM_STATE_SOFT_OFF          (system_deep_sleep_off, latched)
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "indicator.h"

static const struct gpio_dt_spec led_red =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green =
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_blue =
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static void led_set(const struct gpio_dt_spec *led, int value)
{
	(void)gpio_pin_set_dt(led, value);
}

void indicator_init(void)
{
	(void)gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
}

void indicator_active_on(void) { led_set(&led_green, 1); }
void indicator_active_off(void) { led_set(&led_green, 0); }

void indicator_cpu_sleep_on(void) { led_set(&led_red, 1); }
void indicator_cpu_sleep_off(void) { led_set(&led_red, 0); }

void indicator_cpu_deep_sleep_on(void) { led_set(&led_blue, 1); }
void indicator_cpu_deep_sleep_off(void) { led_set(&led_blue, 0); }

void indicator_system_deep_sleep_on(void)
{
	led_set(&led_red, 1);
	led_set(&led_blue, 1);
}
void indicator_system_deep_sleep_off(void)
{
	led_set(&led_red, 0);
	led_set(&led_blue, 0);
}

void indicator_system_ds_ram_on(void)
{
	led_set(&led_green, 1);
	led_set(&led_blue, 1);
}
void indicator_system_ds_ram_off(void)
{
	led_set(&led_green, 0);
	led_set(&led_blue, 0);
}

void indicator_system_ds_off_latch(void)
{
	led_set(&led_red, 1);
	led_set(&led_green, 1);
	led_set(&led_blue, 1);
}
