/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Small in-memory log of cycle-count deltas for named phases
 *        within a single DVFS transition.
 *
 * Filled in by the active DVFS strategy (see
 * @c src/power_manager_hf0_divider.c,
 * @c src/power_manager_pll_retune.c) via
 * @c pm_phase_log_reset() at the start of a transition and repeated
 * @c pm_phase_log_record() calls between the transition's PDL
 * primitives. Dumped by @c pm_phase_log_print() from
 * @c pm_switch_to() right after its aggregate "[pm] transition ..."
 * line, using the same @c effective_hz (MIN of CLK_HF0 pre/post)
 * so per-phase values sum to that aggregate figure.
 *
 * Single-slot: only the most recent transition is stored. That is
 * sufficient because @c pm_switch_to() is the sole consumer and
 * flushes the log right after each call.
 */

#ifndef PM_PHASE_LOG_H_
#define PM_PHASE_LOG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Discard any prior record and set the transition label
 *  (printed as "[pm] <label> phases: ..."). @p label must remain
 *  valid until the next @c pm_phase_log_print() call -- pass a
 *  string literal. */
void pm_phase_log_reset(const char *label);

/** Append a named phase.
 *
 *  @param name    Human-readable phase label; must remain valid
 *                 until the next @c pm_phase_log_print(). Pass a
 *                 string literal.
 *  @param cycles  Cortex-M SysTick cycle count elapsed during the
 *                 phase (delta of two @c k_cycle_get_32() reads).
 *  @param hz      CPU frequency the SysTick was ACCUMULATING AT
 *                 during that phase. Must be per-phase because the
 *                 CPU may run at different rates in different
 *                 phases of a single transition (e.g. PLL retune
 *                 goes source-freq -> IHO bypass -> intermediate ->
 *                 IHO bypass -> target). Silently drops entries
 *                 once the internal capacity is exceeded.
 */
void pm_phase_log_record(const char *name, uint32_t cycles, uint32_t hz);

/** Emit one line:
 *    [pm] <label> phases: name1=NNus name2=NNus ... total=NNus
 *  Each phase converts its own cycles to microseconds via its own
 *  recorded @c hz. No-op if the log is empty. */
void pm_phase_log_print(void);

/** Total wall time of the recorded phases, in microseconds.
 *  Computed as sum-of-(cycles / hz) per entry, so it exactly
 *  matches what @c pm_phase_log_print emits as `total=`. Returns 0
 *  if the log is empty. */
uint32_t pm_phase_log_total_us(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PHASE_LOG_H_ */
