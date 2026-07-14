/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Diagnostics layer for pinpointing HP/LP/ULP freeze mode.
 *
 * We use raw SCB2 register pokes (bypassing Zephyr's Infineon SCB UART
 * driver) so that trace output survives even if the driver's cached
 * baud divider is invalidated by a mode transition. A companion
 * heartbeat thread toggles the blue LED at 2 Hz so a human observer
 * can tell CPU-alive from CPU-hung without any terminal at all.
 *
 * Interpretation matrix after a "freeze" event:
 *
 *   BLUE LED        RAW TRACE MARKERS       DIAGNOSIS
 *   --------------------------------------------------------------
 *   still blinking  streaming after freeze  CPU + UART alive;
 *                                           Zephyr shell task stuck
 *   still blinking  stop at freeze          CPU alive; SCB UART dead
 *   frozen          stop at freeze          Hard CPU hang
 *
 * The raw-SCB path polls TX_FIFO_STATUS.USED (low 8 bits) and treats
 * >= 16 as "wait". This is the exact pattern used by the reference
 * `tmp/zephyr_dvfs_dpm_proposed/m33_ns/src/raw_console.c` — same
 * SCB2 base (0x429a0000) since both apps target the PSE84 kit_pse84
 * app-side console UART.
 */
#ifndef APP_DIAG_H_
#define APP_DIAG_H_

#include <stddef.h>

/**
 * @brief Push a single byte into SCB2's TX FIFO, spinning until the
 *        FIFO has space. Bypasses the Zephyr UART driver entirely so
 *        it keeps working even if `uart_configure()` is broken.
 */
void diag_trace_char(char c);

/**
 * @brief Push a NUL-terminated string via @ref diag_trace_char, LF
 *        translated to CRLF for terminal-friendly newlines.
 */
void diag_trace(const char *s);

/**
 * @brief Block until the SCB2 TX FIFO AND the TX shift register are
 *        fully drained.
 *
 * Call this immediately before any operation that changes the SCB2
 * peripheral clock (e.g. @c Cy_SysClk_PllDisable in
 * @c pm_pll_reconfigure). Any byte still sitting in the TX FIFO or
 * the shift register when the clock changes will be shifted out at
 * the new/fallback bit-time, producing the corrupted-mid-string
 * output that was visible on the console before this call was
 * added.
 *
 * Cheap and safe -- polls two SCB registers, no locking, no calls
 * into the Zephyr driver.
 */
void diag_trace_flush(void);

#endif /* APP_DIAG_H_ */
