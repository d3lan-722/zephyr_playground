/*
 * PSE84 CM55 UDP Radar Streamer.
 *
 * Phase 2: Wi-Fi bring-up only.  Verifies programmatic net_mgmt connect
 * to the SSID/PSK in wifi_credentials.h and DHCPv4 address binding.
 * The radar driver is still initialised so we can confirm nothing about
 * the SPI/GPIO/IRQ stack breaks when the network stack shares the SoC.
 *
 * Phase 3 will re-add the frame acquisition loop and start streaming.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bgt60tr13c.h"
#include "radar_config.h"
#include "wifi_connect.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct device *radar_sensor =
	DEVICE_DT_GET(DT_ALIAS(radar_sensor));

#define WIFI_UP_TIMEOUT K_SECONDS(30)

int main(void)
{
	LOG_INF("Hello from project 10_pse84_ai_m55_udp_radar on board %s",
		CONFIG_BOARD_TARGET);

	if (!device_is_ready(radar_sensor)) {
		LOG_ERR("RADAR sensor device is not ready");
		return -ENODEV;
	}
	LOG_INF("Radar sensor ready (CHIP_ID verified during init)");

	int ret = wifi_connect_start();
	if (ret < 0) {
		LOG_ERR("wifi_connect_start failed: %d", ret);
		return ret;
	}

	LOG_INF("Waiting up to 30 s for Wi-Fi + DHCP...");

	if (wifi_connect_wait_up(WIFI_UP_TIMEOUT) < 0) {
		LOG_WRN("Wi-Fi not yet up after timeout - continuing "
			"(auto-reconnect keeps trying in the background)");
	} else {
		LOG_INF("Wi-Fi link up, ready for Phase 3 (UDP streaming)");
	}

	/* Idle so we can observe reconnect events in the log. */
	while (1) {
		k_sleep(K_SECONDS(10));
		LOG_INF("Heartbeat: wifi_connect_is_up() = %s",
			wifi_connect_is_up() ? "true" : "false");
	}

	return 0;
}
