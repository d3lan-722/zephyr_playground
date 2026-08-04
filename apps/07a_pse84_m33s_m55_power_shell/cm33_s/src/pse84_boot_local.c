/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Local variant of upstream:
 *   zephyr/soc/infineon/edge/pse84/security_config/pse84_boot.c
 *
 * Differences vs. upstream ifx_pse84_cm55_startup():
 *
 *   1. Renamed to app_pse84_cm55_startup() so it never clashes with the
 *      upstream symbol (which is only defined when
 *      CONFIG_SOC_PSE84_M55_ENABLE=y).
 *
 *   2. Does NOT call sys_clock_disable(): CM33 must keep its Zephyr
 *      system timer running after CM55 is released.
 *
 *   3. Does NOT trap the caller in for(;;): returns so main() can
 *      continue executing.
 *
 *   4. Does NOT call SysCtrlBlk_Setup() / NVIC_NS_Setup() / initFPU()
 *      / __enable_irq() from partition_ARMCM33.h. Those helpers are
 *      only correct when the CM33 is about to jump to a NON-SECURE
 *      image (TF-M NS or bare CM33-NS Zephyr): they set BFHFNMINS=1
 *      (routes BusFault / HardFault / NMI to the NS vector table),
 *      mark every NVIC line NS via NVIC->ITNS[], and enable the NS
 *      FPU. In this project CM33 stays SECURE and Zephyr owns the
 *      vector table + NVIC, so those writes turn the first Zephyr
 *      IRQ (LPTIMER tick, UART RX) into a "Bus fault on vector table
 *      read" because VTOR_NS is never programmed.
 *
 *      01a survives calling them only because that CM33-S image has
 *      no interrupts firing (pure k_msleep + polled GPIO). The fault
 *      is latent -- it manifests the moment any IRQ enters.
 *
 * The steps that actually release CM55 and configure the fabric
 * (PD1 enable, CM55-TCM + SMIF peri-group init, MPC config, PDCM
 * dependency clears, Cy_SysEnableCM55, deep-sleep mode config,
 * PPC0/PPC1 init) are kept verbatim so CM55 comes up in the same
 * environment as a stock sysbuild "enable_cm55" companion.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <cy_pdl.h>
#include <system_edge.h>

#include "pse84_s_mpc.h"
#include "pse84_s_protection.h"
#include "pse84_s_system.h"

#include "pse84_boot_local.h"

#define CM55_BOOT_WAIT_TIME_USEC (10U)

/* Print a step marker and give Zephyr's shell/UART TX ring long enough
 * to drain (~5 ms real time at 115200 8N1 for 40 chars). */
#define STEP(msg)                                                              \
	do {                                                                   \
		printk("[cm55-boot] " msg "\n");                               \
		k_msleep(10);                                                  \
	} while (0)

void app_pse84_cm55_startup(void)
{
	STEP("00 enter");

	Cy_System_EnablePD1();
	STEP("01 PD1 enabled");

	Cy_SysClk_PeriGroupSlaveInit(
	    CY_MMIO_CM55_TCM_512K_PERI_NR, CY_MMIO_CM55_TCM_512K_GROUP_NR,
	    CY_MMIO_CM55_TCM_512K_SLAVE_NR, CY_MMIO_CM55_TCM_512K_CLK_HF_NR);
	STEP("02 CM55 TCM peri-group init");

	Cy_SysClk_PeriGroupSlaveInit(
	    CY_MMIO_SMIF0_PERI_NR, CY_MMIO_SMIF0_GROUP_NR,
	    CY_MMIO_SMIF0_SLAVE_NR, CY_MMIO_SMIF0_CLK_HF_NR);
	STEP("03 SMIF0 peri-group init");

	cy_mpc_init();
	STEP("04 MPC init");

	cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPUSS, CY_PD_PDCM_SYSCPU);
	STEP("05 PDCM APPCPUSS<-SYSCPU cleared");

	cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPU, CY_PD_PDCM_SYSCPU);
	STEP("06 PDCM APPCPU<-SYSCPU cleared");

	uint32_t cm55_start_address = DT_REG_ADDR(DT_NODELABEL(m55_xip));
	printk("[cm55-boot] 07 releasing CM55 at 0x%08x\n", cm55_start_address);
	k_msleep(10);

	Cy_SysEnableCM55(MXCM55, cm55_start_address, CM55_BOOT_WAIT_TIME_USEC);
	STEP("08 CM55 released");

	Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
	STEP("09 SetDeepSleepMode DEEPSLEEP");

	Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
	STEP("10 SetSOCMEMDeepSleepMode DEEPSLEEP");

	/* PPC init sets the default response to CY_PPC_BUS_ERR and then
	 * rewrites every PERI0/1 region attribute in a loop. A Zephyr
	 * IRQ (LPTIMER, shell UART RX) that fires mid-loop can hit the
	 * transient BUS_ERR window and the fault handler's own UART
	 * write re-faults, giving a silent lockup. Hold PRIMASK across
	 * the two calls -- upstream ifx_pse84_cm55_startup() masks this
	 * by trapping CM33 in for(;;) immediately after.
	 *
	 * No STEP marker inside the lock: k_msleep needs LPTIMER IRQs,
	 * which are masked here. Prints resume after irq_unlock. */
	unsigned int key = irq_lock();
	cy_ppc0_init();
	cy_ppc1_init();
	irq_unlock(key);
	STEP("11 PPC0+PPC1 init (under irq_lock)");

	STEP("99 exit");
}
