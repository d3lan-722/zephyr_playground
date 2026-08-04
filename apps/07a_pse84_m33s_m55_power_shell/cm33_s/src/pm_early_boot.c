/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Early boot hook -- disable MCWDT0 counters so the Zephyr
 *        LPTIMER driver's init succeeds.
 *
 * Corresponds to pm_early_mcwdt_reset() in project 06's
 * pm_boot_optimize.c. The other 06 boot-time trims (SOCMEM off,
 * SMIF0/1 off, PD1 / APPCPU / APPCPUSS / SOCMEM / U55 PPUs off,
 * HF1..HF9/11..13 gated, DPLLs disabled) are deliberately NOT
 * ported to this project: they would kill CM55 (which lives in PD1,
 * fetches its XIP from SMIF0, and needs APPCPUSS/APPCPU alive).
 *
 * Only the MCWDT reset is kept because it is orthogonal to CM55 and
 * is required for the LPTIMER PDL driver -- without it the driver's
 * Cy_MCWDT_Init returns BAD_PARAM (counters already enabled by SE-ROM),
 * lptimer_init returns -EINVAL, sys_clock never starts, and every
 * subsequent k_msleep/k_busy_wait hangs forever.
 */

#include <zephyr/init.h>

/* soc.h pulls in infineon_kconfig.h which defines COMPONENT_SECURE_DEVICE.
 * Must come before <cy_pdl.h> so cy_device_headers.h selects the secure
 * device variant (pse846gps2dbzc4a_s.h) and cy_pdl_srf.h's unconditional
 * #include "mtb_srf_pool.h" is guarded out for this secure-only build. */
#include <soc.h>
#include <cy_pdl.h>

static int pm_early_mcwdt_reset(void)
{
	Cy_MCWDT_Unlock(MCWDT_STRUCT0);
	Cy_MCWDT_Disable(MCWDT_STRUCT0,
			 CY_MCWDT_CTR0 | CY_MCWDT_CTR1 | CY_MCWDT_CTR2, 100U);
	Cy_MCWDT_ClearInterrupt(MCWDT_STRUCT0,
				CY_MCWDT_CTR0 | CY_MCWDT_CTR1 | CY_MCWDT_CTR2);
	Cy_MCWDT_SetInterruptMask(MCWDT_STRUCT0, 0U);
	return 0;
}

SYS_INIT(pm_early_mcwdt_reset, PRE_KERNEL_1, 0);
