/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * z_pm secure partition - S-side implementation.
 *
 * Phase 6 step 1: minimal ping op. This commit proves the out-of-tree
 * partition mechanism, the CMake plumbing, and the NS->S call path.
 * Real PM dispatch ops (cpu_sleep / cpu_deep_sleep / system_deep_sleep)
 * are added in follow-up commits once the ping round-trip is verified
 * on hardware.
 */

#include <stdint.h>
#include <string.h>

#include "psa/error.h"
#include "psa/service.h"
#include "psa_manifest/z_pm_partition.h"

/* Op IDs carried in psa_call(...,  type, ...). Keep in sync with
 * z_pm_client.h on the NS side.
 */
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
