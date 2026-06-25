/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * z_pm secure partition - S-side implementation.
 *
 * Routes Zephyr NS PM dispatch through TF-M so SLEEPDEEP/WFI and any
 * future PWRMODE/SRSS programming runs at PC2 (TF-M privilege). With
 * COMPONENT_SECURE_DEVICE defined for SPE code, the PDL syspm calls
 * use the direct register path (not the SRF mailbox).
 */

#include <stdint.h>
#include <string.h>

#include "psa/error.h"
#include "psa/service.h"
#include "psa_manifest/z_pm_partition.h"

#include "cy_syspm.h"

/* Op IDs - keep in sync with cm33_ns/src/z_pm_client.h */
#define Z_PM_OP_PING              1
#define Z_PM_OP_CPU_SLEEP         2
#define Z_PM_OP_CPU_DEEP_SLEEP    3
#define Z_PM_OP_SYSTEM_DEEP_SLEEP 4

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

/* PSA status passthrough: PDL returns CY_SYSPM_SUCCESS (=0) on the
 * happy path, which already maps to PSA_SUCCESS. Anything non-zero
 * we surface as PSA_ERROR_GENERIC_ERROR; the partition never panics
 * on a sleep failure (callbacks may legitimately block sleep).
 */
static inline psa_status_t pdl_to_psa(cy_en_syspm_status_t st)
{
	return (st == CY_SYSPM_SUCCESS) ? PSA_SUCCESS
					: PSA_ERROR_GENERIC_ERROR;
}

static psa_status_t z_pm_op_cpu_sleep(const psa_msg_t *msg)
{
	(void)msg;
	return pdl_to_psa(Cy_SysPm_CpuEnterSleep(CY_SYSPM_WAIT_FOR_INTERRUPT));
}

static psa_status_t z_pm_op_cpu_deep_sleep(const psa_msg_t *msg)
{
	(void)msg;
	return pdl_to_psa(
		Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT));
}

static psa_status_t z_pm_op_system_deep_sleep(const psa_msg_t *msg)
{
	(void)msg;
	/* Same primitive as cpu_deep_sleep for now. SRSS auto-collapses
	 * to system DEEPSLEEP once every CPU has voted. Phase 7+ may
	 * specialise this (DS-OFF token, Layer-B bias).
	 */
	return pdl_to_psa(
		Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT));
}

psa_status_t z_pm_service_sfn(const psa_msg_t *msg)
{
	switch (msg->type) {
	case Z_PM_OP_PING:
		return z_pm_op_ping(msg);
	case Z_PM_OP_CPU_SLEEP:
		return z_pm_op_cpu_sleep(msg);
	case Z_PM_OP_CPU_DEEP_SLEEP:
		return z_pm_op_cpu_deep_sleep(msg);
	case Z_PM_OP_SYSTEM_DEEP_SLEEP:
		return z_pm_op_system_deep_sleep(msg);
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}
}

psa_status_t z_pm_partition_init(void) { return PSA_SUCCESS; }
