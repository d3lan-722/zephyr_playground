/*
 * Copyright 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/drivers/clock_management/clock_driver.h>
#include <zephyr/drivers/clock_management/clock_helpers.h>
#include <zephyr/drivers/clock_control/clock_control_ifx_cat1.h>
#include <zephyr/dt-bindings/clock/ifx_clock_source_common.h>

#include "cy_device.h"
#include <cy_sysclk.h>
#include "infineon_clock_management_header.h"

#define DT_DRV_COMPAT infineon_clock_pll

volatile bool flag = false;
volatile uint32_t pll_enable_value = 5000;
struct clock_pll_config {
	STANDARD_CLK_SUBSYS_DATA_DEFINE
	uint32_t rate;
	uint8_t system_clock;
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(dpll_hp))
	cy_stc_dpll_hp_config_t dpll_hp_config;
#endif
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(dpll_lp0)) ||                                             \
	DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(dpll_lp1))
	cy_stc_dpll_lp_config_t dpll_lp_config;
#endif
};

static clock_freq_t pll_recalc_rate(const struct clk *clk_hw, clock_freq_t parent_rate){
	const struct clock_pll_config *config = clk_hw->hw_data;
	uint32_t pll_number = (config->system_clock == IFX_DPLL250_0) ? SRSS_DPLL_LP_0_PATH_NUM
                                        		              : SRSS_DPLL_LP_1_PATH_NUM;
	return (clock_freq_t)Cy_SysClk_DpllLpGetFrequency(pll_number);
}

static clock_freq_t pll_configure_recalc(const struct clk *clk_hw, const void *data, clock_freq_t parent_rate)
{
	
	const ifx_dpll_lp_spec *pll_data = (const ifx_dpll_lp_spec *)data;	
	uint32_t feedback_div = pll_data->fb_div;
	uint32_t reference_div = pll_data->ref_div;
	uint32_t out_div = pll_data->output_div;

	uint64_t fout = ((uint64_t)parent_rate * feedback_div)/((uint64_t)reference_div * out_div);

	return (clock_freq_t)fout;
}

static int pll_lp_set(const struct clk *clk_hw, const void *data)
{
	const struct clock_pll_config *config = clk_hw->hw_data;
	const ifx_dpll_lp_spec *spec = (const ifx_dpll_lp_spec *)data;
	
	cy_stc_dpll_lp_config_t lp_config = {
		.feedbackDiv = spec->fb_div,
		.referenceDiv = spec->ref_div,
		.outputDiv = spec->output_div,
		.pllDcoMode = spec->dco_mode,
		.outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,
		.fracDiv = spec->fraction_div,
		.fracDitherEn = false,                                                             \
		.fracEn = true,                                                                    \
		.dcoCode = 0xFU,                                                                   \
		.kiInt = 0xAU,                                                                     \
		.kiFrac = 0xBU,                                                                    \
		.kiSscg = 0x7U,                                                                    \
		.kpInt = 0x8U,                                                                     \
		.kpFrac = 0x9U,                                                                    \
		.kpSscg = 0x7U, 
	};

	cy_stc_pll_manual_config_t pll_config = {
    		.lpPllCfg = &lp_config,
	};

	uint32_t pll_path = (config->system_clock == IFX_DPLL250_0) ? SRSS_DPLL_LP_0_PATH_NUM
                                        		            : SRSS_DPLL_LP_1_PATH_NUM;
	if (Cy_SysClk_DpllLpIsEnabled(pll_path)) {
		
    		return 0;
	}
	Cy_SysClk_DpllLpDisable(pll_path);
	
	
	if (Cy_SysClk_DpllLpManualConfigure(pll_path, &pll_config) != CY_SYSCLK_SUCCESS) {
   		return -EIO;
	}

	if (Cy_SysClk_DpllLpEnable(pll_path, 10000U) != CY_SYSCLK_SUCCESS) {
    		return -EIO;
	}
	
	return 0;
}


static int pll_configure(const struct clk *clk_hw, const void *data){
	int ret;
	ret = pll_lp_set(clk_hw, data);
	if (ret < 0) {
		return -EIO;
	}
	return ret;
}

static int pll_on_off(const struct clk *clk_hw, bool on)
{
	const struct clock_pll_config *config = clk_hw->hw_data;
	uint32_t pll_path = (config->system_clock == IFX_DPLL250_0) ? SRSS_DPLL_LP_0_PATH_NUM
                                        		            : SRSS_DPLL_LP_1_PATH_NUM;

	if (on) {
		if (Cy_SysClk_DpllLpEnable(pll_path, 10000U) != CY_SYSCLK_SUCCESS) {
			flag = true;
			return -EIO;
		}
	} else {
		Cy_SysClk_DpllLpDisable(pll_path);
		
	}
	return 0;
}

/*clock driver API implementation*/
const struct clock_management_standard_api ifx_pll_api = {
	.shared = {
		.configure = pll_configure,
		.on_off = pll_on_off,
	},
	.recalc_rate = pll_recalc_rate,
#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
	.configure_recalc = pll_configure_recalc,
#endif
};

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(dpll_hp))
#define DPLL_HP_INIT(n)                                                                            \
	.dpll_hp_config = {                                                                        \
		.pDiv = DT_INST_PROP_OR(n, div_p, 0),                                              \
		.nDiv = DT_INST_PROP_OR(n, div_n, 0),                                              \
		.kDiv = DT_INST_PROP_OR(n, div_k, 0),                                              \
		.nDivFract = DT_INST_PROP_OR(n, fraction_div, 0),                                  \
		.freqModeSel = ((cy_en_wait_mode_select_t)DT_INST_PROP_OR(n, freq_mode_sel, 0)),   \
		.ivrTrim = 0x8U,                                                                   \
		.clkrSel = 0x1U,                                                                   \
		.alphaCoarse = 0xCU,                                                               \
		.betaCoarse = 0x5U,                                                                \
		.flockThresh = DT_INST_PROP_OR(n, flock_enable_threshold, 0),                      \
		.flockWait = 0x6U,                                                                 \
		.flockLkThres = 0x7U,                                                              \
		.flockLkWait = 0x4U,                                                               \
		.alphaExt = 0x14U,                                                                 \
		.betaExt = DT_INST_PROP_OR(n, lf_beta_value, 0),                                   \
		.lfEn = 0x1U,                                                                      \
		.dcEn = 0x1U,                                                                      \
		.outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,                                        \
	},
#else
#define DPLL_HP_INIT(n)
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(dpll_lp0)) ||                                             \
	DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(dpll_lp1))
#define DPLL_LP_INIT(n)                                                                            \
	.dpll_lp_config = {                                                                        \
		.feedbackDiv = DT_INST_PROP_OR(n, feedback_div, 0),                                \
		.referenceDiv = DT_INST_PROP_OR(n, reference_div, 0),                              \
		.outputDiv = DT_INST_PROP_OR(n, output_div, 0),                                    \
		.pllDcoMode = DT_INST_PROP_OR(n, dco_mode_enable, false),                          \
		.outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,                                        \
		.fracDiv = DT_INST_PROP_OR(n, fraction_div, 0),                                    \
		.fracDitherEn = false,                                                             \
		.fracEn = true,                                                                    \
		.dcoCode = 0xFU,                                                                   \
		.kiInt = 0xAU,                                                                     \
		.kiFrac = 0xBU,                                                                    \
		.kiSscg = 0x7U,                                                                    \
		.kpInt = 0x8U,                                                                     \
		.kpFrac = 0x9U,                                                                    \
		.kpSscg = 0x7U,                                                                    \
	},
#else
#define DPLL_LP_INIT(n)
#endif

/*
Each clock producer is represented through a struct clk. Equivalent
to device struct.
*/

#define CLOCK_PLL_DEFINE(n)                                                                     \
	static const struct clk *const pll_##n##_parent = 	                                \
		CLOCK_DT_GET(DT_INST_PHANDLE(n,input)); 					\
	static const struct clock_pll_config pll_##n = {                                        \
		STANDARD_CLK_SUBSYS_DATA_INIT(pll_##n##_parent)                                \
		.rate = DT_INST_PROP(n,clock_frequency),                                        \
		.system_clock = DT_INST_PROP(n,system_clock),                                  	\
		DPLL_HP_INIT(n)                                                                 \
		DPLL_LP_INIT(n)                                                                 \
	};                                                                                      \
	CLOCK_DT_INST_DEFINE(n, &pll_##n, &ifx_pll_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_PLL_DEFINE)
