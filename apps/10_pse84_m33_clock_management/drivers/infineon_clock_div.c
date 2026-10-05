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

#define DT_DRV_COMPAT infineon_clock_div
/*
Se esta tratando como un mux con capacidad de dividir 2 specifiers selector y div
*/
struct clock_div_config {
	STANDARD_CLK_SUBSYS_DATA_DEFINE
	uint32_t instance;
	uint32_t clock_division;
};
#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
static clock_freq_t configure_div_recalc(const struct clk *clk_hw, const void *data, clock_freq_t parent_rate){
	uint32_t div_factor = *(const uint32_t*)data;
	uint32_t f_out = (uint32_t)parent_rate/div_factor;
	return (clock_freq_t)f_out;
}
#endif

#if defined(CONFIG_CLOCK_MANAGEMENT_SET_RATE)
static clock_freq_t div_best_rate(const struct clk *clk_hw, clock_freq_t rate_req, clock_freq_t parent_rate, bool commit){
	const struct clock_div_config *config = clk_hw->hw_data;
	uint32_t ratio = (parent_rate + rate_req / 2U) / rate_req;
	cy_en_clkhf_dividers_t div_factor = (cy_en_clkhf_dividers_t)(ratio-1);
	cy_en_sysclk_status_t ret;
	clock_freq_t f_out = parent_rate/ratio;  
	if (commit == true){
		ret = Cy_SysClk_ClkHfSetDivider(config->instance, div_factor);
		if (ret < 0){
			return -EIO;
		}
	}
	return f_out;
}
#endif

static int clock_div_configure(const struct clk *clk_hw, const void *data){
	const struct clock_div_config *config = clk_hw->hw_data;
	const cy_en_clkhf_dividers_t div_spec = *(const cy_en_clkhf_dividers_t*)data - 1;
	int ret = 0;

	if (div_spec < 0 || div_spec > 15)
		return -EIO;

	if (Cy_SysClk_ClkHfSetDivider(config->instance, div_spec) != CY_SYSCLK_SUCCESS){
		return -EIO;
	}
	if (Cy_SysClk_ClkHfEnable(config->instance) != CY_SYSCLK_SUCCESS){
		return -EIO;
	}
	return ret;
}

static clock_freq_t clock_div_recalc_rate(const struct clk *clk_hw, clock_freq_t parent_rate){
	const struct clock_div_config *config = clk_hw->hw_data;
	cy_en_clkhf_dividers_t div_factor = Cy_SysClk_ClkHfGetDivider(config->instance);
	return parent_rate/(clock_freq_t)(div_factor+1);
}

static int clock_div_onoff(const struct clk *clk_hw, bool on){
	const struct clock_div_config *config = clk_hw->hw_data;
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

/*clock driver API implementation*/
const struct clock_management_standard_api ifx_div_clock_api = {
	.shared = {
		.configure = clock_div_configure,
		.on_off = clock_div_onoff,
	},
	.recalc_rate = clock_div_recalc_rate,
#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
	.configure_recalc = configure_div_recalc,
#endif
#if defined(CONFIG_CLOCK_MANAGEMENT_SET_RATE)
	.best_rate = div_best_rate,
#endif
};

/*
Each clock producer is represented through a struct clk. Equivalent
to device struct.
*/

#define CLOCK_DIV_DEFINE(n)                                                     \
	static const struct clk *const div_##n##_parent = 	                \
		CLOCK_DT_GET(DT_INST_PHANDLE(n,input)); 			\
	static const struct clock_div_config div_##n = {                      	\
		STANDARD_CLK_SUBSYS_DATA_INIT(div_##n##_parent)                 \
		.instance = DT_INST_PROP(n,instance),                 		\
		.clock_division = DT_INST_PROP(n, clock_div),          		\
	};                                                                      \
	CLOCK_DT_INST_DEFINE(n, &div_##n, &ifx_div_clock_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_DIV_DEFINE)		

