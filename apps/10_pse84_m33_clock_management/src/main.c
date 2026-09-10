/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/clock_management/clock_driver.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include "cy_device.h"
#include <zephyr/drivers/clock_control/clock_control_ifx_cat1.h>
#include <zephyr/dt-bindings/clock/ifx_clock_source_common.h>
#include "infineon_uart_header.h"
#include "cy_device.h"
#include <cy_sysclk.h>
#include <zephyr/drivers/clock_management.h>
#include <zephyr/drivers/clock_management/clock.h>
/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led2)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
//static const struct clk *const clock_pll_lp1 = CLOCK_DT_GET(DT_NODELABEL(dpll_lp1));
const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(scb2));
extern volatile bool flag;
const struct clk *my_pll_clock = CLOCK_DT_GET(DT_NODELABEL(dpll_lp1));


int main(void)
{ 	
	uint8_t onoff_count = my_pll_clock->subsys_data->usage_cnt;
	printf("%u\n", onoff_count);
	int ret;
    	ret = ifx_cat1_uart_clk_on(dev);
	printf("Hello World! %s\n", CONFIG_BOARD);
	//ret = ifx_cat1_uart_clk_off(dev);
	/*printf("%u",clk_idx);
	bool led_state = true;
	if (!gpio_is_ready_dt(&led)) {
	 	return 0;
	 }

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}
	*/
	while (1) {
		/*ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
	 		return 0;
	 	}

	 	led_state = !led_state;
	 	//printf("LED state: %s\n", led_state ? "ON" : "OFF");
	 	k_msleep(SLEEP_TIME_MS);
		*/
		k_busy_wait(1000);
	 }
	 return 0;
}