#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <cy_sysclk.h>

#include "bgt60tr13c.h"
#include "bgt60tr13c_default_config.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct device *radar_sensor =
    DEVICE_DT_GET(DT_ALIAS(radar_sensor));

#define NUM_SAMPLES BGT60TR13C_DEFAULT_NUM_SAMPLES_PER_FRAME
#define NUM_FRAMES 10
#define FRAME_PERIOD_MS 5 /* matches config frame_repetition_time_s = 5e-3 */

/* IRQ wait budget: chirp cadence is 5 ms; 50 ms tolerates ~10x slack. */
#define FIFO_IRQ_TIMEOUT K_MSEC(50)

/* Buffer for one frame of FIFO data */
static uint16_t samples[NUM_SAMPLES];

/**
 * Log one frame's stats + first 8 raw samples as a single line so the
 * log backend never splits it under back-pressure.
 */
static void log_frame_stats(uint32_t frame_idx, const uint16_t *buf,
			    uint32_t count)
{
	uint16_t min_val = 0x0FFF;
	uint16_t max_val = 0;
	uint32_t sum = 0;

	for (uint32_t i = 0; i < count; i++) {
		uint16_t v = buf[i] & 0x0FFF;
		if (v < min_val) {
			min_val = v;
		}
		if (v > max_val) {
			max_val = v;
		}
		sum += v;
	}

	uint32_t mean = sum / count;

	char line[128];
	int n = snprintf(line, sizeof(line),
			 "Frame %2u: min=%4u max=%4u mean=%4u | ",
			 (unsigned int)frame_idx, min_val, max_val,
			 (unsigned int)mean);

	for (int i = 0; i < 8 && i < (int)count && n < (int)sizeof(line); i++) {
		n += snprintf(line + n, sizeof(line) - n, "%04X ", buf[i]);
	}

	LOG_INF("%s...", line);
}

int main(void)
{
	LOG_INF("BGT60TR13C RADAR Sensor - Phase 3: Real Data");

	if (!device_is_ready(radar_sensor)) {
		LOG_ERR("RADAR sensor device is not ready");
		return -ENODEV;
	}

	const struct bgt60tr13c_api *api = radar_sensor->api;
	int ret;

	LOG_INF("Sensor ready (CHIP_ID verified during init)");

	LOG_INF("Configuring sensor (%u registers)...",
		(unsigned int)BGT60TR13C_DEFAULT_REGS_LEN);
	ret = api->config(radar_sensor, bgt60tr13c_default_regs,
			  BGT60TR13C_DEFAULT_REGS_LEN);
	if (ret < 0) {
		LOG_ERR("config failed: %d", ret);
		return ret;
	}

	ret = api->set_fifo_limit(radar_sensor, NUM_SAMPLES);
	if (ret < 0) {
		LOG_ERR("set_fifo_limit failed: %d", ret);
		return ret;
	}

	LOG_INF("Starting frame acquisition (%u samples/frame, %u frames)",
		NUM_SAMPLES, NUM_FRAMES);

	ret = api->start_frame(radar_sensor, true);
	if (ret < 0) {
		LOG_ERR("start_frame failed: %d", ret);
		return ret;
	}

	for (uint32_t frame = 0; frame < NUM_FRAMES; frame++) {
		ret = api->wait_fifo_ready(radar_sensor, FIFO_IRQ_TIMEOUT);
		if (ret < 0) {
			uint32_t fstat = 0;
			(void)api->get_fifo_status(radar_sensor, &fstat);
			LOG_ERR("Frame %u: IRQ wait failed (ret=%d, "
				"fstat=0x%06X)",
				(unsigned int)frame, ret,
				(unsigned int)fstat);
			break;
		}

		ret = api->get_fifo_data(radar_sensor, samples, NUM_SAMPLES);
		if (ret < 0) {
			LOG_ERR("Frame %u: get_fifo_data failed: %d",
				(unsigned int)frame, ret);
			break;
		}

		log_frame_stats(frame, samples, NUM_SAMPLES);
	}

	api->start_frame(radar_sensor, false);
	LOG_INF("Done.");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}