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

/* 
 * Decrement the usage count of the producers shared by both clock outputs. Gate off any producer
 * whose usage count has dropped to zero.
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
 * Test case: Verifies specific clock management functionality.
 * By convention, all test cases must start with 'test_'.
 *
 * This test checks the rates of clock outputs after prepare_clock_states().
 * It then verifies the behavior when an unsupported state (by hardware) is requested.
 */

ZTEST(clk_runtime_tests, test_clock_default)
{
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret, ret_1, ret_2;
	
	/*
     	 * Get rates for both clock outputs. Active state: Default.
     	 */
	
	 ret_1 = clock_management_get_rate(config_dummy_1->clk_mgmt,dummy_1_clk_1);
	zassert_equal(ret_1, output_1_default_freq, "Rate dummy_1_clk_1 dont match, ret=%d", ret_1);

	ret = clock_management_get_rate(config_dummy_1->clk_mgmt,dummy_1_clk_2);
	zassert_equal(ret, output_2_default_freq, "Rate dummy_1_clk_2 dont match, ret=%d", ret);

	/*
     	 * dummy_1_clk_1 and dummy_2_clk_1 map to the same shared clock output.
     	 * Their rates must be identical.
     	 */
	
	 ret_2 = clock_management_get_rate(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_equal(ret_2, output_1_default_freq, "Rate dummy_2_clk_1 dont match, ret=%d", ret_2);
	zassert_equal(ret_1, ret_2, "Rate dummy_2_clk_1 and dummy_1_clk_1 dont match, ret_1=%d, ret_2=%d", ret_1, ret_2);

	/*
    	 * Clock outputs are 0-based within each consumer.
     	 * dummy_device_1 has two clock outputs:
     	 *   - dummy_1_clk_1 is index 0
     	 *   - dummy_1_clk_2 is index 1
     	 * Requesting an index out of range should return an error.
     	 */
	
	 ret = clock_management_get_rate(config_dummy_1->clk_mgmt,2);
	zassert_not_equal(ret, 0, "Test, ret=%d", ret);

	ret = clock_management_off(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, 0, "Test, ret=%d", ret);
	
	/*
    	 * dummy_device_1 updates its request to free clock output 1.
     	 * This ensures the voting system can accept the upcoming unsupported state request
     	 * from dummy_device_2 without conflict.
     	 */
	
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,iho_req_dummy_1);
	zassert_equal(ret, 0, "Test, ret=%d", ret);
	
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_2, ret=%d", ret);

	ret = clock_management_off(config_dummy_2->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Test, ret=%d", ret);
	
	/*
     	 * dummy_device_2 requests a failed state.
     	 * This state attempts to select a non-existent input source in 'path_mux3'.
     	 * The return value should indicate failure.
     	 */

	ret = clock_management_request_state(config_dummy_2->clk_mgmt,failed_req_dummy_2);
	zassert_not_equal(ret, 0, "Test, ret=%d", ret);

	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_2, ret=%d", ret);

	/*
     	 * When a new request is processed, the subsystem configures all producers
     	 * in the state's clock property sequentially.
     	 *
     	 * Example: For the failed state:
     	 *   clocks = <&clk_pilo 1>, <&div_hf11 2>, <&path_mux3 7>, <&mux_hf11 8>;
     	 *
     	 * Sequence:
     	 * 1. Root 'pilo' is enabled.
     	 * 2. Final divider is reconfigured.
     	 * 3. Error occurs when configuring 'path_mux3' (invalid input).
     	 *
     	 * Result: Only a portion of the clock path is configured.
     	 * The resultant frequency is neither the requested value nor the previous one.
     	 */
	ret = clock_management_get_rate(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_not_equal(ret, 0, "Test, ret=%d", ret);

}

/*
 * Tests how a shared clock output behaves when its consumers have no state
 * request in common.
 */

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
 	 * Both dummy consumers request the fast state, so the shared clk_output_hf11
 	 * switches to that state.
 	 */
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,fast_req_dummy_1);
	ret = clock_management_get_rate(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, output_1_fast_freq, "clk_output_hf11 rate != fast, ret=%d", ret);
}

/*
 * Verifies the behavior of a shared producer when one consumer
 * moves its clock output to a state that requires a different producer
 * configuration.
 *
 * Scenario:
 * 1. Both dummy devices request the shared state. This state uses
 *    <&clk_pilo 1> and <&path_mux3 4> as shared producers with the same
 *    configuration.
 * 2. dummy_device_1 requests the iho state on clk_output_hf13, which forces
 *    the shared path_mux3 multiplexer to switch from input 4 to input 0.
 *    This changes the rate of clk_output_hf11 as a side effect, even though
 *    it remains in the shared state. Since both requests target independent
 *    clock outputs, no conflict is generated and the reconfiguration is
 *    applied. The affected consumer (dummy_device_2) must handle this
 *    situation through its CLOCK_MANAGEMENT_QUERY_RATE_CHANGE callback.
 */

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

	/*
     	 * clk_output_hf11 changed as a side effect of the path_mux3
     	 * reconfiguration: its new rate is neither the shared nor the
     	 * default one.
     	 */
	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 25000000, "clk_output_hf13 rate != shared, ret=%d", ret);
}

#ifdef CONFIG_CLOCK_MANAGEMENT_SET_RATE

/*
 * req_rate() determines the best clock configuration for a given request.
 * It defines the state of a clock output dynamically. Starting from the leaf
 * node, it walks up the clock tree:
 *   - If the node is a mux, it iterates over all of its parents and selects
 *     the one that produces the frequency closest to the requested one.
 *   - If the node is a standard clock, it moves directly to its parent and
 *     keeps going up until it reaches a root.
 * From the root, it then walks back down, notifying its children along the
 * way. The mux effectively evaluates each one of its parents during this
 * phase.
 */


ZTEST(clk_runtime_tests, test_request_rate){
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	//const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	clock_freq_t freq = 100000000;
	ret = clock_management_req_rate(config_dummy_1->clk_mgmt, dummy_1_clk_2, freq);

	/*
     	 * The closest achievable rate is 99999998 Hz, limited by the producers
     	 * resolution
     	 */

	zassert_equal(ret, 99999998, "Req_rate, ret=%d", ret);
}
#endif

/*
 * Verifies the clock gating mechanism based on producer usage counts.
 *
 * Each producer keeps a usage count (subsys_data->usage_cnt) of the active
 * clock paths that go through it. The count is updated by clock_management_on()
 * and clock_management_off(), which walk up the clock tree from the clock output
 * until a root is found. A producer is gated off when its count reaches zero.
 *
 * Scenario:
 * 1. Check the initial usage counts of the producers.
 * 2. Turn off the clock outputs one by one and verify that the counts decrease
 *    and that the producers no longer in use reach zero.
 * 3. Request the fast state while the outputs are off and verify that the
 *    counts do not change.
 * 4. Turn the outputs back on and verify that the counts of the producers in
 *    the fast path increase.
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
     	 * Phase 1: Initial usage counts.
     	 *
     	 * There are four clock_management_on() requests: two from dummy_1 (one per
     	 * clock output), one from dummy_2 and one from uart.
     	 * iho_producer is in the clock path of all four requests, so its count
     	 * must be four.
     	 */

	uint8_t iho_counter = iho_producer->subsys_data->usage_cnt;
	zassert_equal(iho_counter, 4, "iho counter != 4, counter=%d", iho_counter);
	
	/*
    	 * dummy_1 and dummy_2 each request a clock path that includes mux_hf11,
     	 * so its count must be two. mux_hf13 is only used by dummy_1's second
     	 * clock output, so its count must be one.
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

	/*
     	 * Phase 2: Gate off the shared clock output, one consumer at a time.
     	 *
     	 * dummy_1 turns off dummy_1_clk_1. Only one of the two paths through
     	 * mux_hf11 and pll_lp is released. pll_hp is not part of that path, so
     	 * its count must not change.
     	 */

	ret = clock_management_off(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);
	
	mux_hf11_counter = mux_hf11->subsys_data->usage_cnt;
	zassert_equal(mux_hf11_counter, 1, "mux_hf11 counter != 1, counter=%d", mux_hf11_counter);
	
	pll_lp_counter = pll_lp->subsys_data->usage_cnt;
	zassert_equal(pll_lp_counter, 1, "pll_lp counter != 1, counter=%d", pll_lp_counter);

	pll_hp_counter = pll_hp->subsys_data->usage_cnt; 
	zassert_equal(pll_hp_counter, 1, "pll_hp counter != 1, counter=%d", pll_hp_counter);

	/*
     	 * dummy_2 turns off dummy_2_clk_1. This releases the last path through
     	 * mux_hf11 and pll_lp, so both reach zero and are gated off. pll_hp is
     	 * still used by another active request, so it stays on.
      	 */
	
	 ret = clock_management_off(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	pll_hp_counter = pll_hp->subsys_data->usage_cnt; 
	zassert_equal(pll_hp_counter, 1, "pll_hp counter != 1, counter=%d", pll_hp_counter);

	mux_hf11_counter = mux_hf11->subsys_data->usage_cnt;
	zassert_equal(mux_hf11_counter, 0, "mux_hf11 counter != 0, counter=%d", mux_hf11_counter); 

	pll_lp_counter = pll_lp->subsys_data->usage_cnt;
	zassert_equal(pll_lp_counter, 0, "pll_lp counter != 0, counter=%d", pll_lp_counter);

	/*
     	 * Phase 3: Request the fast state.
     	 *
     	 * The new state is configured, but since the clock outputs are gated
     	 * off, the usage counts of the producers in the fast path (e.g. pll_hp)
     	 * must not be increased yet.
     	 */
	
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,fast_req_dummy_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	ret = clock_management_request_state(config_dummy_2->clk_mgmt,fast_req_dummy_2);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);
	
	pll_hp_counter = pll_hp->subsys_data->usage_cnt;
	zassert_not_equal(pll_hp_counter, 3, "pll_hp counter == 3, counter=%d", pll_hp_counter);

	/*
     	 * Phase 4: Turn the outputs back on.
     	 *
     	 * Both consumers now go through pll_hp in the fast state. Together with
     	 * the request that was already using it, its count must be three.
     	 */

	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	pll_hp_counter = pll_hp->subsys_data->usage_cnt;
	zassert_equal(pll_hp_counter, 3, "pll_hp counter != 3, counter=%d", pll_hp_counter);
}

/*
 * Verifies the clock output locking mechanism.
 *
 * A consumer can lock a clock output to prevent other consumers from
 * reconfiguring it. While locked:
 *   - Only the consumer that performed the lock can request new states.
 *   - A new state request from the owner removes the lock if the new state
 *     does not carry the locking suffix.
 *   - Other consumers cannot lock the same output or request a state on it.
 *
 * Scenario:
 * 1. dummy_1 locks its clock output (success).
 * 2. dummy_2 tries to lock the same shared clock output (fails).
 * 3. dummy_1 requests a new non-locking state: the lock is removed.
 * 4. dummy_2 locks its clock output with a locking state (success).
 * 5. dummy_1 requests a state on the locked output (fails).
 * 6. dummy_2 unlocks its clock output (success).
 */

ZTEST(clk_runtime_tests, test_locking_clocks){

	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;

	/*
     	 * dummy_1 locks dummy_1_clk_1. This output is shared with dummy_2's
     	 * clock output, so it protects the whole shared clock output.
     	 */
	ret = clock_management_lock(config_dummy_1->clk_mgmt,dummy_1_clk_1);
	zassert_equal(ret, 0, "Failed locking dummy_1_clk_2, ret=%d", ret);

	/*
     	 * dummy_2 tries to lock the same shared clock output. It must fail
     	 * because the output is already locked by another consumer.
     	 */
	ret = clock_management_lock(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_not_equal(ret, 0, "Success locking dummy_2_clk_1, ret=%d", ret);

	/*
     	 * dummy_1 requests a new non-locking state on its clock output.
     	 * This removes the previous lock condition.
     	 */
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,all_req_dummy_1);
	zassert_equal(ret, 0, "All state not applied, ret=%d", ret);
	
	/*
     	 * dummy_2 requests a locking state on its clock output. Since the
     	 * previous lock was removed, this succeeds.
     	 */
	ret = clock_management_request_state(config_dummy_2->clk_mgmt,locking_req_dummy_2);
	zassert_equal(ret, 0, "Locking state not applied, ret=%d", ret);

	/*
     	 * dummy_1 requests a state on the shared clock output, which is now
     	 * locked by dummy_2. It must fail.
     	 */
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,shared_req_dummy_1);
	zassert_not_equal(ret, 0, "ss, ret=%d", ret);

	/*
     	 * dummy_2 unlocks its clock output.
     	 */
	ret = clock_management_unlock(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_equal(ret, 0, "Failed unlocking dummy_2_clk_1, ret=%d", ret);
}

ZTEST_SUITE(clk_runtime_tests, NULL, clk_suite_setup, prepare_clock_states, reset_clock_states, NULL);