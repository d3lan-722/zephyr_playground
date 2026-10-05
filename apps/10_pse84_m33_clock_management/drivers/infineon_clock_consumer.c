#define DT_DRV_COMPAT infineon_clock_consumer

#include <zephyr/drivers/clock_management.h>
#include <zephyr/device.h>

struct ifx_clock_consumer_config {
	const struct clock_management_data *clk_mgmt;
};

/*
static int clk_mgmt_callback(const struct clock_management_event *ev, const void *user_data){
	if (ev->type==CLOCK_MANAGEMENT_PRE_RATE_CHANGE){


	}
	ARG_UNUSED(ev);
	ARG_UNUSED(user_data);

	return 0;
}
*/

static int ifx_clock_consumer_init(const struct device *dev){
	ARG_UNUSED(dev);
    	return 0;

}

#define INFINEON_CLOCK_CONSUMER_INIT(n)                                                         	      \
	CLOCK_MANAGEMENT_DT_INST_DEFINE(n)                                                      	      \
	static int ifx_clock_consumer_init##n(const struct device *dev)                         	      \
	{                                                                                       	      \
		return ifx_clock_consumer_init(dev);                              		              \
	}  												      \
	static struct ifx_clock_consumer_config ifx_clock_consumer##n##_cfg = {                    	      \
		.clk_mgmt = CLOCK_MANAGEMENT_DT_INST_GET(n),                                       	      \
		};                                                                      	   	      \
                                                                                                   	      \
	DEVICE_DT_INST_DEFINE(n, &ifx_clock_consumer_init##n, NULL, NULL,           		   	      \
			      &ifx_clock_consumer##n##_cfg, PRE_KERNEL_1, 0,  		   	   	      \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(INFINEON_CLOCK_CONSUMER_INIT)