/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief One-shot boot-time trimming of unused SoC subsystems.
 *
 * PSE84 RRAMboot leaves the whole HFCLK tree enabled (HF0..HF13
 * confirmed via util/dump_hfclk.sh), both SMIF controllers active,
 * the SOCMEM controller powered, and every APP-domain PPU in the
 * ON state -- even though this Secure-only demo uses only the CM33
 * core, DPLL_LP0 (feeding CLK_HF0 for the core and CLK_HF10 for
 * SCB2), a handful of GPIO pins, and the RRAM controller.
 *
 * All that extra silicon appears as a constant offset in every
 * PPK2 measurement. This file runs once at APPLICATION init
 * (@c SYS_INIT priority 0), before @c main(), and gates every
 * subsystem the demo does not touch:
 *
 *   1. SOCMEM controller off       (Cy_SysEnableSOCMEM(false))
 *   2. Both SMIF cores off         (SMIF_CORE_CTL.ENABLED cleared)
 *   3. HF3..HF9 + HF11..HF13 gated (Cy_SysClk_ClkHfDisable)
 *   4. GPIO ports we don't drive   (Cy_GPIO_Port_Deinit)
 *
 * Everything the demo NEEDS is deliberately left alone:
 *   - HF0  -- CM33 core clock
 *   - HF10 -- SCB2 UART peripheral clock
 *   - HF1  -- APPCPUSS. Kept because gating it before the APP
 *             domain PPU handshake completes is known to hang
 *             deep-sleep (comment in tmp/app_pm_boot.c). We are
 *             not doing deep sleep so it just sits idle.
 *   - HF2  -- SOCMEM PPU / DPLL_HP. Kept for the same reason.
 *   - DPLL_LP0  -- HF0/HF10 source. Retuned by PLL_RETUNE.
 *   - DPLL_LP1, DPLL_HP -- currently kept enabled (feeding HF7
 *             / HF2 respectively). Could be disabled once we
 *             confirm no downstream depends on them, deferred.
 *   - GPIO port 3  -- P3.1 pm_busy scope trigger.
 *   - GPIO port 16 -- RGB LEDs.
 *
 * What we deliberately do NOT do (would break the demo):
 *   - Disable DPLL_LP0                (needed by PLL_RETUNE)
 *   - Enter SystemUlp profile at boot (breaks HP/LP/ULP switching)
 *   - Disable the debug session       (blocks re-flash without XRES)
 *   - Force PD1 domain OFF via PPUs   (deferred -- requires
 *                                      Cy_SysCM55SetDbgPort +
 *                                      cy_pd_pdcm_clear_dependency,
 *                                      more state to reason about;
 *                                      see OPEN_pd1_collapse.md
 *                                      when we get to it)
 *
 * Inspired by tmp/app_pm_boot.c from the deep-sleep reference
 * project. Kept intentionally SMALLER than that reference to keep
 * the DVFS demo functional -- see the "not done" list above.
 */

#include <zephyr/init.h>

#include <soc.h>

#include "cy_gpio.h"
#include "cy_pdl.h"
#include "cy_sysclk.h"
#include "cy_syspm.h"

#include <zephyr/sys/printk.h>

/** Number of GPIO port peripherals on PSE84 (P0..P21). */
#define PSE84_GPIO_PORT_COUNT 22u

/** GPIO ports the demo keeps live -- everything else gets
 *  Cy_GPIO_Port_Deinit. */
#define KEEP_PORT_PM_BUSY 3u  /* P3.1 -- pm-busy scope trigger. */
#define KEEP_PORT_LEDS    16u /* P16.5/6/7 -- RGB LEDs. */

/** HFCLK indices this build never uses. HF0, HF10, HF1, HF2 are
 *  deliberately absent: HF0 is the CM33 core clock, HF10 is the
 *  SCB2 UART clock, HF1/HF2 gate the APPCPUSS/SOCMEM PPU chain
 *  and disabling them before a PPU collapse would hang the
 *  Q-channel handshake (see tmp/app_pm_boot.c note). */
static const uint8_t s_unused_hf_clocks[] = {
	3,	/* HF3  -- SMIF0                                  */
	4,	/* HF4  -- SMIF1                                  */
	5,	/* HF5  -- SDHC0, PERI_0 group 3, PERI_1 group 2  */
	6,	/* HF6  -- SDHC1, PERI_0 group 4, PERI_1 group 3  */
	7,	/* HF7  -- PDM, PERI_1 group 1 (fed by DPLL_LP1)  */
	8,	/* HF8  -- USB                                    */
	9,	/* HF9  -- SYS_MMIO[5], Autonomous Analog         */
	11,	/* HF11 -- SYS_MMIO[3], SCB[1], PERI_0 group 8    */
	12,	/* HF12 -- MIPI-DSI D-PHY PLL reference           */
	13,	/* HF13 -- I3C, PERI_0 group 6/9                  */
};

/**
 * @brief Disable the on-chip SOCMEM controller.
 *
 * SOCMEM is a 2 MB shared memory that only CM55 uses; this
 * Secure-only CM33 build never touches it. Boot ROM leaves the
 * block powered so the CM55 can boot into it; we don't need
 * that.
 */
static void trim_socmem(void)
{
	Cy_SysEnableSOCMEM(false);
}

/**
 * @brief Disable both Serial Memory Interface controllers.
 *
 * Boot ROM configures SMIF0 for QSPI XIP; this build runs from
 * internal RRAM instead (see snippets/rram/) and never issues an
 * SMIF transaction. Clearing @c CTL.ENABLED stops the block
 * clocks and puts its output pads high-Z.
 *
 * Direct register write to avoid pulling @c cy_smif.h (and its
 * transitive dependencies) into the build.
 */
static void trim_smif(void)
{
	SMIF0_CORE->CTL &= ~SMIF_CORE_CTL_ENABLED_Msk;
	SMIF1_CORE->CTL &= ~SMIF_CORE_CTL_ENABLED_Msk;
}

/**
 * @brief Gate every HFCLK root the demo does not use.
 *
 * Uses @c Cy_SysClk_ClkHfDisable which stops the root's output
 * clock. Downstream peripherals of a gated HF root cannot run,
 * but every downstream peripheral here is one we've already
 * confirmed unused (SMIF0/1, SDHC0/1, PDM, USB, MIPI-DSI, I3C,
 * SCB[1], plus the SYS_MMIO[3..5] bridges).
 */
static void trim_unused_hf_clocks(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(s_unused_hf_clocks); i++) {
		(void)Cy_SysClk_ClkHfDisable(s_unused_hf_clocks[i]);
	}
}

/**
 * @brief Deinit every GPIO port except the two we drive.
 *
 * A GPIO port left at its RRAMboot default may have input
 * buffers active on floating pins, drawing tens of uA per pin.
 * @c Cy_GPIO_Port_Deinit resets the whole port to high-Z analog:
 * HSIOM disconnected, output-drive mode disabled, input buffer
 * off.
 *
 * We keep P3 (P3.1 = pm_busy scope trigger) and P16 (P16.5/6/7 =
 * RGB LEDs) untouched.
 */
static void trim_unused_gpio_ports(void)
{
	for (uint32_t port = 0u; port < PSE84_GPIO_PORT_COUNT; port++) {
		if (port == KEEP_PORT_PM_BUSY || port == KEEP_PORT_LEDS) {
			continue;
		}
		GPIO_PRT_Type *prt =
			(GPIO_PRT_Type *)(CY_GPIO_BASE +
					  (port * GPIO_PRT_SECTION_SIZE));
		Cy_GPIO_Port_Deinit(prt);
	}
}

/**
 * @brief Boot hook -- runs once at APPLICATION init, before main().
 *
 * Ordering doesn't strictly matter for the four helpers here (none
 * depends on another's side effect), but we go in a top-down
 * "coarse to fine" order for readability.
 */
static int pm_boot_optimize(void)
{
	trim_socmem();
	trim_smif();
	trim_unused_hf_clocks();
	trim_unused_gpio_ports();
	printk("[pm-boot] trimmed SOCMEM, SMIF0/1, HF{3..9,11..13}, "
	       "GPIO ports (kept 3, 16)\n");
	return 0;
}

SYS_INIT(pm_boot_optimize, APPLICATION, 0);
