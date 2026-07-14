/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/cache.h>

#include "cy_device.h"
#include "cy_syspm.h"

#include <cmsis_core.h>

/**
 * @brief Arm CM55 as a DS-RAM requestor.
 *
 * On CM55, @c Cy_SysPm_SetDeepSleepMode routes to
 * @c Cy_SysPm_SetAppDeepSleepMode which programs the App-domain PPUs
 * (PD1, APPCPUSS, APPCPU) to their AN237976 Table-2 row for the
 * requested mode.
 *
 * We call it via the PDL wrapper — NOT via direct PWPR writes — even
 * though tmp/17_pse84_ds_ram_exact does direct writes there:
 *
 *   * The PWRMODE PPU register file is in a PC=2-only PPC region.
 *     CM55 runs in NS (SAU "all NS" pattern, PC=6) and BUS-faults
 *     on any direct PWPR access. Empirically confirmed: BFAR reads
 *     0x42413000 (CY_PPU_PD1_BASE) when a direct write is attempted.
 *
 *   * The PDL wrapper `Cy_SysPm_SetAppDeepSleepMode` uses SRF
 *     integration when `CY_PDL_SYSPM_ENABLE_SRF_INTEG` is defined
 *     (which it is on this build — see cy_syspm_srf.h). It packs a
 *     request and IPC-sends it to CM33-S; CM33-S runs the write at
 *     PC=2 and returns. This is the ONLY working PPU-programming
 *     path from CM55 under TF-M on PSE84.
 *
 * Known limitation of this approach — Phase-8 empirical:
 *   On DS-RAM WARM boot, the App PPUs retain their DS-RAM values.
 *   The S-side SRF handler still calls `cy_pd_ppu_set_power_mode ->
 *   ppu_v1_dynamic_enable` which spins on `PWSR.PWR_DYN_STATUS`
 *   after a same-value write. Whether the spin exits depends on
 *   whether dynamic-enable state was retained. In practice the
 *   first DS-RAM cycle after POR commits and warm-resets; subsequent
 *   cycles show "pm: DS-RAM refused (WFI returned)" — the SRSS
 *   PWRMODE state machine cannot fold because CM55 has not
 *   re-voted DS via SetAppDeepSleepMode yet (or the vote is
 *   effectively invalidated on warm reset). A proper fix requires
 *   a CM33-NS <-> CM55 rendezvous protocol so CM55 skips the
 *   SetDeepSleepMode call on warm boot (see
 *   tmp/17_pse84_ds_ram_exact rendezvous / boot-mode detection).
 *   Phase 8 (Option-2 scoped) stops at one-shot DS-RAM proving.
 */
static void cm55_arm_deepsleep_mode(void)
{
	Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP_RAM);
}

/**
 * @brief Stop SysTick + mask IRQs so nothing wakes WFI.
 */
static void zephyr_quiesce_for_deepsleep(void)
{
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	__disable_irq();
}

/**
 * @brief CM55 WFI parking-lot entry point.
 * @return Never returns.
 */
int main(void)
{

	cm55_arm_deepsleep_mode();

	zephyr_quiesce_for_deepsleep();

	for (;;) {
		Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
	}

	return 0;
}
