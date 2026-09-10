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
#include <cy_sysclk.h>
#include "infineon_clock_management_header.h"

#define DT_DRV_COMPAT infineon_clock_hf
/*
Se esta tratando como un mux con capacidad de dividir 2 specifiers selector y div
*/
struct clock_hf_config {
	MUX_CLK_SUBSYS_DATA_DEFINE
	uint32_t system_clock;
	uint32_t instance;
	uint32_t clock_division;
};

static int configure_hf_recalc(const struct clk *clk_hw, const void *data){
	const uint32_t *hf_data = (uint32_t *)data;
	return (int)*hf_data;
}

static int validate_hf_parent(const struct clk *clk_hw, clock_freq_t parent_freq, uint8_t new_idx){
	const struct clock_hf_config *config = clk_hw->hw_data;
	if(new_idx>=config->parent_cnt){
		return -EINVAL;
	}
		return 0;
}

static int hf_clock_configure(const struct clk *clk_hw, const void *data){
	const struct clock_hf_config *config = clk_hw->hw_data;
	const ifx_hf_clock_spec *hf_spec = (const ifx_hf_clock_spec*)data;
	
	int ret;
	if (Cy_SysClk_ClkHfSetSource(config->instance, (hf_spec->selector)+1) != CY_SYSCLK_SUCCESS){
		return -EIO;
	}
	if (Cy_SysClk_ClkHfSetDivider(config->instance, hf_spec->clock_division) != CY_SYSCLK_SUCCESS){
		return -EIO;
	}
	if (Cy_SysClk_ClkHfEnable(config->instance) != CY_SYSCLK_SUCCESS){
		return -EIO;
	}
	return 0;
}

static int hf_clock_onoff(const struct clk *clk_hw, bool on){
	const struct clock_hf_config *config = clk_hw->hw_data;
	if (on){
		if (Cy_SysClk_ClkHfEnable(config->instance) != CY_SYSCLK_SUCCESS){
			return -EIO;
		}
	}else{
		if (Cy_SysClk_ClkHfDisable(config->instance) != CY_SYSCLK_SUCCESS){
			return -EIO;
		}
	}
	return 0;
}
static int ifx_hf_get_parent(const struct clk *clk_hw)
{
	const struct clock_hf_config *config = clk_hw->hw_data;
	uint32_t sel = (uint32_t)Cy_SysClk_ClkHfGetSource(config->instance);
	return sel;
}


/*clock driver API implementation*/
const struct clock_management_mux_api ifx_hf_clock_api = {
	.shared = {
		.configure = hf_clock_configure,
		.on_off = hf_clock_onoff,
	},
	.get_parent = ifx_hf_get_parent,
#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
	.mux_configure_recalc = configure_hf_recalc,
	.mux_validate_parent = validate_hf_parent,
#endif
};

/*
Each clock producer is represented through a struct clk. Equivalent
to device struct.
*/

#define GET_HF_INPUT(node_id, prop, idx) CLOCK_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#define CLOCK_HF_DEFINE(n)                                                                     	\
	static const struct clk *const hf_##n##_parents[] = {                                  	\
		DT_INST_FOREACH_PROP_ELEM(n, input_sources, GET_HF_INPUT)};                   	\
	static const struct clock_hf_config hf_##n = {                                        	\
		MUX_CLK_SUBSYS_DATA_INIT(hf_##n##_parents,                               	\
					 DT_INST_PROP_LEN(n, input_sources))                    \
		.system_clock = DT_INST_PROP(n, system_clock),                                  \
		.instance = DT_INST_PROP(n,instance),                                  		\
		.clock_division = DT_INST_PROP(n, clock_div)                    		\
	};                                                                                      \
	MUX_CLOCK_DT_INST_DEFINE(n, &hf_##n, &ifx_hf_clock_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_HF_DEFINE)		

