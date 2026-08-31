/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control/clock_control_ifx_cat1.h>
#include <zephyr/sys/onoff.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);


int main(void)
{	const struct device *clk_hf10 = DEVICE_DT_GET(DT_NODELABEL(clk_hf10));
	struct onoff_manager *mgr = ifx_clock_get_onoff_manager(clk_hf10);
	
	struct onoff_client cli;
	int rc, res;
	sys_notify_init_spinwait(&cli.notify);

    	rc = onoff_request(mgr, &cli);
    	if (rc < 0) {
        	return rc;
    	}

    	while (sys_notify_fetch_result(&cli.notify, &res) == -EAGAIN) {
        	k_yield();
    	}

    	if (res < 0) {
        	return res;
    	}

	printf("Hello World! %s\n", CONFIG_BOARD);
	int ret;
	bool led_state = true;

	if (!gpio_is_ready_dt(&led)) {
	 	return 0;
	 }

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	ret = gpio_pin_toggle_dt(&led);
	led_state = !led_state;
	rc = onoff_release(mgr);


	/*while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
	 		return 0;
	 	}

	 	led_state = !led_state;
	 	printf("LED state: %s\n", led_state ? "ON" : "OFF");
	 	k_msleep(SLEEP_TIME_MS);
	 }*/
	 return 0;
}