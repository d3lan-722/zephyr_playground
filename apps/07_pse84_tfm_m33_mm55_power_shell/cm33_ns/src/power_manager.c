/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Phase-C STUB implementation of the active-power-mode
 *        manager. Replaced entirely by the Phase-D implementation
 *        that dispatches to the z_pm partition.
 *
 * At the end of Phase C the shell prompt is up and `sleep`,
 * `deep_sleep`, `noidle` all work at HP. Typing `hp`, `lp`, `ulp`
 * or `probe` reaches this stub, which prints "not implemented" and
 * returns cleanly instead of hanging the shell.
 *
 * Phase D replaces the body with a thin wrapper around
 * `z_pm_switch_active_mode()` and `z_pm_clock_probe()` -- see
 * PLAN.md sec. 5.1 / 5.2.
 */

#include "power_manager.h"

#include <errno.h>

#include <zephyr/sys/printk.h>

/** Current active power mode. Boot state is HP (SoC comes up in
 *  the DPLL_LP0-400-MHz / CLK_HF0-/2 = 200 MHz HP configuration
 *  from the board defaults; Phase D adds the retune-to-200-MHz-
 *  baseline via a boot-time z_pm op). */
static pm_mode_t s_current_mode = PM_MODE_HP;

const char *pm_mode_name(pm_mode_t m)
{
	switch (m) {
	case PM_MODE_ULP:
		return "ulp";
	case PM_MODE_LP:
		return "lp";
	case PM_MODE_HP:
		return "hp";
	default:
		return "?";
	}
}

pm_mode_t pm_current_mode(void) { return s_current_mode; }

void pm_init(void) { s_current_mode = PM_MODE_HP; }

int pm_switch_to(pm_mode_t target)
{
	if (target != PM_MODE_HP && target != PM_MODE_LP &&
	    target != PM_MODE_ULP) {
		return -EINVAL;
	}
	if (target == s_current_mode) {
		return 0;
	}
	printk("[pm] Phase-C stub: switch to %s not implemented "
	       "(Phase D wires z_pm SWITCH_ACTIVE_MODE)\n",
	       pm_mode_name(target));
	return -ENOSYS;
}

void pm_clock_probe(void)
{
	printk("[pm] Phase-C stub: clock probe not implemented "
	       "(Phase D wires z_pm CLOCK_PROBE)\n");
}
