/**
 * @file main.c
 * @brief CM33 non-secure shell-controlled LED for kit_pse84_eval.
 *
 * Registers a top-level Zephyr shell command @c led with two
 * subcommands, @c on and @c off, that drive @c led0 (red LED) on the
 * PSE84 evaluation kit. There is no timer and no background work: the
 * LED state changes synchronously inside the shell command handler.
 *
 * @copyright Copyright (c) 2026
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

/** Devicetree handle for @c led0 (red LED, alias in the board DTS). */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/**
 * @brief Shell handler for `led on`.
 *
 * Drives @c led0 to its active state (respecting the polarity flag in
 * devicetree, i.e. @c GPIO_ACTIVE_LOW if present).
 *
 * @param sh   Shell instance the command was entered on.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success.
 */
static int cmd_led_on(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	gpio_pin_set_dt(&led, 1);
	shell_print(sh, "led: on");
	return 0;
}

/**
 * @brief Shell handler for `led off`.
 *
 * Drives @c led0 to its inactive state.
 *
 * @param sh   Shell instance the command was entered on.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success.
 */
static int cmd_led_off(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	gpio_pin_set_dt(&led, 0);
	shell_print(sh, "led: off");
	return 0;
}

/**
 * @brief Subcommand table for the @c led command.
 *
 * Built at link time by the Zephyr shell macros; no runtime
 * registration call is required.
 */
SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_led, SHELL_CMD(on, NULL, "Turn LED on", cmd_led_on),
    SHELL_CMD(off, NULL, "Turn LED off", cmd_led_off), SHELL_SUBCMD_SET_END);

/** Register the top-level @c led command with the Zephyr shell. */
SHELL_CMD_REGISTER(led, &sub_led, "LED control", NULL);

/**
 * @brief Application entry point.
 *
 * Prints a boot banner and configures @c led0 in its inactive state,
 * then returns. The Zephyr shell thread (enabled by
 * @c CONFIG_SHELL=y) keeps the system alive and dispatches user
 * commands from the UART console.
 *
 * @return Always 0.
 */
int main(void)
{
	printf("CM33-NS shell-blinky on %s\n", CONFIG_BOARD);

	if (!gpio_is_ready_dt(&led)) {
		printf("CM33-NS: led0 not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
		printf("CM33-NS: failed to configure led0\n");
		return 0;
	}

	return 0;
}
