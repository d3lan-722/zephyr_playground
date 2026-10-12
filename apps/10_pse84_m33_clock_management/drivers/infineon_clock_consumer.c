#define DT_DRV_COMPAT infineon_clock_consumer

#include <zephyr/drivers/clock_management.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdbool.h>
struct freq_group {
	const uint32_t *freqs;
	size_t len;
};

struct ifx_clock_consumer_config {
	const uint32_t instance;
	const struct clock_management_data *clk_mgmt;
    	const struct freq_group *freq_groups;
    	const size_t num_groups;
};

#define DEFINE_FREQ_ARRAY(n, idx) \
    	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, allowable_frequencies_##idx), \
        	(static const uint32_t freqs_ ## n ## _ ## idx[] = DT_INST_PROP(n, allowable_frequencies_##idx);), \
        	() \
    	)

#define FREQ_GROUP_ENTRY(n, idx) \
    	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, allowable_frequencies_##idx), \
        	({ \
            	.freqs = freqs_ ## n ## _ ## idx, \
            	.len = ARRAY_SIZE(freqs_ ## n ## _ ## idx) \
        	},), \
        	() \
    	)

static int clk_mgmt_callback(const struct clock_management_event *ev, const void *user_data)
{
	/*
	const struct freq_group *consumer_freqs = (const struct freq_group *)user_data;
	if (ev->type == CLOCK_MANAGEMENT_QUERY_RATE_CHANGE) {
        	bool freq_supported = false;

        	for (size_t i = 0; i < consumer_freqs->len; i++) {
            		if (ev->new_rate == consumer_freqs->freqs[i]) {
                		freq_supported = true;
                		break;
            		}
        	}
        	if (!freq_supported) {
            		return -ENOTSUP;
        	}
    	}
	*/	
    	return 0;
}

static int ifx_clock_consumer_init(const struct device *dev)
{
	const struct ifx_clock_consumer_config *cfg = dev->config;
	int ret;
	for (uint8_t i = 0; i < cfg->clk_mgmt->num_outputs; i++) {
		if (i < cfg->num_groups) {
            		ret = clock_management_set_callback(cfg->clk_mgmt, 
                                                	    i, 
                                                	    clk_mgmt_callback, 
                                                	    &cfg->freq_groups[i]);
            	if (ret < 0 && ret != -ENOTSUP) {
                	return ret;
            	}
        	}
    	}
    	return 0;
}

#define INFINEON_CLOCK_CONSUMER_INIT(n)                                              	\
    	CLOCK_MANAGEMENT_DT_INST_DEFINE(n)                                              \
        	                                                                	\
    	DEFINE_FREQ_ARRAY(n, 1)                                                         \
    	DEFINE_FREQ_ARRAY(n, 2)                                                         \
                                                                                     	\
    	static const struct freq_group freq_groups_##n[] = {                            \
        	FREQ_GROUP_ENTRY(n, 1)                                                  \
        	FREQ_GROUP_ENTRY(n, 2)                                                  \
    	};                                                                              \
                                                                                     	\
    	static const struct ifx_clock_consumer_config ifx_clock_consumer_cfg_##n = {    \
        	.instance = DT_INST_PROP(n, instance),                                  \
        	.clk_mgmt = CLOCK_MANAGEMENT_DT_INST_GET(n),                            \
        	.freq_groups = freq_groups_##n,                                         \
        	.num_groups = ARRAY_SIZE(freq_groups_##n),                              \
    	};                                                                              \
                                                                                     	\
    	DEVICE_DT_INST_DEFINE(n,                                                        \
                        ifx_clock_consumer_init,                                   	\
                        NULL,                                                      	\
                        NULL,                                                      	\
                        &ifx_clock_consumer_cfg_##n,                               	\
                        PRE_KERNEL_1,                                              	\
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                       	\
                        NULL);

DT_INST_FOREACH_STATUS_OKAY(INFINEON_CLOCK_CONSUMER_INIT)