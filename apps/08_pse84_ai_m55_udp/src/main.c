/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi UDP streaming client for CYW55513 on PSE84.
 *
 * Connects to a Wi-Fi network (configured via shell), waits for a
 * DHCP address, then streams UDP packets to a configurable server.
 *
 * Usage:
 *   1. Flash and boot the board.
 *   2. Connect via shell:  wifi connect -s "SSID" -k 1 -p "password"
 *   3. Start the Python UDP server on the host.
 *   4. Use the shell commands below to configure and start streaming:
 *        udp server <ip> [port]   – set destination (default port 5005)
 *        udp start [interval_ms]  – begin streaming  (default 100 ms)
 *        udp stop                 – stop streaming
 *        udp status               – show current state
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Helper: format IPv4 address into a static buffer */
static char *fmt_ipv4(const struct in_addr *addr)
{
	static char buf[NET_IPV4_ADDR_LEN];

	snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
		 addr->s4_addr[0], addr->s4_addr[1],
		 addr->s4_addr[2], addr->s4_addr[3]);
	return buf;
}

/* ---------- UDP streaming state ---------- */
#define UDP_STACK_SIZE 2048
#define UDP_PRIORITY 7
#define DEFAULT_PORT 5005
#define DEFAULT_INTERVAL_MS 100
#define UDP_PAYLOAD_MAX 128

static K_THREAD_STACK_DEFINE(udp_stack, UDP_STACK_SIZE);
static struct k_thread udp_thread;
static k_tid_t udp_tid;

static struct {
	struct sockaddr_in dest;
	uint32_t interval_ms;
	volatile bool running;
	bool configured;
	uint32_t tx_count;
	uint32_t err_count;
} udp_state;

/* ---------- UDP sender thread ---------- */

static void udp_sender(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket: %d", errno);
		udp_state.running = false;
		return;
	}

	LOG_INF("UDP streaming to %s:%u every %u ms",
		fmt_ipv4(&udp_state.dest.sin_addr),
		ntohs(udp_state.dest.sin_port), udp_state.interval_ms);

	udp_state.tx_count = 0;
	udp_state.err_count = 0;

	while (udp_state.running) {
		char buf[UDP_PAYLOAD_MAX];
		int len = snprintf(buf, sizeof(buf),
				   "{\"seq\":%u,\"uptime_ms\":%llu}",
				   udp_state.tx_count, k_uptime_get());

		int ret = zsock_sendto(sock, buf, len, 0,
				       (struct sockaddr *)&udp_state.dest,
				       sizeof(udp_state.dest));
		if (ret < 0) {
			udp_state.err_count++;
			if (udp_state.err_count <= 5) {
				LOG_WRN("sendto failed: %d", errno);
			}
		} else {
			udp_state.tx_count++;
		}

		k_msleep(udp_state.interval_ms);
	}

	zsock_close(sock);
	LOG_INF("UDP streaming stopped (sent %u, errors %u)",
		udp_state.tx_count, udp_state.err_count);
}

/* ---------- Shell commands ---------- */

static int cmd_udp_server(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: udp server <ip> [port]");
		return -EINVAL;
	}

	memset(&udp_state.dest, 0, sizeof(udp_state.dest));
	udp_state.dest.sin_family = AF_INET;

	if (zsock_inet_pton(AF_INET, argv[1], &udp_state.dest.sin_addr) != 1) {
		shell_error(sh, "Invalid IP: %s", argv[1]);
		return -EINVAL;
	}

	uint16_t port = DEFAULT_PORT;

	if (argc >= 3) {
		port = (uint16_t)strtoul(argv[2], NULL, 10);
	}
	udp_state.dest.sin_port = htons(port);
	udp_state.configured = true;

	shell_print(sh, "UDP server set to %s:%u", argv[1], port);
	return 0;
}

static int cmd_udp_start(const struct shell *sh, size_t argc, char **argv)
{
	if (!udp_state.configured) {
		shell_error(sh, "Set server first: udp server <ip> [port]");
		return -EINVAL;
	}
	if (udp_state.running) {
		shell_warn(sh, "Already streaming");
		return 0;
	}

	udp_state.interval_ms = DEFAULT_INTERVAL_MS;
	if (argc >= 2) {
		udp_state.interval_ms = (uint32_t)strtoul(argv[1], NULL, 10);
		if (udp_state.interval_ms == 0) {
			udp_state.interval_ms = 1;
		}
	}

	udp_state.running = true;
	udp_tid = k_thread_create(&udp_thread, udp_stack,
				  K_THREAD_STACK_SIZEOF(udp_stack), udp_sender,
				  NULL, NULL, NULL, UDP_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(udp_tid, "udp_sender");

	shell_print(sh, "UDP streaming started (%u ms interval)",
		    udp_state.interval_ms);
	return 0;
}

static int cmd_udp_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!udp_state.running) {
		shell_warn(sh, "Not streaming");
		return 0;
	}

	udp_state.running = false;
	k_thread_join(&udp_thread, K_SECONDS(5));

	shell_print(sh, "Stopped (sent %u, errors %u)", udp_state.tx_count,
		    udp_state.err_count);
	return 0;
}

static int cmd_udp_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!udp_state.configured) {
		shell_print(sh, "Server: not configured");
	} else {
		shell_print(sh, "Server: %s:%u",
			    fmt_ipv4(&udp_state.dest.sin_addr),
			    ntohs(udp_state.dest.sin_port));
	}
	shell_print(sh, "State: %s", udp_state.running ? "streaming" : "idle");
	if (udp_state.tx_count || udp_state.err_count) {
		shell_print(sh, "Packets sent: %u, errors: %u",
			    udp_state.tx_count, udp_state.err_count);
	}
	shell_print(sh, "Interval: %u ms", udp_state.interval_ms);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    udp_cmds,
    SHELL_CMD_ARG(server, NULL, "Set destination: udp server <ip> [port]",
		  cmd_udp_server, 2, 1),
    SHELL_CMD_ARG(start, NULL, "Start streaming: udp start [interval_ms]",
		  cmd_udp_start, 1, 1),
    SHELL_CMD(stop, NULL, "Stop streaming", cmd_udp_stop),
    SHELL_CMD(status, NULL, "Show UDP state", cmd_udp_status),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(udp, &udp_cmds, "UDP streaming commands", NULL);

/* ---------- Main ---------- */

int main(void)
{
	LOG_INF("CYW55513 Wi-Fi UDP client on PSE84");
	LOG_INF("Use shell to connect Wi-Fi, then:");
	LOG_INF("  udp server <host_ip> [port]");
	LOG_INF("  udp start [interval_ms]");

	return 0;
}
