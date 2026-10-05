#include <stdlib.h>
#include <zephyr/drivers/clock_management/clock_driver.h>
#include <zephyr/drivers/clock_management/clock_helpers.h>
#include <zephyr/drivers/clock_control/clock_control_ifx_cat1.h>
#include <zephyr/dt-bindings/clock/ifx_clock_source_common.h>
#include <cy_sysclk.h>


#define DT_DRV_COMPAT infineon_clock_root

struct clock_root_config {
	uint32_t rate;
	uint8_t system_clock;
};

#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
static clock_freq_t configure_root_recalc(const struct clk *clk_hw, const void *data){
	ARG_UNUSED(data);
	const struct clock_root_config *config = clk_hw->hw_data;
	return (clock_freq_t)config->rate;
}
#endif

#if defined(CONFIG_CLOCK_MANAGEMENT_SET_RATE)
clock_freq_t root_clock_best_rate(const struct clk *clk_hw, clock_freq_t rate_req, bool commit){
	ARG_UNUSED(rate_req);
	ARG_UNUSED(commit);
	const struct clock_root_config *config = clk_hw->hw_data;
	clock_freq_t ret;
	switch (config->system_clock)
	{
		case IFX_IHO:
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(clk_iho))
			ret = (clock_freq_t)config->rate;
#endif		
			break;
		case IFX_PILO:
			ret = (clock_freq_t)config->rate;
			break;
		default:
			return -EINVAL;
	}
	return ret;

}

#endif

static int root_configure(const struct clk *clk_hw, const void *data)
{
	const struct clock_root_config *config = clk_hw->hw_data;
	switch (config->system_clock)
	{
		case IFX_IHO:
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(clk_iho))
			Cy_SysClk_IhoEnable();
#endif
			break;
		case IFX_PILO:
			Cy_SysClk_PiloEnable();
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int root_clock_onoff(const struct clk *clk_hw, bool on){
	return 0;
}

static clock_freq_t root_get_rate(const struct clk *clk_hw)
{
	const struct clock_root_config *config = clk_hw->hw_data;
	clock_freq_t ret;
	switch (config->system_clock)
	{
		case IFX_IHO:
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(clk_iho))
			ret = (clock_freq_t)config->rate;
#endif		
			break;
		case IFX_PILO:
			ret = (clock_freq_t)config->rate;
			break;
		default:
			return -EINVAL;
	}
	return ret;
}

/*clock driver API implementation*/
const struct clock_management_root_api ifx_root_api = {
	.shared = {
		.configure = root_configure,
		.on_off = root_clock_onoff,
	},
	.get_rate = root_get_rate,
#if defined(CONFIG_CLOCK_MANAGEMENT_RUNTIME)
	.root_configure_recalc = configure_root_recalc,
#endif
#if defined(CONFIG_CLOCK_MANAGEMENT_SET_RATE)
	.root_best_rate = root_clock_best_rate,
#endif
};

/*
Each clock producer is represented through a struct clk. Equivalent
to device struct.
*/

#define CLOCK_ROOT_DEFINE(n)                                                    \
	static const struct clock_root_config root_##n = {                      \
		.rate = DT_INST_PROP(n, clock_frequency),                       \
		.system_clock = DT_INST_PROP(n, system_clock)                   \
	};                                                                      \
	ROOT_CLOCK_DT_INST_DEFINE(n, &root_##n, &ifx_root_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_ROOT_DEFINE)
