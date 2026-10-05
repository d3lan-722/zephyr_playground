#include <zephyr/ztest.h>
#include <zephyr/drivers/clock_management.h>
#include <zephyr/sys/sys_io.h>
#include <cy_sysclk.h>

#define DUMMY_CONSUMER_1 DT_NODELABEL(dummy_clock_consumer_1)
#define DUMMY_CONSUMER_2 DT_NODELABEL(dummy_clock_consumer_2)

struct ifx_clock_consumer_config {
	const struct clock_management_data *clk_mgmt;
};

static const struct device *dummy_device_1 = DEVICE_DT_GET(DUMMY_CONSUMER_1);
static const struct device *dummy_device_2 = DEVICE_DT_GET(DUMMY_CONSUMER_2);
static const struct clk *iho_producer = CLOCK_DT_GET(DT_NODELABEL(clk_iho));
static const struct clk *mux_hf11 = CLOCK_DT_GET(DT_NODELABEL(mux_hf11));
static const struct clk *mux_hf13 = CLOCK_DT_GET(DT_NODELABEL(mux_hf13));
static const struct clk *pll_lp = CLOCK_DT_GET(DT_NODELABEL(dpll_lp1));
static const struct clk *pll_hp = CLOCK_DT_GET(DT_NODELABEL(dpll_hp));
static const struct clk *path_mux1 = CLOCK_DT_GET(DT_NODELABEL(path_mux1));

static const clock_request_t default_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, default);
static const clock_request_t default_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, default);
static const clock_request_t fast_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, fast);
static const clock_request_t fast_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, fast);
static const clock_request_t shared_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, shared);
static const clock_request_t shared_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, shared);
static const clock_request_t iho_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, iho);
static const clock_request_t all_req_dummy_1 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_1, all);
static const clock_request_t locking_req_dummy_2 = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER_2, locking);

static const clock_output_t dummy_1_clk_1 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_1, clock_output_1_dummy_1);
static const clock_output_t dummy_1_clk_2 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_1, clock_output_2_dummy_1);
static const clock_output_t dummy_2_clk_1 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER_2, clock_output_1_dummy_2);

static const uint32_t output_1_default_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_default_state),clock_frequency);
static const uint32_t output_2_default_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_default_state),clock_frequency);
static const uint32_t output_1_fast_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_fast_state),clock_frequency);
static const uint32_t output_1_shared_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_shared_state),clock_frequency);
static const uint32_t output_2_shared_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_shared_state),clock_frequency);
static const uint32_t output_2_iho_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_iho_state),clock_frequency);


/*
static const clock_request_t slow_req = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER, slow);
static const clock_request_t wrong_req = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER, wrong);
static const uint32_t output_1_slow_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_slow_state),clock_frequency); 
static const uint32_t output_2_default_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_default_state),clock_frequency); 
*/



/*
 * Se llama el callback de todos los consumers atados a un clock output que va a ser reconfigurado.
   Al ejecutar default_req_dummy_1 busca modificar clock output del dummy 1 y clock output del dummy 2
   Llama callbacks de ambos dummies.
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
	 * Must be called after setting the state. Otherwise count usage value will be wrong.
	 */
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_1, ret=%d", ret);
	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, 0, "Failed to power on ouput2 from dummy_1, ret=%d", ret);
	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Failed to power on ouput1 from dummy_2, ret=%d", ret);
}

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
}


/*
Test case. Verifies a specific and well defined functionality;
by default all test cases shall be start with test_
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
	ret_2 = clock_management_get_rate(config_dummy_2->clk_mgmt,dummy_2_clk_1);
	zassert_equal(ret_2, output_1_default_freq, "Rate dummy_2_clk_1 dont match, ret=%d", ret_2);
	zassert_equal(ret_1, ret_2, "Rate dummy_2_clk_1 and dummy_1_clk_1 dont match, ret_1=%d, ret_2=%d", ret_1, ret_2);
}

ZTEST(clk_runtime_tests, test_not_shared_state)
{
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	/*
	 * Dummy 2 is driven by clk_output_hf11. This test is executed after reset_clock_states, which
	   puts clk_output_hf11 into the default state.

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
	   (i.e., the vote count for a state must equal the total number of active requests).

	   Therefore, clk_output_hf11 has no state with 2 votes (there is a 1–1 split), so the framework keeps its current state. 
	   In this case, it should also raise an error to indicate a conflicting vote. Currently, no error is generated.			
	 */

	ret = clock_management_request_state(config_dummy_2->clk_mgmt,fast_req_dummy_2);
	zassert_equal(ret, 0, "State applied, ret=%d", ret); //ret should be different to 0.
	//
	ret = clock_management_get_rate(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_not_equal(ret, output_1_fast_freq, "clk_output_hf11 rate == fast, ret=%d", ret);
	zassert_equal(ret, output_1_default_freq, "clk_output_hf11 rate != default, ret=%d", ret);
	ret = clock_management_request_state(config_dummy_1->clk_mgmt,fast_req_dummy_1);
	ret = clock_management_get_rate(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, output_1_fast_freq, "clk_output_hf11 rate != fast, ret=%d", ret);
}

ZTEST(clk_runtime_tests, test_shared_producer){
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	/*
	 * clk_output_hf11 state will no change because shared state request < active requests
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
	    at the request level and applies the new configuration on path_mux3 (state request == active request), 
	    modifying the frequency of clk_output_hf11. Dummy 2 must register a callback to handle this situation 
	    during the CLOCK_MANAGEMENT_PRE_RATE_CHANGE phase.
	 */

	ret = clock_management_request_state(config_dummy_1->clk_mgmt, iho_req_dummy_1);
	zassert_equal(ret, 0, "Random, ret=%d", ret);
	
	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_2);
	zassert_equal(ret, output_2_iho_freq, "clk_output_hf13 rate != shared, ret=%d", ret);

	ret = clock_management_get_rate(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 25000000, "clk_output_hf13 rate != shared, ret=%d", ret);
	/*
	ret = clock_management_get_rate(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, output_1_shared_freq, "clk_output_hf11 rate != shared from dummy_2, ret=%d", ret);
	*/
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

ZTEST(clk_runtime_tests, test_clock_gating){
	const struct ifx_clock_consumer_config *config_dummy_1 = dummy_device_1->config;
	const struct ifx_clock_consumer_config *config_dummy_2 = dummy_device_2->config;
	int ret;
	/*
	 * The count usage service relies on the clock_management_on implementation, which takes into
	   account the current state. It walks up the clock tree from the clock output until
	   a root is found.
	*/
	/*
	 * There are three clock_management_on requests, two from dummy_1, one from dumm_2
	   iho_producer is in the clock path of all three requests. Counter must be three.
	 */
	uint8_t iho_counter = iho_producer->subsys_data->usage_cnt;
	zassert_equal(iho_counter, 3, "iho counter != 3, counter=%d", iho_counter);
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
	zassert_not_equal(pll_hp_counter, 3, "pll_hp counter != 1, counter=%d", pll_hp_counter);

	ret = clock_management_on(config_dummy_1->clk_mgmt, dummy_1_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	ret = clock_management_on(config_dummy_2->clk_mgmt, dummy_2_clk_1);
	zassert_equal(ret, 0, "Fast state not applied, ret=%d", ret);

	pll_hp_counter = pll_hp->subsys_data->usage_cnt;
	zassert_equal(pll_hp_counter, 3, "pll_hp counter != 1, counter=%d", pll_hp_counter);
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

ZTEST_SUITE(clk_runtime_tests, NULL, NULL, prepare_clock_states, reset_clock_states, NULL);