/*
 * PMU / DVFS demo
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Walks the P-state table programmed from devicetree (P0 -> P3 -> P0),
 * printing the resulting voltage / frequency reported by the PMU model
 * after each transition. Also shows a fault-injection path: a request
 * for an out-of-range P-state which the model reports via
 * DVFSSTATUS.ERROR.
 */
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pmu.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define PMU_NODE DT_ALIAS(pmu0)

static const char *phase_str(uint8_t phase)
{
	switch (phase) {
	case PMU_PHASE_IDLE:
		return "IDLE";
	case PMU_PHASE_VOLT_UP:
		return "VOLT_UP";
	case PMU_PHASE_FREQ_UP:
		return "FREQ_UP";
	case PMU_PHASE_VOLT_DOWN:
		return "VOLT_DOWN";
	case PMU_PHASE_FREQ_DOWN:
		return "FREQ_DOWN";
	default:
		return "?";
	}
}

static void dump_state(const struct device *pmu, const char *tag)
{
	struct pmu_status st;
	uint32_t mv = 0, mhz = 0;

	pmu_get_status(pmu, &st);
	pmu_get_voltage_mv(pmu, &mv);
	pmu_get_freq_mhz(pmu, &mhz);

	printf("[%s] P=%u  V=%u mV  F=%u MHz  busy=%d  pend=%d  phase=%s  "
	       "err=%d\n",
	       tag, st.curr_pstate, mv, mhz, st.busy, st.trans_pending,
	       phase_str(st.trans_phase), st.error);
}

static int switch_pstate(const struct device *pmu, uint8_t target)
{
	int ret;

	printf("--> Requesting P%u\n", target);

	ret = pmu_request_pstate(pmu, target);
	if (ret < 0) {
		printf("    request_pstate failed: %d\n", ret);
		return ret;
	}

	ret = pmu_wait_idle(pmu, K_MSEC(100));
	if (ret < 0) {
		printf("    wait_idle failed: %d\n", ret);
	}

	dump_state(pmu, "after");
	return ret;
}

int main(void)
{
	const struct device *pmu = DEVICE_DT_GET(PMU_NODE);

	printf("PMU DVFS demo (%s)\n", CONFIG_BOARD);

	if (!device_is_ready(pmu)) {
		printf("PMU device not ready\n");
		return -ENODEV;
	}

	/* Show the P-state table that was preloaded from devicetree. */
	printf("Programmed P-state table:\n");
	for (uint8_t i = 0; i < 4; i++) {
		struct pmu_pstate e;

		if (pmu_get_pstate_entry(pmu, i, &e) == 0) {
			printf("  P%u: %u MHz / %u mV\n", i, e.freq_mhz,
			       e.voltage_mv);
		}
	}

	dump_state(pmu, "boot");

	/* Ramp up: P0 -> P1 -> P2 -> P3. */
	for (uint8_t i = 1; i <= 3; i++) {
		switch_pstate(pmu, i);
		k_msleep(200);
	}

	/* Ramp back down to the lowest power state. */
	for (int i = 2; i >= 0; i--) {
		switch_pstate(pmu, (uint8_t)i);
		k_msleep(200);
	}

	/* Fault injection: ask for a P-state above NUMSTATES. The PMU
	 * model must flag ERROR in DVFSSTATUS.
	 */
	printf("\n--> Injecting invalid P-state request (P9)\n");
	pmu_request_pstate(pmu, 9);
	pmu_wait_idle(pmu, K_MSEC(50));
	dump_state(pmu, "err");

	pmu_clear_error(pmu);
	dump_state(pmu, "cleared");

	/* Return to P0 and idle. */
	switch_pstate(pmu, 0);

	printf("Demo done.\n");
	return 0;
}