/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Non-secure client API for the z_pm out-of-tree TF-M partition.
 *
 * The partition lives in apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/
 * and is injected into the TF-M build via TFM_EXTRA_MANIFEST_LIST_FILES /
 * TFM_EXTRA_PARTITION_PATHS from cm33_ns/CMakeLists.txt.
 *
 * Scope (round 7): only the ops that are NOT already reachable via the
 * PDL's built-in SRF integration. Today that means just Z_PM_OP_PING.
 * The former sleep ops (Cy_SysPm_CpuEnter{,Deep}Sleep,
 * SystemEnterHibernate) are called directly by cm33_ns/src/power.c —
 * see z_pm_partition.c header for the rationale.
 *
 * Op IDs are passed as the `type` argument of psa_call. Keep this enum in
 * sync with tfm_partitions/z_pm/z_pm_partition.c.
 */

#ifndef Z_PM_CLIENT_H_
#define Z_PM_CLIENT_H_

#include <stdint.h>
#include "psa/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Op IDs - must match z_pm_partition.c */
#define Z_PM_OP_PING 1

#define Z_PM_PING_COOKIE 0xABCD1234u

/**
 * @brief Round-trip ping the z_pm secure partition.
 *
 * On success, writes Z_PM_PING_COOKIE to *out_cookie.
 *
 * @retval PSA_SUCCESS On success; @p out_cookie contains the cookie.
 * @retval PSA_ERROR_* From psa_call.
 */
psa_status_t z_pm_ping(uint32_t *out_cookie);

#ifdef __cplusplus
}
#endif

#endif /* Z_PM_CLIENT_H_ */
