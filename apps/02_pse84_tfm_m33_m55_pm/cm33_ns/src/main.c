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
 *    ms  | state             | residency >=  | LED     | dispatch
 *   -----+-------------------+---------------+---------+-----------
 *      5 | cpu_sleep         |      1 010 us | red     | PDL direct
 *    100 | cpu_deep_sleep    |     30 050 us | blue    | PDL direct
 *   1500 | system_deep_sleep |  1 000 050 us | magenta | z_pm+PDL
 *
 * Round-7 finding: the CPU-side Cy_SysPm_Cpu{,Deep}Sleep entries
 * dispatch directly to PDL syspm from NS. The NS-side cy_syspm_v4.c is
 * compiled with CY_PDL_SYSPM_ENABLE_SRF_INTEG, so those calls pack an
 * SRF request and psa_call into IFX_EXT_SP; the S side runs the real
 * WFI at PC2. Prerequisite: CONFIG_IDLE_STACK_SIZE >= 2 KiB (Zephyr
 * default 320 B is too small for the tfm_ns_interface_dispatch
 * fpu_ctx_full alloca on this path).
 *
 * The 1500 ms system_deep_sleep entry additionally calls
 * Z_PM_OP_SET_DEEP_SLEEP_MODE via z_pm to program the AN237976
 * Table-2 DEEPSLEEP row on the Sys-domain PPUs (MAIN, SRAM0, SRAM1,
 * SYSCPU) + folded-in Layer-B DS bias.
 *
 * Deeper states (system_deep_sleep_ram / system_deep_sleep_off) are
 * TODO for future implementation; see DS_RAM_RETROSPECTIVE.md for the
 * Phase-8 scoped attempt and what would be required to finish it.
 */
 #define SLEEP_BETWEEN_BLINKS_MS 5 /* cpu_sleep                 */
// #define SLEEP_BETWEEN_BLINKS_MS 100 /* cpu_deep_sleep — direct PDL */
// #define SLEEP_BETWEEN_BLINKS_MS 1500 /* system_deep_sleep       */


volatile int instrumentation_marker;
volatile uint32_t heartbeat;


/* Trigger function: marks the point where instrumentation should
 * START recording. Referenced by CONFIG_INSTRUMENTATION_TRIGGER_FUNCTION.
 */
 
//Compiler options
// - used: Avoid optimization due to lack of use
// - noinline: Function as an external entity, real function calls.
__attribute__((used, noinline))
void instrumentation_trigger(void)
{
    instrumentation_marker = 1;
}
 
 
 
/* Stopper function: marks the point where instrumentation should
 * stop recording. Referenced by CONFIG_INSTRUMENTATION_STOPPER_FUNCTION.
 */
 
__attribute__((used, noinline))
void instrumentation_stopper(void)
{
    instrumentation_marker = 2;
}

int main(void)
{
	//printf("CM33-NS indicator blinky on %s\n", CONFIG_BOARD);

	indicator_init();
	heartbeat = 0;
	bool flag = 0;
	/* Phase 6 step 1: ping the z_pm secure partition to validate the
	 * out-of-tree partition + PSA call infrastructure end-to-end.
	 * Logs the result once at boot; does not affect the blink loop.
	 */
	uint32_t cookie = 0;
	psa_status_t st = z_pm_ping(&cookie);

	if (st == PSA_SUCCESS && cookie == Z_PM_PING_COOKIE) {
		//printf("z_pm ping ok: cookie=0x%08x\n", cookie);
	} else {
		//printf("z_pm ping FAIL: status=%d cookie=0x%08x\n", (int)st,
		//       cookie);
	}
	indicator_active_off();

	//clk_st = z_pm_clk_root_select_disable(6u);
	//clk_st = z_pm_clk_root_select_disable(7u);
	//clk_st = z_pm_clk_root_select_disable(8u);
	//cookie = 0;
	//k_busy_wait(BLINK_ON_MS * 1000U);
	//cookie = cookie + 1;
	//instrumentation_trigger();
	//k_msleep(SLEEP_BETWEEN_BLINKS_MS);
	//cookie++;
	//instrumentation_stopper();
	//if (cookie==5){
	//	indicator_active_on();
	//}
	//k_busy_wait(100000);
	//instrumentation_trigger();
	psa_status_t clk_st = z_pm_clk_root_select_disable(10u);
	//instrumentation_trigger();	
	k_msleep(SLEEP_BETWEEN_BLINKS_MS);
	//instrumentation_stopper();
	clk_st = z_pm_clk_root_select_enable(10u);
	flag = 1; 
	k_busy_wait(500000);
	indicator_active_on();
	
	
	while (1) {
		
		heartbeat++;
		//k_busy_wait(50000);
		k_busy_wait(BLINK_ON_MS * 1000U);
		indicator_active_off();
		//if (flag == 0){
			//instrumentation_trigger();
			clk_st = z_pm_clk_root_select_disable(10u);
			k_msleep(SLEEP_BETWEEN_BLINKS_MS);
			//instrumentation_stopper();
			clk_st = z_pm_clk_root_select_enable(10u);
			indicator_active_on();
		//}else{
		//	k_busy_wait(100000);
			//k_msleep(SLEEP_BETWEEN_BLINKS_MS);
		//	indicator_active_on();
		//}
		

		//k_busy_wait(100000);
		//printk("%u\n", (unsigned)cookie);*/
	}

	return 0;
}
