#ifndef ZEPHYR_DRIVERS_BGT60TR13C_H_
#define ZEPHYR_DRIVERS_BGT60TR13C_H_

#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

/*
 * BGT60TR13C SPI protocol (from official xensiv_bgt60trxx library):
 *
 * Every register access is a single 32-bit SPI word:
 *   Bit 31..25:  Register address (7 bits)
 *   Bit 24:      Write=1 / Read=0
 *   Bit 23..0:   Data (24 bits)
 *
 * The 32-bit word is byte-reversed (big-endian) before sending on the wire.
 * On read, the response carries 24-bit register data in bits [23:0] and
 * GSR0 status in bits [27:24].
 */

/* ── Register addresses (from xensiv_bgt60trxx_regs.h) ────────── */
#define BGT60TR13C_REG_MAIN 0x00
#define BGT60TR13C_REG_ADC0 0x01
#define BGT60TR13C_REG_CHIP_ID 0x02
#define BGT60TR13C_REG_STAT1 0x03
#define BGT60TR13C_REG_SFCTL 0x06
#define BGT60TR13C_REG_STAT0 0x5D
#define BGT60TR13C_REG_FSTAT 0x5F /* TR13C FIFO status */
#define BGT60TR13C_REG_FIFO 0x60  /* TR13C FIFO data */

/* ── SPI word field masks ─────────────────────────────────────── */
#define BGT60TR13C_SPI_WR_BIT BIT(24)
#define BGT60TR13C_SPI_ADDR_POS 25
#define BGT60TR13C_SPI_ADDR_MSK 0xFE000000UL
#define BGT60TR13C_SPI_DATA_MSK 0x00FFFFFFUL

/* ── MAIN register fields ─────────────────────────────────────── */
#define BGT60TR13C_MAIN_FRAME_START BIT(0)
#define BGT60TR13C_MAIN_RESET_POS 1
#define BGT60TR13C_MAIN_RESET_MSK 0x0000000EUL
#define BGT60TR13C_RESET_SW (0x1 << BGT60TR13C_MAIN_RESET_POS)
#define BGT60TR13C_RESET_FSM (0x2 << BGT60TR13C_MAIN_RESET_POS)
#define BGT60TR13C_RESET_FIFO (0x4 << BGT60TR13C_MAIN_RESET_POS)

/* ── CHIP_ID fields ───────────────────────────────────────────── */
#define BGT60TR13C_CHIP_ID_RF_MSK 0x0000FFUL
#define BGT60TR13C_CHIP_ID_DIG_MSK 0xFFFF00UL
#define BGT60TR13C_CHIP_ID_DIG_POS 8
/* BGT60TR13C: digital_id=3, rf_id=3 → CHIP_ID = 0x000303 */
#define BGT60TR13C_CHIP_ID_VALUE 0x000303UL

/* ── SFCTL fields ─────────────────────────────────────────────── */
#define BGT60TR13C_SFCTL_FIFO_CREF_MSK 0x001FFFUL
#define BGT60TR13C_SFCTL_MISO_HS_MSK BIT(16)
#define BGT60TR13C_SFCTL_LFSR_EN_MSK BIT(17)

/* ── FSTAT fields ─────────────────────────────────────────────── */
#define BGT60TR13C_FSTAT_FILL_STATUS_MSK 0x003FFFUL
#define BGT60TR13C_FSTAT_CLK_NUM_ERR_MSK BIT(17)
#define BGT60TR13C_FSTAT_SPI_BURST_ERR_MSK BIT(18)
#define BGT60TR13C_FSTAT_FUF_ERR_MSK BIT(19)
#define BGT60TR13C_FSTAT_EMPTY_MSK BIT(20)
#define BGT60TR13C_FSTAT_CREF_MSK BIT(21)
#define BGT60TR13C_FSTAT_FULL_MSK BIT(22)
#define BGT60TR13C_FSTAT_FOF_ERR_MSK BIT(23)

/* ── GSR0 fields (returned in bits [27:24] of every SPI read) ── */
#define BGT60TR13C_GSR0_FOU_ERR_MSK 0x01UL
#define BGT60TR13C_GSR0_SPI_BURST_ERR_MSK 0x04UL
#define BGT60TR13C_GSR0_CLK_NUM_ERR_MSK 0x08UL

/* ── SPI burst mode constants ─────────────────────────────────── */
#define BGT60TR13C_SPI_BURST_CMD 0xFF000000UL
#define BGT60TR13C_SPI_BURST_SADR_POS 17
#define BGT60TR13C_SPI_BURST_HDR_BYTES 4

/* ── LFSR test sequence ───────────────────────────────────────── */
#define BGT60TR13C_LFSR_INITIAL_WORD 0x0001U

/* ── FIFO constants ───────────────────────────────────────────── */
#define BGT60TR13C_FIFO_SIZE 8192    /* FIFO words */
#define BGT60TR13C_FIFO_WORD_BYTES 3 /* 2 × 12-bit samples per 3 bytes */
#define BGT60TR13C_SAMPLES_PER_WORD 2

/* ── Driver API ───────────────────────────────────────────────── */
struct bgt60tr13c_api {
	int (*read_reg)(const struct device *dev, uint8_t reg, uint32_t *val);
	int (*write_reg)(const struct device *dev, uint8_t reg, uint32_t val);
	int (*soft_reset)(const struct device *dev, uint32_t reset_type);
	int (*config)(const struct device *dev, const uint32_t *regs,
		      uint32_t len);
	int (*set_fifo_limit)(const struct device *dev, uint32_t num_samples);
	int (*start_frame)(const struct device *dev, bool start);
	int (*get_fifo_status)(const struct device *dev, uint32_t *status);
	int (*get_fifo_data)(const struct device *dev, uint16_t *data,
			     uint32_t num_samples);
	int (*enable_test_mode)(const struct device *dev, bool enable);
};

/* ── Device configuration (from devicetree) ───────────────────── */
struct bgt60tr13c_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec reset_gpio;
};

/* ── Device runtime data ──────────────────────────────────────── */
struct bgt60tr13c_runtime_data {
	bool initialized;
};

/**
 * Generate the next LFSR test word.
 * Polynomial: x^12 + x^11 + x^10 + x^4 + 1
 * Seed with BGT60TR13C_LFSR_INITIAL_WORD (0x0001).
 */
static inline uint16_t bgt60tr13c_next_test_word(uint16_t cur)
{
	return (cur >> 1) |
	       (((cur << 11) ^ (cur << 10) ^ (cur << 9) ^ (cur << 3)) &
		0x0800U);
}

#endif /* ZEPHYR_DRIVERS_BGT60TR13C_H_ */