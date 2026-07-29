/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM33 non-secure application bring-up for
 * apps/07_pse84_tfm_m33_mm55_power_shell.
 *
 * Phase D (mode switcher wired up): banner + GPIO indicator init +
 * z_pm PING + two boot-time z_pm ops that program the SoC's sticky
 * PM registers (all Bucket-C, PC=2 only, unreachable from NS):
 *
 *   1. Z_PM_OP_BOOT_CLOCK_RETUNE -- DPLL_LP0 400 MHz -> 200 MHz
 *      + CLK_HF0 divider /2 -> /1, matching project 06's baseline
 *      clock tree.
 *   2. Z_PM_OP_DEEP_SLEEP_BIAS   -- BGREF LP, CoreBuck DS 0.70 V
 *      LP override, IHO/IMO DS-off, ClkBak <- PILO,
 *      Cy_SysPm_SetDeepSleepMode(DEEPSLEEP). Every effect is sticky.
 *
 * Shell commands registered at link time:
 *
 *   noidle [on|off]    -- veto WFI in idle thread (src/cmd_noidle.c)
 *   sleep              -- Cy_SysPm_CpuEnterSleep     (src/cmd_sleep.c)
 *   deep_sleep         -- Cy_SysPm_CpuEnterDeepSleep (src/cmd_deep_sleep.c)
 *   hp / lp / ulp      -- z_pm SWITCH_ACTIVE_MODE    (via power_manager)
 *   probe              -- z_pm CLOCK_PROBE           (via power_manager)
 *
 * At the end of Phase D:
 *   - `probe` prints measured + computed DPLL/HF0/HF10 frequencies.
 *   - `hp`, `lp`, `ulp` transition the SoC voltage / SRAM trims / RRAM
 *     mode / HF0 divider via the S partition and return with the new
 *     mode's CLK_HF0 frequency in effect.
 *   - `deep_sleep` reaches the AN237976 Table-2 DEEPSLEEP sleep floor
 *     because both CPUs have voted DS and every PPU / bias register
 *     is programmed for the retention target.
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "gpio_indicators.h"
#include "power_manager.h"
#include "z_pm_client.h"

/**
 * @brief Force the Infineon SCB UART driver to recompute its baud
 *        divisor from the currently-live CLK_HF10.
 *
 * The Zephyr Infineon SCB driver caches its baud divisor at init
 * time based on the CLK_HF10 rate at that moment. Any op that
 * changes CLK_HF10 later (Z_PM_OP_BOOT_CLOCK_RETUNE at boot,
 * Z_PM_OP_SWITCH_ACTIVE_MODE if it ever touches HF10) invalidates
 * that cache. Calling uart_configure() with the current config makes
 * the driver re-derive the divisor from live HF10.
 */
static void reconfigure_console_uart(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	struct uart_config cfg;

	if (!device_is_ready(console)) {
		return;
	}
	if (uart_config_get(console, &cfg) != 0) {
		return;
	}
	(void)uart_configure(console, &cfg);
}

int main(void)
{
	printf("CM33-NS power-shell on %s\n", CONFIG_BOARD);

	if (gpio_indicators_init() < 0) {
		printf("gpio_indicators_init failed -- LEDs / pm_busy pin "
		       "will be dark\n");
	}

	/* Prove the z_pm out-of-tree TF-M partition + PSA call
	 * infrastructure is intact end-to-end. Non-fatal: a failure
	 * here does not prevent the shell from coming up, but every
	 * subsequent op will also fail. */
	uint32_t cookie = 0;
	psa_status_t st = z_pm_ping(&cookie);

	if (st == PSA_SUCCESS && cookie == Z_PM_PING_COOKIE) {
		printf("z_pm ping ok: cookie=0x%08x\n", cookie);
	} else {
		printf("z_pm ping FAIL: status=%d cookie=0x%08x\n", (int)st,
		       cookie);
	}

	/* Retune DPLL_LP0 to 200 MHz + CLK_HF0 to /1. Must run BEFORE
	 * the divider strategy transitions LP/ULP -- otherwise LP would
	 * land at 400/3 = 133 MHz which is over the CM33 LP spec
	 * ceiling of 80 MHz.
	 *
	 * CLK_HF10 (SCB2 peripheral clock) is derived from DPLL_LP0
	 * with a fixed divider, so this retune halves HF10 too. The
	 * SCB driver caches its baud divisor at boot from the pre-
	 * retune HF10, so we MUST reconfigure the console UART right
	 * after the retune -- otherwise every subsequent console byte
	 * gets clocked out at half-speed and the terminal sees noise.
	 * Since the k_msleep drain below relies on the console being
	 * alive, we drain BEFORE the retune. */
#if 0 /* DISABLED: the S-side Cy_SysClk_PllDisable / PllEnable path            \
       * hangs the CPU (LED stuck on = heartbeat frozen = CM33 stuck           \
       * in S mode). Run `probe` from the shell instead to see the             \
       * boot-time DPLL/HF0/HF10 and pick divider constants (in                \
       * tfm_partitions/z_pm/z_pm_partition.c hf0_div_for_mode) that           \
       * match the boot state directly. Diagnosis + fix is a Phase-E           \
       * task. */
	printf("retuning DPLL_LP0 to 200 MHz + CLK_HF0 /1 ...\n");
	k_msleep(20); /* drain the ring so nothing is in flight during retune */
	st = z_pm_boot_clock_retune();
	reconfigure_console_uart();
	if (st == PSA_SUCCESS) {
		printf("z_pm boot_clock_retune ok "
		       "(DPLL_LP0 = 200 MHz, CLK_HF0 /1)\n");
	} else {
		printf("z_pm boot_clock_retune FAIL: %d "
		       "(mode transitions will over-clock)\n",
		       (int)st);
	}
#else
	printf("z_pm boot_clock_retune SKIPPED (hangs S side; run `probe` "
	       "to see boot clock tree, then adjust hf0_div_for_mode)\n");
	(void)reconfigure_console_uart; /* silence unused-function warning */
#endif

	/* Program the sticky Deep Sleep bias registers. */
	st = z_pm_deep_sleep_bias();
	if (st == PSA_SUCCESS) {
		printf("z_pm deep_sleep_bias ok (BGREF LP / CoreBuck DS / "
		       "IHO/IMO DS-off / DEEPSLEEP mode)\n");
	} else {
		printf("z_pm deep_sleep_bias FAIL: %d "
		       "(deep_sleep floor will be sub-optimal)\n",
		       (int)st);
	}

	pm_init();
	printf("initial power mode: %s\n", pm_mode_name(pm_current_mode()));

	return 0;
}
