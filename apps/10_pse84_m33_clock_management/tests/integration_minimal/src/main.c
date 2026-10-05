#include <zephyr/ztest.h>
#include <zephyr/drivers/clock_management.h>
#include <zephyr/sys/sys_io.h>
#include <cy_sysclk.h>

#define DUMMY_CONSUMER DT_NODELABEL(dummy_clock_consumer)

struct ifx_clock_consumer_config {
	const struct clock_management_data *clk_mgmt;
};

static const struct device *dummy_device = DEVICE_DT_GET(DUMMY_CONSUMER);
static const clock_request_t default_req = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER, default);
static const clock_request_t slow_req = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER, slow);
static const clock_request_t wrong_req = CLOCK_MANAGEMENT_DT_GET_REQUEST(DUMMY_CONSUMER, wrong);
static const clock_output_t dummy_clk_1 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER, clock_output_1);
static const clock_output_t dummy_clk_2 = CLOCK_MANAGEMENT_DT_GET_OUTPUT_BY_NAME(DUMMY_CONSUMER, clock_output_2);
static const uint32_t output_1_default_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_default_state),clock_frequency);
static const uint32_t output_1_slow_freq = DT_PROP(DT_NODELABEL(clk_output_hf11_slow_state),clock_frequency); 
static const uint32_t output_2_default_freq = DT_PROP(DT_NODELABEL(clk_output_hf13_default_state),clock_frequency); 

void reset_clock_states(void *unused)
{
	ARG_UNUSED(unused);
	const struct ifx_clock_consumer_config *config = dummy_device->config;
	int ret;
	ret = clock_management_request_state(config->clk_mgmt,default_req);
	zassert_equal(ret, 0, "Failed to apply reset state, ret=%d", ret);
}

/*
Test case. Verifies a specific and well defined functionality;
by default all test cases shall be start with test_
*/

ZTEST(dummy_minimal_tests, test_clock_output_1)
{
	int ret;
	const struct ifx_clock_consumer_config *config = dummy_device->config;
	ret = clock_management_request_state(config->clk_mgmt,default_req);
	zassert_equal(ret, 0, "Failed to apply default state, ret_value=%08x", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_1);
	zassert_equal((uint32_t)ret, output_1_default_freq, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_on(config->clk_mgmt,dummy_clk_1);
	zassert_equal(ret, 0, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_off(config->clk_mgmt,dummy_clk_1);
	zassert_equal(ret, 0, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_on(config->clk_mgmt,dummy_clk_1);
	zassert_equal(ret, 0, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_1);
	zassert_equal((uint32_t)ret, output_1_default_freq, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_off(config->clk_mgmt,dummy_clk_1);
	zassert_equal(ret, 0, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_1);
	zassert_equal((uint32_t)ret, output_1_default_freq, "Invalid clock default, ret_value=%d", ret);
}

ZTEST(dummy_minimal_tests, test_slow_state)
{
	const struct ifx_clock_consumer_config *config = dummy_device->config;
	int ret;
	ret = clock_management_request_state(config->clk_mgmt,slow_req);
	zassert_equal(ret, 0, "Faile to apply slow state, ret_value=%08x", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_1);
	zassert_equal((uint32_t)ret, output_1_slow_freq, "Rates dont match, ret_value=%d", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_2);
	zassert_equal((uint32_t)ret, output_2_default_freq, "Rates dont match, ret_value=%d", ret);
	ret = clock_management_off(config->clk_mgmt,dummy_clk_2);
	zassert_equal(ret, 0, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_2);
	zassert_equal((uint32_t)ret, output_2_default_freq, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_on(config->clk_mgmt,dummy_clk_2);
	zassert_equal(ret, 0, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_1);
	zassert_equal((uint32_t)ret, output_1_slow_freq, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_off(config->clk_mgmt,dummy_clk_1);
	zassert_equal(ret, 0, "Invalid clock default, ret_value=%d", ret);
	ret = clock_management_get_rate(config->clk_mgmt,dummy_clk_1);
	zassert_equal((uint32_t)ret, output_1_slow_freq, "Invalid clock default, ret_value=%d", ret);
}


ZTEST(dummy_minimal_tests, test_wrong_state)
{
	const struct ifx_clock_consumer_config *config = dummy_device->config;
	int ret;
	ret = clock_management_request_state(config->clk_mgmt,wrong_req);
	zassert_not_equal(ret, 0, "Wrong state applied, ret_value=%08x", ret);
	/*PLL does not lock - Very low input freq*/
}



/*
 * Sequential order: Full clock_output_1 test followed by full clock_output_2 test.
                     Starts with clock path off.
 */




ZTEST_SUITE(dummy_minimal_tests, NULL, NULL, reset_clock_states, NULL, NULL);