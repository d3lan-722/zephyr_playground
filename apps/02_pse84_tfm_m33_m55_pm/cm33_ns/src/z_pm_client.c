/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * NS-side stubs for the z_pm out-of-tree TF-M partition. Marshals args
 * into PSA invecs/outvecs and calls the partition's stateless service
 * handle (Z_PM_SERVICE_HANDLE from psa_manifest/sid.h).
 */

#include <stdint.h>

#include "psa/client.h"
#include "psa_manifest/sid.h"

#include "z_pm_client.h"

#ifndef IOVEC_LEN
#define IOVEC_LEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

psa_status_t z_pm_ping(uint32_t *out_cookie)
{
	if (out_cookie == NULL) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	psa_outvec out_vec[] = {
	    {.base = out_cookie, .len = sizeof(*out_cookie)},
	};

	return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_PING, NULL, 0, out_vec,
			IOVEC_LEN(out_vec));
}

psa_status_t z_pm_layer_b_init(void)
{
	return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_LAYER_B_INIT, NULL, 0,
			NULL, 0);
}

psa_status_t z_pm_set_deep_sleep_mode(uint32_t mode)
{
	psa_invec in_vec[] = {
	    {.base = &mode, .len = sizeof(mode)},
	};

	return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_SET_DEEP_SLEEP_MODE,
			in_vec, IOVEC_LEN(in_vec), NULL, 0);
}

psa_status_t z_pm_enter_ds_ram(void)
{
	return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_ENTER_DS_RAM, NULL, 0,
			NULL, 0);
}
