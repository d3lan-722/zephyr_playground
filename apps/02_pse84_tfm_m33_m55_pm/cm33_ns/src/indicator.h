/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief RGB LED indicator API for per-state visualisation.
 *
 * Plan §6: green = active phase; red/blue/magenta/cyan/white =
 * sleep-state indicators. One function per role so the dispatcher
 * in power.c reads as a straight call list.
 *
 * Implemented in indicator.c.
 */

#ifndef APP_INDICATOR_H_
#define APP_INDICATOR_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configure all three RGB LEDs as outputs, all OFF. */
void indicator_init(void);

/** @brief Active-phase indicator (green) ON. */
void indicator_active_on(void);
/** @brief Active-phase indicator (green) OFF. */
void indicator_active_off(void);

/** @brief PM_STATE_SUSPEND_TO_IDLE (cpu_sleep) — red ON. */
void indicator_cpu_sleep_on(void);
void indicator_cpu_sleep_off(void);

/** @brief PM_STATE_STANDBY substate 1 (cpu_deep_sleep) — blue ON. */
void indicator_cpu_deep_sleep_on(void);
void indicator_cpu_deep_sleep_off(void);

/** @brief PM_STATE_STANDBY substate 2 (system_deep_sleep) — magenta ON. */
void indicator_system_deep_sleep_on(void);
void indicator_system_deep_sleep_off(void);

/** @brief PM_STATE_SUSPEND_TO_RAM (system_deep_sleep_ram) — cyan ON. */
void indicator_system_ds_ram_on(void);
void indicator_system_ds_ram_off(void);

/** @brief PM_STATE_SOFT_OFF (system_deep_sleep_off) — white ON, latched. */
void indicator_system_ds_off_latch(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INDICATOR_H_ */
