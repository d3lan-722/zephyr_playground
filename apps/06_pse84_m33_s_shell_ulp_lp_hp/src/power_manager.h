/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Active-power-mode manager (HP / LP / ULP) for PSoC Edge --
 *        secure-only variant (project 06).
 *
 * Simplest possible shell-driven HP/LP/ULP switcher. Everything runs
 * on CM33-Secure and calls the PDL syspm entries directly, with no
 * SRF trampoline into a partition (project 05 needs that because it
 * runs from CM33-NS under TF-M).
 *
 * Design: three @c cy_stc_syspm_callback_t hooks (one per target
 * mode) are registered with @c Cy_SysPm_RegisterCallback in
 * @ref pm_init. When @ref pm_switch_to calls
 * @c Cy_SysPm_SystemEnter{Hp,Lp,Ulp}, the corresponding callback
 * fires in three phases and takes care of the PLL retune around the
 * voltage step:
 *
 *   BEFORE_TRANSITION : drop DPLL_LP0 to a SRAM-safe intermediate
 *                       (75 MHz for HP<->LP, 41 MHz for LP<->ULP)
 *   Cy_SysPm_SystemEnter*
 *                     : voltage step (buck setpoint + SRAM trims)
 *   AFTER_TRANSITION  : Cy_RRAM_SetVoltageMode + DPLL_LP0 to the
 *                       mode's final target (400/160/100 MHz)
 *
 * CLK_HF0 = DPLL_LP0 / 1 (DT overlay override), so the DPLL output
 * IS the CM33 core frequency:
 *
 *   Mode  |  DPLL_LP0 |  CLK_HF0 (CM33) |  Core V
 *   ------|-----------|-----------------|--------
 *   ULP   |  50 MHz   |   50 MHz        |  0.7 V
 *   LP    |  80 MHz   |   80 MHz        |  0.8 V
 *   HP    | 200 MHz   |  200 MHz        |  0.9 V
 *
 * All rules that the callbacks enforce (raise voltage before clock
 * on up-transitions; drop clock before voltage on down-transitions;
 * stay under the target mode's max HF during the voltage step) are
 * documented at the call sites in @ref power_manager.c.
 */

#ifndef POWER_MANAGER_H_
#define POWER_MANAGER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Available active power modes, ordered by increasing performance. */
typedef enum {
	PM_MODE_ULP = 0, /**< Ultra-Low-Power:  50 MHz CM33, 0.7 V core */
	PM_MODE_LP = 1,	 /**< Low-Power:        80 MHz CM33, 0.8 V core */
	PM_MODE_HP = 2,	 /**< High-Performance: 200 MHz CM33, 0.9 V core */
} pm_mode_t;

/**
 * @brief Initialise the power-mode manager software state.
 *
 * Registers the HP/LP/ULP SysPm callbacks and records the initial
 * mode. Does NOT touch the PLL at boot: the DT overlay has already
 * landed the SoC in an in-spec HP state (DPLL_LP0 200 MHz ->
 * CLK_HF0 200 MHz -> CM33 200 MHz, at the AN237976 HP ceiling), so
 * there is nothing to correct here. Reprogramming the PLL just to
 * re-assert the same target would still transiently invalidate
 * CLK_HF10, garble the next printk, and gain nothing. See the
 * implementation in power_manager.c for the full rationale.
 */
void pm_init(void);

/** @brief Return the current active power mode. */
pm_mode_t pm_current_mode(void);

/** @brief Human-readable name of a power mode (for shell output). */
const char *pm_mode_name(pm_mode_t m);

/**
 * @brief Transition the SoC into @p target.
 *
 * No-op if already in @p target. The heavy lifting (PLL retune
 * around the voltage step, RRAM controller retune) happens inside
 * the SysPm callbacks that were registered by @ref pm_init. This
 * function orchestrates the surrounding SCB baud retune and the
 * post-transition clock probe.
 *
 * @param target  Desired active power mode.
 * @return 0 on success, negative errno-like value on failure
 *         (@c -EINVAL for an unknown mode, @c -EIO if
 *         @c Cy_SysPm_SystemEnter* reported failure).
 */
int pm_switch_to(pm_mode_t target);

/**
 * @brief Measure and print the actual live frequency of DPLL_LP0,
 *        CLK_HF0 (CM33 core) and CLK_HF10 (SCB2 peripheral).
 *
 * Uses the SoC's hardware clock measurement counters
 * (@c Cy_SysClk_StartClkMeasurementCounters) with IHO (50 MHz,
 * silicon-fixed, always running on PSE84) as the reference clock,
 * so the readouts do not depend on the PLL state we are trying to
 * verify. Cross-prints @c Cy_SysClk_ClkHfGetFrequency (PDL's
 * register-readback value) alongside each measurement so a stuck
 * counter is immediately visible. Also prints the return status of
 * the most recent @c Cy_SysClk_PllConfigure / PllEnable so silent
 * lock failures are catchable. Output goes to the console via
 * @c printk.
 *
 * Called automatically at the end of @ref pm_switch_to and on
 * demand via the @c probe shell command.
 */
void pm_clock_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H_ */
