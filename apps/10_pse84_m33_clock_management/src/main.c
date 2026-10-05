#include <zephyr/drivers/clock_management.h>
#include <zephyr/sys/sys_io.h>
#include <cy_sysclk.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#define DUMMY_CONSUMER_1 DT_NODELABEL(dummy_clock_consumer_1)
#define DUMMY_CONSUMER_2 DT_NODELABEL(dummy_clock_consumer_2)

static const struct device *dummy_device_1 = DEVICE_DT_GET(DUMMY_CONSUMER_1);
static const struct device *dummy_device_2 = DEVICE_DT_GET(DUMMY_CONSUMER_2);
static const struct clk *iho_producer = CLOCK_DT_GET(DT_NODELABEL(clk_iho));
static const struct clk *pll_hp = CLOCK_DT_GET(DT_NODELABEL(dpll_hp));
static const struct clk *pll_lp1 = CLOCK_DT_GET(DT_NODELABEL(dpll_lp1));
static const struct clk *pll_lp0 = CLOCK_DT_GET(DT_NODELABEL(dpll_lp0));

static const struct clk *p_mux1 = CLOCK_DT_GET(DT_NODELABEL(path_mux1));



static const clock_request_t default_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, default);
static const clock_request_t default_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, default);

static const clock_request_t fast_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, fast);

static const clock_request_t shared_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, shared);
static const clock_request_t shared_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, shared);

//static const clock_request_t random_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, random);

static const clock_request_t all_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, all);

static const clock_output_t dummy_1_clk_1 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_1, clock_output_1_dummy_1);
static const clock_output_t dummy_1_clk_2 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_1, clock_output_2_dummy_1);
static const clock_output_t dummy_2_clk_1 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_2, clock_output_1_dummy_2);




struct ifx_clock_consumer_config {
	const struct clock_management_data *clk_mgmt;
};

int main(void){
	int ret = -1;
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	uint8_t activeRequest_dummy_1;
	uint8_t activeRequest_dummy_2;

	/*LOCKING TEST
	*/
	/*
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,default_req_dummy_1);
	ret = clock_management_request_state(config_dummy_2->clk_mgmt,default_req_dummy_2);
	ret = clock_management_lock(config_dummy_1->clk_mgmt,dummy_1_clk_1);
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,all_req_dummy_1);


	ret = clock_management_request_state(config_dummy_2->clk_mgmt,fast_req_dummy_2);
*/


	/*ON TEST

	ret = clock_management_request_state(config_dummy_1->clk_mgmt,default_req_dummy_1);
	if (ret<0){
		return 0;
	}
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	uint8_t iho_counter = iho_producer->subsys_data->usage_cnt;
	uint8_t pll_hp_counter = pll_hp->subsys_data->usage_cnt;
	uint8_t pll_lp1_counter = pll_lp1->subsys_data->usage_cnt;
	uint8_t pll_lp0_counter = pll_lp0->subsys_data->usage_cnt;
	uint8_t mux_counter = p_mux1->subsys_data->usage_cnt;
	printk("%d", iho_counter);
	if (iho_counter<0){
		return 0;
	}
	*/
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,default_req_dummy_1);
	if (ret<0){
		return 0;
	}
	ret = clock_management_request_state(config_dummy_2->clk_mgmt,default_req_dummy_2);
	if (ret<0){
		return 0;
	}
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_2_clk_1);

	
	clock_freq_t freq = 100000000;
	ret = clock_management_req_rate(config_dummy_1->clk_mgmt, dummy_1_clk_2, freq);
	if (ret<0){
		return 0;
	}


/*SHARED_TEST

	ret = clock_management_request_state(config_dummy_2->clk_mgmt,shared_req_dummy_2);
	if (ret<0){
		return 0;
	}
	activeRequest_dummy_1 =  *(config_dummy_1->clk_mgmt->active_request);
	activeRequest_dummy_2 = *(config_dummy_2->clk_mgmt->active_request);
	printf("%d\n",activeRequest_dummy_1);
	printf("%d\n",activeRequest_dummy_2);
	
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,shared_req_dummy_1);
	if (ret<0){
		return 0;
	}
	activeRequest_dummy_1 =  *(config_dummy_1->clk_mgmt->active_request);
	activeRequest_dummy_2 = *(config_dummy_2->clk_mgmt->active_request);
	printf("%d\n",activeRequest_dummy_1);
	printf("%d\n",activeRequest_dummy_2);

	ret = clock_management_request_state(config_dummy_1->clk_mgmt,random_req_dummy_1);
	if (ret<0){
		return 0;
	}
	activeRequest_dummy_1 =  *(config_dummy_1->clk_mgmt->active_request);
	activeRequest_dummy_2 = *(config_dummy_2->clk_mgmt->active_request);
	printf("%d\n",activeRequest_dummy_1);
	printf("%d\n",activeRequest_dummy_2);

*/
	while(1){
		
	}
	return 0;
}