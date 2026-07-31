/* BGT60TR13C 60 GHz FMCW Radar Sensor – Zephyr Driver
 *
 * SPI protocol ported from Infineon's official xensiv_bgt60trxx library.
 * Ties to the 'compatible = "infineon,bgt60tr13c"' node in the Devicetree.
 */
#define DT_DRV_COMPAT infineon_bgt60tr13c

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "bgt60tr13c.h"

LOG_MODULE_REGISTER(bgt60tr13c);

/* ─────────────────────────────────────────────────────────────────
 * SPI helpers – 32-bit word protocol
 * ───────────────────────────────────────────────────────────────── */

/**
 * Write a 24-bit value to a sensor register.
 *
 * Wire format (big-endian 32-bit word):
 *   [31:25] register address
 *   [24]    1 (write)
 *   [23:0]  data
 */
static int bgt60tr13c_write_reg(const struct device *dev, uint8_t reg,
				uint32_t val)
{
	const struct bgt60tr13c_config *cfg = dev->config;
	uint32_t word;

	word = ((uint32_t)reg << BGT60TR13C_SPI_ADDR_POS) &
	       BGT60TR13C_SPI_ADDR_MSK;
	word |= BGT60TR13C_SPI_WR_BIT;
	word |= val & BGT60TR13C_SPI_DATA_MSK;

	/* Byte-reverse for big-endian SPI transfer */
	word = sys_cpu_to_be32(word);

	uint8_t tx[4];
	memcpy(tx, &word, 4);

	const struct spi_buf tx_buf = {.buf = tx, .len = 4};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};

	return spi_write_dt(&cfg->spi, &tx_set);
}

/**
 * Read a 24-bit value from a sensor register.
 *
 * Wire format (big-endian 32-bit word):
 *   [31:25] register address
 *   [24]    0 (read)
 *   [23:0]  don't-care on TX; response data on RX
 *
 * The response word carries:
 *   [27:24] GSR0 status bits (checked but not returned)
 *   [23:0]  register data
 */
static int bgt60tr13c_read_reg(const struct device *dev, uint8_t reg,
			       uint32_t *val)
{
	const struct bgt60tr13c_config *cfg = dev->config;
	uint32_t word;

	/* Build read command – WR bit is 0 */
	word = ((uint32_t)reg << BGT60TR13C_SPI_ADDR_POS) &
	       BGT60TR13C_SPI_ADDR_MSK;

	word = sys_cpu_to_be32(word);

	uint8_t tx[4], rx[4];
	memcpy(tx, &word, 4);

	const struct spi_buf tx_buf = {.buf = tx, .len = 4};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf rx_buf = {.buf = rx, .len = 4};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

	int ret = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
	if (ret < 0) {
		return ret;
	}

	/* Byte-reverse the response and extract 24-bit data */
	uint32_t resp;
	memcpy(&resp, rx, 4);
	resp = sys_be32_to_cpu(resp);

	*val = resp & BGT60TR13C_SPI_DATA_MSK;
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Hardware reset
 * ───────────────────────────────────────────────────────────────── */

/**
 * Hard reset sequence.
 *
 * The DT declares reset-gpios with GPIO_ACTIVE_LOW, so gpio_pin_set_dt()
 * uses logical levels:  set(1) = assert reset (pin LOW),
 *                       set(0) = release reset (pin HIGH).
 */
static int bgt60tr13c_hard_reset(const struct device *dev)
{
	const struct bgt60tr13c_config *cfg = dev->config;
	int ret;

	/* Assert reset (logical active → physical LOW) */
	ret = gpio_pin_set_dt(&cfg->reset_gpio, 1);
	if (ret < 0) {
		return ret;
	}
	k_msleep(1);

	/* Release reset (logical inactive → physical HIGH) */
	ret = gpio_pin_set_dt(&cfg->reset_gpio, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(1);

	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────── */

static void bgt60tr13c_fifo_isr(const struct device *port,
				struct gpio_callback *cb,
				gpio_port_pins_t pins);

static int bgt60tr13c_init(const struct device *dev)
{
	const struct bgt60tr13c_config *cfg = dev->config;
	struct bgt60tr13c_runtime_data *data = dev->data;
	int ret;

	LOG_INF("Initializing BGT60TR13C radar sensor");

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
		LOG_ERR("Reset GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure reset GPIO: %d", ret);
		return ret;
	}

	/* Hard reset the sensor */
	ret = bgt60tr13c_hard_reset(dev);
	if (ret < 0) {
		LOG_ERR("Hard reset failed: %d", ret);
		return ret;
	}

	/* Set SPI speed mode (normal, not high-speed) */
	ret = bgt60tr13c_write_reg(dev, BGT60TR13C_REG_SFCTL, 0);
	if (ret < 0) {
		LOG_ERR("Failed to set SPI mode: %d", ret);
		return ret;
	}

	/* Read and verify CHIP_ID */
	uint32_t chip_id;

	ret = bgt60tr13c_read_reg(dev, BGT60TR13C_REG_CHIP_ID, &chip_id);
	if (ret < 0) {
		LOG_ERR("Failed to read CHIP_ID: %d", ret);
		return ret;
	}

	LOG_INF("CHIP_ID = 0x%06X", (unsigned int)chip_id);

	if (chip_id != BGT60TR13C_CHIP_ID_VALUE) {
		LOG_ERR("Unexpected CHIP_ID: 0x%06X (expected 0x%06X)",
			(unsigned int)chip_id,
			(unsigned int)BGT60TR13C_CHIP_ID_VALUE);
		return -ENODEV;
	}

	LOG_INF("BGT60TR13C detected – SPI communication OK");

	/* Optional FIFO-ready IRQ line. Only configure it if the DT node
	 * declares irq-gpios; polling still works otherwise.
	 */
	if (cfg->irq_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->irq_gpio)) {
			LOG_ERR("IRQ GPIO not ready");
			return -ENODEV;
		}

		k_sem_init(&data->fifo_ready, 0, 1);

		ret = gpio_pin_configure_dt(&cfg->irq_gpio, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("Failed to configure IRQ GPIO: %d", ret);
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(&cfg->irq_gpio,
						      GPIO_INT_EDGE_TO_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to enable IRQ: %d", ret);
			return ret;
		}

		gpio_init_callback(&data->irq_cb, bgt60tr13c_fifo_isr,
				   BIT(cfg->irq_gpio.pin));

		ret = gpio_add_callback(cfg->irq_gpio.port, &data->irq_cb);
		if (ret < 0) {
			LOG_ERR("Failed to add IRQ callback: %d", ret);
			return ret;
		}

		LOG_INF("FIFO-ready IRQ wired on pin %u", cfg->irq_gpio.pin);
	}

	data->initialized = true;
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * FIFO-ready IRQ callback – runs in ISR context
 * ────────────────────────────────────────────────────────────── */

static void bgt60tr13c_fifo_isr(const struct device *port,
				struct gpio_callback *cb, gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	struct bgt60tr13c_runtime_data *data =
	    CONTAINER_OF(cb, struct bgt60tr13c_runtime_data, irq_cb);

	k_sem_give(&data->fifo_ready);
}

/* ───────────────────────────────────────────────────────────────
 * Wait for FIFO threshold IRQ
 * ────────────────────────────────────────────────────────────── */

static int bgt60tr13c_wait_fifo_ready(const struct device *dev,
				      k_timeout_t timeout)
{
	struct bgt60tr13c_runtime_data *data = dev->data;

	return k_sem_take(&data->fifo_ready, timeout);
}

/* ─────────────────────────────────────────────────────────────────
 * Soft reset
 * ───────────────────────────────────────────────────────────────── */

#define SOFT_RESET_DELAY_MS 10
#define SOFT_RESET_TIMEOUT 0xFFFFFFFFU

/**
 * Trigger a soft reset and wait for the reset bits to self-clear.
 * @param reset_type  OR-combination of BGT60TR13C_RESET_SW / _FSM / _FIFO
 */
static int bgt60tr13c_soft_reset(const struct device *dev, uint32_t reset_type)
{
	uint32_t tmp;
	int ret;

	ret = bgt60tr13c_read_reg(dev, BGT60TR13C_REG_MAIN, &tmp);
	if (ret < 0) {
		return ret;
	}

	tmp |= reset_type;
	ret = bgt60tr13c_write_reg(dev, BGT60TR13C_REG_MAIN, tmp);
	if (ret < 0) {
		return ret;
	}

	/* Poll until reset bits self-clear */
	uint32_t timeout = SOFT_RESET_TIMEOUT;

	while (timeout > 0) {
		ret = bgt60tr13c_read_reg(dev, BGT60TR13C_REG_MAIN, &tmp);
		if (ret < 0) {
			return ret;
		}
		if ((tmp & reset_type) == 0) {
			break;
		}
		--timeout;
	}

	if (timeout == 0) {
		LOG_ERR("Soft reset timeout");
		return -ETIMEDOUT;
	}

	k_msleep(SOFT_RESET_DELAY_MS);
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Configuration – write register array from configurator tool
 * ───────────────────────────────────────────────────────────────── */

/**
 * Apply a register configuration generated by bgt60-configurator-cli.
 *
 * Each element in @p regs is a packed 32-bit SPI write command:
 *   [31:25] register address, [24] write bit, [23:0] data.
 *
 * Performs a SW reset first, then writes each register.  SFCTL is
 * handled specially: FIFO_CREF is cleared (set later via set_fifo_limit)
 * and MISO_HS_READ is forced off (normal SPI speed).
 */
static int bgt60tr13c_config(const struct device *dev, const uint32_t *regs,
			     uint32_t len)
{
	int ret;

	ret = bgt60tr13c_soft_reset(dev, BGT60TR13C_RESET_SW);
	if (ret < 0) {
		return ret;
	}

	for (uint32_t i = 0; i < len; i++) {
		uint32_t val = regs[i];
		uint8_t reg_addr = (val >> BGT60TR13C_SPI_ADDR_POS) & 0x7F;
		uint32_t reg_data = val & BGT60TR13C_SPI_DATA_MSK;

		if (reg_addr == BGT60TR13C_REG_SFCTL) {
			/* Clear FIFO_CREF (user sets it later) */
			reg_data &= ~BGT60TR13C_SFCTL_FIFO_CREF_MSK;
			/* Force normal-speed SPI */
			reg_data &= ~BGT60TR13C_SFCTL_MISO_HS_MSK;
		}

		ret = bgt60tr13c_write_reg(dev, reg_addr, reg_data);
		if (ret < 0) {
			LOG_ERR("Config write failed at reg 0x%02X: %d",
				reg_addr, ret);
			return ret;
		}
	}

	LOG_INF("Configuration applied (%u registers)", (unsigned int)len);
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * FIFO limit
 * ───────────────────────────────────────────────────────────────── */

/**
 * Set FIFO compare reference (CREF).  When FIFO fill level ≥ @p num_samples,
 * the IRQ pin is asserted.
 * @param num_samples  Must be even and ≤ 2 × FIFO_SIZE.
 */
static int bgt60tr13c_set_fifo_limit(const struct device *dev,
				     uint32_t num_samples)
{
	uint32_t tmp;
	int ret;

	if ((num_samples & 1) || (num_samples / 2 > BGT60TR13C_FIFO_SIZE)) {
		return -EINVAL;
	}

	ret = bgt60tr13c_read_reg(dev, BGT60TR13C_REG_SFCTL, &tmp);
	if (ret < 0) {
		return ret;
	}

	tmp &= ~BGT60TR13C_SFCTL_FIFO_CREF_MSK;
	tmp |= ((num_samples / 2) - 1) & BGT60TR13C_SFCTL_FIFO_CREF_MSK;

	return bgt60tr13c_write_reg(dev, BGT60TR13C_REG_SFCTL, tmp);
}

/* ─────────────────────────────────────────────────────────────────
 * Frame start / stop
 * ───────────────────────────────────────────────────────────────── */

static int bgt60tr13c_start_frame(const struct device *dev, bool start)
{
	uint32_t tmp;
	int ret;

	if (start) {
		ret = bgt60tr13c_read_reg(dev, BGT60TR13C_REG_MAIN, &tmp);
		if (ret < 0) {
			return ret;
		}
		tmp |= BGT60TR13C_MAIN_FRAME_START;
		return bgt60tr13c_write_reg(dev, BGT60TR13C_REG_MAIN, tmp);
	}

	/* Stop: FSM reset halts chirp generation */
	return bgt60tr13c_soft_reset(dev, BGT60TR13C_RESET_FSM);
}

/* ─────────────────────────────────────────────────────────────────
 * FIFO status
 * ───────────────────────────────────────────────────────────────── */

static int bgt60tr13c_get_fifo_status(const struct device *dev,
				      uint32_t *status)
{
	return bgt60tr13c_read_reg(dev, BGT60TR13C_REG_FSTAT, status);
}

/* ─────────────────────────────────────────────────────────────────
 * FIFO data read – 8-bit SPI, manual 12-bit unpacking
 *
 * The FIFO stores 2 × 12-bit ADC samples per 3-byte word:
 *   byte[0]           byte[1]           byte[2]
 *   [S0_11:S0_4]      [S0_3:S0_0|S1_11:S1_8]  [S1_7:S1_0]
 *
 * We issue a burst-mode header (4 bytes), then read raw bytes and
 * unpack into uint16_t[].
 * ───────────────────────────────────────────────────────────────── */

static int bgt60tr13c_get_fifo_data(const struct device *dev, uint16_t *data,
				    uint32_t num_samples)
{
	const struct bgt60tr13c_config *cfg = dev->config;
	int ret;

	if ((num_samples & 1) || (num_samples / 2 > BGT60TR13C_FIFO_SIZE)) {
		return -EINVAL;
	}

	uint32_t fifo_words = num_samples / 2;
	uint32_t raw_bytes = fifo_words * 3;

	/* Build burst-mode header */
	uint32_t hdr =
	    BGT60TR13C_SPI_BURST_CMD |
	    ((uint32_t)BGT60TR13C_REG_FIFO << BGT60TR13C_SPI_BURST_SADR_POS);
	hdr = sys_cpu_to_be32(hdr);

	uint8_t tx_hdr[4];
	uint8_t rx_hdr[4];
	uint8_t tx_pad[raw_bytes];
	uint8_t raw[raw_bytes];

	memcpy(tx_hdr, &hdr, 4);

	/* TX must drive MOSI high during FIFO read (per datasheet) */
	memset(tx_pad, 0xFF, raw_bytes);

	/*
	 * Burst read requires CS held low across the entire transfer:
	 * [4-byte header] [N-byte FIFO data].
	 * Use multi-buffer spi_buf_set so the SPI driver keeps CS asserted.
	 */
	const struct spi_buf tx_bufs[] = {
	    {.buf = tx_hdr, .len = 4},
	    {.buf = tx_pad, .len = raw_bytes},
	};
	const struct spi_buf rx_bufs[] = {
	    {.buf = rx_hdr, .len = 4},
	    {.buf = raw, .len = raw_bytes},
	};
	const struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 2};
	const struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 2};

	ret = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
	if (ret < 0) {
		return ret;
	}

	/* First MISO byte during the 4-byte header is GSR0 (datasheet §5.6,
	 * §5.8). Bits are sticky until SW/HW reset - non-zero means a prior
	 * or current SPI/FIFO error corrupted the burst.
	 */
	uint8_t gsr0 = rx_hdr[0];
	uint8_t gsr0_err_mask = BGT60TR13C_GSR0_FOU_ERR_MSK |
				BGT60TR13C_GSR0_SPI_BURST_ERR_MSK |
				BGT60TR13C_GSR0_CLK_NUM_ERR_MSK;

	if (gsr0 & gsr0_err_mask) {
		LOG_ERR(
		    "FIFO burst aborted: GSR0=0x%02X (%s%s%s)- SW reset needed",
		    gsr0,
		    (gsr0 & BGT60TR13C_GSR0_FOU_ERR_MSK) ? "FOU_ERR " : "",
		    (gsr0 & BGT60TR13C_GSR0_SPI_BURST_ERR_MSK)
			? "SPI_BURST_ERR "
			: "",
		    (gsr0 & BGT60TR13C_GSR0_CLK_NUM_ERR_MSK) ? "CLK_NUM_ERR "
							     : "");
		return -EIO;
	}

	/* Unpack: 3 bytes → 2 × 12-bit samples */
	for (uint32_t i = 0; i < fifo_words; i++) {
		uint8_t b0 = raw[i * 3 + 0];
		uint8_t b1 = raw[i * 3 + 1];
		uint8_t b2 = raw[i * 3 + 2];

		data[i * 2 + 0] = ((uint16_t)b0 << 4) | (b1 >> 4);
		data[i * 2 + 1] = ((uint16_t)(b1 & 0x0F) << 8) | b2;
	}

	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * LFSR data test mode
 * ───────────────────────────────────────────────────────────────── */

static int bgt60tr13c_enable_test_mode(const struct device *dev, bool enable)
{
	uint32_t tmp;
	int ret;

	ret = bgt60tr13c_read_reg(dev, BGT60TR13C_REG_SFCTL, &tmp);
	if (ret < 0) {
		return ret;
	}

	if (enable) {
		tmp |= BGT60TR13C_SFCTL_LFSR_EN_MSK;
	} else {
		tmp &= ~BGT60TR13C_SFCTL_LFSR_EN_MSK;
	}

	return bgt60tr13c_write_reg(dev, BGT60TR13C_REG_SFCTL, tmp);
}

/* ─────────────────────────────────────────────────────────────────
 * Public API wrappers
 * ───────────────────────────────────────────────────────────────── */

static int bgt60tr13c_api_read_reg(const struct device *dev, uint8_t reg,
				   uint32_t *val)
{
	return bgt60tr13c_read_reg(dev, reg, val);
}

static int bgt60tr13c_api_write_reg(const struct device *dev, uint8_t reg,
				    uint32_t val)
{
	return bgt60tr13c_write_reg(dev, reg, val);
}

static int bgt60tr13c_api_soft_reset(const struct device *dev,
				     uint32_t reset_type)
{
	return bgt60tr13c_soft_reset(dev, reset_type);
}

static int bgt60tr13c_api_config(const struct device *dev, const uint32_t *regs,
				 uint32_t len)
{
	return bgt60tr13c_config(dev, regs, len);
}

static int bgt60tr13c_api_set_fifo_limit(const struct device *dev,
					 uint32_t num_samples)
{
	return bgt60tr13c_set_fifo_limit(dev, num_samples);
}

static int bgt60tr13c_api_start_frame(const struct device *dev, bool start)
{
	return bgt60tr13c_start_frame(dev, start);
}

static int bgt60tr13c_api_get_fifo_status(const struct device *dev,
					  uint32_t *status)
{
	return bgt60tr13c_get_fifo_status(dev, status);
}

static int bgt60tr13c_api_get_fifo_data(const struct device *dev,
					uint16_t *data, uint32_t num_samples)
{
	return bgt60tr13c_get_fifo_data(dev, data, num_samples);
}

static int bgt60tr13c_api_enable_test_mode(const struct device *dev,
					   bool enable)
{
	return bgt60tr13c_enable_test_mode(dev, enable);
}

static int bgt60tr13c_api_wait_fifo_ready(const struct device *dev,
					  k_timeout_t timeout)
{
	return bgt60tr13c_wait_fifo_ready(dev, timeout);
}

/* ─────────────────────────────────────────────────────────────────
 * Devicetree instantiation
 * ───────────────────────────────────────────────────────────────── */

static const struct bgt60tr13c_api bgt60tr13c_api_funcs = {
    .read_reg = bgt60tr13c_api_read_reg,
    .write_reg = bgt60tr13c_api_write_reg,
    .soft_reset = bgt60tr13c_api_soft_reset,
    .config = bgt60tr13c_api_config,
    .set_fifo_limit = bgt60tr13c_api_set_fifo_limit,
    .start_frame = bgt60tr13c_api_start_frame,
    .get_fifo_status = bgt60tr13c_api_get_fifo_status,
    .get_fifo_data = bgt60tr13c_api_get_fifo_data,
    .enable_test_mode = bgt60tr13c_api_enable_test_mode,
    .wait_fifo_ready = bgt60tr13c_api_wait_fifo_ready,
};

#define BGT60TR13C_DEFINE(inst)                                                \
	static struct bgt60tr13c_runtime_data bgt60tr13c_data_##inst;          \
	static const struct bgt60tr13c_config bgt60tr13c_config_##inst = {     \
	    .spi = SPI_DT_SPEC_INST_GET(inst,                                  \
					SPI_WORD_SET(8) | SPI_TRANSFER_MSB),   \
	    .reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),            \
	    .irq_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, irq_gpios, {0}),        \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(                                                 \
	    inst, bgt60tr13c_init, NULL, &bgt60tr13c_data_##inst,              \
	    &bgt60tr13c_config_##inst, POST_KERNEL,                            \
	    CONFIG_BGT60TR13C_INIT_PRIORITY, &bgt60tr13c_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(BGT60TR13C_DEFINE)