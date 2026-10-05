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

static clock_freq_t pll_recalc_rate_hp(const struct clk *clk_hw, clock_freq_t parent_rate){
	ARG_UNUSED(clk_hw);
	/*
 	 * FOUT = Fref × (NDIV_INT_SEL + 1 + NDIV_FRACT_SEL/2²¹) / ((PDIV_SEL + 1) × (KDIV_SEL + 1))
 	 */
	
	cy_stc_dpll_hp_config_t hp_config;
	cy_stc_pll_manual_config_t dpll_config = {
		.hpPllCfg = &hp_config,
	};
	if (Cy_SysClk_PllGetConfiguration(SRSS_DPLL_HP_0_PATH_NUM, &dpll_config) != CY_SYSCLK_SUCCESS) {
			return -EIO;
	}
	uint8_t feedback_div = dpll_config.hpPllCfg->nDiv+1;
	uint8_t reference_div = dpll_config.hpPllCfg->pDiv+1;
	uint8_t output_div = dpll_config.hpPllCfg->kDiv + 1;
	uint32_t frac_div = dpll_config.hpPllCfg->nDivFract;

	uint32_t f_out = (uint32_t)parent_rate * (feedback_div + frac_div/(1U<<21)) / (reference_div * output_div);
	
	return (clock_freq_t)f_out;
}

static clock_freq_t pll_recalc_rate_lp(const struct clk *clk_hw, clock_freq_t parent_rate){
	/*
	 * fOUT = (feedbackDiv + fracEn * fracDiv / 2^24) * (fREF / referenceDiv) / outputDiv
	 */
	const struct clock_pll_config *config = clk_hw->hw_data;
	uint32_t pll_num = (config->system_clock == IFX_DPLL250_0) ? SRSS_DPLL_LP_0_PATH_NUM
                                        		           : SRSS_DPLL_LP_1_PATH_NUM;

	cy_stc_dpll_lp_config_t lp_config;
	cy_stc_pll_manual_config_t dpll_config = {
		.lpPllCfg = &lp_config,
	};

	if (Cy_SysClk_DpllLpGetConfiguration(pll_num, &dpll_config) != CY_SYSCLK_SUCCESS) {
			return -EIO;
	}
	uint8_t feedback_div = dpll_config.lpPllCfg->feedbackDiv;
	uint8_t reference_div = dpll_config.lpPllCfg->referenceDiv;
	uint8_t output_div = dpll_config.lpPllCfg->outputDiv;
	uint32_t frac_div = dpll_config.lpPllCfg->fracDiv;
	bool fran_en = dpll_config.lpPllCfg->fracEn;

	uint32_t f_out = (feedback_div + fran_en * frac_div/(1U<<24)) * ((uint32_t)parent_rate/reference_div)/output_div;
	return (clock_freq_t)f_out;
}

static clock_freq_t pll_recalc_rate(const struct clk *clk_hw, clock_freq_t parent_rate){
	const struct clock_pll_config *config = clk_hw->hw_data;
	clock_freq_t ret;
	switch(config->system_clock){
		case IFX_DPLL250_0:
		case IFX_DPLL250_1:
			ret = (clock_freq_t)pll_recalc_rate_lp(clk_hw, parent_rate);
			break;
		case IFX_DPLL500:
			ret = (clock_freq_t)pll_recalc_rate_hp(clk_hw, parent_rate);
			break;
		default:
			ret = -EIO;
			break;
	}
	return ret;
}

#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
static clock_freq_t pll_configure_recalc(const struct clk *clk_hw, const void *data, clock_freq_t parent_rate)
{
	const struct clock_pll_config *config = clk_hw->hw_data;
	const ifx_dpll_lp_spec *pll_data = (const ifx_dpll_lp_spec *)data;
	uint32_t f_out;
	uint8_t feedback_div = pll_data->fb_div;
	uint8_t reference_div = pll_data->ref_div;
	uint8_t output_div =  pll_data->output_div;
	uint32_t frac_div =  pll_data->fraction_div;
	bool fran_en =  pll_data->fraction_enable;

		switch(config->system_clock){
		case IFX_DPLL250_0:
		case IFX_DPLL250_1:
			f_out = (feedback_div + fran_en * frac_div/(1U<<24)) * ((uint32_t)parent_rate/reference_div)/output_div;
			break;
		case IFX_DPLL500:
			f_out = (uint32_t)parent_rate * (feedback_div + frac_div/(1U<<21)) / (reference_div * output_div);
			break;
		default:
			return -EIO;
			break;
	}
	return (clock_freq_t)f_out;
}
#endif

#if defined(CONFIG_CLOCK_MANAGEMENT_SET_RATE)

/* DPLL-LP */

/* DPLL-LP OUTPUT_DIV bitfield allowable range */
#define CY_SYSCLK_DPLL_LP_MIN_OUTPUT_DIV   (1UL)
#define CY_SYSCLK_DPLL_LP_MAX_OUTPUT_DIV   (16UL)

/* DPLL-LP REFERENCE_DIV bitfield allowable range */
#define CY_SYSCLK_DPLL_LP_MIN_REF_DIV      (1UL)
#define CY_SYSCLK_DPLL_LP_MAX_REF_DIV      (16UL)

/* DPLL-LP FEEDBACK_DIV bitfield allowable range selection */
#define CY_SYSCLK_DPLL_LP_MIN_FB_DIV       (16UL)
#define CY_SYSCLK_DPLL_LP_MAX_FB_DIV       (200UL)

/* DPLL-LP Fdco range selection */
#define CY_SYSCLK_DPLL_LP_MIN_FDCO         (160000000UL)
#define CY_SYSCLK_DPLL_LP_MAX_FDCO         (480000000UL)
#define CY_SYSCLK_DPLL_LP_DCO_MODE_LIMIT   (310000000UL)

/* DPLL-LP pdf range selection */
#define CY_SYSCLK_DPLL_LP_MIN_PDF         (4000000UL)
#define CY_SYSCLK_DPLL_LP_MAX_PDF         (8000000UL)

/* DPLL-LP input and output frequency limits */
#define CY_SYSCLK_DPLL_LP_MIN_IN_FREQ  (4000000UL)
#define CY_SYSCLK_DPLL_LP_MAX_IN_FREQ  (64000000UL)
#define CY_SYSCLK_DPLL_LP_MIN_OUT_FREQ (10000000UL)
#define CY_SYSCLK_DPLL_LP_MAX_OUT_FREQ (400000000UL)


struct dpll_lp_divs {
    uint8_t p;
    uint8_t q;
    uint8_t out_div;
    uint32_t frac;
    bool dco_mode;
};

int32_t dpll_lp_calc_dividers(uint32_t parent_rate, uint32_t rate_req,
                  struct dpll_lp_divs *cfg)
{
    uint32_t p, q, out;
    uint32_t fout_best = 0;
    bool found = false;

    if ((parent_rate  < CY_SYSCLK_DPLL_LP_MIN_IN_FREQ)  || (parent_rate  > CY_SYSCLK_DPLL_LP_MAX_IN_FREQ) ||
        (rate_req < CY_SYSCLK_DPLL_LP_MIN_OUT_FREQ) || (rate_req > CY_SYSCLK_DPLL_LP_MAX_OUT_FREQ)) {
        return -EINVAL;
    }

    for (q = CY_SYSCLK_DPLL_LP_MIN_REF_DIV; q <= CY_SYSCLK_DPLL_LP_MAX_REF_DIV; q++) {
        uint32_t pdf = parent_rate / q;
        if ((pdf < CY_SYSCLK_DPLL_LP_MIN_PDF) || (pdf > CY_SYSCLK_DPLL_LP_MAX_PDF)) {
            continue;
        }
        for (p = CY_SYSCLK_DPLL_LP_MIN_FB_DIV; p <= CY_SYSCLK_DPLL_LP_MAX_FB_DIV; p++) {
            uint32_t fdco = pdf * p;
            if ((fdco < CY_SYSCLK_DPLL_LP_MIN_FDCO) || (fdco > CY_SYSCLK_DPLL_LP_MAX_FDCO)) {
                continue;
            }
            for (out = CY_SYSCLK_DPLL_LP_MIN_OUTPUT_DIV; out <= CY_SYSCLK_DPLL_LP_MAX_OUTPUT_DIV; out++) {
                uint64_t t_fixed =
                    (((uint64_t)rate_req * out) << SRSS_DPLL_LP_FRAC_BIT_COUNT)
                    * q / parent_rate;
                uint32_t frac = (uint32_t)
                    (t_fixed & ((1ULL << SRSS_DPLL_LP_FRAC_BIT_COUNT) - 1));
                uint64_t fb = ((uint64_t)p << SRSS_DPLL_LP_FRAC_BIT_COUNT) + frac;
                uint32_t fout = (uint32_t)
                    (((parent_rate * fb) /
                      ((uint64_t)q * out)) >> SRSS_DPLL_LP_FRAC_BIT_COUNT);
                if (!found ||
                    (abs((int32_t)fout - (int32_t)rate_req) <
                     abs((int32_t)fout_best - (int32_t)rate_req))) {
                    found = true;
                    fout_best = fout;
                    cfg->p        = (uint8_t)p;
                    cfg->q        = (uint8_t)q;
                    cfg->out_div  = (uint8_t)out;
                    cfg->frac     = frac;
                    cfg->dco_mode = (fdco >= CY_SYSCLK_DPLL_LP_DCO_MODE_LIMIT);
                }
                if (fout == rate_req) {
                    return (int32_t)fout;
                }
            }
        }
    }

    return found ? (int32_t)fout_best : -ENOENT;
}

static clock_freq_t pll_best_rate(const struct clk *clk_hw, clock_freq_t rate_req, clock_freq_t parent_rate, bool commit){
	const struct clock_pll_config *config = clk_hw->hw_data;
	ARG_UNUSED(parent_rate);
	ARG_UNUSED(rate_req);
	clock_freq_t ret;
	switch (config->system_clock){
		case IFX_DPLL250_1:
			uint32_t pll_path = (config->system_clock == IFX_DPLL250_0) ? SRSS_DPLL_LP_0_PATH_NUM
                                        		            : SRSS_DPLL_LP_1_PATH_NUM;
			struct dpll_lp_divs lp_div;
			
			if (commit == true){
				if (clock_children_check_rate(clk_hw, 0) < 0) {
    					return -EIO;
				}else{
					cy_stc_pll_config_t pll_data_config = {
						.inputFreq = (uint32_t)parent_rate,
						.outputFreq = (uint32_t)rate_req,
					};
					if (Cy_SysClk_DpllLpDisable(pll_path) != CY_SYSCLK_SUCCESS) {
    						return -EIO;
					}
					if (Cy_SysClk_DpllLpConfigure(pll_path, &pll_data_config) != CY_SYSCLK_SUCCESS) {
    						return -EIO;
					}
					if (Cy_SysClk_DpllLpEnable(pll_path, 10000U) != CY_SYSCLK_SUCCESS) {
    						return -EIO;
					}
					ret = pll_recalc_rate(clk_hw, parent_rate);
				}	
			}else{
				ret = (clock_freq_t)dpll_lp_calc_dividers(parent_rate,rate_req,&lp_div);
			}
			break;
		case IFX_DPLL500:
			ret = -EIO;
			break;
		default:
			ret = -EIO;
			break; 
	}
	return ret;
}

#endif
/*
set rate will use Cy_SysClk_DpllHpConfigure and Cy_SysClk_DpllLpConfigure: 
Automatically looks for parameters values that produces the closest desired output frequency.
*/


static int pll_lp_set(const struct clk *clk_hw, const void *data)
{
	/*
	 * feedbackDiv
	 * referenceDiv
	 * outputDiv
	 * pllDcoMode
	 * outputMode
	 * fracDiv
	 * fracDitherEn
	 * fracEn
	 */
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
	};

	cy_stc_pll_manual_config_t pll_config = {
    		.lpPllCfg = &lp_config,
	};

	uint32_t pll_path = (config->system_clock == IFX_DPLL250_0) ? SRSS_DPLL_LP_0_PATH_NUM
                                        		            : SRSS_DPLL_LP_1_PATH_NUM;
	/*
	if (Cy_SysClk_DpllLpIsEnabled(pll_path)) {
    		return 0;
	}
	*/
	Cy_SysClk_DpllLpDisable(pll_path);
	
	
	if (Cy_SysClk_DpllLpManualConfigure(pll_path, &pll_config) != CY_SYSCLK_SUCCESS) {
   		return -EIO;
	}

	if (Cy_SysClk_DpllLpEnable(pll_path, 10000U) != CY_SYSCLK_SUCCESS) {
    		return -EIO;
	}
	return 0;
}

static int pll_hp_set(const struct clk *clk_hw, const void *data){
	//const struct clock_pll_config *config = clk_hw->hw_data;
	const ifx_dpll_lp_spec *spec = (const ifx_dpll_lp_spec *)data;
	/*
	 * nDiv + 1 = feedbackDiv
	 * pDiv + 1 = referenceDiv
	 * kDiv + 1 = outputDiv
	 * nDivFract = fracDiv
	 * outputMode
	 */
	
	 cy_stc_dpll_hp_config_t hp_config = {
		.pDiv = spec->ref_div - 1,                                              \
		.nDiv = spec->fb_div - 1,                                              \
		.kDiv = spec->output_div - 1,                                              \
		.nDivFract = spec->fraction_div,                                  \
		.freqModeSel = ((cy_en_wait_mode_select_t)0x7U),   \
		.ivrTrim = 0x8U,                                                                   \
		.clkrSel = 0x1U,                                                                   \
		.alphaCoarse = 0xCU,                                                               \
		.betaCoarse = 0x5U,                                                                \
		.flockThresh = 0x3U,                      \
		.flockWait = 0x6U,                                                                 \
		.flockLkThres = 0x7U,                                                              \
		.flockLkWait = 0x4U,                                                               \
		.alphaExt = 0x14U,                                                                 \
		.betaExt = 0x14U,                                   \
		.lfEn = 0x1U,                                                                      \
		.dcEn = 0x1U,                                                                      \
		.outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,                                        \
	};

	cy_stc_pll_manual_config_t pll_config = {
    		.hpPllCfg = &hp_config,
	};
	
	if (Cy_SysClk_PllDisable(SRSS_DPLL_HP_0_PATH_NUM) != CY_SYSCLK_SUCCESS) {
   		return -EIO;
	}
	
	if (Cy_SysClk_PllManualConfigure(SRSS_DPLL_HP_0_PATH_NUM, &pll_config) != CY_SYSCLK_SUCCESS) {
   		return -EIO;
	}
	
	if (CY_SYSCLK_SUCCESS != Cy_SysClk_PllEnable(SRSS_DPLL_HP_0_PATH_NUM, 10000u)) {
   		return -EIO;
	}
	return 0;
}

static int pll_configure(const struct clk *clk_hw, const void *data){
	const struct clock_pll_config *config = clk_hw->hw_data;
	int ret;
	switch(config->system_clock){
		case IFX_DPLL250_0:
		case IFX_DPLL250_1:
			ret = pll_lp_set(clk_hw, data);
			break;
		case IFX_DPLL500:
			ret = pll_hp_set(clk_hw, data);
			break;
		default:
			ret = -EIO;
			break;
	}
	return ret;
}

static int pll_on_off(const struct clk *clk_hw, bool on)
{
	const struct clock_pll_config *config = clk_hw->hw_data;
	switch(config->system_clock){
		case IFX_DPLL250_0:
		case IFX_DPLL250_1:
			uint32_t pll_path = (config->system_clock == IFX_DPLL250_0) ? SRSS_DPLL_LP_0_PATH_NUM
                                        		            : SRSS_DPLL_LP_1_PATH_NUM;
			if (on) {
				if (Cy_SysClk_DpllLpEnable(pll_path, 10000U) != CY_SYSCLK_SUCCESS) {
					return -EIO;
				}
			} else {
				if (Cy_SysClk_DpllLpDisable(pll_path) != CY_SYSCLK_SUCCESS){
					return -EIO;
				}
			}
			break;
		case IFX_DPLL500:
			if (on) {
				if (Cy_SysClk_PllEnable(SRSS_DPLL_HP_0_PATH_NUM, 10000U) != CY_SYSCLK_SUCCESS) {
					return -EIO;
				}
			} else {
				if (Cy_SysClk_PllDisable(SRSS_DPLL_HP_0_PATH_NUM) != CY_SYSCLK_SUCCESS){
					return -EIO;
				}
			}
			break;
		default:
			return -EIO;	
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
#if defined(CONFIG_CLOCK_MANAGEMENT_SET_RATE)
	.best_rate = pll_best_rate,
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
