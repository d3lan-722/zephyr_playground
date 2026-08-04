/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GPIO indicators for the HP/LP/ULP demo -- mode-indicator
 *        LEDs and a scope-trigger "PM busy" signal.
 *
 * Owns two kinds of outputs:
 *
 *   - Two mode-indicator LEDs (led0 red, led1 green). The current
 *     power mode is shown by lighting one, both, or neither. Blue
 *     (led2) is deliberately not owned here -- it belongs to the
 *     diagnostic heartbeat thread in @c src/diag.c.
 *
 *   - One PM-busy scope trigger on pin P3.1 (device-tree alias
 *     `pm-busy`). Driven high by the shell command layer while a
 *     mode transition is in progress and low again once the
 *     transition (including SCB retune + clock probe) has
 *     completed. Wire a scope or logic-analyzer probe here to
 *     correlate current-draw waveforms with the software
 *     transition window.
 */

#ifndef APP_GPIO_INDICATORS_H_
#define APP_GPIO_INDICATORS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the indicator LEDs and the pm-busy signal as
 *        outputs in the inactive state.
 *
 * Must be called once at bring-up before any of the other
 * @c gpio_indicators_* functions.
 *
 * @return 0 on success, negative errno if any GPIO is not ready or
 *         cannot be configured. Failure leaves the application
 *         usable but the visual / scope indicators dark.
 */
int gpio_indicators_init(void);

/**
 * @brief Drive the two mode-indicator LEDs to match the incoming
 *        power mode. Blue LED (led2) is left alone.
 *
 * @param red    Non-zero to light red, zero to turn it off.
 * @param green  Non-zero to light green, zero to turn it off.
 */
void gpio_indicators_set_mode_leds(int red, int green);

/**
 * @brief Assert the PM-busy scope trigger (drive P3.1 high) to mark
 *        the start of a mode transition.
 *
 * Idempotent -- safe to call twice in a row, the pin just stays
 * high. Pair with @ref gpio_indicators_transition_end.
 */
void gpio_indicators_transition_begin(void);

/**
 * @brief De-assert the PM-busy scope trigger (drive P3.1 low) to
 *        mark the end of a mode transition.
 *
 * Idempotent. Pair with @ref gpio_indicators_transition_begin.
 */
void gpio_indicators_transition_end(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_GPIO_INDICATORS_H_ */
