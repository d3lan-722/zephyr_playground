/*
 * Wi-Fi bring-up + UDP destination for app 10.
 *
 * Programmatic connect via net_mgmt.  No shell.  Auto-reconnects
 * silently on link loss - the radar loop keeps running and drops
 * frames via sendto(MSG_DONTWAIT) while the link is down.
 */

#ifndef APP_WIFI_CONNECT_H_
#define APP_WIFI_CONNECT_H_

#include <stdbool.h>
#include <zephyr/kernel.h>

/* UDP destination for the radar frame stream.  Host runs
 * apps/08_pse84_ai_m55_udp/host/udp_server.py (or the app-10 fork of it).
 * Edit for your capture host.
 */
#define UDP_SERVER_IP "192.168.178.22"
#define UDP_SERVER_PORT 5005

/**
 * Register net_mgmt callbacks and schedule the first connect attempt.
 * Non-blocking; returns 0 on successful registration, negative errno
 * on setup failure (e.g. no Wi-Fi interface).
 */
int wifi_connect_start(void);

/**
 * Block until the interface is up AND DHCP has bound an IPv4 address.
 * Returns 0 on success, -EAGAIN on timeout.
 */
int wifi_connect_wait_up(k_timeout_t timeout);

/**
 * Non-blocking poll: is the link currently up with an IPv4 address?
 * Safe from any context including the radar acquisition thread.
 */
bool wifi_connect_is_up(void);

#endif /* APP_WIFI_CONNECT_H_ */
