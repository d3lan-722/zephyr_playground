/**
 * @file pmu.h
 * @brief Public interface for the generic Infineon PMU / DVFS driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register offsets and bitfield masks are derived from the vendor
 * SFR headers under fw_register_headers/:
 *   - Ifx_PMU struct layout (register offsets via offsetof)
 *   - IFX_PMU_*_MSK / _OFF (field-local mask and bit offset)
 */
#ifndef ZEPHYR_DRIVERS_PMU_H_
#define ZEPHYR_DRIVERS_PMU_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "fw_register_headers/IfxPmu_regdef.h"
#include "fw_register_headers/IfxPmu_bf.h"

/**
 * @defgroup pmu_regs PMU register offsets
 * Byte offsets of each register inside the 64 KB PMU MMIO window,
 * derived from the Ifx_PMU struct so the driver stays in sync with
 * the vendor headers.
 * @{
 */
#define PMU_REG_DVFSCTRL offsetof(Ifx_PMU, DVFSCTRL)
#define PMU_REG_DVFSPSTATEREQ offsetof(Ifx_PMU, DVFSPSTATEREQ)
#define PMU_REG_DVFSSTATUS offsetof(Ifx_PMU, DVFSSTATUS)
#define PMU_REG_CURRVOLTMV offsetof(Ifx_PMU, CURRVOLTMV)
#define PMU_REG_CURRFREQMHZ offsetof(Ifx_PMU, CURRFREQMHZ)
#define PMU_REG_PSTATE_TABLE_BASE offsetof(Ifx_PMU, DVFSPSTATETABLE_PSTATE[0])
#define PMU_REG_PSTATE_TABLE(i)                                                \
	(PMU_REG_PSTATE_TABLE_BASE +                                           \
	 sizeof(Ifx_PMU_DVFSPSTATETABLE_PSTATE) * (i))
/** @} */

/** Maximum number of P-state table entries the hardware supports. */
#define PMU_MAX_PSTATES                                                        \
	((uint8_t)ARRAY_SIZE(((Ifx_PMU *)0)->DVFSPSTATETABLE_PSTATE))

/**
 * @brief Build a full 32-bit register mask from an IFX field descriptor.
 *
 * The vendor headers give the field-local mask (`_MSK`) and its LSB
 * offset (`_OFF`) separately. Most driver code needs the shifted,
 * register-wide mask.
 *
 * Usage: `PMU_FIELD_MSK(DVFSCTRL_NUMSTATES)` ->
 *        `IFX_PMU_DVFSCTRL_NUMSTATES_MSK << IFX_PMU_DVFSCTRL_NUMSTATES_OFF`
 */
#define PMU_FIELD_MSK(field)                                                   \
	((uint32_t)IFX_PMU_##field##_MSK << IFX_PMU_##field##_OFF)

/** @brief Bit offset (LSB position) of the named field. */
#define PMU_FIELD_POS(field) ((uint32_t)IFX_PMU_##field##_OFF)

/**
 * @defgroup pmu_dvfsctrl DVFSCTRL bitfields
 * @{
 */
#define PMU_DVFSCTRL_DVFSEN_MSK PMU_FIELD_MSK(DVFSCTRL_DVFSEN)
#define PMU_DVFSCTRL_MODESEL_MSK PMU_FIELD_MSK(DVFSCTRL_MODESEL)
#define PMU_DVFSCTRL_NUMSTATES_POS PMU_FIELD_POS(DVFSCTRL_NUMSTATES)
#define PMU_DVFSCTRL_NUMSTATES_MSK PMU_FIELD_MSK(DVFSCTRL_NUMSTATES)
#define PMU_DVFSCTRL_TRANSTIMEOUT_POS PMU_FIELD_POS(DVFSCTRL_TRANSTIMEOUT)
#define PMU_DVFSCTRL_TRANSTIMEOUT_MSK PMU_FIELD_MSK(DVFSCTRL_TRANSTIMEOUT)
/** @} */

/** @brief DVFSPSTATEREQ.TARGETPSTATE mask (register-wide). */
#define PMU_DVFSPSTATEREQ_TARGET_MSK PMU_FIELD_MSK(DVFSPSTATEREQ_TARGETPSTATE)

/**
 * @defgroup pmu_dvfsstatus DVFSSTATUS bitfields
 * @{
 */
#define PMU_DVFSSTATUS_CURRPSTATE_MSK PMU_FIELD_MSK(DVFSSTATUS_CURRPSTATE)
#define PMU_DVFSSTATUS_TRANSPENDING PMU_FIELD_MSK(DVFSSTATUS_TRANSPENDING)
#define PMU_DVFSSTATUS_BUSY PMU_FIELD_MSK(DVFSSTATUS_BUSY)
#define PMU_DVFSSTATUS_TRANSPHASE_POS PMU_FIELD_POS(DVFSSTATUS_TRANSPHASE)
#define PMU_DVFSSTATUS_TRANSPHASE_MSK PMU_FIELD_MSK(DVFSSTATUS_TRANSPHASE)
#define PMU_DVFSSTATUS_ERROR PMU_FIELD_MSK(DVFSSTATUS_ERROR)
/** @} */

/**
 * @defgroup pmu_pstate_table DVFSPSTATETABLE_PSTATE[i] bitfields
 * @{
 */
#define PMU_PSTATE_FREQ_MSK PMU_FIELD_MSK(DVFSPSTATETABLE_PSTATE_FREQ)
#define PMU_PSTATE_VOLTAGE_POS PMU_FIELD_POS(DVFSPSTATETABLE_PSTATE_VOLTAGE)
#define PMU_PSTATE_VOLTAGE_MSK PMU_FIELD_MSK(DVFSPSTATETABLE_PSTATE_VOLTAGE)
/** @} */

/** @brief DVFS target-selection source (DVFSCTRL.MODESEL). */
enum pmu_mode {
	PMU_MODE_REGISTER = 0, /**< Target from DVFSPSTATEREQ.TARGETPSTATE. */
	PMU_MODE_SIGNAL = 1,   /**< Target from loadLevelIn_ signal. */
};

/** @brief Encoded value of DVFSSTATUS.TRANSPHASE. */
enum pmu_trans_phase {
	PMU_PHASE_IDLE = 0,
	PMU_PHASE_VOLT_UP = 1,
	PMU_PHASE_FREQ_UP = 2,
	PMU_PHASE_VOLT_DOWN = 3,
	PMU_PHASE_FREQ_DOWN = 4,
};

/** @brief Decoded snapshot of DVFSSTATUS. */
struct pmu_status {
	uint8_t curr_pstate; /**< DVFSSTATUS.CURRPSTATE (0..NUMSTATES-1). */
	bool trans_pending;  /**< DVFSSTATUS.TRANSPENDING. */
	bool busy;	     /**< DVFSSTATUS.BUSY. */
	uint8_t trans_phase; /**< See @ref pmu_trans_phase. */
	bool error;	     /**< DVFSSTATUS.ERROR (sticky, SW-clearable). */
};

/** @brief One entry of the DVFSPSTATETABLE_PSTATE table. */
struct pmu_pstate {
	uint16_t freq_mhz;   /**< Frequency in MHz (16-bit). */
	uint16_t voltage_mv; /**< Voltage in mV (16-bit). */
};

/**
 * @brief Driver API vtable installed as `struct device::api`.
 *
 * Applications should not call these directly; use the `pmu_*()`
 * inline wrappers below.
 */
struct pmu_driver_api {
	int (*read_reg)(const struct device *dev, uint32_t offset,
			uint32_t *val);
	int (*write_reg)(const struct device *dev, uint32_t offset,
			 uint32_t val);
	int (*set_pstate_entry)(const struct device *dev, uint8_t idx,
				uint16_t freq_mhz, uint16_t voltage_mv);
	int (*get_pstate_entry)(const struct device *dev, uint8_t idx,
				struct pmu_pstate *entry);
	int (*set_num_states)(const struct device *dev, uint8_t num);
	int (*set_timeout_us)(const struct device *dev, uint16_t us);
	int (*set_mode)(const struct device *dev, enum pmu_mode mode);
	int (*enable)(const struct device *dev, bool enable);
	int (*request_pstate)(const struct device *dev, uint8_t idx);
	int (*get_status)(const struct device *dev, struct pmu_status *st);
	int (*get_voltage_mv)(const struct device *dev, uint32_t *mv);
	int (*get_freq_mhz)(const struct device *dev, uint32_t *mhz);
	int (*wait_idle)(const struct device *dev, k_timeout_t timeout);
	int (*clear_error)(const struct device *dev);
};

/**
 * @brief Raw 32-bit register read from the PMU MMIO window.
 * @param dev PMU device.
 * @param off Byte offset (must be 4-aligned and < reg-window size).
 * @param[out] val Register value.
 * @return 0 on success, -EINVAL on bad offset or null pointer.
 */
static inline int pmu_read_reg(const struct device *dev, uint32_t off,
			       uint32_t *val)
{
	const struct pmu_driver_api *api = dev->api;
	return api->read_reg(dev, off, val);
}

/**
 * @brief Raw 32-bit register write into the PMU MMIO window.
 * @param dev PMU device.
 * @param off Byte offset (must be 4-aligned and < reg-window size).
 * @param val Value to store.
 * @return 0 on success, -EINVAL on bad offset.
 */
static inline int pmu_write_reg(const struct device *dev, uint32_t off,
				uint32_t val)
{
	const struct pmu_driver_api *api = dev->api;
	return api->write_reg(dev, off, val);
}

/**
 * @brief Program one entry of the DVFSPSTATETABLE_PSTATE table.
 * @param dev PMU device.
 * @param idx Table index in [0, PMU_MAX_PSTATES).
 * @param freq_mhz Frequency in MHz.
 * @param voltage_mv Voltage in mV.
 * @return 0 on success, -EINVAL on bad index.
 */
static inline int pmu_set_pstate_entry(const struct device *dev, uint8_t idx,
				       uint16_t freq_mhz, uint16_t voltage_mv)
{
	const struct pmu_driver_api *api = dev->api;
	return api->set_pstate_entry(dev, idx, freq_mhz, voltage_mv);
}

/**
 * @brief Read one entry of the DVFSPSTATETABLE_PSTATE table.
 * @param dev PMU device.
 * @param idx Table index in [0, PMU_MAX_PSTATES).
 * @param[out] entry Decoded frequency and voltage.
 * @return 0 on success, -EINVAL on bad index or null pointer.
 */
static inline int pmu_get_pstate_entry(const struct device *dev, uint8_t idx,
				       struct pmu_pstate *entry)
{
	const struct pmu_driver_api *api = dev->api;
	return api->get_pstate_entry(dev, idx, entry);
}

/**
 * @brief Set DVFSCTRL.NUMSTATES (number of active
 * P-states, 1..PMU_MAX_PSTATES).
 * @return 0 on success, -EINVAL if @p num is 0 or > PMU_MAX_PSTATES.
 */
static inline int pmu_set_num_states(const struct device *dev, uint8_t num)
{
	const struct pmu_driver_api *api = dev->api;
	return api->set_num_states(dev, num);
}

/** @brief Set DVFSCTRL.TRANSTIMEOUT (transition timeout in microseconds). */
static inline int pmu_set_timeout_us(const struct device *dev, uint16_t us)
{
	const struct pmu_driver_api *api = dev->api;
	return api->set_timeout_us(dev, us);
}

/** @brief Select target source: register (DVFSPSTATEREQ) or signal
 * (loadLevelIn_). */
static inline int pmu_set_mode(const struct device *dev, enum pmu_mode mode)
{
	const struct pmu_driver_api *api = dev->api;
	return api->set_mode(dev, mode);
}

/** @brief Set or clear DVFSCTRL.DVFSEN. Clearing forces reset outputs. */
static inline int pmu_enable(const struct device *dev, bool enable)
{
	const struct pmu_driver_api *api = dev->api;
	return api->enable(dev, enable);
}

/**
 * @brief Write DVFSPSTATEREQ.TARGETPSTATE in register-driven mode.
 * @param dev PMU device.
 * @param idx Requested target P-state, 0..PMU_MAX_PSTATES-1.
 * @return 0 on success, -EINVAL on bad index.
 *
 * Ignored by the hardware when MODESEL = PMU_MODE_SIGNAL.
 */
static inline int pmu_request_pstate(const struct device *dev, uint8_t idx)
{
	const struct pmu_driver_api *api = dev->api;
	return api->request_pstate(dev, idx);
}

/** @brief Read and decode DVFSSTATUS into @p st. */
static inline int pmu_get_status(const struct device *dev,
				 struct pmu_status *st)
{
	const struct pmu_driver_api *api = dev->api;
	return api->get_status(dev, st);
}

/** @brief Read CURRVOLTMV (current output voltage, mV). */
static inline int pmu_get_voltage_mv(const struct device *dev, uint32_t *mv)
{
	const struct pmu_driver_api *api = dev->api;
	return api->get_voltage_mv(dev, mv);
}

/** @brief Read CURRFREQMHZ (current output frequency, MHz). */
static inline int pmu_get_freq_mhz(const struct device *dev, uint32_t *mhz)
{
	const struct pmu_driver_api *api = dev->api;
	return api->get_freq_mhz(dev, mhz);
}

/**
 * @brief Poll DVFSSTATUS until BUSY and TRANSPENDING both clear.
 * @param dev PMU device.
 * @param timeout How long to poll.
 * @retval 0        Transition completed cleanly.
 * @retval -EIO     Transition finished with DVFSSTATUS.ERROR set.
 * @retval -ETIMEDOUT No completion within @p timeout.
 */
static inline int pmu_wait_idle(const struct device *dev, k_timeout_t timeout)
{
	const struct pmu_driver_api *api = dev->api;
	return api->wait_idle(dev, timeout);
}

/** @brief Clear the sticky DVFSSTATUS.ERROR bit. */
static inline int pmu_clear_error(const struct device *dev)
{
	const struct pmu_driver_api *api = dev->api;
	return api->clear_error(dev);
}

#endif /* ZEPHYR_DRIVERS_PMU_H_ */
