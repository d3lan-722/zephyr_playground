#ifndef INFINEON_UART_HEADER_H_
#define INFINEON_UART_HEADER_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the clock_output_t handle associated with this UART device.
 *
 * @param dev UART device instance.
 * @return clock_output_t index into the device's clock_management_data.
 */
uint8_t ifx_cat1_uart_get_macro_val(const struct device *dev);

/**
 * @brief Enable the clock output (and its parent producers) for this UART.
 *
 * @param dev UART device instance.
 * @return 0 on success, negative errno on failure.
 */
int ifx_cat1_uart_clk_on(const struct device *dev);

/**
 * @brief Disable the clock output (and its parent producers) for this UART.
 *
 * @param dev UART device instance.
 * @return 0 on success, negative errno on failure.
 */
int ifx_cat1_uart_clk_off(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* INFINEON_UART_HEADER_H_ */