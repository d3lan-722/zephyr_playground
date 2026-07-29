/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Non-secure client API for the z_pm out-of-tree TF-M partition.
 *
 * The partition (see tfm_partitions/z_pm/z_pm_partition.c) is injected
 * into the TF-M build via TFM_EXTRA_MANIFEST_LIST_FILES /
 * TFM_EXTRA_PARTITION_PATHS from cm33_ns/CMakeLists.txt.
 *
 * Op-ID summary. Ops 1..3 are inherited from project 02 and are kept
 * defined but not called by this project (this project's boot uses
 * DEEP_SLEEP_BIAS instead of LAYER_B_INIT / SET_DEEP_SLEEP_MODE). Ops
 * 4..7 are new in this project.
 *
 *   1  PING                 -- round-trip proof-of-life.
 *   2  LAYER_B_INIT         -- inherited; not called here.
 *   3  SET_DEEP_SLEEP_MODE  -- inherited; not called here.
 *   4  SWITCH_ACTIVE_MODE   -- HP/LP/ULP transition (HF0 divider).
 *   5  CLOCK_PROBE          -- measure DPLL_LP0/CLK_HF0/CLK_HF10.
 *   6  DEEP_SLEEP_BIAS      -- one-shot boot-time DS bias registers.
 *   7  BOOT_CLOCK_RETUNE    -- one-shot boot-time DPLL retune.
 *
 * Op IDs are passed as the `type` argument of psa_call. Keep in sync
 * with tfm_partitions/z_pm/z_pm_partition.c.
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
#define Z_PM_OP_SWITCH_ACTIVE_MODE 4
#define Z_PM_OP_CLOCK_PROBE 5
#define Z_PM_OP_DEEP_SLEEP_BIAS 6
#define Z_PM_OP_BOOT_CLOCK_RETUNE 7

#define Z_PM_PING_COOKIE 0xABCD1234u

/* Wire-format structs. Kept binary-identical to the S-side structs
 * in tfm_partitions/z_pm/z_pm_partition.c. */

struct z_pm_switch_in {
	uint32_t source; /* Encoded pm_mode_t (0=ULP, 1=LP, 2=HP) */
	uint32_t target; /* Encoded pm_mode_t (0=ULP, 1=LP, 2=HP) */
};

struct z_pm_switch_out {
	int32_t status; /* 0 on success, negative errno-like on failure. */
};

/**
 * @brief Clock-frequency snapshot returned by @ref z_pm_clock_probe.
 *
 * All values in Hz. `meas_*` come from the SoC's clock-measurement
 * counters (IHO reference clock); `comp_*` come from the PDL's
 * `Cy_SysClk_ClkPathGetFrequency` / `ClkHfGetFrequency` register
 * readback. Side-by-side lets a stuck counter or a driver-cache
 * mismatch be spotted immediately.
 */
struct z_pm_clock_probe {
	uint32_t meas_path0;
	uint32_t meas_hf0;
	uint32_t meas_hf10;
	uint32_t comp_path0;
	uint32_t comp_hf0;
	uint32_t comp_hf10;
};

/**
 * @brief Round-trip ping the z_pm secure partition.
 *
 * On success, writes @c Z_PM_PING_COOKIE to @c *out_cookie.
 */
psa_status_t z_pm_ping(uint32_t *out_cookie);

/** @brief Inherited from project 02; not called in this project. */
psa_status_t z_pm_layer_b_init(void);

/** @brief Inherited from project 02; not called in this project. */
psa_status_t z_pm_set_deep_sleep_mode(uint32_t mode);

/**
 * @brief Transition the SoC from @p source to @p target using the
 *        HF0-divider strategy. Runs entirely on the S side.
 *
 * @param source   Encoded pm_mode_t: 0=ULP, 1=LP, 2=HP.
 * @param target   Encoded pm_mode_t: 0=ULP, 1=LP, 2=HP.
 * @param out_rc   Non-NULL. Populated with the S-side status: 0 on
 *                 success, negative errno-like on failure. Only
 *                 meaningful if the return value is PSA_SUCCESS.
 * @retval PSA_SUCCESS on success (S handler ran; @c *out_rc holds
 *                     the strategy result).
 * @retval PSA_ERROR_* From psa_call machinery (protocol failure).
 */
psa_status_t z_pm_switch_active_mode(uint32_t source, uint32_t target,
				     int32_t *out_rc);

/**
 * @brief Fill @p report with the current DPLL_LP0 / CLK_HF0 /
 *        CLK_HF10 frequencies (both measured and computed).
 */
psa_status_t z_pm_clock_probe(struct z_pm_clock_probe *report);

/**
 * @brief One-shot boot-time programming of the sticky Deep Sleep
 *        bias registers.
 *
 * Body: Cy_SysPm_Init, SRSS_PWR_CTL2.BGREF_LPMODE, CoreBuck DS
 * voltage/mode/override, IHO/IMO DS-off, ClkBak <- PILO,
 * Cy_SysPm_SetDeepSleepMode(DEEPSLEEP).
 *
 * Call once at boot before the first `deep_sleep` command. All
 * effects are sticky -- programmed once, then automatically re-
 * applied by the PMU state machine on every future SLEEPDEEP entry.
 */
psa_status_t z_pm_deep_sleep_bias(void);

/**
 * @brief One-shot boot-time DPLL_LP0 retune to 200 MHz + CLK_HF0
 *        divider /1.
 *
 * See tfm_partitions/z_pm/z_pm_partition.c for the caveat around
 * SCB2 baud invalidation if HF10 changes as a side effect.
 */
psa_status_t z_pm_boot_clock_retune(void);

#ifdef __cplusplus
}
#endif

#endif /* Z_PM_CLIENT_H_ */
