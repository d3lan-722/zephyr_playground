/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * z_pm secure partition - S-side implementation.
 *
 * SCOPE: this partition exposes only the operations that cannot be
 * reached from CM33-NS through the PDL's own SRF integration
 * (see cy_syspm_v4.c #ifdef CY_PDL_SYSPM_ENABLE_SRF_INTEG). Today:
 *
 *   Z_PM_OP_PING                 -- proof-of-life for the out-of-tree
 *                                   partition machinery.
 *   Z_PM_OP_LAYER_B_INIT         -- once-at-boot Layer-B static bias.
 *                                   Phase 6 measurement showed the
 *                                   "aggressive" Layer-B knobs
 *                                   (Cy_SysPm_Init recall, BAK↔PILO,
 *                                   BGREF LP, CoreBuck DS voltage) only
 *                                   pay off once the SoC actually enters
 *                                   a full system deep-sleep, which is
 *                                   Phase 7's per-transition op below.
 *                                   Only the IHO / IMO deep-sleep
 *                                   keep-alive clears live here (harmless
 *                                   on this SoC — the bits happen to be
 *                                   0 by default).
 *   Z_PM_OP_SET_DEEP_SLEEP_MODE  -- per-transition Table-2 PPU
 *                                   programming for the system-DS
 *                                   variants (DEEPSLEEP / DEEPSLEEP_RAM /
 *                                   DEEPSLEEP_OFF), bundled with the
 *                                   BGREF LP + CoreBuck DS knobs that
 *                                   Phase 6 deferred. Called by the NS
 *                                   dispatcher for STANDBY substate 2
 *                                   (system_deep_sleep) just before
 *                                   Cy_SysPm_CpuEnterDeepSleep. Argument:
 *                                   uint32_t mode
 * (cy_en_syspm_deep_sleep_mode_t).
 *   Z_PM_OP_ENTER_DS_RAM         -- Phase 8 scoped DS-RAM pre-arm.
 *                                   Runs from NS just before the DS-RAM
 *                                   WFI. Does the PC2-only bits: drop
 *                                   APPCPUSS<-SYSCPU PDCM link, direct
 *                                   PWPR writes to set the AN237976
 *                                   Table-2 DEEPSLEEP_RAM row on the Sys
 *                                   PPUs + PD1, plant the warm-boot
 *                                   diagnostic token in RTC->BREG_SET1[1],
 *                                   apply Layer-B DS bias, and set
 *                                   cy_DeepSleepMode = DEEPSLEEP_RAM so
 *                                   the S-side Cy_SysPm_CpuEnterDeepSleep
 *                                   picks the DS-RAM RAMCTL trims.
 *                                   App-domain PPUs (APPCPU / APPCPUSS)
 *                                   remain CM55's job (CM55 image sets
 *                                   the App PPUs via its own
 *                                   Cy_SysPm_SetDeepSleepMode call).
 *                                   SOCMEM PPU is skipped because SOCMEM
 *                                   PD is off in project 02.

 *
 * Every register touched by these ops is in a PSA-ROT / PC2 region
 * (SRSS_MAIN / PWRMODE_PWRMODE / core-buck) that NS cannot reach
 * directly.
 *
 * The three SRF-covered sleep entry points (Cy_SysPm_CpuEnter{,Deep}Sleep,
 * CM33-side Cy_SysPm_SystemEnterHibernate) are NOT wrapped here:
 * cy_syspm_v4.c on the NS side already packs them into an SRF request
 * and psa_call()s into IFX_EXT_SP; the S handler runs the actual
 * SLEEPDEEP+WFI at PC2. See cm33_ns/src/power.c for the direct-PDL
 * dispatch.
 *
 * Future non-SRF-wrapped ops (phase 9+):
 *   Z_PM_OP_CM55_HIBERNATE_RELAY     -- SRSS_PWR_HIBERNATE from CM55.
 */

#include <stdint.h>

#include "psa/error.h"
#include "psa/service.h"
#include "psa_manifest/z_pm_partition.h"

#include "cy_syspm.h"
#include "cy_sysclk.h"
#include "cy_syspm_pdcm.h"
#include "ppu_v1.h"

/* Op IDs - keep in sync with cm33_ns/src/z_pm_client.h */
#define Z_PM_OP_PING 1
#define Z_PM_OP_LAYER_B_INIT 2
#define Z_PM_OP_SET_DEEP_SLEEP_MODE 3
#define Z_PM_OP_ENTER_DS_RAM 4

#define Z_PM_PING_COOKIE 0xABCD1234u

/* Phase 8: warm-boot diagnostic token planted in RTC->BREG_SET1[1]
 * from Z_PM_OP_ENTER_DS_RAM right before the caller's WFI, read and
 * cleared from cm33_ns/src/main.c on the next boot. Must match
 * cm33_ns/src/warm_boot.h :: WARM_BOOT_TOKEN_DS_RAM. RTC BREG
 * survives every reset short of POR (battery-backed). */
#define WARM_BOOT_TOKEN_DS_RAM 0x16D5DA01u

static psa_status_t z_pm_op_ping(const psa_msg_t *msg)
{
	uint32_t cookie = Z_PM_PING_COOKIE;

	if (msg->out_size[0] < sizeof(cookie)) {
		return PSA_ERROR_BUFFER_TOO_SMALL;
	}

	psa_write(msg->handle, 0, &cookie, sizeof(cookie));
	return PSA_SUCCESS;
}

/*
 * Layer-B static bias (phase 6 — minimal).
 *
 * Runs once at NS boot (SYS_INIT in cm33_ns/src/power.c). Every write
 * targets an SRSS_MAIN / PWRMODE / core-buck register in a PC2-only PPC
 * region — NS cannot perform any of them itself.
 *
 * PHASE-6 EMPIRICAL BISECTION (kit_pse84_eval, cpu_deep_sleep and
 * system_deep_sleep — both currently land on Cy_SysPm_CpuEnterDeepSleep
 * because Phase 7 has not wired per-transition PPU config yet):
 *
 *   Baseline (phase 5.75, no Layer-B):                         62 µA
 *   + Cy_SysPm_Init() (recall of TF-M-S's init_cycfg_power):   +4 µA
 *   + Cy_SysClk_ClkBakSetSource(PILO):                         +2 µA
 *   + SRSS_PWR_CTL2_BGREF_LPMODE_Msk:                          +1 µA
 *   + CoreBuck DS 0.70 V / LP / override:                       0 µA
 *   + IHO / IMO deep-sleep keep-alive cleared:                  0 µA
 *   Full Layer-B (all knobs):                                   67 µA
 *
 * The four "aggressive" knobs (Init recall, BAK↔PILO, BGREF LP,
 * CoreBuck DS) only pay off once the SoC actually enters full
 * system deep sleep — i.e. once Phase 7 programs the Table-2 PPU
 * modes via Cy_SysPm_SetDeepSleepMode(DEEPSLEEP) per transition
 * and every CPU has voted DS. On the current CPU-only-DS path they
 * either add small constant leak (BAK/PILO, BGREF LP) or override
 * TF-M-S's cycfg-programmed defaults with something that would
 * only make sense in a real system-DS context (CoreBuck override).
 *
 * They are therefore DEFERRED to Phase 7, which will fold the
 * relevant writes into the per-transition Z_PM_OP_SET_DEEP_SLEEP_MODE
 * op alongside the PPU programming. Only the physically-harmless
 * IHO / IMO DS-off writes are kept here.
 */
static psa_status_t z_pm_op_layer_b_init(const psa_msg_t *msg)
{
	(void)msg;

	/* Kill the deep-sleep keep-alive on the HF oscillators. PILO
	 * is intentionally left running: it clocks MCWDT0 which is the
	 * Zephyr kernel tick — disabling it would make k_msleep never
	 * return.
	 *
	 * On this build these bits are already 0 by default (measured
	 * neutral to sleep current), so the writes are effectively
	 * a no-op. Kept in the partition op so that a future SoC or
	 * boot-time cycfg change that leaves them set still ends up in
	 * the intended DS-off state. */
	Cy_SysClk_IhoDeepsleepDisable();
	SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;

	return PSA_SUCCESS;
}

/*
 * Per-transition system-DS setup (phase 7).
 *
 * Invoked by the NS system_deep_sleep dispatcher just before
 * Cy_SysPm_CpuEnterDeepSleep. Programs the SRSS-global deep-sleep
 * mode (which selects the AN237976 Table-2 row of PPU retention
 * settings applied when every CPU has voted DS) and applies the
 * per-transition Layer-B bias that Phase 6 measurement deferred
 * out of the at-boot Z_PM_OP_LAYER_B_INIT op.
 *
 * Arguments:
 *   in_vec[0].base = &mode (uint32_t, cy_en_syspm_deep_sleep_mode_t)
 *
 * Valid mode values (bounds-checked): CY_SYSPM_MODE_DEEPSLEEP,
 * CY_SYSPM_MODE_DEEPSLEEP_RAM, CY_SYSPM_MODE_DEEPSLEEP_OFF.
 * The last two are wired end-to-end by Phase 8 / 9; today only
 * DEEPSLEEP is exercised, but the S handler accepts all three so
 * that phases 8/9 don't need to touch this op again.
 *
 * The BGREF LP + CoreBuck DS knobs applied here (deferred from
 * Phase 6) are safe here because reaching this op means the caller
 * is transitioning to system-DS: the SoC will actually enter a
 * PWRMODE state where those knobs matter. On the plain
 * cpu_deep_sleep path this op is never called, so the CPU-only-DS
 * regression measured in Phase 6 does not occur.
 */
static psa_status_t z_pm_op_set_deep_sleep_mode(const psa_msg_t *msg)
{
	uint32_t mode;

	if (msg->in_size[0] < sizeof(mode)) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	if (psa_read(msg->handle, 0, &mode, sizeof(mode)) != sizeof(mode)) {
		return PSA_ERROR_COMMUNICATION_FAILURE;
	}

	/* Bounds check: cy_en_syspm_deep_sleep_mode_t is 0..2 on this
	 * PDL (DEEPSLEEP = 0, DEEPSLEEP_RAM = 1, DEEPSLEEP_OFF = 2). */
	if (mode > (uint32_t)CY_SYSPM_MODE_DEEPSLEEP_OFF) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	if (Cy_SysPm_SetDeepSleepMode((cy_en_syspm_deep_sleep_mode_t)mode) !=
	    CY_SYSPM_SUCCESS) {
		return PSA_ERROR_GENERIC_ERROR;
	}

	/* App-domain (PD1 / APPCPUSS / APPCPU) and SOCMEM PPU
	 * programming is DELIBERATELY NOT done here.
	 *
	 * On CM33, Cy_SysPm_SetDeepSleepMode() routes to
	 * Cy_SysPm_SetSysDeepSleepMode() only — Sys PPUs (MAIN,
	 * SRAM0, SRAM1, SYSCPU) get retention modes. The App PPUs
	 * and SOCMEM PPU are left in their power-on state, so the
	 * PWRMODE state machine cannot fully collapse to system DS
	 * from CM33-S alone. That is why sleep current currently
	 * plateaus at ~68 µA in system_deep_sleep (same floor as
	 * cpu_deep_sleep) — measured Phase 7 first pass.
	 *
	 * A first attempt to also call Cy_SysPm_SetAppDeepSleepMode
	 * and Cy_SysPm_SetSOCMEMDeepSleepMode from here bus-faulted
	 * the S handler:
	 *
	 *   BFAR = 0x54660008 (SOCMEM_PPU->PWSR)
	 *   CFSR = 0x8200 (BFARVALID | PRECISERR)
	 *   PC   = ppu_v1_get_power_mode+0 (ldr r0, [r0, #8])
	 *   LR   = Cy_SysPm_SetSOCMEMDeepSleepMode+... (the self-gate
	 *          read Cy_SysPm_SetSOCMEMDeepSleepMode uses to decide
	 *          whether SOCMEM PPU is ON before writing it)
	 *
	 * i.e. the SOCMEM PPU register file is inaccessible while the
	 * SOCMEM/App domain is off — even a read faults. The same
	 * hazard applies to any of the App-domain PPU registers.
	 * CM33-S has no cheap way to probe PD1/SOCMEM state without
	 * touching one of those PPUs first.
	 *
	 * Correct architecture: CM55 programs its own App-domain +
	 * SOCMEM PPUs (its own Cy_SysPm_SetAppDeepSleepMode /
	 * Cy_SysPm_SetSOCMEMDeepSleepMode calls run at CM55 execution
	 * time, when PD1/SOCMEM are trivially ON). Wiring that is a
	 * Phase-7b task on the CM55 image and is tracked in
	 * porting_plan.md. Once CM55 does its share, the SoC will
	 * actually collapse to system DS with retention on every
	 * PPU and the ~62-68 µA floor should drop into the low
	 * tens of µA. */

	/* Layer-B knobs deferred from Phase 6 (see file header + Phase 6
	 * bisection table in porting_plan.md). Safe here: the caller has
	 * committed to a system-DS transition, so the SoC will actually
	 * reach a PWRMODE state where these bias the leakage floor down
	 * instead of up. */
	SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;
	Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
	Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
	Cy_SysPm_CoreBuckDpslpEnableOverride(true);

	return PSA_SUCCESS;
}

/*
 * Phase 8 (scoped): DS-RAM pre-arm from NS.
 *
 * Called by cm33_ns/src/power.c :: enter_ds_ram just before the NS
 * CPU-state prep (FPU power-gate / SysTick stop / MCWDT arm / NVIC
 * silence / DCache clean) and the final Cy_SysPm_CpuEnterDeepSleep.
 * All work here is PC2-only:
 *
 *   1. Drop the APPCPUSS <- SYSCPU PDCM dependency so APPCPUSS can
 *      fold while SYSCPU is requested OFF (our DS-RAM target).
 *      Re-asserted by HW on every cold boot; must run on every entry.
 *
 *   2. Re-assert AN237976 Table-2 DS-RAM policies on the PPUs we
 *      own from CM33-S:
 *          MAIN / SRAM0 / SRAM1 / PD1 -> MEM_RET
 *          SYSCPU                     -> OFF
 *      NS Cy_SysPm_Init (via init_cycfg_power on boot) programs
 *      MAIN=FULL_RET, SYSCPU=FULL_RET which would demote DS-RAM to
 *      plain DEEPSLEEP; we clobber those back to MEM_RET/OFF on
 *      every entry. APPCPU / APPCPUSS PPUs are CM55's job (see
 *      cm55/src/main.c). SOCMEM is off in project 02 (unused) so
 *      its PPU is left alone (register file is inaccessible while
 *      SOCMEM PD is down -- reads bus-fault, see Phase 7 finding).
 *
 *      DIRECT PWPR writes, NOT cy_pd_ppu_set_power_mode: the PDL
 *      wrapper's ppu_v1_dynamic_enable spins on PWSR.PWR_DYN_STATUS
 *      after the write, which never re-asserts when the write is a
 *      no-op (warm-boot path from a previous DS-RAM cycle). Direct
 *      register write is safe because writing the same policy value
 *      is idempotent at the PWPR level -- there is no HW handshake
 *      to complete. Reference: tmp/17_pse84_ds_ram_exact/
 *      m33_ns/src/ds_ram_enter.c :: ds_ram_program_ppu_policies.
 *
 *   3. Layer-B DS bias: BGREF LP + CoreBuck 0.70 V / LP / override.
 *      Same knobs the Phase-7 Z_PM_OP_SET_DEEP_SLEEP_MODE applies;
 *      the SUSPEND_TO_RAM dispatcher does NOT go through that op so
 *      we re-apply here.
 *
 *   4. Plant WARM_BOOT_TOKEN_DS_RAM in RTC->BREG_SET1[1]. NS reads
 *      and clears the token in main() on the next boot to confirm
 *      the previous cycle actually reached WFI (warm-boot proof).
 *
 *   5. Cy_SysPm_DeepSleepSetup(DEEPSLEEP_RAM) -- writes the driver-
 *      internal cy_DeepSleepMode variable that the S-side
 *      Cy_SysPm_CpuEnterDeepSleep consults to pick the DS-RAM
 *      RAMCTL trim path (Cy_SysPm_SetRamTrimsPreDs). Without this
 *      the chip silently demotes to plain DEEPSLEEP.
 *
 * Returns to the NS caller so it can run the NS-side CPU prep and
 * issue the final Cy_SysPm_CpuEnterDeepSleep itself; that call
 * takes the NS-side SRF branch and re-lands in the S SLEEPDEEP+WFI
 * handler at PC2. SRAM macro retention (MXSRAMC PWR_MACRO_CTL) is
 * intentionally not touched in this scoped-Option-2 first cut -- all
 * SRAM stays retained (default), giving us maximum warm-boot
 * survivability so we can verify the round-trip first.
 */
static psa_status_t z_pm_op_enter_ds_ram(const psa_msg_t *msg)
{
	(void)msg;

	/* Phase-8 bisection C (2026-07-14): PDCM clear re-added.
	 *
	 * Bisection A removed three suspects to fix the CPUSS peripheral
	 * fault (ifx_fault_irq_handler): PDCM clear, PD1 PPU write, and
	 * BREG token plant. Bisection B moved on to fix a HardFault by
	 * disabling CONFIG_FPU / CONFIG_FPU_SHARING (removed vstmia from
	 * TF-M NS dispatch). Result: one DS-RAM commit succeeds after
	 * POR, then all subsequent entries print "pm: DS-RAM refused"
	 * (WFI returns without warm reset).
	 *
	 * Per tmp/17_pse84_ds_ram_exact/m33_ns/src/ds_ram_enter.c ::
	 * ds_ram_clear_pdcm_link: the APPCPUSS<-SYSCPU PDCM dependency
	 * is re-asserted by HW on every cold and warm boot; without
	 * clearing it, APPCPUSS cannot fold while SYSCPU is requested
	 * OFF (our DS-RAM target), so the PWRMODE state machine demotes
	 * the request to plain DEEPSLEEP.
	 *
	 * If this write triggers the CPUSS fault we saw in bisection A,
	 * PDCM was the culprit and we'll need a different way. If it
	 * doesn't fault AND DS-RAM starts cycling, we then add back the
	 * BREG token plant + PD1 PPU write in bisection D.
	 */
	(void)cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPUSS,
					  CY_PD_PDCM_SYSCPU);

	/* Direct-PWPR DS-RAM policies on the PPUs we own here.
	 * PD1 is temporarily disabled - see bisection note above. */
	struct ppu_target {
		volatile uint32_t *pwpr;
		uint32_t mode;
	};
	static const struct ppu_target ppu_targets[] = {
	    {(volatile uint32_t *)(CY_PPU_MAIN_BASE + 0U), PPU_V1_MODE_MEM_RET},
	    {(volatile uint32_t *)(CY_PPU_SRAM0_BASE + 0U),
	     PPU_V1_MODE_MEM_RET},
	    {(volatile uint32_t *)(CY_PPU_SRAM1_BASE + 0U),
	     PPU_V1_MODE_MEM_RET},
	    {(volatile uint32_t *)(CY_PPU_SYSCPU_BASE + 0U), PPU_V1_MODE_OFF},
	    /* {(volatile uint32_t *)(CY_PPU_PD1_BASE + 0U),
	     *  PPU_V1_MODE_MEM_RET}, */
	};
	for (uint32_t i = 0U;
	     i < (sizeof(ppu_targets) / sizeof(ppu_targets[0])); i++) {
		uint32_t pwpr = *ppu_targets[i].pwpr;
		pwpr &= ~PPU_V1_PWPR_POLICY;
		pwpr |= PPU_V1_PWPR_DYNAMIC_EN | ppu_targets[i].mode;
		*ppu_targets[i].pwpr = pwpr;
	}

	/* Layer-B DS bias. */
	SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;
	Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
	Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
	Cy_SysPm_CoreBuckDpslpEnableOverride(true);

	/* Tell the S-side syspm which DS variant we intend to enter
	 * so Cy_SysPm_CpuEnterDeepSleep picks the DS-RAM RAMCTL trims. */
	Cy_SysPm_DeepSleepSetup(CY_SYSPM_MODE_DEEPSLEEP_RAM);

	return PSA_SUCCESS;
}

psa_status_t z_pm_service_sfn(const psa_msg_t *msg)
{
	switch (msg->type) {
	case Z_PM_OP_PING:
		return z_pm_op_ping(msg);
	case Z_PM_OP_LAYER_B_INIT:
		return z_pm_op_layer_b_init(msg);
	case Z_PM_OP_SET_DEEP_SLEEP_MODE:
		return z_pm_op_set_deep_sleep_mode(msg);
	case Z_PM_OP_ENTER_DS_RAM:
		return z_pm_op_enter_ds_ram(msg);
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}
}

psa_status_t z_pm_partition_init(void) { return PSA_SUCCESS; }
