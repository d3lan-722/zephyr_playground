/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33 non-secure indicator-loop blinky for the kit_pse84_eval.
 * Phase 1 of the porting plan: drive the green RGB indicator only,
 * no PM core involvement yet.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "indicator.h"
#include "z_pm_client.h"

#define BLINK_ON_MS 200

/* Per-state knob — pattern lifted from
 * tmp/16_pse84_3img_rram_pm/m33_ns/src/main.c. Uncomment exactly
 * ONE line to pick which PM state Zephyr's default residency
 * policy lands on during the sleep window between blinks.
 *
 * Residency thresholds come from cm33_ns/boards/kit_*.overlay
 * (min-residency-us + exit-latency-us); rows are ordered ascending.
 *
 *    ms  | state                 | residency >=  | LED     | dispatch
 *   -----+-----------------------+---------------+---------+-----------
 *      5 | cpu_sleep             |      1 010 us | red     | PDL direct
 *    100 | cpu_deep_sleep        |     30 050 us | blue    | PDL direct
 *   1500 | system_deep_sleep     |  1 000 050 us | magenta | PDL direct
 *   2500 | system_deep_sleep_ram |  2 000 500 us | cyan    | (unimpl)
 *   5000 | system_deep_sleep_off |  4 100 000 us | white   | (unimpl)
 *
 * Round-7 finding: every row above dispatches directly to PDL syspm
 * from NS. The NS-side cy_syspm_v4.c is compiled with
 * CY_PDL_SYSPM_ENABLE_SRF_INTEG, so Cy_SysPm_Cpu{Enter,Deep}Sleep and
 * SystemEnterHibernate (CM33) all pack an SRF request and psa_call
 * into IFX_EXT_SP; the S side runs the real WFI at PC2. Prerequisite:
 * CONFIG_IDLE_STACK_SIZE >= 2 KiB (Zephyr default 320 B is too small
 * for the tfm_ns_interface_dispatch fpu_ctx_full alloca on this path).
 *
 * The 2500 / 5000 lines are commented-out placeholders — their
 * dispatchers only print a "not implemented yet" line today
 * (see power.c). Uncomment them only after wiring the
 * corresponding partition ops (z_pm will hold them because
 * Cy_SysPm_SetSysDeepSleepMode and Cy_SysPm_SystemEnterHibernate
 * on CM55 are NOT SRF-wrapped in the PDL).
 */
#define SLEEP_BETWEEN_BLINKS_MS 5 /* cpu_sleep                     */
// #define SLEEP_BETWEEN_BLINKS_MS 100 /* cpu_deep_sleep — direct PDL   */
//  #define SLEEP_BETWEEN_BLINKS_MS 1500 /* system_deep_sleep             */
//  #define SLEEP_BETWEEN_BLINKS_MS 2500 /* system_deep_sleep_ram (unimpl) */
//  #define SLEEP_BETWEEN_BLINKS_MS 5000 /* system_deep_sleep_off (unimpl) */

int main(void)
{
	printf("CM33-NS indicator blinky on %s\n", CONFIG_BOARD);

	indicator_init();

	/* Phase 6 step 1: ping the z_pm secure partition to validate the
	 * out-of-tree partition + PSA call infrastructure end-to-end.
	 * Logs the result once at boot; does not affect the blink loop.
	 */
	uint32_t cookie = 0;
	psa_status_t st = z_pm_ping(&cookie);

	if (st == PSA_SUCCESS && cookie == Z_PM_PING_COOKIE) {
		printf("z_pm ping ok: cookie=0x%08x\n", cookie);
	} else {
		printf("z_pm ping FAIL: status=%d cookie=0x%08x\n", (int)st,
		       cookie);
	}

	while (1) {
		indicator_active_on();
		k_busy_wait(BLINK_ON_MS * 1000U);
		indicator_active_off();
		k_msleep(SLEEP_BETWEEN_BLINKS_MS);
	}

	return 0;
}
