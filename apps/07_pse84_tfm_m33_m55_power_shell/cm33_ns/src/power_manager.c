/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief NS-side dispatcher for the HP <-> LP power-mode manager.
 *
 * ULP is intentionally not supported on this build. See
 * `INVESTIGATION_pse84_ulp.md` for the analysis: SPM state and S-side
 * stacks live in SRAM whose reliable-read window at 0.7 V requires an
 * SRAM_TRIM_POST write that can only be issued from NS -- but between
 * the S-side CoreBuck change and the NS post-trim write SPM does a
 * PendSV round-trip through SRAM at the mistrimmed corner. Every
 * mitigation we tried (PRIMASK, HF0 /16 slow-down, atomic PDL
 * SystemEnterUlp from S) hits one wall or another (PPC / SRF path /
 * frequency floor).
 *
 * Security-model split for HP <-> LP:
 *
 *   NS direct writes:
 *     - SRSS RAM_TRIM_STRUCT       -- PPC permits Master-0 (NS master)
 *                                     but blocks Master-1 (S master).
 *
 *   S-side via z_pm Z_PM_OP_SWITCH_ACTIVE_MODE:
 *     - Cy_SysClk_ClkHfSetDivider              -- HF0 divider write.
 *     - Cy_SysPm_CoreBuckSetProfile + Status   -- SRSS_MAIN, S-only.
 *     - Cy_SysPm_SramLdoEnable                 -- reserved for LP<->ULP.
 *
 * Interrupt handling:
 *   Even for the HP <-> LP window we mask NS interrupts using PRIMASK
 *   (`cpsid i` / `cpsie i`) across pre-trim + psa_call + post-trim.
 *   PRIMASK gives a hard guarantee no exception entry fires while
 *   voltage is mid-transition.
 *
 * The SRAM_TRIM sequences are ported verbatim from the DSL PDL
 * `Cy_SysPm_SystemTransition{HpToLp,LpToHp}` bodies.
 */

#include "power_manager.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cy_pdl.h"
#include "cy_syspm.h"

#include "gpio_indicators.h"
#include "z_pm_client.h"

/** Current active power mode. */
static pm_mode_t s_current_mode = PM_MODE_HP;

const char *pm_mode_name(pm_mode_t m)
{
	switch (m) {
	case PM_MODE_ULP:
		return "ULP (unsupported on this build)";
	case PM_MODE_LP:
		return "LP  ( 66 MHz target)";
	case PM_MODE_HP:
		return "HP  (200 MHz target)";
	default:
		return "?";
	}
}

static const char *pm_mode_short(pm_mode_t m)
{
	switch (m) {
	case PM_MODE_ULP:
		return "ulp";
	case PM_MODE_LP:
		return "lp";
	case PM_MODE_HP:
		return "hp";
	default:
		return "?";
	}
}

pm_mode_t pm_current_mode(void) { return s_current_mode; }

void pm_init(void) { s_current_mode = PM_MODE_HP; }

/* -------------------------------------------------------------------
 * SRAM_TRIM sequences ported verbatim from the DSL PDL's
 * Cy_SysPm_SystemTransition{HpToLp,LpToHp,UlpToLp,LpToUlp}. Each
 * table lists (index, value) writes to SRSS_TRIM_RAM_CTL[index];
 * `pre` writes run before the CoreBuck profile change, `post` writes
 * run after.
 * ------------------------------------------------------------------- */

struct trim_write {
	uint8_t index;
	uint32_t value;
};

/* HP -> LP pre-buck (from Cy_SysPm_SystemTransitionHpToLp steps 2-5). */
static const struct trim_write trim_hp_to_lp_pre[] = {
    {0, 0x343}, {1, 0x343}, {2, 0x206}, {3, 0x206}, {4, 0x20F}, {5, 0x20F},
    {6, 0x3F},	{7, 0x3F},  {8, 0xB},	{2, 0x204}, {3, 0x204}, {4, 0x20C},
    {5, 0x20C}, {6, 0x2D},  {7, 0x2D},	{4, 0x204}, {5, 0x204}, {0, 0x743},
    {1, 0x743}, {2, 0x604}, {3, 0x604}, {6, 0x22D}, {7, 0x22D},
};

/* HP -> LP post-buck (step 7). */
static const struct trim_write trim_hp_to_lp_post[] = {
    {4, 0x604},
    {5, 0x604},
};

/* LP -> HP pre-buck (steps 2-5). */
static const struct trim_write trim_lp_to_hp_pre[] = {
    {2, 0x606}, {3, 0x606}, {4, 0x606}, {5, 0x606}, {6, 0x23F}, {7, 0x23F},
    {4, 0x607}, {5, 0x607}, {4, 0x60F}, {5, 0x60F}, {4, 0x20F}, {5, 0x20F},
};

/* LP -> HP post-buck (steps 7-8). */
static const struct trim_write trim_lp_to_hp_post[] = {
    {0, 0x343}, {1, 0x343}, {2, 0x206}, {3, 0x206}, {6, 0x3F},
    {7, 0x3F},	{8, 0xA},   {0, 0x342}, {1, 0x342}, {2, 0x202},
    {3, 0x202}, {4, 0x20B}, {5, 0x20B}, {6, 0x1B},  {7, 0x1B},
};

/* ULP <-> LP trim tables (trim_ulp_to_lp_pre/_post, trim_lp_to_ulp_pre/_post)
 * are intentionally omitted -- see INVESTIGATION_pse84_ulp.md for the
 * analysis. If ULP support is ever revisited, port them verbatim from
 * the DSL PDL's Cy_SysPm_SystemTransition{UlpToLp,LpToUlp} bodies. */

static inline void apply_trim_seq(const struct trim_write *seq, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		SRSS_TRIM_RAM_CTL(seq[i].index) = seq[i].value;
	}
}

#define APPLY_TRIM(seq) apply_trim_seq((seq), ARRAY_SIZE(seq))

/* PRIMASK-based IRQ mask/unmask. Chosen over Zephyr's irq_lock (which
 * uses BASEPRI) because BASEPRI does not mask NMI/HardFault/priority-0
 * IRQs; PRIMASK gives a hard guarantee no exception entry fires
 * during the transition window. */
static inline unsigned int pm_irq_disable(void)
{
	unsigned int primask;
	__asm__ __volatile__("mrs %0, primask" : "=r"(primask));
	__asm__ __volatile__("cpsid i" ::: "memory");
	return primask;
}

static inline void pm_irq_restore(unsigned int primask)
{
	__asm__ __volatile__("msr primask, %0" : : "r"(primask) : "memory");
}

/* -------------------------------------------------------------------
 * One-step HP <-> LP transition. z_pm S handles the HF0 divider and
 * the CoreBuck profile change atomically inside the psa_call. NS
 * applies SRAM_TRIM around the psa_call because PPC restricts the
 * RAM_TRIM_SRSS_SRAM region to Master-0 (NS).
 *
 * The whole sequence runs with IRQs masked (PRIMASK) to prevent
 * exception entry in the voltage-transition window.
 * ------------------------------------------------------------------- */

static int step_transition(pm_mode_t source, pm_mode_t target,
			   const struct trim_write *pre, size_t pre_n,
			   const struct trim_write *post, size_t post_n)
{
	psa_status_t psa_rc;
	int32_t s_rc = 0;
	unsigned int key;

	key = pm_irq_disable();
	apply_trim_seq(pre, pre_n);
	psa_rc = z_pm_switch_active_mode(source, target, &s_rc);
	if (psa_rc == PSA_SUCCESS && s_rc == 0) {
		apply_trim_seq(post, post_n);
	}
	pm_irq_restore(key);

	return (psa_rc == PSA_SUCCESS && s_rc == 0) ? 0 : -EIO;
}

static int step_hp_to_lp(void)
{
	return step_transition(PM_MODE_HP, PM_MODE_LP, trim_hp_to_lp_pre,
			       ARRAY_SIZE(trim_hp_to_lp_pre),
			       trim_hp_to_lp_post,
			       ARRAY_SIZE(trim_hp_to_lp_post));
}

static int step_lp_to_hp(void)
{
	return step_transition(PM_MODE_LP, PM_MODE_HP, trim_lp_to_hp_pre,
			       ARRAY_SIZE(trim_lp_to_hp_pre),
			       trim_lp_to_hp_post,
			       ARRAY_SIZE(trim_lp_to_hp_post));
}

int pm_switch_to(pm_mode_t target)
{
	pm_mode_t source;
	uint32_t t_start, t_end, cycles;
	int rc;

	if (target != PM_MODE_HP && target != PM_MODE_LP) {
		if (target == PM_MODE_ULP) {
			printk("[pm] ULP is not supported on this build. "
			       "See INVESTIGATION_pse84_ulp.md.\n");
			return -ENOTSUP;
		}
		return -EINVAL;
	}
	if (target == s_current_mode) {
		return 0;
	}

	source = s_current_mode;

	t_start = k_cycle_get_32();
	gpio_indicators_transition_begin();

	rc = 0;
	if (source == PM_MODE_HP && target == PM_MODE_LP) {
		rc = step_hp_to_lp();
	} else if (source == PM_MODE_LP && target == PM_MODE_HP) {
		rc = step_lp_to_hp();
	} else {
		rc = -EINVAL;
	}

	gpio_indicators_transition_end();
	t_end = k_cycle_get_32();

	if (rc != 0) {
		printk("[pm] switch %s -> %s failed (%d)\n",
		       pm_mode_short(source), pm_mode_short(target), rc);
		return rc;
	}

	s_current_mode = target;

	cycles = t_end - t_start;
	printk("[pm] transition %s -> %s : %u LPTIMER cycles (@32768 Hz)\n",
	       pm_mode_short(source), pm_mode_short(target), cycles);
	return 0;
}

void pm_clock_probe(void)
{
	struct z_pm_clock_probe report;
	psa_status_t psa_rc;

	psa_rc = z_pm_clock_probe(&report);
	if (psa_rc != PSA_SUCCESS) {
		printk("[clk] psa_call failed: status=%d\n", (int)psa_rc);
		return;
	}

	printk("[clk] DPLL_LP0  meas=%3u.%03u MHz (%9u Hz)  "
	       "comp=%3u.%03u MHz (%9u Hz)\n",
	       report.meas_path0 / 1000000u,
	       (report.meas_path0 / 1000u) % 1000u, report.meas_path0,
	       report.comp_path0 / 1000000u,
	       (report.comp_path0 / 1000u) % 1000u, report.comp_path0);
	printk("[clk] CLK_HF0   meas=%3u.%03u MHz (%9u Hz)  "
	       "comp=%3u.%03u MHz (%9u Hz)\n",
	       report.meas_hf0 / 1000000u, (report.meas_hf0 / 1000u) % 1000u,
	       report.meas_hf0, report.comp_hf0 / 1000000u,
	       (report.comp_hf0 / 1000u) % 1000u, report.comp_hf0);
	printk("[clk] CLK_HF10  meas=%3u.%03u MHz (%9u Hz)  "
	       "comp=%3u.%03u MHz (%9u Hz)\n",
	       report.meas_hf10 / 1000000u, (report.meas_hf10 / 1000u) % 1000u,
	       report.meas_hf10, report.comp_hf10 / 1000000u,
	       (report.comp_hf10 / 1000u) % 1000u, report.comp_hf10);
}
