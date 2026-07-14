/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33-Secure shell-controlled active-power-mode demo.
 *
 * Registers three top-level Zephyr shell commands -- @c ulp, @c lp,
 * @c hp -- that switch the PSoC Edge active power mode between
 * Ultra-Low-Power (50 MHz CM33 core), Low-Power (80 MHz) and
 * High-Performance (200 MHz). A fourth command @c probe measures
 * and prints the actual DPLL_LP0 / CLK_HF0 / CLK_HF10 frequencies.
 *
 * Each mode command drives the on-board RGB LEDs to indicate the
 * incoming mode BEFORE the SoC starts changing clocks and voltage:
 *
 *   Mode  |  Red (led0)  Green (led1)
 *   ------|-----------------------------
 *   ULP   |     off          on
 *   LP    |     on           off
 *   HP    |     off          off
 *
 * @note led2 (blue) is owned by the diagnostic heartbeat thread
 *       (see src/diag.c). It blinks at 2 Hz whenever the CPU is
 *       alive — a solid or dark blue after a mode-switch "freeze"
 *       means the CPU itself has hung, blinking blue means only
 *       the console path is broken.
 *
 * The mode transitions themselves live in @ref power_manager.c and
 * call the PDL syspm entries directly — no TF-M, no SRF, no partition
 * trampoline. This is the simplest working shell-based mode switcher
 * on PSE84 (contrast with project 05, which sits on CM33-NS under
 * TF-M and has to route the same PDL calls through the SRF path via
 * an ifx_ext_sp user-module).
 */

#include <stdio.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "power_manager.h"

/** Devicetree handles for the two mode-indicator LEDs. led2 (blue)
 *  is owned by the diagnostic heartbeat thread and MUST NOT be
 *  touched here — see src/diag.c. */
static const struct gpio_dt_spec led_red =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green =
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/**
 * @brief Drive the two indicator LEDs so red/green match the target
 *        power mode. Blue is left alone (see file comment).
 *
 * Values are 0 (off) or 1 (on); the GPIO polarity from devicetree is
 * honoured by @c gpio_pin_set_dt().
 */
static void indicator_set(int red, int green)
{
	(void)gpio_pin_set_dt(&led_red, red);
	(void)gpio_pin_set_dt(&led_green, green);
}

/**
 * @brief Common shell handler body for a mode-switch command.
 *
 * Sets the indicator LEDs first (so the user sees the intended target
 * before the SoC starts changing clocks / voltages) and then invokes
 * @c pm_switch_to().
 */
static int do_switch(const struct shell *sh, pm_mode_t target, int red,
		     int green)
{
	shell_print(sh, "switching to %s", pm_mode_name(target));
	indicator_set(red, green);

	int rc = pm_switch_to(target);

	if (rc != 0) {
		shell_error(sh, "power mode switch failed (%d)", rc);
		return rc;
	}
	shell_print(sh, "now in %s", pm_mode_name(pm_current_mode()));
	return 0;
}

/** @brief Shell handler for `ulp` (green on, red off; 50 MHz). */
static int cmd_ulp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return do_switch(sh, PM_MODE_ULP, 0, 1);
}

/** @brief Shell handler for `lp` (red on, green off; 80 MHz). */
static int cmd_lp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return do_switch(sh, PM_MODE_LP, 1, 0);
}

/** @brief Shell handler for `hp` (red & green off; 200 MHz). Blue
 *         is heartbeat-owned; it blinks whether or not we're in HP. */
static int cmd_hp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return do_switch(sh, PM_MODE_HP, 0, 0);
}

/** @brief Shell handler for `probe`: run the hardware clock
 *         measurement counters on DPLL_LP0 / CLK_HF0 / CLK_HF10 and
 *         print the actual measured frequencies. Useful to check
 *         the boot-time HP state (before any mode command has run)
 *         vs the state after `hp` / `lp` / `ulp`. */
static int cmd_probe(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	pm_clock_probe();
	return 0;
}

/* Register the top-level shell commands. */
SHELL_CMD_REGISTER(ulp, NULL, "Enter Ultra-Low-Power mode (50 MHz)", cmd_ulp);
SHELL_CMD_REGISTER(lp, NULL, "Enter Low-Power mode (80 MHz)", cmd_lp);
SHELL_CMD_REGISTER(hp, NULL, "Enter High-Performance mode (200 MHz)", cmd_hp);
SHELL_CMD_REGISTER(probe, NULL, "Measure actual clock frequencies", cmd_probe);

int main(void)
{
	printf("CM33-S shell-power-mode on %s\n", CONFIG_BOARD);

	if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
		printf("CM33-S: RGB LEDs not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0) {
		printf("CM33-S: failed to configure RGB LEDs\n");
		return 0;
	}

	pm_init();

	/* HP is the boot state; leave both indicator LEDs off. The blue
	 * (led2) heartbeat thread (src/diag.c) will already be blinking
	 * at ~2 Hz by the time we reach here. */
	indicator_set(0, 0);
	printf("CM33-S: initial power mode %s\n",
	       pm_mode_name(pm_current_mode()));

	return 0;
}
