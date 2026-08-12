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
 * Scope: only the ops that are NOT already reachable via the PDL's
 * built-in SRF integration. Today that is:
 *   - Z_PM_OP_PING         — round-trip proof-of-life.
 *   - Z_PM_OP_LAYER_B_INIT — once-at-boot static bias (SRSS_MAIN /
 *     PWRMODE writes: Cy_SysPm_Init, ClkBakSetSource(PILO), BGREF LP,
 *     CoreBuck DS knobs, IHO/IMO DS-off).
 *
 * The three SRF-covered sleep entry points (Cy_SysPm_CpuEnter{,Deep}Sleep,
 * CM33-side Cy_SysPm_SystemEnterHibernate) are called directly by
 * cm33_ns/src/power.c — see z_pm_partition.c header for the rationale.
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
#define Z_PM_OP_LAYER_B_INIT 2
#define Z_PM_OP_SET_DEEP_SLEEP_MODE 3
#define Z_PM_OP_CLK_ROOT_SELECT_ENABLE  4
#define Z_PM_OP_CLK_ROOT_SELECT_DISABLE 5


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

/**
 * @brief Run the Layer-B static-bias setup on the S side.
 *
 * Executes once at NS boot (from a SYS_INIT in power.c). Reaches the
 * PSA-ROT-only SRSS / PWRMODE / core-buck registers that NS cannot
 * touch directly.
 *
 * Phase 6 empirical bisection on this SoC showed that the "aggressive"
 * Layer-B knobs (Cy_SysPm_Init recall, ClkBakSetSource(PILO), BGREF LP,
 * CoreBuck DS voltage / mode / override) only pay off once the SoC
 * actually enters a full system deep sleep. That requires Phase 7's
 * per-transition Table-2 PPU programming. On the current CPU-only-DS
 * path those knobs either add small constant leak or override
 * TF-M-S's cycfg defaults. They are deferred to Phase 7 and folded
 * into Z_PM_OP_SET_DEEP_SLEEP_MODE at that time.
 *
 * The minimal handler kept in Phase 6 only clears the IHO / IMO
 * deep-sleep keep-alive bits (harmless on this build — they are 0
 * by default, so this is defensive for future SoC / cycfg drift).
 * PILO is intentionally left running so MCWDT0 (kernel tick) keeps
 * counting.
 *
 * @retval PSA_SUCCESS       On success.
 * @retval PSA_ERROR_*       From psa_call.
 */
psa_status_t z_pm_layer_b_init(void);

/**
 * @brief Program the SoC-global deep-sleep mode + Phase-7 Layer-B knobs.
 *
 * Wraps @c Cy_SysPm_SetDeepSleepMode(mode) on the S side. That call
 * programs the AN237976 Table-2 row of PPU retention settings that will
 * apply once every CPU has voted deep sleep (MAIN / SRAM0 / SRAM1 /
 * SYSCPU / PD1 / APPCPUSS / APPCPU / SOCMEM / U55). All those PPU
 * registers live in the PSA-ROT PC2 PPC group and are unreachable from
 * NS — which is why this has to go through z_pm.
 *
 * The S handler also applies the BGREF LP + CoreBuck DS knobs that
 * Phase 6 measured as adding leak on the CPU-only-DS path. They are
 * safe here because reaching this op means the caller is committing
 * to a system-DS transition.
 *
 * Called by cm33_ns/src/power.c :: enter_system_deep_sleep just before
 * @c Cy_SysPm_CpuEnterDeepSleep. Not called on the plain
 * @c cpu_deep_sleep path (that state is CPU-local).
 *
 * @param mode  One of @c CY_SYSPM_MODE_DEEPSLEEP,
 *              @c CY_SYSPM_MODE_DEEPSLEEP_RAM,
 *              @c CY_SYSPM_MODE_DEEPSLEEP_OFF (cast to uint32_t — the
 *              wrapper avoids pulling cy_syspm.h into this header to
 *              keep the PDL out of NS translation units that don't need
 *              it).
 *
 * @retval PSA_SUCCESS               Mode programmed successfully.
 * @retval PSA_ERROR_INVALID_ARGUMENT @p mode out of range.
 * @retval PSA_ERROR_GENERIC_ERROR    Cy_SysPm_SetDeepSleepMode failed on
 *                                    the S side.
 * @retval PSA_ERROR_*                From psa_call.
 */
psa_status_t z_pm_set_deep_sleep_mode(uint32_t mode);

psa_status_t z_pm_clk_root_select_enable(uint32_t index);
psa_status_t z_pm_clk_root_select_disable(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* Z_PM_CLIENT_H_ */
