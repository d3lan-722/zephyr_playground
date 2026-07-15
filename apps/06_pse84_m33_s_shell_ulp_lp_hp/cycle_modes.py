#!/usr/bin/env python3
"""Cycle the pm shell commands on the CM33-S shell of project 06 so
that every iteration exercises all six directed HP/LP/ULP transitions
exactly once, printing whatever the console emits after each command.

The command sequence ``ulp, lp, ulp, hp, lp, hp`` is an Euler circuit
on the six-edge transition graph starting and ending at HP:

    step  command   transition       kind
    ----  -------   --------------   -----------------
     1    ulp       HP  -> ULP       down direct
     2    lp        ULP -> LP        up 1 step
     3    ulp       LP  -> ULP       down 1 step
     4    hp        ULP -> HP        up direct
     5    lp        HP  -> LP        down 1 step
     6    hp        LP  -> HP        up 1 step

After step 6 the SoC is back in HP, so the loop repeats cleanly.

Usage:
    ./cycle_modes.py                    # default: /dev/ttyACM0, 5 s
    ./cycle_modes.py --dev /dev/ttyACM0 --period 3

Requires: pyserial (already available in the dev container's venv).
"""

import argparse
import glob
import sys
import time

import serial

BAUD = 115200

# Euler circuit over the six directed HP/LP/ULP transitions, starting
# and ending in HP -- see the module docstring for the per-step table.
COMMANDS = ["ulp", "lp", "ulp", "hp", "lp", "hp"]

# Stable udev id for the KitProg3 UART interface (CDC ACM). The
# `-if02` suffix picks the UART CDC interface, not the DAP one.
KITPROG_GLOB = (
    "/dev/serial/by-id/" "usb-Cypress_Semiconductor_KitProg3_CMSIS-DAP_*-if02"
)


def find_device(cli_dev: str | None) -> str:
    if cli_dev:
        return cli_dev
    matches = glob.glob(KITPROG_GLOB)
    if matches:
        return matches[0]
    return "/dev/ttyACM0"


def drain(ser: serial.Serial, quiet_ms: int = 300, max_ms: int = 2000) -> str:
    """Read everything the port emits until it has been silent for
    ``quiet_ms`` milliseconds or ``max_ms`` total elapsed."""
    deadline_total = time.monotonic() + max_ms / 1000.0
    deadline_quiet = time.monotonic() + quiet_ms / 1000.0
    buf = b""
    while time.monotonic() < deadline_total:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            deadline_quiet = time.monotonic() + quiet_ms / 1000.0
        else:
            if time.monotonic() >= deadline_quiet:
                break
            time.sleep(0.02)
    return buf.decode("utf-8", errors="replace")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--dev", default=None, help="serial device (default: auto-detect KitProg3 CDC)"
    )
    p.add_argument(
        "--period",
        type=float,
        default=5.0,
        help="seconds between commands (default: 5)",
    )
    args = p.parse_args()

    dev = find_device(args.dev)
    print(
        f"# opening {dev} @ {BAUD} 8N1, period={args.period}s, "
        f"cycle={' -> '.join(COMMANDS)}",
        flush=True,
    )

    with serial.Serial(dev, BAUD, timeout=0.1) as ser:
        # Drain and echo any boot banner / prompt sitting in the FIFO.
        banner = drain(ser, quiet_ms=500, max_ms=2000)
        if banner:
            sys.stdout.write(banner)
            sys.stdout.flush()

        i = 0
        try:
            while True:
                cmd = COMMANDS[i % len(COMMANDS)]
                i += 1
                start = time.monotonic()

                ser.write((cmd + "\n").encode())
                text = drain(ser, quiet_ms=500, max_ms=3000)
                sys.stdout.write(text)
                sys.stdout.flush()

                elapsed = time.monotonic() - start
                remaining = args.period - elapsed
                if remaining > 0:
                    time.sleep(remaining)
        except KeyboardInterrupt:
            print("\n# interrupted", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
