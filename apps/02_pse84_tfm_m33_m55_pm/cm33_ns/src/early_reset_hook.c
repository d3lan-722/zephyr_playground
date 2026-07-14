/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 8 (scoped DS-RAM): early reset hook that runs BEFORE C runtime
 * initialisation (in reset.S context, before z_prep_c). Its only job is
 * to undo the FPU/MVE power-gate that @c enter_ds_ram in power.c applied
 * just before WFI on the previous cycle.
 *
 * enter_ds_ram() power-gates CP10/CP11 via:
 *   SCS_CPPWR   |=  SCS_ENABLE_CPPWR_SU10_SU11;
 *   SCB->CPACR  &= ~SCB_ENABLE_CPACR_CP10_CP11;
 * because silicon requires it for the chip to fold to DEEPSLEEP_RAM
 * instead of plain DEEPSLEEP. On the DS-RAM wake reset, the NS view of
 * both registers is preserved (they are not part of the reset scope),
 * so the very first FPU / MVE instruction executed after reset — which
 * is picolibc's memset/memcpy over .bss/.data during C runtime init —
 * traps NOCP and escalates to HardFault before @c main() ever runs.
 *
 * Re-enabling CP10/CP11 here in the reset.S window (before z_prep_c and
 * before any .bss touch) fixes that. Runs on POR too — a no-op in that
 * case because CPACR/CPPWR come up in their enabled state at cold boot.
 *
 * Requires @c CONFIG_SOC_EARLY_RESET_HOOK=y. See
 * ~/zephyrproject/zephyr/kernel/Kconfig.init and
 * ~/zephyrproject/zephyr/arch/arm/core/cortex_m/reset.S for the wiring.
 *
 * Reference: tmp/17_pse84_ds_ram_exact/m33_ns/src/early_reset_hook.c.
 */

#include <cmsis_core.h>

#include "cy_device.h"

void soc_early_reset_hook(void)
{
	SCS_CPPWR &= ~SCS_ENABLE_CPPWR_SU10_SU11;
	SCB->CPACR |= SCB_ENABLE_CPACR_CP10_CP11;
	__DSB();
	__ISB();
}
