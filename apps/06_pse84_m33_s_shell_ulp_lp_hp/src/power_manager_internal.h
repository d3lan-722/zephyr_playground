/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Internal API between the orchestration layer
 *        (@c power_manager.c) and the currently-selected DVFS
 *        strategy (@c power_manager_pll_retune.c or
 *        @c power_manager_hf0_divider.c).
 *
 * Not part of the module's public API -- @c shell_cmds.c and
 * friends must only include @c power_manager.h.
 *
 * Both strategy files declare the same @c pm_strategy_* symbols
 * but only one contributes them per build (each file is guarded
 * top-to-bottom by its own @c PM_STRATEGY_* macro).
 *
 * See @c PLAN_dual_dvfs.md for the strategy trade-offs and
 * @c PLAN_pm_refactor.md for the reason this split exists.
 */

#ifndef POWER_MANAGER_INTERNAL_H_
#define POWER_MANAGER_INTERNAL_H_

#include <stdbool.h>

/* soc.h must precede cy_pdl.h -- it sets the CY_DEVICE_* macros
 * that cy_device_headers.h checks. Without it, that header
 * short-circuits with `#error "Unsupported PSE84 device."`. */
#include <soc.h>
#include "cy_pdl.h"

#include "power_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * DVFS strategy selection (compile-time).
 *
 * Exactly one of the two macros below must be defined at build time.
 * The macro is placed in this header (not the .c) so both the
 * orchestration file (power_manager.c) and the two strategy files
 * (power_manager_pll_retune.c, power_manager_hf0_divider.c) see the
 * same value: each strategy file wraps its whole body in
 * #ifdef PM_STRATEGY_..., so only one contributes symbols per build.
 *
 * See PLAN_dual_dvfs.md for the trade-offs; short version:
 *   PM_STRATEGY_PLL_RETUNE   -- reprograms DPLL_LP0 per mode via
 *                               SysPm callbacks. Hits CM33 spec
 *                               exactly (200/80/50 MHz) but ~450 ms
 *                               per transition.
 *   PM_STRATEGY_HF0_DIVIDER  -- keeps DPLL at 200 MHz, changes only
 *                               CLK_HF0 divider. LP lands at 66 MHz
 *                               (200/3, closest integer); ~1-5 ms
 *                               per transition.
 * ------------------------------------------------------------------ */

// #define PM_STRATEGY_PLL_RETUNE 1
#define PM_STRATEGY_HF0_DIVIDER 1

#if defined(PM_STRATEGY_PLL_RETUNE) == defined(PM_STRATEGY_HF0_DIVIDER)
#error                                                                         \
    "Exactly one of PM_STRATEGY_PLL_RETUNE / PM_STRATEGY_HF0_DIVIDER must be defined"
#endif

/* ------------------------------------------------------------------
 * Strategy contract -- implemented by the selected
 * power_manager_<strategy>.c file.
 * ------------------------------------------------------------------ */

/** Called once from @c pm_init(). Any strategy-specific bring-up
 *  (SysPm callback registration, initial hardware state, ...). */
void pm_strategy_init(void);

/** Perform the actual mode transition. Returns 0 on success or
 *  a negative errno. Called from @c pm_switch_to() after the
 *  no-op / invalid-target guards. */
int pm_strategy_transition(pm_mode_t source, pm_mode_t target);

/** Human-readable label for @p m, including the CM33 frequency the
 *  strategy actually delivers at that mode. Backs @c pm_mode_name(). */
const char *pm_strategy_mode_name(pm_mode_t m);

/** Emit a one-line strategy-status marker after the clock probe. */
void pm_strategy_probe_status(void);

/** True if @c pm_switch_to() must retune the SCB baud divider after
 *  a transition (PLL-retune changes @c CLK_HF10; HF0-divider does
 *  not). */
bool pm_strategy_needs_uart_retune(void);

/** Print a one-line breakdown of the phases inside the last
 *  @c pm_strategy_transition() call, converted to microseconds
 *  using @p effective_hz (the same MIN(pre, post) CLK_HF0 rate
 *  that @c pm_switch_to uses for its aggregate "us" figure). A
 *  strategy that has nothing to report may leave this empty. */
void pm_strategy_print_last_phases(uint32_t effective_hz);

/* ------------------------------------------------------------------
 * Shared helper -- implemented in power_manager.c, used by both
 * strategy files as the voltage-step primitive.
 * ------------------------------------------------------------------ */

/** Call the PDL @c Cy_SysPm_SystemEnter* entry matching @p target
 *  and emit the raw-SCB @c switch:Enter... marker. */
cy_en_syspm_status_t pm_syspm_enter(pm_mode_t target);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_INTERNAL_H_ */
