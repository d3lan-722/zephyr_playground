/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr PM dispatcher for the CM33-NS image.
 *
 * ARCHITECTURE (Phase 6 — Option D applied):
 *
 * This build assumes Option D from doc/TFM_tutorial.md §29 has been
 * applied — i.e. the TF-M-Secure PPC configuration has been narrowed
 * so that SRSS_MAIN, SRSS_HIB_DATA, PWRMODE_PWRMODE, APPCPUSS_AP,
 * M55APPCPUSS, RAMC0_RAM_PWR, RAMC1_RAM_PWR and M33SYSCPUSS are all
 * NS-accessible. Run util/apply_option_d.sh before first build (and
 * again after any `west update` that reverts the modules tree).
 *
 * With Option D in place, every PDL syspm call — including the
 * un-SRF-wrapped ones (Cy_SysPm_SetSysDeepSleepMode,
 * SetSOCMEMDeepSleepMode, SetAppDeepSleepMode, CoreBuckDpslp*,
 * BGREF_LPMODE_Msk write, IHO/IMO DS-disable) — executes directly
 * from NS. The out-of-tree z_pm partition is NOT used for any PM
 * functionality; it survives only as the PING proof-of-life for the
 * TFM_partition_tutorial demo.
 *
 * Isolation cost of Option D: NS can now reprogram SRSS clocks,
 * hibernate, PWRMODE PPU, SRAM PPUs, APPCPUSS-domain PPUs, and CM33
 * SYSCPU (which also opens MSC/DDFT/AP debug windows). Acceptable
 * for dev-board bring-up. See doc/TFM_tutorial.md §30 "What
 * wrapping actually buys you" for the trade-off analysis.
 *
 * BOOT-TIME INIT (Layer-B):
 *
 * ifx_pm_init runs at PRE_KERNEL_1 and applies the static SRSS
 * biasing that lowers deep-sleep current: BGREF low-power mode,
 * core-buck deep-sleep voltage/mode/override, and IHO/IMO
 * deep-sleep keep-alive disable. Ported from tmp/16 with the
 * boot-time Cy_SysPm_SetDeepSleepMode(DEEPSLEEP) call INTENTIONALLY
 * OMITTED — the SRSS-global deep-sleep mode must be set
 * per-transition (Phase 7) so the residency policy can pick DS-RAM
 * or DS-OFF at runtime without a boot-time lock-in.
 *
 * PER-TRANSITION DISPATCH:
 *
 * pm_state_set overrides Zephyr's weak default and dispatches to
 * per-state helpers. Each helper switches to PRIMASK before WFI
 * (see pm_irq_prologue for why). The SoC-supplied pm_state_set (in
 * soc/infineon/edge/pse84/power.c) is dropped from the build by
 * cm33_ns/CMakeLists.txt because its own PRE_KERNEL_1 SYS_INIT
 * calls Cy_SysPm_SetDeepSleepMode(DEEPSLEEP) which locks the mode.
 *
 * PREREQUISITE:
 *
 * CONFIG_IDLE_STACK_SIZE=2048. tfm_ns_interface_dispatch allocates
 * a 136-byte fpu_ctx_full on the caller's stack; on the deeper
 * NS→S call chains the default 320-byte idle stack overflows. See
 * prj.conf. (This applies only to any residual psa_call in the NS
 * image — with Option D no PM path uses psa_call, but z_pm_ping
 * from main.c still does.)
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/printk.h>

#include <cmsis_core.h>

#include "cy_device.h"
#include "cy_syspm.h"
#include "cy_sysclk.h"

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
 * (@c Cy_SysPm_CpuEnterDeepSleep) but kept as a distinct call site
 * so phase 7+ can specialise it without disturbing the per-CPU
 * deep-sleep path. Those extensions (DS-RAM / DS-OFF selection,
 * Layer-B bias, retention patterns) will call
 * @c Cy_SysPm_SetSysDeepSleepMode et al., which are NOT SRF-wrapped
 * in the PDL and MUST route through the z_pm partition (or Option D,
 * see doc/TFM_tutorial.md §29). Uses the magenta indicator LED.
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

/* -------------------------------------------------------------------
 * Phase 6 — Layer-B static bias
 * -------------------------------------------------------------------
 * Runs once at boot. All calls touch SRSS registers that are
 * PPC-secured by default; Option D (see file header) is what makes
 * this reachable from NS. */

/**
 * @brief Put the bandgap reference into low-power mode.
 *
 * Only affects the current the BGREF draws while the chip is in
 * DEEPSLEEP. No effect on Active mode.
 * PPC region: SRSS_MAIN.
 */
static void enable_bgref_low_power_mode(void)
{
	SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;
}

/**
 * @brief Reconfigure the core buck for DeepSleep.
 *
 * Drops the DS-time regulated voltage to 0.70 V and switches the
 * buck to its low-power (high-ripple) loop. Override-on forces the
 * DS branch of the buck FSM regardless of any competing vote.
 * PPC region: SRSS_MAIN.
 */
static void configure_core_buck_for_deep_sleep(void)
{
	Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
	Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
	Cy_SysPm_CoreBuckDpslpEnableOverride(true);
}

/**
 * @brief Clear the deep-sleep keep-alive on IHO and IMO.
 *
 * PILO is deliberately left running: it clocks MCWDT0 (Zephyr
 * kernel tick). Disabling it here would make `k_msleep` never
 * return.
 * PPC region: SRSS_MAIN.
 */
static void disable_oscillators_in_deep_sleep(void)
{
	Cy_SysClk_IhoDeepsleepDisable();
	SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;
}

/**
 * @brief Boot-time PM initialisation.
 *
 * @details
 * Applies the state-independent Layer-B biases documented in
 * tmp/16 `ifx_pm_init` and in AN237976 (BGREF LP, core-buck DS,
 * IHO/IMO DS-disable), plus a clock-select for the backup domain
 * so CLK_BAK stays on PILO through every DS variant.
 *
 * DELIBERATELY OMITTED from the reference implementation:
 * `Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP)`. Setting
 * the SRSS-global deep-sleep mode at boot locks the project to
 * one variant and prevents runtime residency-policy dispatch to
 * DS-RAM / DS-OFF. Mode selection lives in each per-state
 * dispatcher (Phase 7 — not yet wired).
 *
 * Runs at `PRE_KERNEL_1`; overrides the SoC-supplied ifx_pm_init
 * (dropped by cm33_ns/CMakeLists.txt because it calls
 * SetDeepSleepMode at boot).
 */
static int ifx_pm_init(void)
{
	Cy_SysPm_Init();
	Cy_SysClk_ClkBakSetSource(CY_SYSCLK_BAK_IN_PILO);
	enable_bgref_low_power_mode();
	configure_core_buck_for_deep_sleep();
	disable_oscillators_in_deep_sleep();
	return 0;
}
SYS_INIT(ifx_pm_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
