/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr PM dispatcher for the CM33-NS image.
 *
 * Round 7: all three currently-implemented PM entry points call
 * PDL syspm directly from NS. The NS-side cy_syspm_v4.c is compiled
 * with CY_PDL_SYSPM_ENABLE_SRF_INTEG (auto-defined by cy_syspm_srf.h
 * because at least one of the four CYCFG_PPC_SECURED_{SRSS_MAIN,
 * SRSS_HIB_DATA, PWRMODE_PWRMODE, M55APPCPUSS} bits is 1U). That
 * activates the SRF branch inside each Cy_SysPm_Cpu*Enter*Sleep
 * function, which packs an SRF request and psa_call()s into
 * IFX_EXT_SP; the S-side handler runs the actual SLEEPDEEP+WFI at
 * PC2.  No project-local partition wrap is needed for these APIs.
 *
 * z_pm still exists (see tfm_partitions/z_pm/) but only exposes
 * Z_PM_OP_PING today. It is the placeholder for future ops that
 * PDL DOES NOT SRF-wrap: Cy_SysPm_SetSysDeepSleepMode,
 * Cy_SysPm_SetSOCMEMDeepSleepMode, CM55-side hibernate, Layer-B
 * bias, retention patterns.
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
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/pm/pm.h>

#include <cmsis_core.h>

#include "cy_mcwdt.h"
#include "cy_syspm.h"

#include "indicator.h"
#include "z_pm_client.h"

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
	//indicator_cpu_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	//indicator_cpu_sleep_off();
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
	//indicator_cpu_deep_sleep_on();
	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	//indicator_cpu_deep_sleep_off();
}

/**
 * @brief Enter PM_STATE_STANDBY substate 2 (system_deep_sleep).
 *
 * @details
 * Same CPU-level primitive as @ref enter_cpu_deep_sleep
 * (@c Cy_SysPm_CpuEnterDeepSleep), but with a per-transition system
 * setup performed first via the z_pm secure partition:
 *
 *   1. @c z_pm_set_deep_sleep_mode(CY_SYSPM_MODE_DEEPSLEEP) — S side
 *      runs @c Cy_SysPm_SetDeepSleepMode(DEEPSLEEP) which programs
 *      the AN237976 Table-2 DEEPSLEEP column (MAIN, SRAM0, SRAM1,
 *      SYSCPU, PD1, APPCPUSS, APPCPU, SOCMEM, U55) so the SoC
 *      actually collapses to system deep sleep when every CPU has
 *      voted. The same op also applies the Layer-B BGREF LP +
 *      CoreBuck DS knobs that Phase 6 measurement deferred out of
 *      the at-boot bias — safe here because reaching this state
 *      commits us to a system-DS transition.
 *   2. @ref pm_irq_prologue — swap BASEPRI for PRIMASK so the wake
 *      IRQ can pend.
 *   3. @c Cy_SysPm_CpuEnterDeepSleep — SRF-wrapped by the PDL; the S
 *      side runs SLEEPDEEP+WFI at PC2.
 *
 * The PPU / Layer-B state programmed here is SRSS-global and thus
 * "sticky" across future entries. That is fine as long as this is the
 * only path that reprograms it: Phases 8 / 9 will re-issue
 * @c z_pm_set_deep_sleep_mode with @c CY_SYSPM_MODE_DEEPSLEEP_RAM /
 * @c CY_SYSPM_MODE_DEEPSLEEP_OFF from their own dispatchers.
 *
 * Uses the magenta indicator LED.
 */
static void enter_system_deep_sleep(void)
{
	//indicator_system_deep_sleep_on();

	psa_status_t st =
	    z_pm_set_deep_sleep_mode((uint32_t)CY_SYSPM_MODE_DEEPSLEEP);
	if (st != PSA_SUCCESS) {

	}

	pm_irq_prologue();
	(void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	//indicator_system_deep_sleep_off();
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
 * | State + substate         | Handler                      | LED     |
 * |--------------------------|------------------------------|---------|
 * | SUSPEND_TO_IDLE          | @ref enter_cpu_sleep         | red     |
 * | STANDBY substate 1       | @ref enter_cpu_deep_sleep    | blue    |
 * | STANDBY substate 2       | @ref enter_system_deep_sleep | magenta |
 *
 * SUSPEND_TO_RAM / SOFT_OFF are TODO for future implementation and are
 * NOT declared in the board overlay, so Zephyr will never dispatch them
 * here. See DS_RAM_RETROSPECTIVE.md for the Phase-8 scoped attempt at
 * DS-RAM and what would be required to finish it.
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
			break;
		}
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

/**
 * @brief Pre-emptively disable MCWDT0 before the Zephyr LPTIMER driver
 *        touches it.
 *
 * @details
 * Reference: tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c :: ifx_pm_init.
 * Cy_MCWDT_Init returns @c CY_MCWDT_BAD_PARAM if any of the three
 * counters is already enabled, and the SE-ROM / RRAM boot leaves the
 * MCWDT0 counters running on cold boot. Without this preemptive
 * disable, drivers/timer/infineon_lp_timer_pdl.c can fail silently:
 * lptimer_init returns -EINVAL, the system clock never starts, and
 * k_msleep hangs forever waiting for a tick.
 *
 * Runs at @c PRE_KERNEL_1 with priority 0 to beat the LPTIMER driver
 * (@c PRE_KERNEL_2 / @c CONFIG_SYSTEM_CLOCK_INIT_PRIORITY). MCWDT_STRUCT0
 * is NS-accessible under the current PPC config (the LPTIMER driver
 * running from NS at PRE_KERNEL_2 proves that).
 */
static int ns_preempt_mcwdt0_disable(void)
{
	Cy_MCWDT_Unlock(MCWDT_STRUCT0);
	Cy_MCWDT_Disable(MCWDT_STRUCT0,
			 CY_MCWDT_CTR0 | CY_MCWDT_CTR1 | CY_MCWDT_CTR2, 100U);
	Cy_MCWDT_ClearInterrupt(MCWDT_STRUCT0,
				CY_MCWDT_CTR0 | CY_MCWDT_CTR1 | CY_MCWDT_CTR2);
	Cy_MCWDT_SetInterruptMask(MCWDT_STRUCT0, 0U);
	return 0;
}

SYS_INIT(ns_preempt_mcwdt0_disable, PRE_KERNEL_1, 0);

/**
 * @brief Kick the z_pm partition's Layer-B static-bias setup.
 *
 * @details
 * Delegates to @ref z_pm_layer_b_init which psa_calls the z_pm secure
 * partition. That op runs at PC2 and programs the SRSS_MAIN /
 * PWRMODE / core-buck registers that NS cannot touch:
 * @c Cy_SysPm_Init, @c Cy_SysClk_ClkBakSetSource(PILO),
 * BGREF LP, CoreBuck 0.70 V / LP / override on, IHO/IMO DS-off.
 * See tfm_partitions/z_pm/z_pm_partition.c :: z_pm_op_layer_b_init
 * for the exact sequence and the porting plan Phase 6 for rationale.
 *
 * Runs at @c APPLICATION priority 0 (after the TF-M NS interface
 * comes up at @c POST_KERNEL and after every driver has initialised)
 * so psa_call has everything it needs. The Layer-B changes only
 * affect deep-sleep-time behaviour, so running late does not disturb
 * active-mode operation.
 *
 * Logs the result once; failure is non-fatal (system still runs, just
 * with the SE-ROM defaults which leak more in DS).
 */
static int ns_layer_b_init(void)
{
	psa_status_t st = z_pm_layer_b_init();

	if (st == PSA_SUCCESS) {
	} else {
	}
	return 0;
}

SYS_INIT(ns_layer_b_init, APPLICATION, 0);
