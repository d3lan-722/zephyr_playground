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
 * with three (pll_pre, volt, pll_post). Bump if a future strategy
 * needs finer granularity. */
#define PM_PHASE_LOG_MAX_PHASES 8

static uint32_t s_cycles[PM_PHASE_LOG_MAX_PHASES];
static uint32_t s_hz[PM_PHASE_LOG_MAX_PHASES];
static const char *s_names[PM_PHASE_LOG_MAX_PHASES];
static uint8_t s_count;
static const char *s_label = "";

static inline uint32_t entry_us(uint8_t i)
{
	uint32_t hz = (s_hz[i] != 0u) ? s_hz[i] : 1u;

	return (uint32_t)(((uint64_t)s_cycles[i] * 1000000ULL) / hz);
}

void pm_phase_log_reset(const char *label)
{
	s_label = (label != NULL) ? label : "";
	s_count = 0u;
}

void pm_phase_log_record(const char *name, uint32_t cycles, uint32_t hz)
{
	if (s_count < PM_PHASE_LOG_MAX_PHASES) {
		s_names[s_count] = (name != NULL) ? name : "?";
		s_cycles[s_count] = cycles;
		s_hz[s_count] = hz;
		s_count++;
	}
}

void pm_phase_log_print(void)
{
	uint32_t total_us = 0u;
	uint8_t i;

	if (s_count == 0u) {
		return;
	}
	printk("[pm] %s phases:", s_label);
	for (i = 0u; i < s_count; i++) {
		uint32_t us = entry_us(i);

		total_us += us;
		printk(" %s=%uus", s_names[i], us);
	}
	printk(" total=%uus\n", total_us);
}

uint32_t pm_phase_log_total_us(void)
{
	uint32_t total_us = 0u;
	uint8_t i;

	for (i = 0u; i < s_count; i++) {
		total_us += entry_us(i);
	}
	return total_us;
}
