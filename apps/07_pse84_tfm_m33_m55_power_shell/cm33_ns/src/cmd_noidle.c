/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief `noidle` shell command + Zephyr idle-hook to suppress WFI.
 *
 * Motivation: on this build the Zephyr idle thread runs
 * @c arch_cpu_idle() which normally does @c __WFI(). That means
 * "shell prompt idle" current on the PPK2 is already CPU-sleep
 * current -- WFI is happening whenever no thread has work.
 *
 * For a DVFS demo we sometimes want to see the OPPOSITE: what
 * does the SoC pull when the CM33 is genuinely executing code
 * (fetching, decoding, executing) at 200 / 80 / 50 MHz? That is
 * the delta the "sleep" command will reclaim, and without a way
 * to force the CPU into that state on demand there is nothing to
 * compare against.
 *
 * Zephyr exposes exactly this knob via @c CONFIG_ARM_ON_ENTER_CPU_IDLE_HOOK.
 * When enabled, the arch layer calls @c z_arm_on_enter_cpu_idle()
 * right before it would execute WFI. Returning @c false skips
 * WFI entirely -- the idle thread then just spin-loops through
 * @c arch_cpu_idle() returning immediately, and higher-priority
 * threads (or ISRs) still preempt as usual.
 *
 * Shell interface:
 *
 *   noidle          -- print current state ("on" or "off")
 *   noidle on       -- suppress WFI in idle. CPU runs continuously
 *                      at the current HP/LP/ULP mode clock rate.
 *                      PPK2 shows the real active current for
 *                      that mode.
 *   noidle off      -- restore normal WFI in idle. This is the
 *                      default state at boot. PPK2 shows the
 *                      sleep floor plus whatever the workqueue /
 *                      housekeeping wakes cost.
 *
 * Nothing else in this project queries the flag -- the hook is
 * the only consumer. Made @c atomic_t so a stray future concurrent
 * writer is well-defined; the hook itself runs from the idle
 * thread and cannot be preempted by another writer today.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include <stddef.h>

/**
 * @brief When non-zero, @c z_arm_on_enter_cpu_idle returns false
 *        and the idle thread's WFI is skipped.
 */
static atomic_t s_prevent_idle;

/**
 * @brief ARM idle-entry hook (enabled by CONFIG_ARM_ON_ENTER_CPU_IDLE_HOOK).
 *
 * Called from @c arch_cpu_idle() with interrupts already disabled
 * via PRIMASK and BASEPRI set to 0. Return @c true to let the WFI
 * run (default), @c false to bypass it and return immediately.
 *
 * Keeping the check as a single atomic-read means no lock, no
 * branch predictor surprise, and the hook itself costs a handful
 * of cycles per idle entry -- negligible next to the WFI it
 * gates.
 */
bool z_arm_on_enter_cpu_idle(void) { return atomic_get(&s_prevent_idle) == 0; }

/**
 * @brief Shell handler for `noidle [on|off]`.
 *
 * With no argument, reports the current state. With "on" or
 * "off", sets it. Any other argument is a syntax error.
 */
static int cmd_noidle(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_print(sh, "noidle is %s",
			    (atomic_get(&s_prevent_idle) != 0) ? "on" : "off");
		return 0;
	}

	if (argc != 2) {
		shell_error(sh, "usage: noidle [on|off]");
		return -EINVAL;
	}

	if (strcmp(argv[1], "on") == 0) {
		atomic_set(&s_prevent_idle, 1);
		shell_print(sh, "noidle on -- CPU spins in idle (no WFI)");
		return 0;
	}
	if (strcmp(argv[1], "off") == 0) {
		atomic_set(&s_prevent_idle, 0);
		shell_print(sh, "noidle off -- WFI in idle (default)");
		return 0;
	}

	shell_error(sh, "usage: noidle [on|off]");
	return -EINVAL;
}

SHELL_CMD_REGISTER(noidle, NULL,
		   "Suppress WFI in idle thread. Use `noidle on` to see real "
		   "active current at the current HP/LP/ULP mode.",
		   cmd_noidle);
