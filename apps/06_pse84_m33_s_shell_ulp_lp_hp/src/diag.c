/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * See diag.h for the diagnosis matrix and rationale.
 */

#include "diag.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/*
 * SCB2 register block base. Hard-coded rather than derived from DT
 * because we deliberately want to poke this SCB even when the Zephyr
 * driver's cached configuration might be broken.
 *
 * PSE84 exposes SCB2 through two AHB aliases; the correct one depends
 * on the security state of the accessing bus master:
 *
 *   Non-Secure alias (M33-NS / M55-NS code):  0x429a0000
 *   Secure     alias (M33-S code):            0x529a0000
 *
 * This project builds for `kit_pse84_eval/pse846gps2dbzc4a/m33` (the
 * Secure-only board variant, CONFIG_TRUSTED_EXECUTION_SECURE=y), so
 * we MUST use the Secure alias. Writing the NS alias from Secure
 * code raises a BusFault (\"peripheral not accessible from current
 * security state\") that escalates to HardFault -- observed as the
 * whole CPU freezing on the very first diag_trace() call, the
 * heartbeat thread never getting to run, and no bytes on the wire.
 *
 * The pse84_s.dtsi node confirms the mapping:
 *   scb2: scb@529a0000 { reg = <0x529a0000 0xfd0>; ... }
 *
 * Note: the reference implementation
 * `tmp/zephyr_dvfs_dpm_proposed/common/pse84_aliases.h` hard-codes
 * 0x429a0000 because that project's raw-console lives in CM33-NS.
 *
 * SCB register offsets from Infineon `pdl/devices/include/ip/cyip_scb.h`
 * (verified: TX_FIFO_STATUS at 0x208, TX_FIFO_WR at 0x240 -- these
 * are IP-version-stable on the PSE84 SCB v3):
 *   TX_FIFO_STATUS  0x208   bits[8:0]  = USED entry count
 *                           bit  15    = SR_VALID (TX shift reg has a byte)
 *   TX_FIFO_WR      0x240   write here to enqueue a byte
 *
 * Earlier revisions of this file tried to use INTR_TX.UART_DONE
 * (offset 0xF80, bit 9) for the "TX complete" check. UART_DONE is a
 * LATCHED interrupt bit -- it only re-asserts on the transition
 * from "TX busy" to "TX idle". If the FIFO is already empty when
 * diag_trace_flush() is called (which is the norm at the end of
 * pm_switch_to, because pm_pll_reconfigure() flushed inside the
 * callback and nothing else was queued in the meantime), UART_DONE
 * never transitions and the flush spins forever, freezing the
 * console.
 *
 * TX_FIFO_STATUS.{USED, SR_VALID} are STATE bits, not latches --
 * safe to poll unconditionally. This mirrors the PDL helper
 * Cy_SCB_IsTxComplete() which uses exactly these two fields.
 */
#define APP_SCB2_BASE 0x529a0000u
#define APP_SCB2_TX_FIFO_STATUS (*(volatile uint32_t *)(APP_SCB2_BASE + 0x208u))
#define APP_SCB2_TX_FIFO_WR (*(volatile uint32_t *)(APP_SCB2_BASE + 0x240u))

/* From cyip_scb.h SCB_TX_FIFO_STATUS_{USED,SR_VALID}_Msk -- exposed
 * here as local constants so the raw-SCB path stays completely
 * independent of the PDL. */
#define APP_SCB2_TX_FIFO_STATUS_USED_Msk 0x000001FFu
#define APP_SCB2_TX_FIFO_STATUS_SR_VALID_Msk 0x00008000u

/*
 * SCB v3 TX FIFO on PSE84 is 256 entries deep, but the block is
 * initialised with a smaller effective depth. Waiting until USED
 * drops below 16 leaves plenty of headroom and matches the reference
 * implementation's threshold.
 */
#define APP_SCB2_TX_FIFO_HEADROOM 16u

void diag_trace_char(char c)
{
	while ((APP_SCB2_TX_FIFO_STATUS & APP_SCB2_TX_FIFO_STATUS_USED_Msk) >=
	       APP_SCB2_TX_FIFO_HEADROOM) {
		/* spin — wait for TX FIFO room */
	}
	APP_SCB2_TX_FIFO_WR = (uint32_t)(uint8_t)c;
}

void diag_trace(const char *s)
{
	if (s == NULL) {
		return;
	}
	while (*s != '\0') {
		if (*s == '\n') {
			diag_trace_char('\r');
		}
		diag_trace_char(*s);
		s++;
	}
}

void diag_trace_flush(void)
{
	/* Wait until both the TX FIFO and the shift register are empty.
	 * Both fields live in TX_FIFO_STATUS -- one register read per
	 * loop iteration. Naturally returns immediately if the SCB is
	 * already idle, because both fields are true state (not latches
	 * that need a fresh transition to re-arm). Equivalent to the
	 * PDL's Cy_SCB_IsTxComplete(). */
	const uint32_t busy_mask = APP_SCB2_TX_FIFO_STATUS_USED_Msk |
				   APP_SCB2_TX_FIFO_STATUS_SR_VALID_Msk;

	while ((APP_SCB2_TX_FIFO_STATUS & busy_mask) != 0u) {
		/* spin */
	}
}

/*
 * Heartbeat thread ---------------------------------------------------
 *
 * Owns led2 (blue) exclusively so its blink rate is a pure indicator
 * of CPU liveness. Independent of console, Zephyr shell backend, and
 * any mode-transition side-effect. If the blue LED continues blinking
 * at ~2 Hz after a console "freeze", the CPU is still executing
 * scheduled threads — the fault must be in the console path (Zephyr
 * shell task, SCB UART driver, or the shell's ring-buffer).
 *
 * Uses a low-priority preemptible thread so it never starves the
 * shell RX / TX handlers. Cooperative would be fine too but we keep
 * it preemptible for identical behaviour under HP/LP/ULP.
 */
static const struct gpio_dt_spec led_heartbeat =
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

#define HEARTBEAT_STACK_SIZE 512
#define HEARTBEAT_PRIORITY 10 /* preemptible, well below shell */

static void heartbeat_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	if (!gpio_is_ready_dt(&led_heartbeat)) {
		return;
	}
	(void)gpio_pin_configure_dt(&led_heartbeat, GPIO_OUTPUT_INACTIVE);

	for (;;) {
		(void)gpio_pin_toggle_dt(&led_heartbeat);
		k_msleep(500);
	}
}

K_THREAD_DEFINE(heartbeat_tid, HEARTBEAT_STACK_SIZE, heartbeat_entry, NULL,
		NULL, NULL, HEARTBEAT_PRIORITY, 0, 0);
