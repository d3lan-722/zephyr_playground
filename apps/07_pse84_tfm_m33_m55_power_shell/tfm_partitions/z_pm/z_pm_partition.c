/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * z_pm secure partition — S-side implementation.
 *
 * The partition dispatches PSA calls from CM33-NS Zephyr to PDL entry
 * points that reach Bucket-C registers (SRSS_MAIN / PWRMODE_PWRMODE /
 * RRAM_SFR / core-buck) -- regions that are PC=2 only on this build
 * and cannot be reached from NS through the PDL's built-in SRF
 * integration.
 *
 * Ops carried over from project 02 (unchanged, kept for compatibility):
 *   Z_PM_OP_PING                 -- proof-of-life.
 *   Z_PM_OP_LAYER_B_INIT         -- minimal IHO/IMO DS-off writes.
 *                                   Superseded by DEEP_SLEEP_BIAS in
 *                                   this project but retained.
 *   Z_PM_OP_SET_DEEP_SLEEP_MODE  -- per-transition PPU programming +
 *                                   inline BGREF/CoreBuck bias.
 *                                   Superseded by DEEP_SLEEP_BIAS in
 *                                   this project but retained.
 *
 * Ops added by this project (Phase D of the plan in PLAN.md):
 *   Z_PM_OP_SWITCH_ACTIVE_MODE   -- HP/LP/ULP transition body (HF0
 *                                   divider strategy).
 *   Z_PM_OP_CLOCK_PROBE          -- measured + computed frequency
 *                                   readouts (DPLL_LP0/CLK_HF0/
 *                                   CLK_HF10) using the SoC's
 *                                   Cy_SysClk_StartClkMeasurementCounters
 *                                   with IHO as the reference clock.
 *   Z_PM_OP_DEEP_SLEEP_BIAS      -- one-shot boot-time programming of
 *                                   the SRSS/PMU/buck/clock knobs
 *                                   that lower the sleep floor of
 *                                   subsequent Cy_SysPm_CpuEnterDeepSleep
 *                                   calls (BGREF LP, CoreBuck DS 0.70 V
 *                                   LP override, IHO/IMO DS-off,
 *                                   ClkBak <- PILO, SetDeepSleepMode
 *                                   DEEPSLEEP).
 *   Z_PM_OP_BOOT_CLOCK_RETUNE    -- one-shot boot-time DPLL_LP0
 *                                   retune to 200 MHz + CLK_HF0
 *                                   divider /1, matching project 06's
 *                                   baseline. Only called from NS if
 *                                   the divider strategy needs it
 *                                   (probe first, decide later).
 *
 * SRF-covered sleep entry points (Cy_SysPm_CpuEnter{,Deep}Sleep,
 * Cy_SysPm_SystemEnterHibernate, Cy_SysCM55Enable) are NOT wrapped
 * here: the DSL PDL already packs them into an SRF request and
 * psa_call()s into IFX_EXT_SP; the S handler runs the actual
 * SLEEPDEEP+WFI at PC=2 on our behalf.
 */

#include <stdint.h>
#include <string.h>

#include "psa/error.h"
#include "psa/service.h"
#include "psa_manifest/z_pm_partition.h"

#include "cy_pdl.h"
#include "cy_rram.h"
#include "cy_sysclk.h"
#include "cy_syspm.h"

/* The DSL PDL's Cy_SysClk_DpllLp/HpConfigure use abs() from <stdlib.h>.
 * The TF-M SPE build with picolibc does not pull libc into the S
 * link, so provide a minimal implementation here. Referenced from
 * Z_PM_OP_BOOT_CLOCK_RETUNE via Cy_SysClk_PllConfigure. */
int abs(int x);
int abs(int x) { return x < 0 ? -x : x; }

/* -------------------------------------------------------------------
 * Attempted S-side raw-SCB2 trace REMOVED.
 *
 * A one-byte write to `*(volatile uint32_t *)0x529a0240` from this
 * partition context hangs the SPE. The empirical proof: adding a
 * single `*trace = 'P'` at the start of z_pm_op_ping (an op that was
 * previously round-tripping fine) made the NS-side `z_pm_ping()`
 * never return -- and the "z_pm ping ok" line stopped appearing in
 * the boot log.
 *
 * Likely cause: SCB2 Secure alias 0x529a0000 lies in a PPC region
 * whose access-list does not include the z_pm partition. Project 06
 * uses the same alias fine because it runs as a bare CM33-S image
 * with no PPC/TF-M gating. Enabling access here would require adding
 * SCB2 to z_pm's partition-region grant in cycfg_system.c (not
 * covered by z_pm_partition.yaml today) -- Phase-E work.
 *
 * Diagnostic strategy without S-side console: binary-search
 * elimination via the `Z_PM_S_DRY_RUN_TRANS` macro below.
 * ------------------------------------------------------------------- */

/* Binary-search staging for the transition body. Each stage adds one
 * PDL call to the previous stage's set:
 *
 *   0 -- dry run, all three PDL calls stubbed. Proves PSA plumbing.
 *   1 -- + Cy_SysClk_ClkHfSetDivider(0, div).
 *   2 -- + Cy_SysPm_SystemEnter{Hp,Lp,Ulp}. HANGS: those functions
 *        internally call Cy_SysPm_SetTrimRamCtl, which writes
 *        SRSS->RAM_TRIM_STRUCT. That register lives in a separate
 *        PPC region (PROT_PERI0_RAM_TRIM_SRSS_SRAM) which is only
 *        listed in cycfg_system.c's M33_M55_ppc_0_regions[]
 *        (NS-accessible) and NOT in any of the TFM_SP_* per-partition
 *        access lists. Writes from the z_pm partition context bus-
 *        fault silently and TF-M halts (HALT_ON_CORE_PANIC=ON).
 *   2A - STAGE 1 + a bare Cy_SysPm_CoreBuckSetProfile + Status poll
 *        WITHOUT the SRAM_TRIM writes. Works. Voltage transient is
 *        real; SRAM margin is off-spec but tolerable for the demo.
 *   3  - + Cy_RRAM_SetVoltageMode. HANGS: BFAR = 0x52213024
 *        (dump_pc.sh, HardFault, CFSR.BFARVALID+PRECISERR). That
 *        address is inside RRAMC0->RRAMC[0] whose parent PPC region
 *        `RRAMC0_RRAM_SFR_RRAMC_SFR_USER` is NS-only per cycfg_ppc.h
 *        -- writes from z_pm bus-fault. Same class of issue as
 *        SRAM_TRIM at STAGE 2. RRAM VMODE only tunes the RRAM
 *        controller's read/write timing for the current voltage;
 *        skipping it leaves the controller at HP timings during
 *        LP/ULP, which just adds access margin (not a crash).
 *
 * Landed state: STAGE 1 + Z_PM_S_TRANS_STAGE_2A = 1. Both SRAM_TRIM
 * and RRAM VMODE skipped due to PPC allowlist. Fixing them properly
 * requires adding the two regions to z_pm's mmio access list
 * (edits modules-tree cycfg files -- Phase-E task). */
#define Z_PM_S_TRANS_STAGE 1

/* When set, use the STAGE 2A CoreBuck-only bypass INSTEAD of
 * Cy_SysPm_SystemEnter* (which faults on the SRAM_TRIM writes).
 * Keep at 1 until SRAM_TRIM access is granted to z_pm. */
#define Z_PM_S_TRANS_STAGE_2A 1

/* Op IDs — keep in sync with cm33_ns/src/z_pm_client.h */
#define Z_PM_OP_PING 1
#define Z_PM_OP_LAYER_B_INIT 2
#define Z_PM_OP_SET_DEEP_SLEEP_MODE 3
#define Z_PM_OP_SWITCH_ACTIVE_MODE 4
#define Z_PM_OP_CLOCK_PROBE 5
#define Z_PM_OP_DEEP_SLEEP_BIAS 6
#define Z_PM_OP_BOOT_CLOCK_RETUNE 7

#define Z_PM_PING_COOKIE 0xABCD1234u

/* Encoded pm_mode_t — see z_pm_client.h. Ordered by increasing
 * performance (matches the NS-side pm_mode_t so the raw uint32_t
 * casts work). */
#define Z_PM_MODE_ULP 0u
#define Z_PM_MODE_LP 1u
#define Z_PM_MODE_HP 2u

/* Wire-format structs for the SWITCH_ACTIVE_MODE and CLOCK_PROBE ops.
 * Kept binary-identical to the NS-side structs in z_pm_client.h. */
struct z_pm_switch_in {
	uint32_t source; /* Encoded pm_mode_t */
	uint32_t target; /* Encoded pm_mode_t */
};

struct z_pm_switch_out {
	int32_t status; /* 0 on success, negative errno-like on failure. */
};

struct z_pm_clock_probe_out {
	/* All values in Hz. `meas_*` come from the SoC's clock-measurement
	 * counters (Cy_SysClk_StartClkMeasurementCounters, IHO reference);
	 * `comp_*` come from the PDL's Cy_SysClk_ClkPathGetFrequency /
	 * ClkHfGetFrequency register readback. Side-by-side lets a stuck
	 * counter or a driver-cache mismatch be spotted immediately. */
	uint32_t meas_path0;
	uint32_t meas_hf0;
	uint32_t meas_hf10;
	uint32_t comp_path0;
	uint32_t comp_hf0;
	uint32_t comp_hf10;
};

/* -------------------------------------------------------------------
 * Op: PING (id 1). Round-trip proof-of-life for the partition +
 * PSA-call plumbing.
 * ------------------------------------------------------------------- */
static psa_status_t z_pm_op_ping(const psa_msg_t *msg)
{
	uint32_t cookie = Z_PM_PING_COOKIE;

	if (msg->out_size[0] < sizeof(cookie)) {
		return PSA_ERROR_BUFFER_TOO_SMALL;
	}

	psa_write(msg->handle, 0, &cookie, sizeof(cookie));
	return PSA_SUCCESS;
}

/* -------------------------------------------------------------------
 * Op: LAYER_B_INIT (id 2). Minimal Layer-B bias — kept as-is from
 * project 02 for compatibility. This project's NS side does not call
 * it: the fuller DEEP_SLEEP_BIAS op below covers everything.
 * ------------------------------------------------------------------- */
static psa_status_t z_pm_op_layer_b_init(const psa_msg_t *msg)
{
	(void)msg;
	Cy_SysClk_IhoDeepsleepDisable();
	SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;
	return PSA_SUCCESS;
}

/* -------------------------------------------------------------------
 * Op: SET_DEEP_SLEEP_MODE (id 3). Per-transition SysDeepSleepMode +
 * inline BGREF/CoreBuck knobs — kept as-is from project 02 for
 * compatibility. This project's NS side does not call it: the
 * DEEP_SLEEP_BIAS op below programs the same knobs once at boot.
 * ------------------------------------------------------------------- */
static psa_status_t z_pm_op_set_deep_sleep_mode(const psa_msg_t *msg)
{
	uint32_t mode;

	if (msg->in_size[0] < sizeof(mode)) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	if (psa_read(msg->handle, 0, &mode, sizeof(mode)) != sizeof(mode)) {
		return PSA_ERROR_COMMUNICATION_FAILURE;
	}
	if (mode > (uint32_t)CY_SYSPM_MODE_DEEPSLEEP_OFF) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	if (Cy_SysPm_SetDeepSleepMode((cy_en_syspm_deep_sleep_mode_t)mode) !=
	    CY_SYSPM_SUCCESS) {
		return PSA_ERROR_GENERIC_ERROR;
	}
	SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;
	Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
	Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
	Cy_SysPm_CoreBuckDpslpEnableOverride(true);
	return PSA_SUCCESS;
}

/* -------------------------------------------------------------------
 * Op: SWITCH_ACTIVE_MODE (id 4). HP/LP/ULP transitions using the
 * HF0-divider strategy. Body ported from project 06's
 * power_manager_hf0_divider.c trans_* helpers, minus the phase-cycle
 * logging (that dependency on k_cycle_get_32() doesn't transfer to
 * the S build).
 *
 * DPLL_LP0 boot state on kit_pse84_eval m33/ns is 400 MHz (verified
 * on hardware via CLOCK_PROBE below). CLK_HF10 boots at DPLL/4 =
 * 100 MHz. We do NOT retune DPLL -- the S-side PllDisable/Enable
 * sequence hangs the CPU on this build (Phase-E investigation).
 * Instead the dividers are chosen relative to the 400 MHz DPLL to
 * hit spec-compliant target frequencies:
 *
 *   Mode | CLK_HF0 divider | CLK_HF0     | CM33 spec ceiling
 *   -----|-----------------|-------------|--------------------
 *   HP   |    /2 (boot)    |   200 MHz   |   200 MHz  (at spec)
 *   LP   |    /6           |    66.67 MHz|    80 MHz  (below)
 *   ULP  |    /8           |    50 MHz   |    50 MHz  (at spec)
 *
 * CLK_HF10 (SCB2 pclk) stays at DPLL/4 = 100 MHz in every mode --
 * we never touch it -- so the console baud divisor never becomes
 * stale and pm_reconfigure_console_uart() is unnecessary. That's
 * the second big win of picking the divider strategy over PLL retune.
 *
 * Direction rule (AN237976 + Infineon reference):
 *   Down (voltage falls): SET_DIV first, then SystemEnter*,
 *                         then Cy_RRAM_SetVoltageMode.
 *   Up   (voltage rises): SystemEnter* first, then
 *                         Cy_RRAM_SetVoltageMode, then SET_DIV.
 * ------------------------------------------------------------------- */

/** Divider that maps a target mode to the CLK_HF0 pre-scaler value.
 *  Values chosen for a DPLL_LP0 = 400 MHz baseline (SE-ROM default on
 *  kit_pse84_eval m33/ns -- verified via CLOCK_PROBE). */
static cy_en_clkhf_dividers_t hf0_div_for_mode(uint32_t target)
{
	switch (target) {
	case Z_PM_MODE_HP:
		return CY_SYSCLK_CLKHF_DIVIDE_BY_2; /* 400/2 = 200 MHz */
	case Z_PM_MODE_LP:
		return CY_SYSCLK_CLKHF_DIVIDE_BY_6; /* 400/6 =  66.7 MHz */
	case Z_PM_MODE_ULP:
		return CY_SYSCLK_CLKHF_DIVIDE_BY_8; /* 400/8 =  50 MHz */
	default:
		return CY_SYSCLK_CLKHF_DIVIDE_BY_2;
	}
}

/** Map a target mode to its CoreBuck profile enum value. */
static cy_en_syspm_core_buck_profile_t
core_buck_profile_for_mode(uint32_t target)
{
	switch (target) {
	case Z_PM_MODE_HP:
		return CY_SYSPM_CORE_BUCK_PROFILE_HP;
	case Z_PM_MODE_LP:
		return CY_SYSPM_CORE_BUCK_PROFILE_LP;
	case Z_PM_MODE_ULP:
		return CY_SYSPM_CORE_BUCK_PROFILE_ULP;
	default:
		return CY_SYSPM_CORE_BUCK_PROFILE_HP;
	}
}

/** Bare CoreBuck profile change + SramLdo enable/disable, no
 *  SRAM_TRIM writes.
 *
 *  Empirically (Phase-D fault forensics): the PPC allows Master-0
 *  (NS master) to write RAM_TRIM_SRSS_SRAM but blocks Master-1 (S
 *  data-master). Attempts to call Cy_SysPm_SystemEnter{Lp,Ulp} from
 *  this partition trigger a PERI_0_PERI_MS1_PPC_VIO on the very
 *  first SRSS->RAM_TRIM_STRUCT[i] store. So the trim writes MUST
 *  stay on the NS side (see power_manager.c apply_trim_seq). */
static cy_en_syspm_status_t core_buck_only_enter(uint32_t source,
						 uint32_t target)
{
	cy_en_syspm_status_t st;

	/* LP -> ULP: enable SRAM LDO first (per PDL sequence), then
	 * switch buck profile. */
	if (source == Z_PM_MODE_LP && target == Z_PM_MODE_ULP) {
		st = Cy_SysPm_SramLdoEnable(true);
		if (st != CY_SYSPM_SUCCESS) {
			return st;
		}
		Cy_SysPm_CoreBuckSetProfile(CY_SYSPM_CORE_BUCK_PROFILE_ULP);
		return Cy_SysPm_CoreBuckStatus();
	}

	/* ULP -> LP: switch buck profile first, then disable SRAM LDO
	 * so CoreBuck supplies SRAM directly (per PDL sequence). */
	if (source == Z_PM_MODE_ULP && target == Z_PM_MODE_LP) {
		Cy_SysPm_CoreBuckSetProfile(CY_SYSPM_CORE_BUCK_PROFILE_LP);
		st = Cy_SysPm_CoreBuckStatus();
		if (st != CY_SYSPM_SUCCESS) {
			return st;
		}
		return Cy_SysPm_SramLdoEnable(false);
	}

	/* HP <-> LP: just switch the buck profile. */
	Cy_SysPm_CoreBuckSetProfile(core_buck_profile_for_mode(target));
	return Cy_SysPm_CoreBuckStatus();
}

/** Direction-split transition — HP<->LP only.
 *
 *  z_pm S handles the HF0 divider write and the CoreBuck/SramLdo
 *  change. NS applies SRAM_TRIM around the psa_call because PPC
 *  denies MS1 (S) writes to RAM_TRIM_SRSS_SRAM.
 *
 *  ULP (0.7 V) transitions are intentionally not supported --
 *  see INVESTIGATION_pse84_ulp.md for the analysis. NS refuses
 *  ULP targets in pm_switch_to() so this handler only ever sees
 *  HP <-> LP requests.
 *
 *  Direction rule (voltage vs. clock):
 *    Down: SET_DIV first (safe: high V, low F), then CoreBuck.
 *    Up:   CoreBuck first (voltage rises), then SET_DIV. */
static int32_t trans_down(uint32_t source, uint32_t target)
{
	Cy_SysClk_ClkHfSetDivider(0u, hf0_div_for_mode(target));
	if (core_buck_only_enter(source, target) != CY_SYSPM_SUCCESS) {
		Cy_SysClk_ClkHfSetDivider(0u, hf0_div_for_mode(source));
		return -6; /* -EIO */
	}
	return 0;
}

static int32_t trans_up(uint32_t source, uint32_t target)
{
	if (core_buck_only_enter(source, target) != CY_SYSPM_SUCCESS) {
		return -6; /* -EIO */
	}
	Cy_SysClk_ClkHfSetDivider(0u, hf0_div_for_mode(target));
	return 0;
}

static psa_status_t z_pm_op_switch_active_mode(const psa_msg_t *msg)
{
	struct z_pm_switch_in in;
	struct z_pm_switch_out out = {.status = 0};

	if (msg->in_size[0] < sizeof(in)) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	if (msg->out_size[0] < sizeof(out)) {
		return PSA_ERROR_BUFFER_TOO_SMALL;
	}
	if (psa_read(msg->handle, 0, &in, sizeof(in)) != sizeof(in)) {
		return PSA_ERROR_COMMUNICATION_FAILURE;
	}

	if (in.source > Z_PM_MODE_HP || in.target > Z_PM_MODE_HP) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	if (in.source == in.target) {
		out.status = 0;
		psa_write(msg->handle, 0, &out, sizeof(out));
		return PSA_SUCCESS;
	}

	if (in.target < in.source) {
		out.status = trans_down(in.source, in.target);
	} else {
		out.status = trans_up(in.source, in.target);
	}

	psa_write(msg->handle, 0, &out, sizeof(out));
	return PSA_SUCCESS;
}

/* -------------------------------------------------------------------
 * Op: CLOCK_PROBE (id 5). Measure DPLL_LP0 / CLK_HF0 / CLK_HF10 using
 * the SoC's clock-measurement counters (24-bit counter clocked by the
 * measured clock, gated by a fixed number of IHO reference cycles).
 * Print alongside PDL-computed values so a stuck counter or driver-
 * cache mismatch is immediately visible.
 * ------------------------------------------------------------------- */
#define PM_PROBE_REF_COUNT 50000u /* 1 ms wall time at IHO 50 MHz */
#define PM_MEAS_SAFETY_ITERS 1000000u

static uint32_t measure_hz(cy_en_meas_clks_t measured)
{
	cy_en_sysclk_status_t st;
	uint32_t safety;

	st = Cy_SysClk_StartClkMeasurementCounters(
	    CY_SYSCLK_MEAS_CLK_IHO, PM_PROBE_REF_COUNT, measured);
	if (st != CY_SYSCLK_SUCCESS) {
		return 0u;
	}
	safety = PM_MEAS_SAFETY_ITERS;
	while (!Cy_SysClk_ClkMeasurementCountersDone() && (--safety != 0u)) {
		/* spin */
	}
	if (safety == 0u) {
		return 0u;
	}
	return Cy_SysClk_ClkMeasurementCountersGetFreq(true,
						       CY_SYSCLK_IHO_FREQ);
}

static psa_status_t z_pm_op_clock_probe(const psa_msg_t *msg)
{
	struct z_pm_clock_probe_out out;

	if (msg->out_size[0] < sizeof(out)) {
		return PSA_ERROR_BUFFER_TOO_SMALL;
	}

	out.meas_path0 = measure_hz(CY_SYSCLK_MEAS_CLK_PATH0);
	out.meas_hf0 = measure_hz(CY_SYSCLK_MEAS_CLK_CLKHF0);
	out.meas_hf10 = measure_hz(CY_SYSCLK_MEAS_CLK_CLKHF10);
	out.comp_path0 = Cy_SysClk_ClkPathGetFrequency(0u);
	out.comp_hf0 = Cy_SysClk_ClkHfGetFrequency(0u);
	out.comp_hf10 = Cy_SysClk_ClkHfGetFrequency(10u);

	psa_write(msg->handle, 0, &out, sizeof(out));
	return PSA_SUCCESS;
}

/* -------------------------------------------------------------------
 * Op: DEEP_SLEEP_BIAS (id 6). Boot-time programming of the sticky
 * SRSS/PMU/buck/clock knobs that lower the sleep floor of every
 * subsequent Cy_SysPm_CpuEnterDeepSleep. Called ONCE from main().
 *
 * Body mirrors project 06's cmd_deep_sleep.c pm_deep_sleep_init
 * PRE_KERNEL_2 SYS_INIT, which itself is lifted from
 * tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c ifx_pm_init.
 * ------------------------------------------------------------------- */
static psa_status_t z_pm_op_deep_sleep_bias(const psa_msg_t *msg)
{
	(void)msg;

	Cy_SysPm_Init();

	/* BGREF into low-power mode -- lower bandgap-reference current
	 * during Deep Sleep. */
	SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;

	/* Core buck DS override: 0.70 V LP topology, auto-switched when
	 * SLEEPDEEP is asserted. */
	Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
	Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
	Cy_SysPm_CoreBuckDpslpEnableOverride(true);

	/* Stop IHO + IMO in Deep Sleep (they have no consumer while HF
	 * clocks are gated; saves their bias current). PILO is
	 * deliberately left running: it clocks MCWDT0 which is the
	 * Zephyr kernel tick AND the only wake source for DS. */
	Cy_SysClk_IhoDeepsleepDisable();
	SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;

	/* Point CLK_BAK at PILO so backup-domain peripherals (RTC,
	 * BREGs) keep ticking through Deep Sleep. */
	Cy_SysClk_ClkBakSetSource(CY_SYSCLK_BAK_IN_PILO);

	/* Vote plain DEEPSLEEP as the SoC-global deep-sleep mode. Once
	 * both CPUs have voted (CM55 does the same from its own boot),
	 * the SRSS state machine collapses to the AN237976 Table-2
	 * DEEPSLEEP row -- Sys-domain PPUs (MAIN, SRAM0/1, SYSCPU) go
	 * to Full Retention. App-domain + SOCMEM PPUs are programmed
	 * from CM55's side. */
	(void)Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

	return PSA_SUCCESS;
}

/* -------------------------------------------------------------------
 * Op: BOOT_CLOCK_RETUNE (id 7). One-shot boot-time DPLL_LP0 retune to
 * 200 MHz + CLK_HF0 divider /1, matching project 06's baseline clock
 * tree. Called once from NS main() BEFORE the shell starts polling
 * the UART.
 *
 * On this SoC the board-default DPLL_LP0 is 400 MHz and CLK_HF0 comes
 * up at /2 -> 200 MHz for CM33 (so the CM33 core clock is already at
 * the AN237976 HP spec at boot). Retuning DPLL to 200 MHz + /1 keeps
 * CLK_HF0 at 200 MHz (no CM33 spec change) and pulls DPLL down so
 * that the divider-strategy targets (LP = DPLL/3 = 66 MHz, ULP =
 * DPLL/4 = 50 MHz) land at the intended frequencies.
 *
 * CLK_HF10 (SCB2 peripheral clock) is derived off DPLL_LP0 via its
 * own divider (board default). If the retune changes the effective
 * HF10 rate, the SCB baud divider becomes stale and the console
 * garbles. If that happens on the bench, either invoke this op
 * BEFORE the UART is configured (would need a PRE_KERNEL_1 hook)
 * or reconfigure the SCB baud from NS afterwards -- Phase-E work if
 * needed.
 * ------------------------------------------------------------------- */
static psa_status_t z_pm_op_boot_clock_retune(const psa_msg_t *msg)
{
	(void)msg;
	cy_stc_pll_config_t cfg = {
	    .inputFreq = 50000000u, /* IHO */
	    .outputFreq = 200000000u,
	    .lfMode = false,
	    .outputMode = CY_SYSCLK_FLLPLL_OUTPUT_OUTPUT,
	};

	(void)Cy_SysClk_PllDisable(1u); /* DPLL_LP0 */
	if (Cy_SysClk_PllConfigure(1u, &cfg) != CY_SYSCLK_SUCCESS) {
		return PSA_ERROR_GENERIC_ERROR;
	}
	if (Cy_SysClk_PllEnable(1u, 10000u) != CY_SYSCLK_SUCCESS) {
		return PSA_ERROR_GENERIC_ERROR;
	}
	(void)Cy_SysClk_ClkHfSetDivider(0u, CY_SYSCLK_CLKHF_NO_DIVIDE);

	return PSA_SUCCESS;
}

/* -------------------------------------------------------------------
 * Dispatcher.
 * ------------------------------------------------------------------- */
psa_status_t z_pm_service_sfn(const psa_msg_t *msg)
{
	switch (msg->type) {
	case Z_PM_OP_PING:
		return z_pm_op_ping(msg);
	case Z_PM_OP_LAYER_B_INIT:
		return z_pm_op_layer_b_init(msg);
	case Z_PM_OP_SET_DEEP_SLEEP_MODE:
		return z_pm_op_set_deep_sleep_mode(msg);
	case Z_PM_OP_SWITCH_ACTIVE_MODE:
		return z_pm_op_switch_active_mode(msg);
	case Z_PM_OP_CLOCK_PROBE:
		return z_pm_op_clock_probe(msg);
	case Z_PM_OP_DEEP_SLEEP_BIAS:
		return z_pm_op_deep_sleep_bias(msg);
	case Z_PM_OP_BOOT_CLOCK_RETUNE:
		return z_pm_op_boot_clock_retune(msg);
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}
}

psa_status_t z_pm_partition_init(void) { return PSA_SUCCESS; }
