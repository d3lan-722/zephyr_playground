/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief `sleep` shell command -- put CM33 into CPU-sleep (WFI) until
 *        the next NVIC IRQ (typically a UART RX character).
 *
 * Motivation: with `noidle on` the CPU spin-loops in idle and pulls
 * the mode's real active current (HP 5.36 / LP 2.34 / ULP 1.54 mA
 * measured). Running an explicit `sleep` puts the core into WFI on
 * demand and drops the current back to the sleep floor (HP 3.34 /
 * LP 1.73 / ULP 1.16 mA measured) until a keypress. Together with
 * `noidle`, the pair gives an on-demand active-vs-sleep A/B
 * measurement without waiting for Zephyr's idle thread to decide.
 *
 * Cycle instrumentation: reads @c k_cycle_get_32 (which on this SoC
 * is @c Cy_MCWDT_GetCountCascaded (counter2 @ 50 MHz LPTIMER)) both
 * sides of the @c __WFI. The delta is wall-clock time -- the CPU
 * pipeline was stopped, but the LPTIMER runs off HF10 (independent
 * of HF0). This is the number PPK2's D7 sees as the sleep window
 * width.
 *
 * pm-busy GPIO framing so external post-processing (PPK2 + D7 line)
 * can locate the WFI window automatically:
 *
 *   pm-busy HIGH       cmd running (banner print + TX drain, ~20 ms)
 *   pm-busy LOW        WFI window (variable duration -- user waits,
 *                                  presses key, chip wakes)
 *   pm-busy HIGH       post-wake (elapsed print, ~1 ms)
 *   pm-busy LOW        back to shell prompt
 *
 * IRQ prologue mirrors tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c:
 * PRIMASK on, BASEPRI off, so the CPU still wakes on any enabled
 * NVIC source but interrupts don't actually take (they just pend
 * and get serviced after __enable_irq). Without this, WFI can
 * either miss the wake (BASEPRI masks the source out) or run the
 * ISR mid-instrumentation (jitters the cycle count).
 *
 * Break-even relative to `noidle on`:
 *   The entry/exit overhead is dominated by shell_print + k_msleep
 *   (tens of ms) which is a demo-only cost. The interesting number
 *   is the WFI entry/exit itself: on Cortex-M33 that is a handful
 *   of CPU cycles either side of the instruction (order 10 ns at
 *   200 MHz). At any HP/LP/ULP active-vs-sleep delta above ~0.4 mA,
 *   break-even is under 1 us. That is: any sleep window longer than
 *   ~1 us is a net energy win. Post-processing table quantifies
 *   this per mode.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/time_units.h>

#include <cmsis_core.h>
#include <cy_sysclk.h>

#include <stddef.h>

#include "gpio_indicators.h"

/**
 * @brief Shell handler for `sleep`.
 *
 * Blocks the shell thread on WFI until the next NVIC interrupt.
 * The chip's UART RX IRQ is enabled and unmasked, so any character
 * typed in the terminal wakes the CPU. Other enabled ISRs
 * (LPTIMER, workqueue) may also wake spuriously; the printed
 * duration lets the user distinguish "keypress wake" (100+ ms) from
 * "housekeeping wake" (~80 ms periodic on this build).
 */
static int cmd_sleep(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/* Mark: sleep command is running (pm-busy HIGH). */
	gpio_indicators_transition_begin();

	shell_print(sh, "entering CPU sleep -- press any key to wake");

	/* Let the shell TX ring drain before WFI. In IRQ-driven mode
	 * the TX-empty IRQ would spuriously wake us mid-drain; a
	 * short sleep here yields to that IRQ path via the idle
	 * thread (which itself still WFIs -- noidle only affects
	 * arch_cpu_idle, not k_sleep) and returns once the shell TX
	 * is quiescent. 20 ms is enough for ~200 chars at 115200 8N1. */
	k_msleep(20);

	/* Mark: about to WFI (pm-busy HIGH -> LOW). Post-processing
	 * uses this falling edge as the sleep-window start. */
	gpio_indicators_transition_end();

	uint32_t t_enter = k_cycle_get_32();

	/* PRIMASK on, BASEPRI off -- see file-level comment. */
	__disable_irq();
	irq_unlock(0);
	__DSB();
	__WFI();
	__enable_irq();

	uint32_t t_exit = k_cycle_get_32();

	/* Mark: woke up (pm-busy LOW -> HIGH). Post-processing uses
	 * this rising edge as the sleep-window end. */
	gpio_indicators_transition_begin();

	uint32_t dwell_cyc = t_exit - t_enter;

	/* IMPORTANT: k_cycle_get_32 reads Cortex-M SysTick, which is
	 * clocked from HF0 (the CPU clock). This build reprograms HF0
	 * per mode (HP=200 / LP=66 / ULP=50 MHz), but Zephyr's
	 * CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC is a compile-time constant
	 * baked at HP. Using k_cyc_to_us_floor32 here would mis-report
	 * dwell by up to 4x. Convert against the LIVE HF0 frequency
	 * instead, read from the PDL at print time.
	 *
	 * Broader impact (not fixed here): every Zephyr timer function
	 * -- k_msleep, k_sleep, k_timer, thread timeouts -- is scaled
	 * by the same wrong factor at LP/ULP. That is why the
	 * "mysterious 80 ms periodic wake" observed at LP is really
	 * Zephyr's ~26 ms housekeeping tick stretched by the HF0
	 * ratio. Fix belongs in the system-timer selection (use
	 * MCWDT0/LPTIMER on HF10 which is constant across modes).
	 * Tracked separately. */
	uint32_t hf0_hz = Cy_SysClk_ClkHfGetFrequency(0U);
	uint32_t dwell_us =
	    (uint32_t)(((uint64_t)dwell_cyc * 1000000U) / hf0_hz);

	shell_print(sh, "woke after %u us (%u cycles @ live HF0 = %u Hz)",
		    dwell_us, dwell_cyc, hf0_hz);

	/* Mark: shell prompt returning (pm-busy HIGH -> LOW). */
	gpio_indicators_transition_end();
	return 0;
}

SHELL_CMD_REGISTER(sleep, NULL,
		   "Enter CPU sleep (WFI) until any NVIC IRQ (e.g. UART RX). "
		   "Prints wall-clock dwell on wake. Pair with `noidle on` "
		   "to see the active-vs-sleep current delta.",
		   cmd_sleep);
