/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr PM dispatcher for the CM33-NS image.
 *
 * ARCHITECTURE (Phase 5.75 — post round-7):
 *
 * The three currently-implemented PM entry points call PDL syspm
 * directly from NS. The NS-side cy_syspm_v4.c is compiled with
 * CY_PDL_SYSPM_ENABLE_SRF_INTEG (auto-defined by cy_syspm_srf.h
 * because at least one of the four CYCFG_PPC_SECURED_{SRSS_MAIN,
 * SRSS_HIB_DATA, PWRMODE_PWRMODE, M55APPCPUSS} bits is 1U). That
 * activates the SRF branch inside each Cy_SysPm_Cpu*Enter*Sleep
 * function, which packs an SRF request and psa_call()s into
 * IFX_EXT_SP; the S-side handler runs the actual SLEEPDEEP+WFI at
 * PC2. No project-local partition wrap is needed for these APIs.
 *
 * The SoC default pm_state_set (in soc/infineon/edge/pse84/power.c)
 * is dropped from the build by the application CMakeLists — its
 * PRE_KERNEL_1 SYS_INIT calls Cy_SysPm_SetDeepSleepMode which is
 * NOT SRF-wrapped and bus-faults from NS.
 *
 * Prerequisite for the direct-NS path: CONFIG_IDLE_STACK_SIZE must
 * be large enough for tfm_ns_interface_dispatch's fpu_ctx_full
 * alloca (136 bytes) on top of the pool_allocate + Cy_SysPm_*
 * frames. See prj.conf; 2 KiB works, the Zephyr default 320 bytes
 * does not.
 *
 * NOT IMPLEMENTED HERE (deferred, gated on runtime PPC investigation):
 *   * Layer-B static bias (BGREF LP, core-buck DS, IHO/IMO DS-off).
 *     Attempted in the branch that added an ifx_pm_init SYS_INIT with
 *     Cy_SysPm_Init + CoreBuck + BGREF + oscillator calls; hit an
 *     immediate BusFault at boot because these calls transitively
 *     touch PWRMODE.PPU_MAIN via cy_pd_ppu_set_power_mode, which the
 *     runtime PPC configuration (cycfg_system.c :: M33S_ppc_0_regions)
 *     still gates as Secure-only. Reverted. See porting_plan.md
 *     Phase 6 "post-mortem" for the details.
 *   * Per-transition Cy_SysPm_SetDeepSleepMode(mode) — same class of
 *     un-SRF-wrapped call; blocked by the same runtime PPC.
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>

#include "cy_syspm.h"

#include "indicator.h"

/**
 * @brief Switch IRQ masking from BASEPRI to PRIMASK before WFI.
 *
 * @details
 * Zephyr enters ::pm_state_set with BASEPRI raised by
 * @c irq_lock(), which masks every maskable interrupt including the
 * timer that is supposed to wake us. PSE84 / Cortex-M33 only honours
 * wake from standby/sleep when the wake source is signalled with
 * PRIMASK set; BASEPRI keeps the interrupt pending forever.
 *
 * This helper matches the SoC default @c pm_state_set:
 * @c __disable_irq() sets PRIMASK, @c irq_unlock(0) clears BASEPRI.
 * @ref pm_state_exit_post_ops re-enables interrupts via
 * @c __enable_irq() after WFI returns.
 */
static inline void pm_irq_prologue(void)
{
	__disable_irq();
	irq_unlock(0);
}

/**
 * @brief Enter PM_STATE_SUSPEND_TO_IDLE (SLEEPDEEP=0 + WFI).
 *
 * @details
 * Toggles the red indicator LED, masks IRQs, then calls
 * @c Cy_SysPm_CpuEnterSleep. The NS-side PDL takes the SRF branch
 * (see file header) which packs a PSA request to @c IFX_EXT_SP;
 * the S-side handler executes the real WFI at PC2. Returns after
 * the wake interrupt is delivered to NS.
 *
 * Called by ::pm_state_set. Blocking; runs on the idle thread with
 * PRIMASK set.
 */
static void enter_cpu_sleep(void)
{
	indicator_cpu_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	indicator_cpu_sleep_off();
}

/**
 * @brief Enter PM_STATE_STANDBY substate 1 (cpu_deep_sleep).
 *
 * @details
 * SLEEPDEEP=1 + WFI, plus PDL syspm callbacks and PPU-trim fixups
 * executed on the S side. Same NS→SRF→S path as
 * @ref enter_cpu_sleep, dispatched via
 * @c Cy_SysPm_CpuEnterDeepSleep. Toggles the blue indicator LED
 * around the call. Returns after the wake IRQ.
 *
 * SRSS collapses the system to deep-sleep automatically once every
 * CPU has voted; a single CM33-NS caller is sufficient because CM55
 * is either parked or has already voted its own deep-sleep.
 */
static void enter_cpu_deep_sleep(void)
{
	indicator_cpu_deep_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	indicator_cpu_deep_sleep_off();
}

/**
 * @brief Enter PM_STATE_STANDBY substate 2 (system_deep_sleep).
 *
 * @details
 * Same underlying primitive as @ref enter_cpu_deep_sleep today
 * (@c Cy_SysPm_CpuEnterDeepSleep). Distinct call site is retained
 * so a future specialisation (Layer-B, per-transition
 * @c Cy_SysPm_SetDeepSleepMode, DS-RAM, DS-OFF) can go in without
 * disturbing substate 1. Uses the magenta indicator LED.
 */
static void enter_system_deep_sleep(void)
{
	indicator_system_deep_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	indicator_system_deep_sleep_off();
}

/**
 * @brief Zephyr PM hook: enter the requested low-power state.
 *
 * @details
 * Called by the Zephyr PM subsystem from the idle thread when its
 * residency policy elects a low-power state for the current idle
 * window. Runs with @c irq_lock() held (BASEPRI raised). Each
 * per-state helper calls @ref pm_irq_prologue to switch to PRIMASK
 * before WFI so the wake IRQ can actually reach the CPU.
 *
 * State mapping (residency thresholds live in the board overlay
 * @c cm33_ns/boards/kit_*.overlay; LED colours are per @ref indicator.h):
 *
 * | State + substate         | Handler                    | LED     |
 * |--------------------------|----------------------------|---------|
 * | SUSPEND_TO_IDLE          | @ref enter_cpu_sleep       | red     |
 * | STANDBY substate 1       | @ref enter_cpu_deep_sleep  | blue    |
 * | STANDBY substate 2       | @ref enter_system_deep_sleep | magenta |
 * | SUSPEND_TO_RAM           | not implemented — prints   | cyan    |
 * | SOFT_OFF                 | not implemented — prints   | white   |
 *
 * @param state       The Zephyr PM state the residency policy picked.
 * @param substate_id Vendor-defined substate index (only meaningful
 *                    for @c PM_STATE_STANDBY here).
 *
 * @note Overrides Zephyr's weak default. The SoC-supplied
 *       @c pm_state_set in soc/infineon/edge/pse84/power.c is dropped
 *       from the build by cm33_ns/CMakeLists.txt because its
 *       PRE_KERNEL_1 SYS_INIT bus-faults from NS under TF-M.
 */
void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	switch (state) {
	case PM_STATE_SUSPEND_TO_IDLE:
		enter_cpu_sleep();
		break;
	case PM_STATE_STANDBY:
		switch (substate_id) {
		case 1U:
			enter_cpu_deep_sleep();
			break;
		case 2U:
			enter_system_deep_sleep();
			break;
		default:
			printk("pm: STANDBY substate %u not implemented\n",
			       substate_id);
			break;
		}
		break;
	case PM_STATE_SUSPEND_TO_RAM:
		printk("pm: SUSPEND_TO_RAM not implemented yet\n");
		break;
	case PM_STATE_SOFT_OFF:
		printk("pm: SOFT_OFF not implemented yet\n");
		break;
	default:
		break;
	}
}

/**
 * @brief Zephyr PM hook: post-WFI cleanup.
 *
 * @details
 * Called by the Zephyr PM subsystem after ::pm_state_set returns
 * from WFI. Symmetric to @ref pm_irq_prologue: clears PRIMASK
 * (which the prologue set) so normal interrupt delivery resumes.
 * BASEPRI is left at 0 (the prologue's @c irq_unlock(0) cleared
 * it); Zephyr's own scheduler-lock/unlock takes over from here.
 *
 * @param state       Same value passed to the paired ::pm_state_set.
 * @param substate_id Same value passed to the paired ::pm_state_set.
 */
void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
	__enable_irq();
}
