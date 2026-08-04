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
 * Each mode command:
 *   1. Prints "switching to <mode>".
 *   2. Drives the mode-indicator LEDs to the target colour
 *      BEFORE the SoC starts changing clocks and voltage, so the
 *      intended target is visible even if the transition itself
 *      hangs.
 *   3. Asserts the PM-busy scope trigger (P3.1 high) so external
 *      instrumentation can correlate the software transition
 *      window with current-draw waveforms.
 *   4. Calls pm_switch_to().
 *   5. De-asserts the PM-busy scope trigger (P3.1 low).
 *   6. Prints "now in <mode>" on success.
 *
 * LED colour map (see @c src/gpio_indicators.c):
 *
 *   Mode  |  Red (led0)  Green (led1)
 *   ------|-----------------------------
 *   ULP   |     off          on
 *   LP    |     on           off
 *   HP    |     off          off
 */

#include "shell_cmds.h"

#include <stddef.h>

#include <zephyr/shell/shell.h>

#include "gpio_indicators.h"
#include "power_manager.h"

/**
 * @brief Common body for a mode-switch shell command.
 *
 * Drives the mode-indicator LEDs, asserts the PM-busy scope
 * trigger, runs the transition, de-asserts the trigger, and
 * prints the outcome.
 *
 * @param sh      Zephyr shell context to print to.
 * @param target  The power mode to enter.
 * @param red     Value for the red indicator LED (0 = off, 1 = on).
 * @param green   Value for the green indicator LED (0 = off, 1 = on).
 * @return 0 on success, negative errno-like value from
 *         @c pm_switch_to on failure.
 */
static int do_mode_switch(const struct shell *sh, pm_mode_t target, int red,
			  int green)
{
	int rc;

	shell_print(sh, "switching to %s", pm_mode_name(target));
	gpio_indicators_set_mode_leds(red, green);

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
 * command has run) vs the state after `hp` / `lp` / `ulp`.
 */
static int cmd_probe(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	pm_clock_probe();
	return 0;
}

/* Register the top-level shell commands. SHELL_CMD_REGISTER uses
 * linker sections, so linking this translation unit into the image
 * is enough -- no runtime registration hook needed. */
SHELL_CMD_REGISTER(ulp, NULL, "Enter Ultra-Low-Power mode (CM33 50 MHz)",
		   cmd_ulp);
SHELL_CMD_REGISTER(lp, NULL, "Enter Low-Power mode (CM33 80 MHz)", cmd_lp);
SHELL_CMD_REGISTER(hp, NULL, "Enter High-Performance mode (CM33 200 MHz)",
		   cmd_hp);
SHELL_CMD_REGISTER(probe, NULL, "Measure actual clock frequencies", cmd_probe);
