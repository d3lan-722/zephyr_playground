/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Implementation of the per-transition phase log.
 * See @c pm_phase_log.h for the interface contract.
 */

#include "pm_phase_log.h"

#include <stdint.h>

#include <zephyr/sys/printk.h>

/* Sized for the strategy with the most phases -- currently PLL_RETUNE
 * and HF0_DIVIDER both use three (pll_pre/volt/pll_post and
 * div/enter/rram respectively). Bump if a future strategy needs
 * finer granularity. */
#define PM_PHASE_LOG_MAX_PHASES 8

static uint32_t s_cycles[PM_PHASE_LOG_MAX_PHASES];
static const char *s_names[PM_PHASE_LOG_MAX_PHASES];
static uint8_t s_count;
static const char *s_label = "";

void pm_phase_log_reset(const char *label)
{
	s_label = (label != NULL) ? label : "";
	s_count = 0u;
}

void pm_phase_log_record(const char *name, uint32_t cycles)
{
	if (s_count < PM_PHASE_LOG_MAX_PHASES) {
		s_names[s_count] = (name != NULL) ? name : "?";
		s_cycles[s_count] = cycles;
		s_count++;
	}
}

void pm_phase_log_print(void)
{
	uint32_t total = 0u;
	uint8_t i;

	if (s_count == 0u) {
		return;
	}
	printk("[pm] %s phase cycles:", s_label);
	for (i = 0u; i < s_count; i++) {
		total += s_cycles[i];
		printk(" %s=%u", s_names[i], s_cycles[i]);
	}
	printk(" total=%u\n", total);
}

uint32_t pm_phase_log_total_cycles(void)
{
	uint32_t total = 0u;
	uint8_t i;

	for (i = 0u; i < s_count; i++) {
		total += s_cycles[i];
	}
	return total;
}
