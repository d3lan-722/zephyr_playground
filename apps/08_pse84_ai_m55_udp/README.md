# 08 — AIROC Wi-Fi UDP client (PSE84 CM55)

Streams UDP packets from the CYW55513 Wi-Fi radio on **kit_pse84_ai** (CM55 core)
to a small Python server running on your PC.

## 1. Build & flash

The M55 target on `kit_pse84_ai` **requires `--sysbuild`** — the M55 image
alone is not launchable without the companion M33-Secure image that sysbuild
produces and co-flashes.

```bash
cd apps/08_pse84_ai_m55_udp
rm -rf build
west build -b kit_pse84_ai/pse846gps2dbzc4a/m55 --sysbuild
west flash
```

Open the console (`uart2`, 115200 8N1) — you should see:

```text
*** Booting Zephyr OS build ... ***
<inf> main: CYW55513 Wi-Fi UDP client on PSE84
uart:~$
```

## 2. Start the UDP server on a machine on the same LAN

Run [host/udp_server.py](host/udp_server.py) on any Linux/Mac/Windows machine
that is on the **same LAN** as the board (WSL / dev-container / Docker
internal networks are unreachable — the board sits on the physical Wi-Fi).

```bash
python3 udp_server.py                # listens on 0.0.0.0:5005
# or log to file:
python3 udp_server.py --output udp.log
```

The script prints a ready-to-copy shell command with the host's LAN IP(s):

```text
[udp_server] Listening on 0.0.0.0:5005
[udp_server] Point the board at ONE of these host IPs (same LAN as the board):
[udp_server]     udp server 192.168.178.42 5005
```

Pick the address on the **same subnet as the board** (`net iface` on the
board shows its DHCP address).

> **Managed / corporate Windows PCs (CrowdStrike Falcon, SentinelOne, etc.):**
> these endpoint-security agents install a WFP callout driver that filters
> above Windows Firewall and will silently drop the inbound UDP even with a
> firewall allow-rule. Wireshark will show the packets arriving, but Python
> receives nothing. Workaround: use a **private Linux/Mac laptop, Raspberry
> Pi, or phone UDP-server app** on the same Wi-Fi. Check with:
> `Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct`.

## 3. Configure the board and stream

On the board's shell:

```text
uart:~$ wifi connect -s "YOUR_SSID" -k 1 -p "YOUR_PASSWORD"
uart:~$ wifi status                       # wait for State: COMPLETED
uart:~$ net iface                         # confirm a DHCP IPv4 address
uart:~$ udp server 192.168.178.42         # <-- IP printed by udp_server.py
uart:~$ udp start                         # 100 ms interval (default)
uart:~$ udp status
uart:~$ udp stop
```

`wifi connect -k` key type: `0` open, `1` WPA2-PSK, `5` WPA3-SAE
(see `wifi connect --help` for the full list).

On the PC you should see one line per packet:

```text
[     1] {"ts":"2026-07-29T12:34:56.789","src":"192.168.178.58:12345","seq":0,"uptime_ms":15234}
```

## Notes

- The Python receiver must run on a machine that is on the **same LAN** as
  the board. Container / WSL / Docker internal addresses (`172.x.x.x`,
  `10.x.x.x`, `127.0.0.1`) are not reachable from the board.
- If Wi-Fi bring-up fails with `sdhc_infineon: Cannot take sem!` or
  `airoc_wifi_init_primary failed ret = -19`, you almost certainly forgot
  `--sysbuild` — the CM55 image was never launched and you are running an old
  M33 image from a previous flash.
