#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <cy_sysclk.h>

#include "bgt60tr13c.h"
#include "bgt60tr13c_default_config.h"

LOG_MODULE_REGISTER(main);

static const struct device *radar_sensor =
    DEVICE_DT_GET(DT_ALIAS(radar_sensor));

#define NUM_SAMPLES BGT60TR13C_DEFAULT_NUM_SAMPLES_PER_FRAME
#define NUM_FRAMES 10
#define FRAME_PERIOD_MS 5 /* matches config frame_repetition_time_s = 5e-3 */

/* Buffer for one frame of FIFO data */
static uint16_t samples[NUM_SAMPLES];

/**
 * Wait for FIFO to reach the required fill level.
 * Returns 0 on success, negative on error/timeout.
 */
static int wait_for_fifo(const struct bgt60tr13c_api *api,
			 uint32_t needed_words, uint32_t *out_fstat)
{
	uint32_t fstat;
	int ret;

	for (int i = 0; i < 5000; i++) {
		ret = api->get_fifo_status(radar_sensor, &fstat);
		if (ret < 0) {
			return ret;
		}

		uint32_t fill = fstat & BGT60TR13C_FSTAT_FILL_STATUS_MSK;
		if (fill >= needed_words) {
			*out_fstat = fstat;
			return 0;
		}

		if (fstat & BGT60TR13C_FSTAT_FOF_ERR_MSK) {
			printk("  FIFO overflow detected\n");
			*out_fstat = fstat;
			return -EOVERFLOW;
		}

		k_usleep(50);
	}

	*out_fstat = fstat;
	return -ETIMEDOUT;
}

/**
 * Print frame statistics: min, max, mean of 12-bit ADC samples.
 */
static void print_frame_stats(uint32_t frame_idx, const uint16_t *buf,
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

	printk("Frame %2u: min=%4u  max=%4u  mean=%4u  | ",
	       (unsigned int)frame_idx, min_val, max_val, (unsigned int)mean);

	/* Print first 8 raw samples */
	for (int i = 0; i < 8 && i < (int)count; i++) {
		printk("%04X ", buf[i]);
	}
	printk("...\n");
}

int main(void)
{
	printk("BGT60TR13C RADAR Sensor - Phase 3: Real Data\n");
	printk("=============================================\n\n");

	if (!device_is_ready(radar_sensor)) {
		printk("Error: RADAR sensor device is not ready\n");
		return -ENODEV;
	}

	const struct bgt60tr13c_api *api = radar_sensor->api;
	int ret;

	printk("Sensor ready (CHIP_ID verified during init)\n");

	/* Step 1: Apply register configuration */
	printk("Configuring sensor (%u registers)...\n",
	       (unsigned int)BGT60TR13C_DEFAULT_REGS_LEN);
	ret = api->config(radar_sensor, bgt60tr13c_default_regs,
			  BGT60TR13C_DEFAULT_REGS_LEN);
	if (ret < 0) {
		printk("ERROR: config failed: %d\n", ret);
		return ret;
	}
	printk("Configuration OK\n");

	/* Step 2: Set FIFO limit = frame size */
	ret = api->set_fifo_limit(radar_sensor, NUM_SAMPLES);
	if (ret < 0) {
		printk("ERROR: set_fifo_limit failed: %d\n", ret);
		return ret;
	}

	/* Step 3: Start frame generation (real ADC data, no LFSR) */
	printk(
	    "Starting frame acquisition (%u samples/frame, %u frames)...\n\n",
	    NUM_SAMPLES, NUM_FRAMES);

	ret = api->start_frame(radar_sensor, true);
	if (ret < 0) {
		printk("ERROR: start_frame failed: %d\n", ret);
		return ret;
	}

	/* Step 4: Acquire frames */
	uint32_t needed_words = NUM_SAMPLES / 2;

	for (uint32_t frame = 0; frame < NUM_FRAMES; frame++) {
		uint32_t fstat = 0;

		ret = wait_for_fifo(api, needed_words, &fstat);
		if (ret < 0) {
			printk("Frame %u: FIFO error (ret=%d, fstat=0x%06X)\n",
			       (unsigned int)frame, ret, (unsigned int)fstat);
			break;
		}

		ret = api->get_fifo_data(radar_sensor, samples, NUM_SAMPLES);
		if (ret < 0) {
			printk("Frame %u: get_fifo_data failed: %d\n",
			       (unsigned int)frame, ret);
			break;
		}

		print_frame_stats(frame, samples, NUM_SAMPLES);
	}

	/* Step 5: Stop frame generation */
	api->start_frame(radar_sensor, false);
	printk("\nDone.\n");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}