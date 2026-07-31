# App 10 — PSE84 CM55 UDP Radar Streamer — Implementation Plan

Combines the BGT60TR13C radar acquisition from
[apps/09_pse84_ai_m33_radar/](../09_pse84_ai_m33_radar/) with the Wi-Fi
UDP streaming pattern from [apps/08_pse84_ai_m55_udp/](../08_pse84_ai_m55_udp/)
to push raw ADC frames to a host running
[apps/08_pse84_ai_m55_udp/host/udp_server.py](../08_pse84_ai_m55_udp/host/udp_server.py)
for offline analysis (FFT, range-Doppler, ML training data).

---

## Scope decisions (locked in)

- **Target core: CM55** (not CM33 as the parent plan item 7 initially suggested). The
  [kit_pse84_ai_m55.dts](../../../../home/ubuntu/zephyrproject/zephyr/boards/infineon/kit_pse84_ai/kit_pse84_ai_m55.dts#L102)
  is the only place the `airoc-wifi` node lives, and the board
  [Kconfig.defconfig](../../../../home/ubuntu/zephyrproject/zephyr/boards/infineon/kit_pse84_ai/Kconfig.defconfig)
  gates the AIROC + CYW55500 + Murata module selection to `M55` only.
  Adding Wi-Fi to CM33 would be a board-support port; moving the radar
  to CM55 is a one-file overlay change.
- **Wi-Fi credentials in a git-ignored header.** No shell, no runtime
  input. The app calls `net_mgmt(NET_REQUEST_WIFI_CONNECT, ...)` once
  at boot with the `#define`-d SSID/PSK.
- **UDP destination hardcoded** in `wifi_connect.h` (non-secret — server IP
  and port). Not gitignored.
- **Link-loss behaviour: option (c) — drop frames silently until link is up.**
  The radar loop never blocks on Wi-Fi; `zsock_sendto()` returns `-ENOMEM`
  or similar when the link is down and we just increment a drop counter.
  A separate connection-monitor callback kicks a reconnect thread.
- **No `CONFIG_SHELL` and friends** — estimated save 25–40 KB FLASH and
  8–10 KB RAM vs. app 08's shell-based flow.

---

## Architecture

```
   ┌─────────────────┐          ┌─────────────────┐
   │  radar thread   │          │ wifi_mgr thread │
   │ (main)          │          │ (callback-only) │
   │                 │          │                 │
   │ get_fifo_ready  │          │ net_mgmt events │
   │        │        │          │        │        │
   │ get_fifo_data   │          │ auto-reconnect  │
   │        │        │          │                 │
   │ build packet    │          │                 │
   │        │        │          │                 │
   │ zsock_sendto ───┼──►───────┼──► AIROC/WHD ──►│──► UDP ──► host
   │  (never blocks) │          │                 │
   └─────────────────┘          └─────────────────┘
```

Single-thread radar loop; Wi-Fi lives entirely in the mgmt callback and
kernel work queues. `sendto` uses `MSG_DONTWAIT` so a missing / dropped
link never stalls the radar-driver's FIFO drain.

---

## Wire format (from parent plan item 7)

```
offset  size  field
0       4     magic          = 0x42475452   ('BGTR', little-endian)
4       4     seq            (u32 frame counter, wrap ok)
8       8     timestamp_ns   (k_uptime_ns() at get_fifo_data completion)
16      256   samples[128]   (u16 little-endian, native from the driver)
```

Total 272 B per packet. 200 fps at 5 ms cadence = 54.4 kB/s wire =
435 kbit/s — trivial on Wi-Fi.

---

## Phased implementation

### Phase 1 — Skeleton on CM55 (radar only, no Wi-Fi)

Verify the radar driver runs on CM55. This is the highest-risk step
because `scb3_clock_fix.c` was written for CM33 quirks and might behave
differently on CM55.

1. Create the app dir (this file already exists).
2. `CMakeLists.txt` — mirror app 09, `ZEPHYR_EXTRA_MODULES` for the driver.
3. `prj.conf` — copy app 09 (radar + LOG, no networking yet).
4. `boards/kit_pse84_ai_pse846gps2dbzc4a_m55.overlay` — the SCB3 SPI +
   `bgt60tr13c@0` block from app 09's overlay, adapted to whatever CM55
   sees for gpio ports and pin controllers.
5. `src/main.c` — the app 09 acquisition loop, unchanged (10 frames + Done).
6. `src/radar_config.h` — copy of app 09's file, may retune later.
7. `src/scb3_clock_fix.c` — copy verbatim; **verify** whether the fix is
   needed on CM55 (it may be a no-op or may need different registers).

**Exit criteria**: same 10-frame log as app 09 but with `<inf> main` from
CM55.

### Phase 2 — Wi-Fi bring-up (no radar data)

Get `wifi status` reporting connected, DHCP address bound, without
touching the radar loop.

1. `src/wifi_credentials.h` — `#define WIFI_SSID` and `#define WIFI_PSK`.
   **Gitignored.**
2. `src/wifi_credentials.h.example` — committed template with `"YOUR_SSID"`
   / `"YOUR_PSK"` placeholders.
3. `.gitignore` — one line: `src/wifi_credentials.h`.
4. `README.md` — bootstrap instructions (copy .example to .h, edit).
5. `src/wifi_connect.h` — public API (`wifi_connect_start(void)`),
   `#define UDP_SERVER_IP "192.168.x.y"`, `#define UDP_SERVER_PORT 5005`.
6. `src/wifi_connect.c` — programmatic connect via `net_mgmt`:
   - Register `NET_EVENT_WIFI_CONNECT_RESULT` + `NET_EVENT_WIFI_DISCONNECT_RESULT`.
   - On boot: `net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof)`.
   - On disconnect: retry every 5 s with exponential backoff to 30 s max.
   - Expose a `wifi_connect_is_up()` boolean the radar thread can peek at.
7. `prj.conf` — add the networking block from app 08 (`CONFIG_NETWORKING`,
   `CONFIG_NET_IPV4`, `CONFIG_NET_UDP`, `CONFIG_NET_SOCKETS`,
   `CONFIG_NET_DHCPV4`, `CONFIG_WIFI`, buffer counts, heap). **No shell.**
8. `main.c` — just call `wifi_connect_start()` and sleep, so we can
   observe the connect events in the log.

**Exit criteria**: log shows association → DHCP → address bound.

### Phase 3 — Combine: UDP streaming

Merge phases 1 and 2. Radar loop builds packets, calls `zsock_sendto`
with `MSG_DONTWAIT`, counts drops.

1. `src/main.c`:
   - After radar init, call `wifi_connect_start()`.
   - Wait up to N seconds for `wifi_connect_is_up()`; log a warning if not
     yet up but proceed (radar keeps running, packets are dropped).
   - Create one UDP socket, connect to `UDP_SERVER_IP:UDP_SERVER_PORT`
     (so `zsock_send` can be used instead of `sendto`).
   - Replace the "10 frames + Done" limit with an infinite loop.
   - Every frame: `wait_fifo_ready` → `get_fifo_data` → build 272 B
     packet → `zsock_send(sock, buf, 272, MSG_DONTWAIT)`.
   - On `-EAGAIN` / `-ENOTCONN` / `-ENOMEM`: increment `drop_count`,
     `LOG_WRN` every 100 drops (rate-limited).
2. Prime buffers: `CONFIG_NET_PKT_TX_COUNT`, `CONFIG_NET_BUF_TX_COUNT`
   should be enough that `sendto` never blocks the radar loop. Start
   with app 08's numbers (10, 20) and only raise if we see drops with
   the link up.
3. `LOG_INF` a running summary every N frames (e.g. every 1000 frames =
   every 5 seconds): sent count, drop count, RSSI.

**Exit criteria**: `udp_server.py` receives 200 packets/s continuously,
`min/max/mean` per packet match the on-target log (which will be
suppressed to avoid console back-pressure).

### Phase 4 — Host-side script extension

Extend the existing `apps/08_pse84_ai_m55_udp/host/udp_server.py` (or
fork into a dedicated `apps/10_.../host/udp_server.py`) to detect the
'BGTR' magic and:

- Print a compact summary line every 100 packets (seq range, timestamp
  delta, packet loss).
- Optionally dump the raw sample block to a `session_YYYYMMDD_HHMMSS.raw`
  file (`--output-raw`).
- Optionally decode and print per-packet `min/max/mean` (`--verbose`).

Fork rather than extend: parent-app's host script is text-JSON only.
Keep both stand-alone.

**Exit criteria**: 60 seconds of streaming produces a `.raw` file with
`60 × 200 × 256 = 3,072,000` bytes and packet-loss stats reported.

---

## Files to create

| Path | Purpose | Committed |
|---|---|---|
| `apps/10_pse84_ai_m55_udp_radar/implementation_plan.md` | this file | yes (already exists) |
| `.gitignore` | excludes `src/wifi_credentials.h` | yes |
| `README.md` | bootstrap: how to set up credentials | yes |
| `CMakeLists.txt` | Zephyr project + radar module | yes |
| `prj.conf` | Kconfig (radar + net + wifi, no shell) | yes |
| `boards/kit_pse84_ai_pse846gps2dbzc4a_m55.overlay` | SCB3 SPI + BGT60TR13C node | yes |
| `src/main.c` | radar loop + UDP send | yes |
| `src/wifi_connect.h` | Wi-Fi API + UDP dest constants | yes |
| `src/wifi_connect.c` | programmatic connect + auto-reconnect | yes |
| `src/wifi_credentials.h` | SSID / PSK | **no (gitignored)** |
| `src/wifi_credentials.h.example` | credentials template | yes |
| `src/radar_config.h` | copy of app 09's config | yes |
| `src/scb3_clock_fix.c` | verify then keep or drop | yes |
| `host/udp_server.py` | binary radar decoder | yes |

---

## Risks and open questions

1. **`scb3_clock_fix.c` on CM55.** May be unnecessary, may need
   different register writes. Verify in Phase 1; delete or adapt.
2. **AIROC/WHD RAM footprint on CM55.** App 08 sets
   `CONFIG_HEAP_MEM_POOL_SIZE=20480` and picks up
   `HEAP_MEM_POOL_ADD_SIZE_BOARD = 15000` from the board defconfig.
   Radar uses ~13 KB RAM on top of that. Total RAM
   budget on this CM55 target is 132 KB; expect ~50 KB overall use.
3. **DHCP timing.** `net_mgmt` fires connect-result *before* DHCP
   completes. The `wifi_connect_is_up()` check must gate on
   `NET_EVENT_IPV4_ADDR_ADD` too, not just Wi-Fi association.
4. **`zsock_send` semantics with `MSG_DONTWAIT`.** Zephyr's socket
   layer sometimes returns `-EAGAIN` under stack back-pressure even
   when the link is up. Rate-limited logging + a running drop counter
   will surface this if it becomes systemic.
5. **CM55 target build vs. CM33 config.** App 09 targets
   `kit_pse84_ai/pse846gps2dbzc4a/m33`. App 10 targets the `/m55` variant.
   The `west build -b` command changes; `west flash` invocation may need
   a different `--runner-args` for the CM55 core.

---

## Verification checklist per phase

- [ ] Phase 1: pristine build succeeds, 10 frames captured, log format
      matches app 09 but with CM55 origin.
- [ ] Phase 2: `<inf> net_dhcpv4` shows address bound, `wifi_connect_is_up()`
      returns true within 15 s of boot.
- [ ] Phase 3: `udp_server.py` receives ≥ 190 pkt/s (< 5% loss) over
      a 60 s window with `iperf`-clean Wi-Fi conditions.
- [ ] Phase 4: `.raw` file byte-count matches expected `sent_count × 256`.
