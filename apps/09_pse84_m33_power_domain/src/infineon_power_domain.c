#include <infineon_kconfig.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/clock_control_ifx_cat1.h>
#include <zephyr/dt-bindings/clock/ifx_clock_source_common.h>
#include <zephyr/dt-bindings/clock/ifx_clock_source_boards.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>


#define DT_DRV_COMPAT power_domain


volatile uint32_t flag = 0;

static int clock_domain_pd_action(const struct device *dev, enum pm_device_action action)
{
	int err;
    	switch (action) {
    		case PM_DEVICE_ACTION_RESUME:
			flag = 1;
    			err = Cy_SysClk_ClkHfEnable(10u);
        		//pm_device_children_action_run(dev, PM_DEVICE_ACTION_TURN_ON, NULL);
        		break;
    		case PM_DEVICE_ACTION_SUSPEND:
			//flag = 2;
        		//pm_device_children_action_run(dev, PM_DEVICE_ACTION_TURN_OFF, NULL);;
			err = Cy_SysClk_ClkHfDisable(10u);
        		break;
    		case PM_DEVICE_ACTION_TURN_ON:
			//flag = 3;
			//err = Cy_SysClk_ClkHfEnable(10u);
        		break;
    		case PM_DEVICE_ACTION_TURN_OFF:
			//flag = 4;
			//err = Cy_SysClk_ClkHfDisable(10u);
        		break;
    		default:
        		return -ENOTSUP;
    }
	return 0;
}

static int clock_domain_pd_init(const struct device *dev){
	return pm_device_driver_init(dev, clock_domain_pd_action);
}

#define INFINEON_CLOCK_PD_INIT(n)                                               \
        PM_DEVICE_DT_INST_DEFINE(n, clock_domain_pd_action);                    \
	DEVICE_DT_INST_DEFINE(n, &clock_domain_pd_init,                     	\
                          PM_DEVICE_DT_INST_GET(n),                             \
                          NULL,                        				\
                          NULL,                         			\
                          PRE_KERNEL_1,                                    	\
                          11,                     				\
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(INFINEON_CLOCK_PD_INIT)
