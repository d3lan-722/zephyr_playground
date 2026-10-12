#include <zephyr/ztest.h>
#include <zephyr/drivers/clock_management.h>
#include <zephyr/drivers/clock_management/clock_helpers.h>
#include <zephyr/sys/sys_io.h>
#include <cy_sysclk.h>

#define DUMMY_CONSUMER_1 DT_NODELABEL(dummy_clock_consumer_1)
#define DUMMY_CONSUMER_2 DT_NODELABEL(dummy_clock_consumer_2)
#define UART_CONSUMER DT_NODELABEL(dummy_clock_consumer_3)

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

static const struct device *dummy_device_1 = DEVICE_DT_GET(DUMMY_CONSUMER_1);
static const struct device *dummy_device_2 = DEVICE_DT_GET(DUMMY_CONSUMER_2);
static const struct device *uart_device = DEVICE_DT_GET(UART_CONSUMER);

static const struct clk *iho_producer = CLOCK_DT_GET(DT_NODELABEL(clk_iho));
static const struct clk *mux_hf11 = CLOCK_DT_GET(DT_NODELABEL(mux_hf11));
static const struct clk *mux_hf13 = CLOCK_DT_GET(DT_NODELABEL(mux_hf13));
static const struct clk *pll_lp0 = CLOCK_DT_GET(DT_NODELABEL(dpll_lp0));
static const struct clk *pll_lp = CLOCK_DT_GET(DT_NODELABEL(dpll_lp1));
static const struct clk *pll_hp = CLOCK_DT_GET(DT_NODELABEL(dpll_hp));
static const struct clk *path_mux1 = CLOCK_DT_GET(DT_NODELABEL(path_mux1));

static const clock_request_t default_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, default);
static const clock_request_t default_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, default);
static const clock_request_t default_req_uart = CLOCK_MANAGEMENT_DT_GET_REQUEST(UART_CONSUMER, default);

static const clock_request_t fast_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, fast);
static const clock_request_t fast_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, fast);
static const clock_request_t shared_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, shared);
static const clock_request_t shared_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, shared);
static const clock_request_t iho_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, iho);
static const clock_request_t all_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, all);
static const clock_request_t locking_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, locking);
static const clock_request_t failed_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, failed);

static const clock_output_t dummy_1_clk_1 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_1, clock_output_1_dummy_1);
static const clock_output_t dummy_1_clk_2 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_1, clock_output_2_dummy_1);
static const clock_output_t dummy_2_clk_1 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_2, clock_output_1_dummy_2);
static const clock_output_t uart_clock = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(UART_CONSUMER, uart_clk);

static const uint32_t output_1_default_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_default_state),clock_frequency);
static const uint32_t output_2_default_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_default_state),clock_frequency);
static const uint32_t output_1_fast_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_fast_state),clock_frequency);
static const uint32_t output_1_shared_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_shared_state),clock_frequency);
static const uint32_t output_2_shared_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_shared_state),clock_frequency);
static const uint32_t output_2_iho_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_iho_state),clock_frequency);

/*
 * Function invoked at the start of the test suite. It assigns the default state to the 
 * uart_dummy_consumer in order to keep it on when disable_unused() is called. Otherwise the clock of the
 * real UART device would be disable and an "unknown error" would appear.
 */
static void *clk_suite_setup(void)
{
	const struct ifx_clock_consumer_config *config_uart = uart_device->config;
    	int ret;

    	ret = clock_management_request_state(config_uart->clk_mgmt, default_req_uart);
    	zassert_equal(ret, 0, "Failed to apply initial state, ret%d = ", ret);

	ret = clock_management_on(config_uart->clk_mgmt, uart_clock);
	zassert_equal(ret, 0, "Failed to power on uart clock, ret=%d", ret);

    	return NULL; 
}

/*
 * Function called at the beginning of each test case. It prepares the clock outputs of both
 * dummy consumers towards their default state. It also calls management_on() in order to set
 * the counter of the default producers.
 */
void prepare_clock_states(void *unused)
{
	ARG_UNUSED(unused);
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,default_req_dummy_1);
	zassert_equal(ret, 0, "Failed to apply reset state_1, ret=%d", ret);
	ret = clock_management_request_state(config_dummy_2->clk_mgmt,default_req_dummy_2);
	zassert_equal(ret, 0, "Failed to apply reset state_2, ret=%d", ret);

	/*
	 * clock_management_on() must be called after setting the state. Otherwise count usage value will be wrong.
	 * It moves from the specified clock output to the root, following the actual parent of each producer
	 */
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_1, ret=%d", ret);
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, 0, "Failed to power on ouput2 from dummy_1, ret=%d", ret);
	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_2, ret=%d", ret);
	
	
}

/* Decrement the count usage of producers used by both clock outputs. Then it gates off every
 *  producer which is not being used.
 */

void reset_clock_states(void *unused)
{
	ARG_UNUSED(unused);
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	ret = clock_management_off(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_1, ret=%d", ret);
	ret = clock_management_off(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, 0, "Failed to power on ouput2 from dummy_1, ret=%d", ret);
	ret = clock_management_off(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_2, ret=%d", ret);
	clock_management_disable_unused();
}


/*
 * Test case. Verifies a specific and well defined functionality;
 * by default all test cases shall be start with test_
 * Checks the rates of the clock outputs after prepare_clock_states(). Then it cheks what happens
 * when a not supported (by the hardware) state is requested.
*/

ZTEST(clk_runtime_tests, test_clock_default)
{
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret, ret_1, ret_2;
	/*
	 * Getting rate for both clock outputs. Active state: Default
	 */
	ret_1 = clock_management_get_rate(config_dummy_1->clk_mgmt,dummy_1_clk_1);
	zassert_equal(ret_1, output_1_default_freq, "Rate dummy_1_clk_1 dont match, ret=%d", ret_1);

	ret = clock_management_get_rate(config_dummy_1->clk_mgmt,dummy_1_clk_2);
	zassert_equal(ret, output_2_default_freq, "Rate dummy_1_clk_2 dont match, ret=%d", ret);

	/* 
	 * dummy_1_clk_1 and dummy_2_clk_1 are the same shared clock output. Its rate must be the same.
	 */
	ret_2 = clock_management_get_rate(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_equal(ret_2, output_1_default_freq, "Rate dummy_2_clk_1 dont match, ret=%d", ret_2);
	zassert_equal(ret_1, ret_2, "Rate dummy_2_clk_1 and dummy_1_clk_1 dont match, ret_1=%d, ret_2=%d", ret_1, ret_2);

	/*
	 * Clock outputs are 0 biased in each consumers they belong to.  
	 * Dummy_device_1 has two clock outputs:
	 * 	-dummy_1_clk_1 is clock output number 0
	 * 	-dummy_1_clk_2 is clock output number 1
	 * If we ask for a clock output above it, we will have an error.
	 */
	ret = clock_management_get_rate(config_dummy_1->clk_mgmt,2);
	zassert_not_equal(ret, 0, "Test, ret=%d", ret);

	ret = clock_management_off(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, 0, "Test, ret=%d", ret);
	/*
	 *  dummy_device_1 changes its request in order to free clock output 1. And try the failed state. Otherwise, if the 
	 *  failed state is requested before doing this, the requested wont happen because there would be two active requests
	 *  on the same clock output with different states. The voting system wouldnt select the new requested state. 
	 */
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,iho_req_dummy_1);
	zassert_equal(ret, 0, "Test, ret=%d", ret);

	/*
	 * Update the counters usage
	 */
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_2, ret=%d", ret);

	ret = clock_management_off(config_dummy_2->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Test, ret=%d", ret);
	/*
	 * dummy_device_2 requests the failed state. This state tries to select an inexistent input source in the path_mux3
	 * producer. Return value unsecuseful
	 */
	ret = clock_management_request_state(config_dummy_2->clk_mgmt,failed_req_dummy_2);
	zassert_not_equal(ret, 0, "Test, ret=%d", ret);

	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_2, ret=%d", ret);

	/*
	 * When new request happens, the subsystem takes every producer within the clocks property of the state. 
	   It configures each producer sequentially. For example in the failed state
	   clocks = <&clk_pilo 1>, <&div_hf11 2>, <&path_mux3 7>, <&mux_hf11 8>;

	   Root pilo is configured first (enabled). Then the final divider is reconfigured, then the error happens. 
	   It configured a portion of the clock path, but not totally. The resultant frequency is neither the requested or the before.
	 */
	ret = clock_management_get_rate(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_not_equal(ret, 0, "Test, ret=%d", ret);

/*
 * Evaluates what happens when a shared clock output does not have a shared state request. 
 */

}

ZTEST(clk_runtime_tests, test_not_shared_state)
{
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	/*
	 * Dummy 2 is driven by clk_output_hf11. This test is executed after prepare_clock_states(), 
	   which puts clk_output_hf11 into the default state.

	   Dummy 2 requests the fast state. 
	   Active requests would be:  
	   	- "default" request from dummy_1: clk_output_hf11 in default state
	   					  clk_output_hf13 in default_state
				            
		- "fast" request from dummy_2:    clk_output_hf11 in fast state

	   The framework evaluates all active requests and counts the votes per state.
		- clk_output_hf11: Its in 2 active requests
  			- 1 votes for the default state
  			- 1 votes for the fast state

		- clk_output_hf13: Its in 1 active request
  			- 1 votes for the default state

	   To apply a final state to a clock output, all active requests for that output must vote for the same state 
	   (i.e., the vote count for a state must, at least, be equal the total number of active requests).

	   Therefore, clk_output_hf11 has no state with 2 votes (there is a 1–1 split), so the framework keeps its current state. 
	   In this case, it should also raise an error to indicate a conflicting vote. Currently, no error is generated.			
	 */

	/*
	 * Return value should be uncessfull != 0 
 	 */
	ret = clock_management_request_state(config_dummy_2->clk_mgmt,fast_req_dummy_2);
	zassert_equal(ret, 0, "State applied, ret=%d", ret);

	ret = clock_management_get_rate(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_not_equal(ret, output_1_fast_freq, "clk_output_hf11 rate == fast, ret=%d", ret);
	zassert_equal(ret, output_1_default_freq, "clk_output_hf11 rate != default, ret=%d", ret);

	/*
	 * Both dummies request the fast state. So the clk_output_hf11 moves to it.
	 */
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,fast_req_dummy_1);
	ret = clock_management_get_rate(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, output_1_fast_freq, "clk_output_hf11 rate != fast, ret=%d", ret);
}

ZTEST(clk_runtime_tests, test_shared_producer){
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	/*
	 * dummy_device_1 and dummy_device_2 request the shared state. 
	   This state uses <&clk_pilo 1> and <&path_mux3 4> as shared producers
	   with the same configuration
	 */
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,shared_req_dummy_1);
	zassert_equal(ret, 0, "Shared state not applied, ret=%d", ret);

	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, output_2_shared_freq, "clk_output_hf13 rate != shared from dummy_1, ret=%d", ret);

	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, output_1_default_freq, "clk_output_hf11 rate == shared from dummy_1, ret=%d", ret);

	ret = clock_management_request_state(config_dummy_2->clk_mgmt,shared_req_dummy_2);
	zassert_equal(ret, 0, "Shared state not applied, ret=%d", ret);

	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, output_1_shared_freq, "clk_output_hf11 rate != shared from dummy_1, ret=%d", ret);

	/*
	 *  Dummy 1 requests the iho state on clk_output_hf13 clock output, this modifies the path_mux3 
	    multiplexer which is being shared by both clock outputs:
	    	- clk_output_hf11: Its in shared state, with path_mux3 selecting input number 4.
		- clk_output_hf13: Its moving to iho state, path_mux3 is supposed to select input number 0.

	    The active requests would be:
	    	- Dummy 1: iho_req_dummy_1 -> References only clk_output_hf13 clock output
	    	- Dummy 2: shared_req_dummy_2 -> References only clk_output_hf11 clock output

	    Since these requests target independent clock outputs, the framework does not generate a conflict
	    at the request level and applies the new configuration on path_mux3 (state request count == active request count), 
	    modifying the frequency of clk_output_hf11. Dummy 2 must register a callback to handle this situation 
	    during the CLOCK_MANAGEMENT_QUERY_RATE_CHANGE phase.
	 */

	ret = clock_management_request_state(config_dummy_1->clk_mgmt, iho_req_dummy_1);
	zassert_equal(ret, 0, "Random, ret=%d", ret);
	
	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, output_2_iho_freq, "clk_output_hf13 rate != shared, ret=%d", ret);

	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 25000000, "clk_output_hf13 rate != shared, ret=%d", ret);
}

#ifdef CONFIG_CLOCK_MANAGEMENT_SET_RATE
ZTEST(clk_runtime_tests, test_request_rate){
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	//const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	clock_freq_t freq = 100000000;
	ret = clock_management_req_rate(config_dummy_1->clk_mgmt, dummy_1_clk_2, freq);
	zassert_equal(ret, 99999998, "Req_rate, ret=%d", ret);
}
#endif
/*
 * Test case for the clock gating.
 */
ZTEST(clk_runtime_tests, test_clock_gating){
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	/*
	 * The count usage service relies on the clock_management_on implementation, which takes into
	 * account the current state. It walks up the clock tree from the clock output until
	 * a root is found.
	*/
	/*
	 * There are four clock_management_on requests, two from dummy_1 (one per clock output), 
	 * one from dummy_2 and one from uart. 
	 * iho_producer is in the clock path of all four requests. Counter must be four.
	 */
	uint8_t iho_counter = iho_producer->subsys_data->usage_cnt;
	zassert_equal(iho_counter, 4, "iho counter != 4, counter=%d", iho_counter);
	/*
	 * Dummy 1 and dummy 2 are requesting clock paths with mux_hf11 in it. Counter must
	   be two.
	 */
	uint8_t mux_hf11_counter = mux_hf11->subsys_data->usage_cnt;
	zassert_equal(mux_hf11_counter, 2, "mux_hf11 counter != 2, counter=%d", mux_hf11_counter); 

	uint8_t mux_hf13_counter = mux_hf13->subsys_data->usage_cnt;
	zassert_equal(mux_hf13_counter, 1, "mux_hf13 counter != 1, counter=%d", mux_hf13_counter); 
	
	uint8_t pll_hp_counter = pll_hp->subsys_data->usage_cnt; 
	zassert_equal(pll_hp_counter, 1, "pll_hp counter != 1, counter=%d", pll_hp_counter);

	uint8_t pll_lp_counter = pll_lp->subsys_data->usage_cnt;
	zassert_equal(pll_lp_counter, 2, "pll_lp counter != 2, counter=%d", pll_lp_counter);

	uint8_t pll_lp0_counter = pll_lp0->subsys_data->usage_cnt;
	zassert_equal(pll_lp0_counter, 1, "pll_lp0 counter != 1, counter=%d", pll_lp0_counter);

	uint8_t path_mux1_counter = path_mux1->subsys_data->usage_cnt;
	zassert_equal(path_mux1_counter, 2, "path_mux1 counter != 2, counter=%d", path_mux1_counter);

	ret = clock_management_off(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);
	
	mux_hf11_counter = mux_hf11->subsys_data->usage_cnt;
	zassert_equal(mux_hf11_counter, 1, "mux_hf11 counter != 1, counter=%d", mux_hf11_counter);
	
	pll_lp_counter = pll_lp->subsys_data->usage_cnt;
	zassert_equal(pll_lp_counter, 1, "pll_lp counter != 1, counter=%d", pll_lp_counter);

	pll_hp_counter = pll_hp->subsys_data->usage_cnt; 
	zassert_equal(pll_hp_counter, 1, "pll_hp counter != 1, counter=%d", pll_hp_counter);

	ret = clock_management_off(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	pll_hp_counter = pll_hp->subsys_data->usage_cnt; 
	zassert_equal(pll_hp_counter, 1, "pll_hp counter != 1, counter=%d", pll_hp_counter);

	mux_hf11_counter = mux_hf11->subsys_data->usage_cnt;
	zassert_equal(mux_hf11_counter, 0, "mux_hf11 counter != 0, counter=%d", mux_hf11_counter); 

	pll_lp_counter = pll_lp->subsys_data->usage_cnt;
	zassert_equal(pll_lp_counter, 0, "pll_lp counter != 0, counter=%d", pll_lp_counter);

	ret = clock_management_request_state(config_dummy_1->clk_mgmt,fast_req_dummy_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	ret = clock_management_request_state(config_dummy_2->clk_mgmt,fast_req_dummy_2);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);
	
	pll_hp_counter = pll_hp->subsys_data->usage_cnt;
	zassert_not_equal(pll_hp_counter, 3, "pll_hp counter == 3, counter=%d", pll_hp_counter);

	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	pll_hp_counter = pll_hp->subsys_data->usage_cnt;
	zassert_equal(pll_hp_counter, 3, "pll_hp counter != 3, counter=%d", pll_hp_counter);
}


ZTEST(clk_runtime_tests, test_locking_clocks){

	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	ret = clock_management_lock(config_dummy_1->clk_mgmt,dummy_1_clk_1);
	zassert_equal(ret, 0, "Failed locking dummy_1_clk_2, ret=%d", ret);

	ret = clock_management_lock(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_not_equal(ret, 0, "Success locking dummy_2_clk_1, ret=%d", ret);

	ret = clock_management_request_state(config_dummy_1->clk_mgmt,all_req_dummy_1);
	zassert_equal(ret, 0, "All state not applied, ret=%d", ret);
	/*
	 * If the consumer that locked a clock ouput requests a new state on that clock output,
	   the locking condition will be deleted if this new request does not have the
	   locking suffix.
	 */
	ret = clock_management_request_state(config_dummy_2->clk_mgmt,locking_req_dummy_2);
	zassert_equal(ret, 0, "Locking state not applied, ret=%d", ret);

	ret = clock_management_request_state(config_dummy_1->clk_mgmt,shared_req_dummy_1);
	zassert_not_equal(ret, 0, "ss, ret=%d", ret);

	ret = clock_management_unlock(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_equal(ret, 0, "Failed unlocking dummy_2_clk_1, ret=%d", ret);
}

ZTEST_SUITE(clk_runtime_tests, NULL, clk_suite_setup, prepare_clock_states, reset_clock_states, NULL);