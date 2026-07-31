#include "wifi_connect.h"
#include "wifi_credentials.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wifi_conn, LOG_LEVEL_INF);

#define WIFI_EVENTS                                                            \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)
#define IPV4_EVENTS (NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL)

#define RECONNECT_INITIAL_MS 5000U
#define RECONNECT_MAX_MS 30000U

static struct k_sem link_up_sem;
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static atomic_t link_up = ATOMIC_INIT(0);

static uint32_t backoff_ms = RECONNECT_INITIAL_MS;

static void connect_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);

static struct net_if *get_wifi_iface(void)
{
	/* net_if_get_first_wifi() only exists since Zephyr v3.6+; if it
	 * disappears from your tree fall back to net_if_get_default().
	 */
	struct net_if *iface = net_if_get_first_wifi();

	return iface ? iface : net_if_get_default();
}

static void connect_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	struct net_if *iface = get_wifi_iface();

	if (!iface) {
		LOG_ERR("No Wi-Fi interface, retrying in %u ms", backoff_ms);
		k_work_schedule(&connect_work, K_MSEC(backoff_ms));
		return;
	}

	struct wifi_connect_req_params params = {
	    .ssid = (const uint8_t *)WIFI_SSID,
	    .ssid_length = strlen(WIFI_SSID),
	    .psk = (const uint8_t *)WIFI_PSK,
	    .psk_length = strlen(WIFI_PSK),
	    .security = WIFI_SECURITY_TYPE_PSK,
	    .channel = WIFI_CHANNEL_ANY,
	    .mfp = WIFI_MFP_OPTIONAL,
	    .timeout = SYS_FOREVER_MS,
	};

	LOG_INF("Connecting to SSID '%s'...", WIFI_SSID);

	int ret =
	    net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	if (ret < 0) {
		LOG_WRN("net_mgmt CONNECT rejected (%d), retry in %u ms", ret,
			backoff_ms);
		k_work_schedule(&connect_work, K_MSEC(backoff_ms));
		backoff_ms = MIN(backoff_ms * 2U, RECONNECT_MAX_MS);
	}
}

static void handle_wifi_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	if (status && status->status) {
		LOG_WRN("Wi-Fi connect failed (status=%d), retry in %u ms",
			status->status, backoff_ms);
		k_work_schedule(&connect_work, K_MSEC(backoff_ms));
		backoff_ms = MIN(backoff_ms * 2U, RECONNECT_MAX_MS);
		return;
	}

	LOG_INF("Wi-Fi associated, waiting for DHCP...");
	backoff_ms = RECONNECT_INITIAL_MS;
}

static void handle_wifi_disconnect(void)
{
	atomic_clear(&link_up);
	k_sem_reset(&link_up_sem);
	LOG_WRN("Wi-Fi disconnected, scheduling reconnect");
	k_work_schedule(&connect_work, K_MSEC(backoff_ms));
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		handle_wifi_result(cb);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		handle_wifi_disconnect();
		break;
	default:
		break;
	}
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
		char buf[NET_IPV4_ADDR_LEN];
		struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

		if (ipv4) {
			net_addr_ntop(AF_INET,
				      &ipv4->unicast[0].ipv4.address.in_addr,
				      buf, sizeof(buf));
			LOG_INF("IPv4 bound: %s", buf);
		} else {
			LOG_INF("IPv4 bound");
		}

		atomic_set(&link_up, 1);
		k_sem_give(&link_up_sem);
	} else if (mgmt_event == NET_EVENT_IPV4_ADDR_DEL) {
		atomic_clear(&link_up);
		k_sem_reset(&link_up_sem);
		LOG_WRN("IPv4 address lost");
	}
}

int wifi_connect_start(void)
{
	k_sem_init(&link_up_sem, 0, 1);

	net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
				     WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);

	net_mgmt_init_event_callback(&ipv4_cb, ipv4_mgmt_event_handler,
				     IPV4_EVENTS);
	net_mgmt_add_event_callback(&ipv4_cb);

	k_work_schedule(&connect_work, K_NO_WAIT);
	return 0;
}

int wifi_connect_wait_up(k_timeout_t timeout)
{
	if (atomic_get(&link_up)) {
		return 0;
	}
	return k_sem_take(&link_up_sem, timeout) == 0 ? 0 : -EAGAIN;
}

bool wifi_connect_is_up(void) { return atomic_get(&link_up) != 0; }
