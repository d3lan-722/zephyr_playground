/**
 * @file pmu.c
 * @brief Zephyr driver for the generic Infineon PMU / DVFS block.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Memory-mapped driver for the sample generic Power Modeling Unit
 * modeled in SystemC/TLM. Bound to the "infineon,pmu-dvfs" DT
 * compatible.
 */
#define DT_DRV_COMPAT infineon_pmu_dvfs

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>

#include "pmu.h"

LOG_MODULE_REGISTER(pmu_dvfs, CONFIG_PMU_DVFS_LOG_LEVEL);

/** @brief Immutable per-instance data resolved from devicetree. */
struct pmu_config {
	uintptr_t base;			/**< MMIO base address. */
	uint32_t reg_size;		/**< MMIO window size in bytes. */
	uint8_t num_pstates;		/**< Number of active P-states. */
	uint16_t transition_timeout_us; /**< DVFSCTRL.TRANSTIMEOUT value. */
	bool init_enable;		/**< Enable DVFS at end of init. */
	uint8_t init_pstate;		/**< First target when init-enable. */
	const uint32_t *pstate_table;	/**< Flat (freq,volt) pairs or NULL. */
	uint32_t pstate_table_len;	/**< Number of uint32 entries. */
};

/** @brief Mutable runtime state. */
struct pmu_data {
	bool initialized;
};

/* ─────────────────────────────────────────────────────────────────
 * Low-level helpers
 * ───────────────────────────────────────────────────────────────── */

/**
 * @brief Validate a caller-supplied register offset.
 * @return 0 if @p offset is 4-aligned and within the MMIO window,
 *         -EINVAL otherwise.
 */
static int pmu_reg_check(const struct pmu_config *cfg, uint32_t offset)
{
	if (offset & 0x3U) {
		return -EINVAL;
	}
	if (offset >= cfg->reg_size) {
		return -EINVAL;
	}
	return 0;
}

/**
 * @brief Read-modify-write on DVFSCTRL.
 * @param base MMIO base of the PMU instance.
 * @param mask Bits allowed to change (register-wide mask).
 * @param val  New bit values, aligned inside @p mask.
 */
static void pmu_ctrl_rmw(uintptr_t base, uint32_t mask, uint32_t val)
{
	uint32_t reg = sys_read32(base + PMU_REG_DVFSCTRL);

	reg = (reg & ~mask) | (val & mask);
	sys_write32(reg, base + PMU_REG_DVFSCTRL);
}

/* ─────────────────────────────────────────────────────────────────
 * Raw register access
 * ───────────────────────────────────────────────────────────────── */

/**
 * @brief API: raw 32-bit read from an offset inside the PMU window.
 * @return 0 on success, -EINVAL on misaligned/out-of-range offset
 *         or if @p val is NULL.
 */
static int pmu_api_read_reg(const struct device *dev, uint32_t offset,
			    uint32_t *val)
{
	const struct pmu_config *cfg = dev->config;
	int ret = pmu_reg_check(cfg, offset);

	if (ret < 0 || val == NULL) {
		return ret ? ret : -EINVAL;
	}
	*val = sys_read32(cfg->base + offset);
	return 0;
}

/**
 * @brief API: raw 32-bit write to an offset inside the PMU window.
 * @return 0 on success, -EINVAL on misaligned/out-of-range offset.
 */
static int pmu_api_write_reg(const struct device *dev, uint32_t offset,
			     uint32_t val)
{
	const struct pmu_config *cfg = dev->config;
	int ret = pmu_reg_check(cfg, offset);

	if (ret < 0) {
		return ret;
	}
	sys_write32(val, cfg->base + offset);
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * P-state table
 * ───────────────────────────────────────────────────────────────── */

/**
 * @brief API: program DVFSPSTATETABLE_PSTATE[idx] with (freq, volt).
 *
 * Layout: bits [15:0] FREQ (MHz), bits [31:16] VOLTAGE (mV).
 *
 * @return 0 on success, -EINVAL if @p idx >= PMU_MAX_PSTATES.
 */
static int pmu_api_set_pstate_entry(const struct device *dev, uint8_t idx,
				    uint16_t freq_mhz, uint16_t voltage_mv)
{
	const struct pmu_config *cfg = dev->config;

	if (idx >= PMU_MAX_PSTATES) {
		return -EINVAL;
	}

	uint32_t val = ((uint32_t)voltage_mv << PMU_PSTATE_VOLTAGE_POS) |
		       ((uint32_t)freq_mhz & PMU_PSTATE_FREQ_MSK);

	sys_write32(val, cfg->base + PMU_REG_PSTATE_TABLE(idx));
	return 0;
}

/**
 * @brief API: read DVFSPSTATETABLE_PSTATE[idx] and decode it.
 * @return 0 on success, -EINVAL on bad index or NULL @p entry.
 */
static int pmu_api_get_pstate_entry(const struct device *dev, uint8_t idx,
				    struct pmu_pstate *entry)
{
	const struct pmu_config *cfg = dev->config;

	if (idx >= PMU_MAX_PSTATES || entry == NULL) {
		return -EINVAL;
	}

	uint32_t val = sys_read32(cfg->base + PMU_REG_PSTATE_TABLE(idx));

	entry->freq_mhz = val & PMU_PSTATE_FREQ_MSK;
	entry->voltage_mv =
	    (val & PMU_PSTATE_VOLTAGE_MSK) >> PMU_PSTATE_VOLTAGE_POS;
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Control-register helpers
 * ───────────────────────────────────────────────────────────────── */

/**
 * @brief API: update DVFSCTRL.NUMSTATES.
 * @return 0 on success, -EINVAL if @p num is 0 or > PMU_MAX_PSTATES.
 */
static int pmu_api_set_num_states(const struct device *dev, uint8_t num)
{
	const struct pmu_config *cfg = dev->config;

	if (num == 0U || num > PMU_MAX_PSTATES) {
		return -EINVAL;
	}
	pmu_ctrl_rmw(cfg->base, PMU_DVFSCTRL_NUMSTATES_MSK,
		     (uint32_t)num << PMU_DVFSCTRL_NUMSTATES_POS);
	return 0;
}

/** @brief API: update DVFSCTRL.TRANSTIMEOUT (transition timeout, us). */
static int pmu_api_set_timeout_us(const struct device *dev, uint16_t us)
{
	const struct pmu_config *cfg = dev->config;

	pmu_ctrl_rmw(cfg->base, PMU_DVFSCTRL_TRANSTIMEOUT_MSK,
		     (uint32_t)us << PMU_DVFSCTRL_TRANSTIMEOUT_POS);
	return 0;
}

/** @brief API: update DVFSCTRL.MODESEL (register- vs signal-driven). */
static int pmu_api_set_mode(const struct device *dev, enum pmu_mode mode)
{
	const struct pmu_config *cfg = dev->config;

	pmu_ctrl_rmw(cfg->base, PMU_DVFSCTRL_MODESEL_MSK,
		     (mode == PMU_MODE_SIGNAL) ? PMU_DVFSCTRL_MODESEL_MSK : 0U);
	return 0;
}

/** @brief API: set or clear DVFSCTRL.DVFSEN. */
static int pmu_api_enable(const struct device *dev, bool enable)
{
	const struct pmu_config *cfg = dev->config;

	pmu_ctrl_rmw(cfg->base, PMU_DVFSCTRL_DVFSEN_MSK,
		     enable ? PMU_DVFSCTRL_DVFSEN_MSK : 0U);
	return 0;
}

/**
 * @brief API: write DVFSPSTATEREQ.TARGETPSTATE.
 *
 * The hardware ignores this write when MODESEL = PMU_MODE_SIGNAL.
 *
 * @return 0 on success, -EINVAL if @p idx >= PMU_MAX_PSTATES.
 */
static int pmu_api_request_pstate(const struct device *dev, uint8_t idx)
{
	const struct pmu_config *cfg = dev->config;

	if (idx >= PMU_MAX_PSTATES) {
		return -EINVAL;
	}
	sys_write32((uint32_t)idx & PMU_DVFSPSTATEREQ_TARGET_MSK,
		    cfg->base + PMU_REG_DVFSPSTATEREQ);
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Status / telemetry
 * ───────────────────────────────────────────────────────────────── */

/** @brief API: read DVFSSTATUS and decode into @p st. */
static int pmu_api_get_status(const struct device *dev, struct pmu_status *st)
{
	const struct pmu_config *cfg = dev->config;

	if (st == NULL) {
		return -EINVAL;
	}

	uint32_t reg = sys_read32(cfg->base + PMU_REG_DVFSSTATUS);

	st->curr_pstate = reg & PMU_DVFSSTATUS_CURRPSTATE_MSK;
	st->trans_pending = (reg & PMU_DVFSSTATUS_TRANSPENDING) != 0U;
	st->busy = (reg & PMU_DVFSSTATUS_BUSY) != 0U;
	st->trans_phase = (reg & PMU_DVFSSTATUS_TRANSPHASE_MSK) >>
			  PMU_DVFSSTATUS_TRANSPHASE_POS;
	st->error = (reg & PMU_DVFSSTATUS_ERROR) != 0U;
	return 0;
}

/** @brief API: read CURRVOLTMV (current output voltage in mV). */
static int pmu_api_get_voltage_mv(const struct device *dev, uint32_t *mv)
{
	const struct pmu_config *cfg = dev->config;

	if (mv == NULL) {
		return -EINVAL;
	}
	*mv = sys_read32(cfg->base + PMU_REG_CURRVOLTMV);
	return 0;
}

/** @brief API: read CURRFREQMHZ (current output frequency in MHz). */
static int pmu_api_get_freq_mhz(const struct device *dev, uint32_t *mhz)
{
	const struct pmu_config *cfg = dev->config;

	if (mhz == NULL) {
		return -EINVAL;
	}
	*mhz = sys_read32(cfg->base + PMU_REG_CURRFREQMHZ);
	return 0;
}

/**
 * @brief API: busy-poll DVFSSTATUS until BUSY and TRANSPENDING clear.
 *
 * @retval 0          Transition completed cleanly.
 * @retval -EIO       Transition finished with DVFSSTATUS.ERROR set.
 * @retval -ETIMEDOUT No completion within @p timeout.
 */
static int pmu_api_wait_idle(const struct device *dev, k_timeout_t timeout)
{
	const struct pmu_config *cfg = dev->config;
	k_timepoint_t end = sys_timepoint_calc(timeout);

	for (;;) {
		uint32_t reg = sys_read32(cfg->base + PMU_REG_DVFSSTATUS);

		if ((reg & (PMU_DVFSSTATUS_BUSY |
			    PMU_DVFSSTATUS_TRANSPENDING)) == 0U) {
			return (reg & PMU_DVFSSTATUS_ERROR) ? -EIO : 0;
		}
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
		k_busy_wait(1);
	}
}

/**
 * @brief API: clear DVFSSTATUS.ERROR (write-1-to-clear).
 *
 * ERROR is the only SW-writable bit in DVFSSTATUS per the PMU spec.
 */
static int pmu_api_clear_error(const struct device *dev)
{
	const struct pmu_config *cfg = dev->config;

	sys_write32(PMU_DVFSSTATUS_ERROR, cfg->base + PMU_REG_DVFSSTATUS);
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Init
 * ───────────────────────────────────────────────────────────────── */

/**
 * @brief Zephyr device init hook.
 *
 * Executes the bring-up sequence recommended by the PMU spec:
 *   1. Disable DVFS.
 *   2. Preload the P-state table from devicetree (if provided).
 *   3. Program NUMSTATES + TRANSTIMEOUT.
 *   4. If `init-enable` is set: write the initial target and set
 *      DVFSCTRL.DVFSEN=1.
 */
static int pmu_init(const struct device *dev)
{
	const struct pmu_config *cfg = dev->config;
	struct pmu_data *data = dev->data;

	LOG_INF("PMU init: base=0x%08lx size=0x%x num_pstates=%u",
		(unsigned long)cfg->base, cfg->reg_size, cfg->num_pstates);

	sys_write32(0U, cfg->base + PMU_REG_DVFSCTRL);
	sys_write32(0U, cfg->base + PMU_REG_DVFSPSTATEREQ);

	if (cfg->pstate_table != NULL) {
		if (cfg->pstate_table_len != 2U * cfg->num_pstates) {
			LOG_ERR("pstate-table length %u != 2 * num-pstates %u",
				cfg->pstate_table_len, cfg->num_pstates);
			return -EINVAL;
		}
		for (uint8_t i = 0; i < cfg->num_pstates; i++) {
			uint16_t freq = cfg->pstate_table[2U * i];
			uint16_t volt = cfg->pstate_table[2U * i + 1U];
			uint32_t val =
			    ((uint32_t)volt << PMU_PSTATE_VOLTAGE_POS) |
			    ((uint32_t)freq & PMU_PSTATE_FREQ_MSK);

			sys_write32(val, cfg->base + PMU_REG_PSTATE_TABLE(i));
			LOG_DBG("  P%u: %u MHz / %u mV", i, freq, volt);
		}
	}

	/* MODESEL stays 0 (register-driven), DVFSEN stays 0. */
	uint32_t ctrl =
	    ((uint32_t)cfg->num_pstates << PMU_DVFSCTRL_NUMSTATES_POS) |
	    ((uint32_t)cfg->transition_timeout_us
	     << PMU_DVFSCTRL_TRANSTIMEOUT_POS);

	sys_write32(ctrl, cfg->base + PMU_REG_DVFSCTRL);

	if (cfg->init_enable) {
		if (cfg->pstate_table == NULL) {
			LOG_ERR("init-enable set but no pstate-table");
			return -EINVAL;
		}
		if (cfg->init_pstate >= cfg->num_pstates) {
			LOG_ERR("init-pstate %u >= num-pstates %u",
				cfg->init_pstate, cfg->num_pstates);
			return -EINVAL;
		}
		sys_write32((uint32_t)cfg->init_pstate,
			    cfg->base + PMU_REG_DVFSPSTATEREQ);
		ctrl |= PMU_DVFSCTRL_DVFSEN_MSK;
		sys_write32(ctrl, cfg->base + PMU_REG_DVFSCTRL);
		LOG_INF("DVFS enabled, initial P-state = %u", cfg->init_pstate);
	}

	data->initialized = true;
	return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * API vtable + devicetree instantiation
 * ───────────────────────────────────────────────────────────────── */

static const struct pmu_driver_api pmu_api_funcs = {
    .read_reg = pmu_api_read_reg,
    .write_reg = pmu_api_write_reg,
    .set_pstate_entry = pmu_api_set_pstate_entry,
    .get_pstate_entry = pmu_api_get_pstate_entry,
    .set_num_states = pmu_api_set_num_states,
    .set_timeout_us = pmu_api_set_timeout_us,
    .set_mode = pmu_api_set_mode,
    .enable = pmu_api_enable,
    .request_pstate = pmu_api_request_pstate,
    .get_status = pmu_api_get_status,
    .get_voltage_mv = pmu_api_get_voltage_mv,
    .get_freq_mhz = pmu_api_get_freq_mhz,
    .wait_idle = pmu_api_wait_idle,
    .clear_error = pmu_api_clear_error,
};

#define PMU_PSTATE_TABLE_DEFINE(inst)                                          \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(inst, pstate_table),                  \
		   (static const uint32_t pmu_pstate_table_##inst[] =          \
			DT_INST_PROP(inst, pstate_table);))

#define PMU_PSTATE_TABLE_PTR(inst)                                             \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, pstate_table),                 \
		    (pmu_pstate_table_##inst), (NULL))

#define PMU_PSTATE_TABLE_LEN(inst)                                             \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, pstate_table),                 \
		    (ARRAY_SIZE(pmu_pstate_table_##inst)), (0U))

#define PMU_DEFINE(inst)                                                       \
	PMU_PSTATE_TABLE_DEFINE(inst)                                          \
	static struct pmu_data pmu_data_##inst;                                \
	static const struct pmu_config pmu_config_##inst = {                   \
	    .base = DT_INST_REG_ADDR(inst),                                    \
	    .reg_size = DT_INST_REG_SIZE(inst),                                \
	    .num_pstates = DT_INST_PROP(inst, num_pstates),                    \
	    .transition_timeout_us =                                           \
		DT_INST_PROP(inst, transition_timeout_us),                     \
	    .init_enable = DT_INST_PROP(inst, init_enable),                    \
	    .init_pstate = DT_INST_PROP(inst, init_pstate),                    \
	    .pstate_table = PMU_PSTATE_TABLE_PTR(inst),                        \
	    .pstate_table_len = PMU_PSTATE_TABLE_LEN(inst),                    \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(inst, pmu_init, NULL, &pmu_data_##inst,          \
			      &pmu_config_##inst, POST_KERNEL,                 \
			      CONFIG_PMU_DVFS_INIT_PRIORITY, &pmu_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(PMU_DEFINE)
