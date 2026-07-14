/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * DS-RAM warm-boot diagnostic token (Phase 8).
 *
 * `Z_PM_OP_ENTER_DS_RAM` on the S side writes @ref WARM_BOOT_TOKEN_DS_RAM
 * into @c RTC->BREG_SET1[@ref WARM_BOOT_BREG_INDEX] immediately before
 * the caller's WFI. After the next reset (whether DS-RAM wake, XRES, or
 * watchdog) the NS `main()` banner reads (and clears) the token to
 * confirm that the previous boot actually reached WFI and that the chip
 * came back through the DS-RAM path rather than dying mid-entry.
 *
 * The token is purely diagnostic and does NOT participate in any
 * warm-boot-entry-pointer mechanism — PSE84's L1-boot does not use one;
 * cold and warm boot follow the same path. Use `Cy_SysPm_GetBootMode()`
 * for the actual boot-cause check.
 *
 * RTC BREGs are battery-backed and survive every reset short of POR.
 * Slot index 1 was picked to match tmp/17_pse84_ds_ram_exact's choice
 * (slot 0 is the PDL's nominal `CY_SYSPM_BOOTROM_ENTRYPOINT_ADDR`).
 *
 * Keep the constants in this header in lock-step with the copies in
 * tfm_partitions/z_pm/z_pm_partition.c :: Z_PM_OP_ENTER_DS_RAM.
 */

#ifndef APP_WARM_BOOT_H_
#define APP_WARM_BOOT_H_

#define WARM_BOOT_BREG_INDEX 1U
#define WARM_BOOT_TOKEN_DS_RAM 0x16D5DA01U /* "16-DS-DA01" tag */

#endif /* APP_WARM_BOOT_H_ */
