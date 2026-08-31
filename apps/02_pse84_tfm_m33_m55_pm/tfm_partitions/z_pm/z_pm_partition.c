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
 *                                   (Cy_SysPm_Init recall, BAK<->PILO,
 *                                   BGREF LP, CoreBuck DS voltage) only
 *                                   pay off once the SoC actually enters
 *                                   a full system deep-sleep, which is
 *                                   Phase 7's per-transition op below.
 *                                   Only the IHO / IMO deep-sleep
 *                                   keep-alive clears live here (harmless
 *                                   on this SoC -- the bits happen to be
 *                                   0 by default).
 *   Z_PM_OP_SET_DEEP_SLEEP_MODE  -- per-transition Table-2 PPU
 *                                   programming for system_deep_sleep,
 *                                   bundled with the BGREF LP +
 *                                   CoreBuck DS knobs that Phase 6
 *                                   deferred. Called by the NS
 *                                   dispatcher for STANDBY substate 2
 *                                   (system_deep_sleep) just before
 *                                   Cy_SysPm_CpuEnterDeepSleep.
 *                                   Argument: uint32_t mode
 *                                   (cy_en_syspm_deep_sleep_mode_t).
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
 * Phase 8 (DS-RAM, Z_PM_OP_ENTER_DS_RAM = 4) was implemented and
 * reverted; see apps/02_pse84_tfm_m33_m55_pm/DS_RAM_RETROSPECTIVE.md
 * for the write-up of what was tried and what would be required to
 * finish it.
 */

#include <stdint.h>

#include "psa/error.h"
#include "psa/service.h"
#include "psa_manifest/z_pm_partition.h"

#include "cy_syspm.h"
#include "cy_sysclk.h"
#include "cy_device.h"

/* Op IDs - keep in sync with cm33_ns/src/z_pm_client.h */
#define Z_PM_OP_PING 1
#define Z_PM_OP_LAYER_B_INIT 2
#define Z_PM_OP_SET_DEEP_SLEEP_MODE 3
#define Z_PM_OP_CLK_ROOT_SELECT_ENABLE  4
#define Z_PM_OP_CLK_ROOT_SELECT_DISABLE 5
#define Z_PM_OP_READ_REGISTER 6
//#define Z_PM_OP_WRITE_REGISTER 7
#define Z_PM_PING_COOKIE 0xABCD1234u

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

static psa_status_t z_pm_op_clk_root_select_enable(const psa_msg_t *msg)
{
    uint32_t index;
    if (msg->in_size[0] < sizeof(index)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (psa_read(msg->handle, 0, &index, sizeof(index)) != sizeof(index)) {
        return PSA_ERROR_COMMUNICATION_FAILURE;
    }
    if (index > 15u) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    SRSS->CLK_ROOT_SELECT[index] |= (1u << 31u);
    return PSA_SUCCESS;
}

static psa_status_t z_pm_op_clk_root_select_disable(const psa_msg_t *msg)
{
    uint32_t index;
    if (msg->in_size[0] < sizeof(index)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (psa_read(msg->handle, 0, &index, sizeof(index)) != sizeof(index)) {
        return PSA_ERROR_COMMUNICATION_FAILURE;
    }
    if (index > 15u) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    SRSS->CLK_ROOT_SELECT[index] &= ~(1u << 31u);
    return PSA_SUCCESS;
}

static psa_status_t z_pm_op_read_register(const psa_msg_t *msg)
{
    uint32_t address;
    uint32_t value;

    if (msg->in_size[0] < sizeof(address)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (msg->out_size[0] < sizeof(value)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }


    if (psa_read(msg->handle, 0, &address, sizeof(address)) != sizeof(address)) {
        return PSA_ERROR_COMMUNICATION_FAILURE;
    }

    value = *(uint32_t *)address;
    
    psa_write(msg->handle, 0, &value, sizeof(value));

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
	case Z_PM_OP_CLK_ROOT_SELECT_ENABLE:
    		return z_pm_op_clk_root_select_enable(msg);
	case Z_PM_OP_CLK_ROOT_SELECT_DISABLE:
    		return z_pm_op_clk_root_select_disable(msg);
	case Z_PM_OP_READ_REGISTER:
		return z_pm_op_read_register(msg);
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}
}

psa_status_t z_pm_partition_init(void) { return PSA_SUCCESS; }
