#!/usr/bin/env python3
"""
UDP server for the PSE84 CM55 radar streamer.

Detects packets starting with the 'BGTR' magic and decodes them as
radar frames (16-byte header + 128 x uint16 samples).  Non-radar
packets are printed as text (backwards compatible with the parent
apps/08_pse84_ai_m55_udp/host/udp_server.py behaviour).

Usage:
    python udp_server.py [--port PORT] [--output-raw PATH] [--verbose]

Wire format (little-endian, 272 B total):
    offset  size  field
    0       4     magic          = 'BGTR'
    4       4     seq            uint32
    8       8     timestamp_ns   uint64
    16      256   samples[128]   int16 packed as uint16
"""

import argparse
import socket
import struct
import sys
import time
from datetime import datetime, timezone

MAGIC = b"BGTR"
HDR_STRUCT = struct.Struct("<4sIQ")  # magic, seq, timestamp_ns
NUM_SAMPLES = 128
PACKET_BYTES = HDR_STRUCT.size + NUM_SAMPLES * 2
SUMMARY_EVERY_N = 100  # summary line every N packets


def local_ipv4_addresses():
    """Best-effort discovery of the host's own IPv4 addresses."""
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


def decode_radar(payload):
    """Parse one 272-byte radar packet.  Returns (seq, ts_ns, samples list)."""
    magic, seq, ts_ns = HDR_STRUCT.unpack_from(payload, 0)
    if magic != MAGIC:
        raise ValueError("not a radar packet")
    samples = struct.unpack_from(f"<{NUM_SAMPLES}H", payload, HDR_STRUCT.size)
    return seq, ts_ns, samples


def frame_stats(samples):
    """Return (min, max, mean) of a 12-bit sample tuple."""
    lo, hi, total = 4095, 0, 0
    for v in samples:
        v &= 0x0FFF
        if v < lo:
            lo = v
        if v > hi:
            hi = v
        total += v
    return lo, hi, total // len(samples)


def main():
    parser = argparse.ArgumentParser(description="Radar UDP receiver")
    parser.add_argument(
        "--port", type=int, default=5005, help="listen port (default: 5005)"
    )
    parser.add_argument(
        "--output-raw",
        type=str,
        default=None,
        help="write raw sample bytes to this file",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="decode + print every packet"
    )
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", args.port))

    print(f"Listening on 0.0.0.0:{args.port}")
    for ip in local_ipv4_addresses():
        print(f"  reachable at {ip}:{args.port}")

    raw_out = None
    if args.output_raw:
        raw_out = open(args.output_raw, "wb")
        print(f"Writing raw samples to {args.output_raw}")

    total_pkts = 0
    radar_pkts = 0
    last_seq = None
    lost = 0
    start_wall = time.time()
    last_ts_ns = None

    try:
        while True:
            payload, addr = sock.recvfrom(2048)
            total_pkts += 1

            if len(payload) == PACKET_BYTES and payload.startswith(MAGIC):
                seq, ts_ns, samples = decode_radar(payload)
                radar_pkts += 1

                if last_seq is not None:
                    gap = (seq - last_seq - 1) & 0xFFFFFFFF
                    lost += gap
                last_seq = seq

                if raw_out is not None:
                    raw_out.write(payload[HDR_STRUCT.size :])

                if args.verbose:
                    lo, hi, mean = frame_stats(samples)
                    print(
                        f"seq={seq:7d} ts={ts_ns/1e6:10.3f}ms "
                        f"min={lo:4d} max={hi:4d} mean={mean:4d}"
                    )

                if radar_pkts % SUMMARY_EVERY_N == 0:
                    ts_delta_s = (
                        (ts_ns - last_ts_ns) / 1e9 if last_ts_ns is not None else 0.0
                    )
                    last_ts_ns = ts_ns
                    wall = time.time() - start_wall
                    fps = radar_pkts / wall if wall > 0 else 0.0
                    print(
                        f"[{datetime.now(timezone.utc).isoformat(timespec='seconds')}] "
                        f"pkts={radar_pkts:7d} lost={lost:5d} "
                        f"rate={fps:6.1f} pps last-seq={seq}"
                    )
            else:
                # Fall back to text handling for non-radar packets.
                try:
                    text = payload.decode("utf-8", errors="replace").rstrip()
                except Exception:
                    text = repr(payload)
                print(f"{addr[0]}:{addr[1]} (text)  {text}")
    except KeyboardInterrupt:
        pass
    finally:
        if raw_out is not None:
            raw_out.close()
        wall = time.time() - start_wall
        print(
            f"\nStopped after {wall:.1f} s: total={total_pkts} "
            f"radar={radar_pkts} lost={lost}"
        )


if __name__ == "__main__":
    main()
