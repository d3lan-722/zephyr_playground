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
 * line.
 *
 * The log stores RAW SysTick cycle counts, not microseconds. The
 * CPU frequency changes several times mid-transition (source ->
 * IHO bypass -> intermediate -> IHO bypass -> target for PLL
 * retune; source -> target for HF0 divider), and no single-rate
 * conversion is correct for the whole span -- so the firmware
 * side prints cycles and leaves wall-time authority to external
 * instrumentation (PPK2 D7 GPIO pulse, scope on P3.1, ...).
 * Post-processing (scripts/postprocess.py) cross-references cycle
 * counts with PPK2-observed pulse widths.
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
 *  (printed as "[pm] <label> phase cycles: ..."). @p label must
 *  remain valid until the next @c pm_phase_log_print() call --
 *  pass a string literal. */
void pm_phase_log_reset(const char *label);

/** Append a named phase.
 *
 *  @param name    Human-readable phase label; must remain valid
 *                 until the next @c pm_phase_log_print(). Pass a
 *                 string literal.
 *  @param cycles  Cortex-M SysTick cycle count elapsed during the
 *                 phase (delta of two @c k_cycle_get_32() reads).
 *                 Raw count -- do NOT convert to us here, because
 *                 the CPU clock rate changes across phases.
 */
void pm_phase_log_record(const char *name, uint32_t cycles);

/** Emit one line:
 *    [pm] <label> phase cycles: name1=NN name2=NN ... total=NN
 *  Values are raw cycle counts. No-op if the log is empty. */
void pm_phase_log_print(void);

/** Total cycles across the recorded phases (sum). Returns 0 if
 *  the log is empty. */
uint32_t pm_phase_log_total_cycles(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PHASE_LOG_H_ */
