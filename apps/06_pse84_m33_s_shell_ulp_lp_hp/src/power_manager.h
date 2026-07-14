/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Active-power-mode manager (HP / LP / ULP) for PSoC Edge —
 *        secure-only variant (project 06).
 *
 * This is the simplest possible shell-driven HP/LP/ULP switcher:
 * everything runs on CM33-Secure and calls the PDL syspm entries
 * directly, no SRF trampoline into a partition (which is what
 * project 05 needs because it runs from CM33-NS under TF-M).
 *
 * Direction-aware step sequences match the reference implementation
 * at tmp/zephyr_dvfs_dpm_proposed/m33_ns/src/power_manager.c:
 *
 *   CPU_CLK   SysPm mode   RRAM voltage   ClkHf0 divider
 *   -------   ----------   ------------   --------------
 *    50 MHz   ULP           VMODE_ULP      /4  (200 MHz / 4)
 *    66 MHz   LP            VMODE_LP       /3  (200 MHz / 3)
 *   200 MHz   HP            VMODE_HP       /1  (200 MHz / 1)
 *
 * Transitions:
 *   HP  → LP  : divide ClkHf0 /3, EnterLp,  RRAM_LP
 *   HP  → ULP : divide ClkHf0 /4, EnterUlp, RRAM_ULP
 *   LP  → ULP : divide ClkHf0 /4, EnterUlp, RRAM_ULP
 *   ULP → LP  : EnterLp,  RRAM_LP,  divide ClkHf0 /3
 *   ULP → HP  : EnterHp,  RRAM_HP,  divide ClkHf0 /1
 *   LP  → HP  : EnterHp,  RRAM_HP,  divide ClkHf0 /1
 *
 * The rule is: raise voltage BEFORE raising clock; lower clock
 * BEFORE lowering voltage. `Cy_SysPm_SystemEnter*` handles the
 * voltage step internally; `Cy_SysClk_ClkHfSetDivider` handles the
 * clock step; `Cy_RRAM_SetVoltageMode` retunes the RRAM controller
 * so reads stay valid at the new voltage.
 */

#ifndef POWER_MANAGER_H_
#define POWER_MANAGER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Available active power modes, ordered by increasing performance. */
typedef enum {
	PM_MODE_ULP = 0, /**< Ultra-Low-Power:  50 MHz */
	PM_MODE_LP = 1,	 /**< Low-Power:        66 MHz */
	PM_MODE_HP = 2,	 /**< High-Performance: 200 MHz */
} pm_mode_t;

/**
 * @brief Initialise the power-mode manager software state.
 *
 * Refreshes @c SystemCoreClock and records the initial mode. The
 * hardware is already in HP at boot (SE-ROM / secure init leaves it
 * there), so no register writes are needed here.
 */
void pm_init(void);

/** @brief Return the current active power mode. */
pm_mode_t pm_current_mode(void);

/** @brief Human-readable name of a power mode (for shell output). */
const char *pm_mode_name(pm_mode_t m);

/**
 * @brief Transition the SoC into @p target.
 *
 * If already in @p target this is a no-op. Uses the direction-aware
 * step sequences listed in the file header; the PDL syspm callbacks
 * fire on every step.
 *
 * @param target  Desired active power mode.
 * @return 0 on success, negative errno-like value on failure.
 */
int pm_switch_to(pm_mode_t target);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H_ */
