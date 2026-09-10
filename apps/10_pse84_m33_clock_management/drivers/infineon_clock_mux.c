#include <stdlib.h>
#include <zephyr/drivers/clock_management/clock_driver.h>
#include <zephyr/drivers/clock_management/clock_helpers.h>
#include <zephyr/drivers/clock_control/clock_control_ifx_cat1.h>
#include <zephyr/dt-bindings/clock/ifx_clock_source_common.h>
#include <cy_sysclk.h>
//#include "infineon_clock_management_header.h"


#define DT_DRV_COMPAT infineon_clock_mux

struct clock_mux_config {

	/*
		Pointer struct to parent nodes.
		Counter for number of parents.
	*/
	MUX_CLK_SUBSYS_DATA_DEFINE
	/*
		Type of mux. 
	*/
	uint32_t block;
	/*
		Path number.
	*/
	uint8_t instance;

};

static int configure_mux_recalc(const struct clk *clk_hw, const void *data){
	const uint32_t *mux_data = (uint32_t *)data;
	return (int)*mux_data;
}

static int validate_mux_parent(const struct clk *clk_hw, clock_freq_t parent_freq, uint8_t new_idx){
	const struct clock_mux_config *config = clk_hw->hw_data;
	if(new_idx>=config->parent_cnt){
		return -EINVAL;
	}
		return 0;
}

static int mux_configure(const struct clk *clk_hw, const void *mux)
{
	const struct clock_mux_config *config = clk_hw->hw_data;
	/*
	   Source selection sent as an specifier from the framework
	*/
	uint32_t source = *(const uint32_t *)mux;

	uint32_t ret;
	
	switch (config->block)
	{
		case IFX_PATHMUX:
			ret = Cy_SysClk_ClkPathSetSource(config->instance,source);
		break;

		default:
			return -EINVAL;
	}
	return ret;
}

static int ifx_cat1_mux_get_parent(const struct clk *clk_hw)
{
	const struct clock_mux_config *config = clk_hw->hw_data;
	uint32_t sel = (uint32_t)Cy_SysClk_ClkPathGetSource(config->instance);
	return sel;
}

/*clock driver API implementation*/
const struct clock_management_mux_api ifx_cat1_path_mux_api = {
	.shared.configure = mux_configure,
	.get_parent = ifx_cat1_mux_get_parent,
#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
	.mux_configure_recalc = configure_mux_recalc,
	.mux_validate_parent = validate_mux_parent,
#endif
};

/*
Each clock producer is represented through a struct clk. Equivalent
to device struct.
GET_MUX_INPUT returns a pointer to the clk struct of the parent node referenced
in the property prop in the index idx.
Parents struct, with pointers to clk structs, is builded.
*/
#define GET_MUX_INPUT(node_id, prop, idx) CLOCK_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#define CLOCK_MUX_DEFINE(n)                                                                     \
	static const struct clk *const mux_##n##_parents[] = {                                  \
		DT_INST_FOREACH_PROP_ELEM(n, input_sources, GET_MUX_INPUT)};                   \
	static const struct clock_mux_config mux_##n = {                                        \
		MUX_CLK_SUBSYS_DATA_INIT(mux_##n##_parents,                                     \
					 DT_INST_PROP_LEN(n, input_sources))                    \
		.block = DT_INST_PROP(n, system_clock),                                         \
		.instance = DT_INST_PROP(n,instance)                                  		\
	};                                                                                      \
	MUX_CLOCK_DT_INST_DEFINE(n, &mux_##n, &ifx_cat1_path_mux_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_MUX_DEFINE)
