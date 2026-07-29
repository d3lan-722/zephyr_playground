#!/usr/bin/env python3
"""
UDP server for receiving streamed data from the PSE84 Wi-Fi client.

Usage:
    python udp_server.py [--port PORT] [--output FILE]

Listens on all interfaces (0.0.0.0) and logs each received packet
to stdout and optionally to a file for further processing.
"""

import argparse
import json
import socket
import sys
from datetime import datetime


def local_ipv4_addresses():
    """Return a sorted list of the host's non-loopback IPv4 addresses.

    Combines a hostname lookup (catches all bound interfaces) with a UDP
    'connect' trick (picks the OS's preferred outbound address).
    """
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith("127."):
                ips.add(ip)
    except socket.gaierror:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    return sorted(ips)


def main():
    parser = argparse.ArgumentParser(description="UDP data receiver")
    parser.add_argument(
        "--port", type=int, default=5005, help="Listen port (default: 5005)"
    )
    parser.add_argument(
        "--output",
        type=str,
        default=None,
        help="Optional output file to append received JSON lines",
    )
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    # Short timeout so blocking recvfrom() wakes up periodically -- this lets
    # Ctrl+C be delivered on Windows (WinSock does not interrupt a blocking
    # socket call the way POSIX signals do).
    sock.settimeout(0.5)

    outfile = None
    if args.output:
        outfile = open(args.output, "a")

    print(f"[udp_server] Listening on 0.0.0.0:{args.port}")
    ips = local_ipv4_addresses()
    if ips:
        print(
            "[udp_server] Point the board at ONE of these host IPs (same LAN as the board):"
        )
        for ip in ips:
            print(f"[udp_server]     udp server {ip} {args.port}")
    else:
        print(
            "[udp_server] Could not determine local IP; check `ipconfig` / `ip addr`."
        )
    print(f"[udp_server] Press Ctrl+C to stop\n")

    pkt_count = 0
    try:
        while True:
            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                continue
            pkt_count += 1
            ts = datetime.now().isoformat(timespec="milliseconds")

            try:
                payload = json.loads(data.decode("utf-8", errors="replace"))
                line = json.dumps(
                    {"ts": ts, "src": f"{addr[0]}:{addr[1]}", **payload},
                    separators=(",", ":"),
                )
            except (json.JSONDecodeError, UnicodeDecodeError):
                line = json.dumps(
                    {
                        "ts": ts,
                        "src": f"{addr[0]}:{addr[1]}",
                        "raw": data.hex(),
                    },
                    separators=(",", ":"),
                )

            print(f"[{pkt_count:>6}] {line}")

            if outfile:
                outfile.write(line + "\n")
                outfile.flush()

    except KeyboardInterrupt:
        print(f"\n[udp_server] Stopped. Total packets received: {pkt_count}")
    finally:
        sock.close()
        if outfile:
            outfile.close()


if __name__ == "__main__":
    main()
