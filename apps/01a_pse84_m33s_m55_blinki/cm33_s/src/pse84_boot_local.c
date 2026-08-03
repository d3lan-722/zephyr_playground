/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Local variant of upstream:
 *   zephyr/soc/infineon/edge/pse84/security_config/pse84_boot.c
 *
 * Differences vs. upstream ifx_pse84_cm55_startup():
 *   - Renamed to app_pse84_cm55_startup() so it never clashes with the
 *     upstream symbol (which is only defined when
 *     CONFIG_SOC_PSE84_M55_ENABLE=y).
 *   - Does NOT call sys_clock_disable(): the CM33-Secure must keep
 *     SysTick running so the Zephyr scheduler can drive its own threads
 *     (e.g. the red-LED blink loop) after CM55 has been released.
 *   - Does NOT trap the caller in for(;;): returns so main() can
 *     continue executing.
 *
 * All other steps (SysCtrlBlk / NVIC-NS / FPU-NS setup, PD1 enable,
 * TCM + SMIF peri-group init, MPC config, PDCM dependency clears,
 * Cy_SysEnableCM55, deep-sleep mode config, PPC0/PPC1 init) match
 * upstream verbatim so that CM55 comes up in the same environment as
 * a stock sysbuild "enable_cm55" companion.
 */

#include <zephyr/kernel.h>

#include <cy_pdl.h>
#include <system_edge.h>
#include <partition_ARMCM33.h>

#include "pse84_s_mpc.h"
#include "pse84_s_protection.h"
#include "pse84_s_system.h"

#include "pse84_boot_local.h"

#define CM55_BOOT_WAIT_TIME_USEC (10U)

void app_pse84_cm55_startup(void)
{
	SysCtrlBlk_Setup();
	NVIC_NS_Setup();

#if defined(__FPU_USED) && (__FPU_USED == 1U) && defined(TZ_FPU_NS_USAGE) &&   \
    (TZ_FPU_NS_USAGE == 1U)
	initFPU();
#endif

	__enable_irq();

	Cy_System_EnablePD1();

	Cy_SysClk_PeriGroupSlaveInit(
	    CY_MMIO_CM55_TCM_512K_PERI_NR, CY_MMIO_CM55_TCM_512K_GROUP_NR,
	    CY_MMIO_CM55_TCM_512K_SLAVE_NR, CY_MMIO_CM55_TCM_512K_CLK_HF_NR);

	Cy_SysClk_PeriGroupSlaveInit(
	    CY_MMIO_SMIF0_PERI_NR, CY_MMIO_SMIF0_GROUP_NR,
	    CY_MMIO_SMIF0_SLAVE_NR, CY_MMIO_SMIF0_CLK_HF_NR);

	cy_mpc_init();

	cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPUSS, CY_PD_PDCM_SYSCPU);
	cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPU, CY_PD_PDCM_SYSCPU);

	uint32_t cm55_start_address = DT_REG_ADDR(DT_NODELABEL(m55_xip));

	Cy_SysEnableCM55(MXCM55, cm55_start_address, CM55_BOOT_WAIT_TIME_USEC);

	Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
	Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

	cy_ppc0_init();
	cy_ppc1_init();
}
