/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shell command handlers for project 06.
 *
 * Registers four top-level Zephyr shell commands:
 *
 *   ulp    -- transition SoC to Ultra-Low-Power mode (CM33 50 MHz)
 *   lp     -- transition SoC to Low-Power mode         (CM33 80 MHz)
 *   hp     -- transition SoC to High-Performance mode  (CM33 200 MHz)
 *   probe  -- measure and print live DPLL_LP0 / CLK_HF0 / CLK_HF10
 *
 * Each mode command drives the on-board RGB LEDs to the target
 * mode's color BEFORE the SoC starts changing clocks and voltage,
 * so the intended target is visible even if the transition itself
 * hangs. The colour map:
 *
 *   Mode  |  Red (led0)  Green (led1)
 *   ------|-----------------------------
 *   ULP   |     off          on
 *   LP    |     on           off
 *   HP    |     off          off
 *
 * @note led2 (blue) is owned by the diagnostic heartbeat thread
 *       (see @c src/diag.c) and is deliberately NOT touched here.
 */

#include "shell_cmds.h"

#include <stddef.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>

#include "power_manager.h"

/** @brief Devicetree handle for led0 (red -- LP indicator). */
static const struct gpio_dt_spec led_red =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/** @brief Devicetree handle for led1 (green -- ULP indicator). */
static const struct gpio_dt_spec led_green =
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/**
 * @brief Drive the two indicator LEDs so red/green match the target
 *        power mode. Blue is left alone (see file comment).
 *
 * @param red    Non-zero to light red, zero to turn it off.
 * @param green  Non-zero to light green, zero to turn it off.
 */
static void indicator_set(int red, int green)
{
	(void)gpio_pin_set_dt(&led_red, red);
	(void)gpio_pin_set_dt(&led_green, green);
}

/**
 * @brief Common body for a mode-switch shell command.
 *
 * Prints "switching to <mode>", drives the indicator LEDs (so the
 * intended target is visible before the SoC starts changing clocks
 * and voltage), invokes @c pm_switch_to, and prints
 * "now in <mode>" on success or "power mode switch failed (%d)" on
 * failure.
 *
 * @param sh      Zephyr shell context to print to.
 * @param target  The power mode to enter.
 * @param red     Indicator-LED value for the red LED.
 * @param green   Indicator-LED value for the green LED.
 * @return 0 on success, negative errno-like value from
 *         @c pm_switch_to on failure.
 */
static int do_mode_switch(const struct shell *sh, pm_mode_t target, int red,
			  int green)
{
	int rc;

	shell_print(sh, "switching to %s", pm_mode_name(target));
	indicator_set(red, green);

	rc = pm_switch_to(target);
	if (rc != 0) {
		shell_error(sh, "power mode switch failed (%d)", rc);
		return rc;
	}
	shell_print(sh, "now in %s", pm_mode_name(pm_current_mode()));
	return 0;
}

/** @brief Shell handler for `ulp` (green on, red off; CM33 50 MHz). */
static int cmd_ulp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return do_mode_switch(sh, PM_MODE_ULP, 0, 1);
}

/** @brief Shell handler for `lp` (red on, green off; CM33 80 MHz). */
static int cmd_lp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return do_mode_switch(sh, PM_MODE_LP, 1, 0);
}

/**
 * @brief Shell handler for `hp` (red & green off; CM33 200 MHz).
 *
 * Blue LED is heartbeat-owned and keeps blinking regardless of the
 * active mode; see @c src/diag.c.
 */
static int cmd_hp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return do_mode_switch(sh, PM_MODE_HP, 0, 0);
}

/**
 * @brief Shell handler for `probe`: run the hardware clock
 *        measurement counters on DPLL_LP0 / CLK_HF0 / CLK_HF10 and
 *        print the actual measured frequencies.
 *
 * Useful for checking the boot-time HP state (before any mode
 * command has run) vs the state after `hp` / `lp` / `ulp`, and for
 * sanity-checking the numbers that @c pm_switch_to prints
 * automatically at the end of every transition.
 */
static int cmd_probe(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	pm_clock_probe();
	return 0;
}

/* Register the top-level shell commands with the Zephyr shell.
 * SHELL_CMD_REGISTER uses linker sections, so simply linking this
 * translation unit into the image is enough -- no runtime hook. */
SHELL_CMD_REGISTER(ulp, NULL, "Enter Ultra-Low-Power mode (CM33 50 MHz)",
		   cmd_ulp);
SHELL_CMD_REGISTER(lp, NULL, "Enter Low-Power mode (CM33 80 MHz)", cmd_lp);
SHELL_CMD_REGISTER(hp, NULL, "Enter High-Performance mode (CM33 200 MHz)",
		   cmd_hp);
SHELL_CMD_REGISTER(probe, NULL, "Measure actual clock frequencies", cmd_probe);

int shell_cmds_init(void)
{
	if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) < 0) {
		return -EIO;
	}
	return 0;
}
