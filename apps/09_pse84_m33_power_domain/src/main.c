/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>



/* 1000 msec = 1 sec */

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
const struct device *domain = DEVICE_DT_GET(DT_NODELABEL(hf_clock_domain_10));

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
volatile int instrumentation_marker;
extern volatile uint32_t flag;

__attribute__((used, noinline))
void instrumentation_trigger(void)
{
    instrumentation_marker = 1;
}
 
__attribute__((used, noinline))
void instrumentation_stopper(void)
{
    instrumentation_marker = 2;
}

int main(void)
{
    enum pm_device_state state;
    int rc;

    /* Estado ANTES de cualquier printk */
    rc = pm_device_state_get(domain, &state);
    if (rc == 0) {
        if (state == PM_DEVICE_STATE_ACTIVE) {
            printk("1");
        } else if (state == PM_DEVICE_STATE_SUSPENDED) {
            printk("2");
        } else if (state == PM_DEVICE_STATE_OFF) {
            printk("3");
        }
    }


    k_busy_wait(100000);
    /* Estado DESPUÉS del printk anterior */
    rc = pm_device_state_get(domain, &state);
    if (rc == 0) {
        if (state == PM_DEVICE_STATE_ACTIVE) {
            printk("4");
        } else if (state == PM_DEVICE_STATE_SUSPENDED) {
            printk("5");
        } else if (state == PM_DEVICE_STATE_OFF) {
            printk("6");
        }
    }

    while (1) {
        k_busy_wait(100000);
    	rc = pm_device_state_get(domain, &state);

	
    }
    return 0;
}