/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief DVFS strategy: keep DPLL_LP0 at 200 MHz, change only the
 *        CLK_HF0 divider per mode. Selected by
 *        @c PM_STRATEGY_HF0_DIVIDER.
 *
 * Six direction-aware helpers wrap @c Cy_SysClk_ClkHfSetDivider(0,.),
 * the voltage step (@c pm_syspm_enter) and @c Cy_RRAM_SetVoltageMode.
 * DPLL_LP0 is never touched, so @c CLK_HF10 (SCB2 pclk) stays at
 * 50 MHz in every mode and the console baud divider never needs
 * retuning.
 *
 * Trade-off vs. the PLL-retune strategy: LP lands at 66 MHz
 * (200 / 3, closest integer divider) instead of the 80 MHz LP spec
 * ceiling, and peripheral clocks do not scale down. Transition
 * wall time drops from ~450 ms to ~1-5 ms.
 *
 * Direction rule (from AN237976 + the Infineon switch_power_modes
 * reference):
 *   Down (voltage falls): drop the ClkHf0 divider FIRST, then
 *                         Cy_SysPm_SystemEnter*, then RRAM VMODE.
 *   Up   (voltage rises): Cy_SysPm_SystemEnter* FIRST, then RRAM
 *                         VMODE, then remove the ClkHf0 divider.
 *
 * See @c PLAN_dual_dvfs.md for the design rationale.
 */

#include "power_manager_internal.h"

#ifdef PM_STRATEGY_HF0_DIVIDER

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cy_pdl.h"

/* ------------------------------------------------------------------
 * Six direction-aware transition helpers.
 * Each returns 0 on success, -EIO on PDL failure.
 * ------------------------------------------------------------------ */

/** HP -> LP (down): /3, EnterLp, RRAM_LP. */
static int trans_hp_to_lp(void)
{
	cy_en_syspm_status_t st;

	Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_DIVIDE_BY_3);
	st = pm_syspm_enter(PM_MODE_LP);
	if (st != CY_SYSPM_SUCCESS) {
		Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_NO_DIVIDE);
		printk("[pm] HP->LP EnterLp failed (%d)\n", (int)st);
		return -EIO;
	}
	Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_LP);
	return 0;
}

/** HP -> ULP (down, direct -- EnterUlp does the two-step voltage
 *  drop internally): /4, EnterUlp, RRAM_ULP. */
static int trans_hp_to_ulp(void)
{
	cy_en_syspm_status_t st;

	Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_DIVIDE_BY_4);
	st = pm_syspm_enter(PM_MODE_ULP);
	if (st != CY_SYSPM_SUCCESS) {
		Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_NO_DIVIDE);
		printk("[pm] HP->ULP EnterUlp failed (%d)\n", (int)st);
		return -EIO;
	}
	Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_ULP);
	return 0;
}

/** LP -> ULP (down): /4, EnterUlp, RRAM_ULP. On failure restore
 *  the LP divider (/3). */
static int trans_lp_to_ulp(void)
{
	cy_en_syspm_status_t st;

	Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_DIVIDE_BY_4);
	st = pm_syspm_enter(PM_MODE_ULP);
	if (st != CY_SYSPM_SUCCESS) {
		Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_DIVIDE_BY_3);
		printk("[pm] LP->ULP EnterUlp failed (%d)\n", (int)st);
		return -EIO;
	}
	Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_ULP);
	return 0;
}

/** ULP -> LP (up): EnterLp (voltage rises), RRAM_LP, /3. */
static int trans_ulp_to_lp(void)
{
	cy_en_syspm_status_t st = pm_syspm_enter(PM_MODE_LP);

	if (st != CY_SYSPM_SUCCESS) {
		printk("[pm] ULP->LP EnterLp failed (%d)\n", (int)st);
		return -EIO;
	}
	Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_LP);
	Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_DIVIDE_BY_3);
	return 0;
}

/** LP -> HP (up): EnterHp, RRAM_HP, /1. */
static int trans_lp_to_hp(void)
{
	cy_en_syspm_status_t st = pm_syspm_enter(PM_MODE_HP);

	if (st != CY_SYSPM_SUCCESS) {
		printk("[pm] LP->HP EnterHp failed (%d)\n", (int)st);
		return -EIO;
	}
	Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_HP);
	Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_NO_DIVIDE);
	return 0;
}

/** ULP -> HP (up, direct -- EnterHp does the two-step voltage rise
 *  internally): EnterHp, RRAM_HP, /1. */
static int trans_ulp_to_hp(void)
{
	cy_en_syspm_status_t st = pm_syspm_enter(PM_MODE_HP);

	if (st != CY_SYSPM_SUCCESS) {
		printk("[pm] ULP->HP EnterHp failed (%d)\n", (int)st);
		return -EIO;
	}
	Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_HP);
	Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_NO_DIVIDE);
	return 0;
}

/* ------------------------------------------------------------------
 * Strategy interface (see power_manager_internal.h).
 * ------------------------------------------------------------------ */

void pm_strategy_init(void)
{
	/* No callbacks to register; boot state already matches the HP
	 * mode (ClkHf0 divider /1, DPLL_LP0 at 200 MHz per DT overlay). */
}

int pm_strategy_transition(pm_mode_t source, pm_mode_t target)
{
	switch (source) {
	case PM_MODE_HP:
		if (target == PM_MODE_LP)  { return trans_hp_to_lp(); }
		if (target == PM_MODE_ULP) { return trans_hp_to_ulp(); }
		break;
	case PM_MODE_LP:
		if (target == PM_MODE_HP)  { return trans_lp_to_hp(); }
		if (target == PM_MODE_ULP) { return trans_lp_to_ulp(); }
		break;
	case PM_MODE_ULP:
		if (target == PM_MODE_HP)  { return trans_ulp_to_hp(); }
		if (target == PM_MODE_LP)  { return trans_ulp_to_lp(); }
		break;
	default:
		break;
	}
	return -EINVAL;
}

const char *pm_strategy_mode_name(pm_mode_t m)
{
	switch (m) {
	case PM_MODE_HP:  return "HP  (200 MHz)";
	/* 200 MHz / 3 = 66 MHz -- closest integer divider from 200 to
	 * the 80 MHz LP spec ceiling; unavoidable with this strategy. */
	case PM_MODE_LP:  return "LP  ( 66 MHz)";
	case PM_MODE_ULP: return "ULP ( 50 MHz)";
	default:          return "UNKNOWN";
	}
}

void pm_strategy_probe_status(void)
{
	printk("[div] approach=divider-only  DPLL frozen at boot value\n");
}

bool pm_strategy_needs_uart_retune(void)
{
	return false;
}

#endif /* PM_STRATEGY_HF0_DIVIDER */
