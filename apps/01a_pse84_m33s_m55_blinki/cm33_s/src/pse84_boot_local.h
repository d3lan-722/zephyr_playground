/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_PSE84_BOOT_LOCAL_H_
#define APP_PSE84_BOOT_LOCAL_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Local variant of upstream ifx_pse84_cm55_startup(). Configures the
 * secure/non-secure partitioning (MPC/PPC), enables PD1, releases the
 * CM55 core, and RETURNS so the caller can continue executing.
 */
void app_pse84_cm55_startup(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PSE84_BOOT_LOCAL_H_ */
