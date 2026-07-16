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
 *   1. SOCMEM controller off             (Cy_SysEnableSOCMEM)
 *   2. Both SMIF cores off               (CTL.ENABLED cleared)
 *   3. APP power domain (PD1) collapsed  (PPUs -> OFF)
 *   4. HF1..HF9 + HF11..HF13 gated       (Cy_SysClk_ClkHfDisable)
 *   5. DPLL_LP1 + DPLL_HP disabled       (Cy_SysClk_Dpll*Disable)
 *   6. GPIO ports we don't drive         (Cy_GPIO_Port_Deinit)
 *
 * Everything the demo NEEDS is deliberately left alone:
 *   - HF0        -- CM33 core clock
 *   - HF10       -- SCB2 UART peripheral clock
 *   - DPLL_LP0   -- HF0/HF10 source (retuned by PLL_RETUNE)
 *   - GPIO P3    -- pm_busy scope trigger
 *   - GPIO P16   -- RGB LEDs
 *
 * What we deliberately do NOT do (would break the demo):
 *   - Disable DPLL_LP0                (needed by PLL_RETUNE)
 *   - Enter SystemUlp profile at boot (caps HF0 <= 50 MHz, breaks
 *                                      HP mode)
 *   - Disable the debug session       (blocks re-flash without XRES,
 *                                      kills the KitProg CDC UART)
 *   - Gate peri-group slave clocks    (would need per-group audit of
 *                                      which SCB / GPIO / HSIOM slice
 *                                      we still depend on -- HF1..HF9
 *                                      gating already kills their
 *                                      clock source anyway)
 *
 * Inspired by tmp/app_pm_boot.c from the deep-sleep reference
 * project. This is the runtime-active analogue: same set of
 * subsystems shut down, but WITHOUT the ULP-profile / debug
 * disable / DS-RAM PPU targets that would make the demo unusable.
 */

#include <zephyr/init.h>

#include <soc.h>

#include "cy_gpio.h"
#include "cy_pdl.h"
#include "cy_sysclk.h"
#include "cy_syspm.h"
#include "cy_syspm_pdcm.h"
#include "cy_syspm_ppu.h"
#include "system_edge.h"

#include <zephyr/sys/printk.h>

/** Number of GPIO port peripherals on PSE84 (P0..P21). */
#define PSE84_GPIO_PORT_COUNT 22u

/** GPIO ports the demo keeps live -- everything else gets
 *  Cy_GPIO_Port_Deinit. */
#define KEEP_PORT_PM_BUSY 3u /* P3.1 -- pm-busy scope trigger. */
#define KEEP_PORT_LEDS 16u   /* P16.5/6/7 -- RGB LEDs. */

/** HFCLK indices this build never uses. HF0 and HF10 are
 *  deliberately absent: HF0 is the CM33 core clock, HF10 is the
 *  SCB2 UART clock. HF1 (APPCPUSS) and HF2 (SOCMEM PPU / DPLL_HP)
 *  are included -- the "must complete PPU Q-channel handshake
 *  first" caveat from tmp/app_pm_boot.c only matters for deep-sleep
 *  entry; in active runtime, gating them idles the block with no
 *  side-effect because CM55 is never booted and SOCMEM was already
 *  disabled above. */
static const uint8_t s_unused_hf_clocks[] = {
    1,	/* HF1  -- APPCPUSS, APP_MMIO[0..4], PERI_1 group 0  */
    2,	/* HF2  -- SOCMEM, PERI_1 group 5                    */
    3,	/* HF3  -- SMIF0                                     */
    4,	/* HF4  -- SMIF1                                     */
    5,	/* HF5  -- SDHC0, PERI_0 group 3, PERI_1 group 2     */
    6,	/* HF6  -- SDHC1, PERI_0 group 4, PERI_1 group 3  */
    7,	/* HF7  -- PDM, PERI_1 group 1 (fed by DPLL_LP1)  */
    8,	/* HF8  -- USB                                    */
    9,	/* HF9  -- SYS_MMIO[5], Autonomous Analog         */
    11, /* HF11 -- SYS_MMIO[3], SCB[1], PERI_0 group 8    */
    12, /* HF12 -- MIPI-DSI D-PHY PLL reference           */
    13, /* HF13 -- I3C, PERI_0 group 6/9                  */
};

/**
 * @brief Disable the on-chip SOCMEM controller.
 *
 * SOCMEM is a 2 MB shared memory that only CM55 uses; this
 * Secure-only CM33 build never touches it. Boot ROM leaves the
 * block powered so the CM55 can boot into it; we don't need
 * that.
 */
static void trim_socmem(void) { Cy_SysEnableSOCMEM(false); }

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
 * @brief Collapse the APP power domain (PD1) via its PPUs.
 *
 * PSE84 splits the SoC into two hard power domains:
 *   PD0 (SYS): SYSCPU (CM33), SYS_MMIO*, RRAM, SRSS, BACKUP.
 *   PD1 (APP): APPCPU (CM55), APPCPUSS, U55 (NPU), SOCMEM, plus
 *              their MMIO bridges (APP_MMIO0..4).
 *
 * This build uses ONLY the CM33 in PD0. Everything in PD1 is
 * dead code -- CM55 is never booted, U55 is never used, SOCMEM
 * was already gated via @c trim_socmem above. But RRAMboot
 * leaves every PD1 PPU in the ON state (so a debugger could
 * poke around before CM55 boots), which keeps the whole APP
 * rail powered.
 *
 * Requesting @c PPU_V1_MODE_OFF on the four APP PPUs plus PD1
 * itself tells the arm-controller Q-channel to collapse each
 * block as soon as its dependents are quiescent. Two things
 * have to happen first for the collapse to actually complete:
 *
 *   (a) Clear the boot-ROM-installed PDCM edge that lists
 *       APPCPUSS as a hard dependent of SYSCPU. Without this
 *       our CM33 (SYSCPU=ON) forever pins APPCPUSS=ON.
 *
 *   (b) Detach the CM55 debug AP via @c Cy_SysCM55SetDbgPort.
 *       RRAMboot leaves @c APPCPUSS->AP_CTL asserting the CM55
 *       DAP enables; an enabled DAP is a hardware requestor on
 *       APPCPU's domain even without a debugger physically
 *       attached, which pins APPCPU=ON. Clearing AP_CTL is the
 *       moral equivalent of the SDK's DS-RAM path (which
 *       actually boots CM55, tells it to WFI, and lets the
 *       Q-channel see it drop). We take the cheaper route --
 *       just yank the DAP.
 *
 * We do NOT touch the SYSCPU PPU (PD0). CM33 is running out of
 * that domain; putting it OFF would be self-destructive.
 *
 * Must run BEFORE @c trim_unused_hf_clocks. The Q-channel
 * handshake between the PPU and its slave interfaces uses HF1
 * (APPCPUSS clock) and HF2 (SOCMEM PPU clock). If we gate those
 * first, the handshake stalls and the PPU is stuck in a chimera
 * state (PWPR=OFF requested, PWSR still ON). Reference firmware
 * enforces the same ordering.
 */
static void trim_app_domain(void)
{
	struct ppu_v1_reg *ppu_pd1 =
	    (struct ppu_v1_reg *)CY_PPU_PD1_BASE;
	struct ppu_v1_reg *ppu_socmem =
	    (struct ppu_v1_reg *)CY_PPU_SOCMEM_BASE;
	struct ppu_v1_reg *ppu_appcpuss =
	    (struct ppu_v1_reg *)CY_PPU_APPCPUSS_BASE;
	struct ppu_v1_reg *ppu_appcpu =
	    (struct ppu_v1_reg *)CY_PPU_APPCPU_BASE;
	struct ppu_v1_reg *ppu_u55 =
	    (struct ppu_v1_reg *)CY_PPU_U55_BASE;

	/* (a) Break the boot-ROM PDCM edge SYSCPU->APPCPUSS. */
	(void)cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPUSS,
					  CY_PD_PDCM_SYSCPU);

	/* (b) Drop the CM55 debug AP so APPCPU can quiesce. */
	Cy_SysCM55SetDbgPort(APPCPUSS_DBG_DISABLE);

	/* Request OFF on parent first, then children. Actual
	 * sequencing is enforced by the Q-channel handshake, not
	 * by these register writes. */
	(void)cy_pd_ppu_set_power_mode(ppu_pd1,
				       (uint32_t)PPU_V1_MODE_OFF);
	(void)cy_pd_ppu_set_power_mode(ppu_socmem,
				       (uint32_t)PPU_V1_MODE_OFF);
	(void)cy_pd_ppu_set_power_mode(ppu_appcpuss,
				       (uint32_t)PPU_V1_MODE_OFF);
	(void)cy_pd_ppu_set_power_mode(ppu_appcpu,
				       (uint32_t)PPU_V1_MODE_OFF);
	(void)cy_pd_ppu_set_power_mode(ppu_u55,
				       (uint32_t)PPU_V1_MODE_OFF);
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
 * @brief Disable DPLL_LP1 and DPLL_HP.
 *
 * After @c trim_unused_hf_clocks gates HF1..HF9 and HF11..HF13:
 *   - DPLL_LP1 has no downstream (HF7 was its only consumer)
 *   - DPLL_HP  has no downstream (HF2 was its only consumer)
 *
 * A powered-on PLL block that no HFCLK routes through still draws
 * bias current -- disabling the block itself saves that. Must run
 * AFTER @c trim_unused_hf_clocks so the ordering guarantees no
 * downstream HFCLK loses its source mid-use.
 *
 * DPLL_LP0 is deliberately left alone -- HF0 (CM33 core) and HF10
 * (SCB2 UART) both feed from it, and PM_STRATEGY_PLL_RETUNE
 * reprograms it around every DVFS transition.
 */
static void trim_unused_plls(void)
{
	(void)Cy_SysClk_DpllLpDisable(1);
	(void)Cy_SysClk_DpllHpDisable(0);
}

/**
 * @brief Boot hook -- runs once at APPLICATION init, before main().
 *
 * Order matters between three adjacent pairs:
 *   - trim_socmem must precede trim_app_domain (the SOCMEM
 *     controller and its PPU are two independent power controls;
 *     stopping the controller first quiesces its Q-channel so the
 *     PPU collapse doesn't stall)
 *   - trim_app_domain must precede trim_unused_hf_clocks (PPU
 *     Q-channel handshake needs HF1 (APPCPUSS) and HF2 (SOCMEM
 *     PPU) alive; gating them first stalls the collapse)
 *   - trim_unused_hf_clocks must precede trim_unused_plls (PLLs
 *     only safely disable after their last downstream HFCLK is
 *     gated)
 * The remaining helpers are independent -- ordering here just
 * follows "coarse to fine" for readability.
 */
static int pm_boot_optimize(void)
{
	trim_socmem();
	trim_smif();
	trim_app_domain();
	trim_unused_hf_clocks();
	trim_unused_plls();
	trim_unused_gpio_ports();
	printk("[pm-boot] trimmed SOCMEM, SMIF0/1, PD1 (APP domain), "
	       "HF{1..9,11..13}, DPLL_LP1, DPLL_HP, "
	       "GPIO ports (kept 3, 16)\n");
	return 0;
}

SYS_INIT(pm_boot_optimize, APPLICATION, 0);
