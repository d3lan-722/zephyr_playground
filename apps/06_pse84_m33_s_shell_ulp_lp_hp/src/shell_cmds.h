/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell command handlers for project 06 -- mode-switch and
 *        clock-probe commands, plus the RGB indicator LEDs they own.
 *
 * Public surface is deliberately minimal: the Zephyr shell auto-
 * registers commands at link time via @c SHELL_CMD_REGISTER, so no
 * runtime registration call is needed. This module only needs
 * @ref shell_cmds_init to put the indicator LEDs into a known-off
 * state before the first command can run.
 */

#ifndef APP_SHELL_CMDS_H_
#define APP_SHELL_CMDS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the indicator LEDs (led0 red, led1 green) as
 *        outputs in the inactive state.
 *
 * led2 (blue) is owned by the diagnostic heartbeat thread in
 * @c src/diag.c and MUST NOT be touched here.
 *
 * @return 0 on success, negative errno on GPIO not-ready or
 *         configure failure. On failure the shell will still work
 *         but the visual mode indicator will be dark.
 */
int shell_cmds_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SHELL_CMDS_H_ */
