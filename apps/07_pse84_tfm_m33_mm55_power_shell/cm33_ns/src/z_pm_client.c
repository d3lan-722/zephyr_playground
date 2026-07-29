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

psa_status_t z_pm_switch_active_mode(uint32_t source, uint32_t target,
				     int32_t *out_rc)
{
	struct z_pm_switch_in in = {.source = source, .target = target};
	struct z_pm_switch_out out = {.status = 0};
	psa_status_t st;

	if (out_rc == NULL) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	psa_invec in_vec[] = {
	    {.base = &in, .len = sizeof(in)},
	};
	psa_outvec out_vec[] = {
	    {.base = &out, .len = sizeof(out)},
	};

	st = psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_SWITCH_ACTIVE_MODE, in_vec,
		      IOVEC_LEN(in_vec), out_vec, IOVEC_LEN(out_vec));
	*out_rc = out.status;
	return st;
}

psa_status_t z_pm_clock_probe(struct z_pm_clock_probe *report)
{
	if (report == NULL) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	psa_outvec out_vec[] = {
	    {.base = report, .len = sizeof(*report)},
	};

	return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_CLOCK_PROBE, NULL, 0,
			out_vec, IOVEC_LEN(out_vec));
}

psa_status_t z_pm_deep_sleep_bias(void)
{
	return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_DEEP_SLEEP_BIAS, NULL, 0,
			NULL, 0);
}

psa_status_t z_pm_boot_clock_retune(void)
{
	return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_BOOT_CLOCK_RETUNE, NULL, 0,
			NULL, 0);
}

