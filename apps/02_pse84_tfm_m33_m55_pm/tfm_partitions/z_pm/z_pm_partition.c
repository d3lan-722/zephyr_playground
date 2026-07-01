/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * z_pm secure partition - S-side implementation.
 *
 * SCOPE: this partition intentionally exposes ONLY the operations
 * that cannot be reached from CM33-NS through the PDL's own SRF
 * integration (see cy_syspm_v4.c #ifdef CY_PDL_SYSPM_ENABLE_SRF_INTEG
 * branches). Today that leaves just:
 *
 *   Z_PM_OP_PING  -- proof-of-life for the out-of-tree partition
 *                    machinery (validates manifest, SID mapping,
 *                    NS interface tree, psa_call round-trip).
 *
 * The three sleep entry points that used to live here
 * (Z_PM_OP_CPU_SLEEP, CPU_DEEP_SLEEP, SYSTEM_DEEP_SLEEP) were
 * removed after round-7 empirical verification: Cy_SysPm_CpuEnterSleep,
 * Cy_SysPm_CpuEnterDeepSleep, and Cy_SysPm_SystemEnterHibernate
 * are all SRF-wrapped in the NS-side PDL and reach the same S-side
 * execution path as a partition wrapper would. NS can call them
 * directly (see cm33_ns/src/power.c); z_pm added no function beyond
 * the wrapper itself.
 *
 * Future non-SRF-wrapped ops that WILL be added here (phase 7+):
 *   Z_PM_OP_SET_SYS_DEEP_SLEEP_MODE  -- Cy_SysPm_SetSysDeepSleepMode
 *                                       (PWRMODE_PPU_MAIN + RAMC0/1_PPU +
 *                                        CPUSS_PPU are all PPC-secured)
 *   Z_PM_OP_SET_SOCMEM_DEEP_SLEEP_MODE + PD1-up gating
 *   Z_PM_OP_CM55_HIBERNATE_RELAY     -- SRSS_PWR_HIBERNATE from CM55
 *   Layer-B bias / retention pattern setup
 * When adding those, re-add the ifx_pdl_inc_s dep in CMakeLists and
 * bring #include "cy_syspm.h" back.
 */

#include <stdint.h>

#include "psa/error.h"
#include "psa/service.h"
#include "psa_manifest/z_pm_partition.h"

/* Op IDs - keep in sync with cm33_ns/src/z_pm_client.h */
#define Z_PM_OP_PING 1

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

psa_status_t z_pm_service_sfn(const psa_msg_t *msg)
{
	switch (msg->type) {
	case Z_PM_OP_PING:
		return z_pm_op_ping(msg);
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}
}

psa_status_t z_pm_partition_init(void) { return PSA_SUCCESS; }
