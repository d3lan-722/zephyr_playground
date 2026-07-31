/*
 * PSE84 CM55 UDP Radar Streamer - Phase 3.
 *
 * Single-thread pattern:
 *   wait_fifo_ready (blocks on GPIO IRQ semaphore)
 *   get_fifo_data   (SPI burst read + 12-bit unpack)
 *   build 272 B packet: 16 B header ('BGTR' | seq | timestamp_ns)
 *                       + 256 B samples (128 x u16 LE)
 *   zsock_send(sock, buf, 272, MSG_DONTWAIT)
 *
 * If the Wi-Fi link is down or the TX stack is congested, sendto
 * returns -EAGAIN / -ENOTCONN.  We increment a drop counter and keep
 * running - the radar loop never stalls on the network.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>

#include "bgt60tr13c.h"
#include "radar_config.h"
#include "wifi_connect.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct device *radar_sensor =
    DEVICE_DT_GET(DT_ALIAS(radar_sensor));

#define NUM_SAMPLES RADAR_NUM_SAMPLES_PER_FRAME	       /* 128 */
#define SAMPLES_BYTES (NUM_SAMPLES * sizeof(uint16_t)) /* 256 */
#define PACKET_HDR_BYTES 16
#define PACKET_BYTES (PACKET_HDR_BYTES + SAMPLES_BYTES) /* 272 */

/* Frame IRQ wait budget: chirp cadence is 5 ms; 50 ms tolerates ~10x slack. */
#define FIFO_IRQ_TIMEOUT K_MSEC(50)
#define WIFI_UP_TIMEOUT K_SECONDS(30)

/* Wire-format magic 'BGTR' little-endian.  Host script tests
 * packet[0..3] == "BGTR" to distinguish from other UDP flows.
 */
#define PACKET_MAGIC 0x52544742U /* 'R','T','G','B' in LE memory order */

/* Log a running summary every N frames (5 ms cadence -> ~5 s per 1000). */
#define SUMMARY_EVERY_N_FRAMES 1000U

static uint16_t samples[NUM_SAMPLES];
static uint8_t packet[PACKET_BYTES];

/**
 * Fill @p packet in place with the radar-stream wire format.
 * Header layout matches the parent implementation plan (item 7).
 */
static void build_packet(uint32_t seq, uint64_t timestamp_ns,
			 const uint16_t *buf, uint32_t count)
{
	sys_put_le32(PACKET_MAGIC, &packet[0]);
	sys_put_le32(seq, &packet[4]);
	sys_put_le64(timestamp_ns, &packet[8]);

	/* Samples: little-endian on the wire regardless of host word order. */
	for (uint32_t i = 0; i < count; i++) {
		sys_put_le16(buf[i], &packet[PACKET_HDR_BYTES + i * 2]);
	}
}

/**
 * Open one UDP socket connected to the destination.  Returns socket fd
 * on success, negative errno on failure.
 */
static int udp_socket_open(void)
{
	int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock < 0) {
		LOG_ERR("socket() failed: %d", errno);
		return -errno;
	}

	struct sockaddr_in dest = {
	    .sin_family = AF_INET,
	    .sin_port = htons(UDP_SERVER_PORT),
	};

	if (zsock_inet_pton(AF_INET, UDP_SERVER_IP, &dest.sin_addr) != 1) {
		LOG_ERR("Invalid UDP_SERVER_IP '%s'", UDP_SERVER_IP);
		zsock_close(sock);
		return -EINVAL;
	}

	/* connect() a UDP socket so subsequent send() can skip the address. */
	if (zsock_connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
		LOG_ERR("connect() failed: %d", errno);
		zsock_close(sock);
		return -errno;
	}

	LOG_INF("UDP socket -> %s:%u", UDP_SERVER_IP, UDP_SERVER_PORT);
	return sock;
}

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

	if (wifi_connect_wait_up(WIFI_UP_TIMEOUT) < 0) {
		LOG_WRN("Wi-Fi not yet up after timeout - continuing (frames "
			"drop until reconnect succeeds in background)");
	}

	int sock = udp_socket_open();

	if (sock < 0) {
		return sock;
	}

	const struct bgt60tr13c_api *api = radar_sensor->api;

	LOG_INF("Configuring sensor (%u registers)...",
		(unsigned int)RADAR_REGS_LEN);
	ret = api->config(radar_sensor, radar_regs, RADAR_REGS_LEN);
	if (ret < 0) {
		LOG_ERR("config failed: %d", ret);
		return ret;
	}

	ret = api->set_fifo_limit(radar_sensor, NUM_SAMPLES);
	if (ret < 0) {
		LOG_ERR("set_fifo_limit failed: %d", ret);
		return ret;
	}

	ret = api->start_frame(radar_sensor, true);
	if (ret < 0) {
		LOG_ERR("start_frame failed: %d", ret);
		return ret;
	}
	LOG_INF("Streaming started - %u B packets to %s:%u", PACKET_BYTES,
		UDP_SERVER_IP, UDP_SERVER_PORT);

	uint32_t seq = 0;
	uint32_t sent_ok = 0;
	uint32_t dropped = 0;

	for (;;) {
		ret = api->wait_fifo_ready(radar_sensor, FIFO_IRQ_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("wait_fifo_ready failed: %d", ret);
			break;
		}

		ret = api->get_fifo_data(radar_sensor, samples, NUM_SAMPLES);
		if (ret < 0) {
			LOG_ERR("get_fifo_data failed: %d", ret);
			break;
		}

		build_packet(seq, k_uptime_get() * 1000000ULL, samples,
			     NUM_SAMPLES);

		ret =
		    zsock_send(sock, packet, PACKET_BYTES, ZSOCK_MSG_DONTWAIT);
		if (ret == PACKET_BYTES) {
			sent_ok++;
		} else {
			dropped++;
		}

		if ((seq + 1U) % SUMMARY_EVERY_N_FRAMES == 0U) {
			LOG_INF("seq=%u sent=%u dropped=%u link=%s", seq + 1U,
				sent_ok, dropped,
				wifi_connect_is_up() ? "up" : "DOWN");
		}

		seq++;
	}

	api->start_frame(radar_sensor, false);
	zsock_close(sock);
	LOG_INF("Streaming stopped: seq=%u sent=%u dropped=%u", seq, sent_ok,
		dropped);
	return ret;
}
