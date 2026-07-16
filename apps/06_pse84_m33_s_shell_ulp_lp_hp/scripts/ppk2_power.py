#!/usr/bin/env python3
"""Toggle the Nordic PPK2's internal VIN->VOUT FET switch on or off.

Used by run.sh to power the DUT up before `west flash` (KitProg DAP
needs the target powered to program it) and down again afterwards.
Standalone -- can also be invoked manually:

    ./ppk2_power.py on           # close the switch, DUT gets power
    ./ppk2_power.py off          # open the switch, DUT loses power
    ./ppk2_power.py --dev /dev/ttyACM1 on
    ./ppk2_power.py --supply-mv 1800 on

If the ppk2-api library is missing or no PPK2 is attached, the
script exits 0 with a diagnostic. That lets run.sh call it
unconditionally: on hosts without a PPK2 the flash still proceeds
against whatever externally-powered DUT is there.
"""

import argparse
import glob
import sys
import time
import types

PPK2_GLOB = "/dev/serial/by-id/usb-Nordic_Semiconductor_PPK2_*-if01"


def _quiesce(ppk2, quiet_ms: int = 500, max_ms: int = 3000) -> None:
    """Stop any in-flight streaming and drain the serial port until
    it has been silent for quiet_ms."""
    try:
        ppk2.stop_measuring()
    except Exception:
        pass
    deadline = time.monotonic() + max_ms / 1000.0
    last_data = time.monotonic()
    while time.monotonic() < deadline:
        try:
            n = ppk2.ser.in_waiting
        except Exception:
            n = 0
        if n:
            try:
                ppk2.ser.read(n)
            except Exception:
                pass
            last_data = time.monotonic()
        else:
            if (time.monotonic() - last_data) * 1000.0 > quiet_ms:
                return
            time.sleep(0.02)


def _install_safe_metadata(ppk2) -> None:
    """Tolerant _read_metadata replacement (decode with errors='ignore').
    Same rationale as in scripts/cycle_modes.py."""

    def _safe(self):
        for _ in range(10):
            read = self.ser.read(self.ser.in_waiting)
            time.sleep(0.1)
            if read:
                decoded = read.decode("utf-8", errors="ignore")
                if "END" in decoded:
                    return decoded
        return ""

    ppk2._read_metadata = types.MethodType(_safe, ppk2)


def find_ppk2(cli_dev: str | None) -> str | None:
    if cli_dev:
        return cli_dev
    matches = glob.glob(PPK2_GLOB)
    return matches[0] if matches else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "state",
        choices=["on", "off"],
        help="close (on) or open (off) the DUT-power switch",
    )
    ap.add_argument(
        "--dev", default=None, help="PPK2 CDC device (default: autodetect -if01)"
    )
    ap.add_argument(
        "--supply-mv",
        type=int,
        default=3300,
        help="digital-ref voltage in mV (required by "
        "start_measuring() even in ampere-meter mode)",
    )
    args = ap.parse_args()

    try:
        from ppk2_api.ppk2_api import PPK2_API
    except ImportError:
        print("# [ppk2] ppk2-api not installed -- skipping toggle")
        return 0

    dev = find_ppk2(args.dev)
    if dev is None:
        print("# [ppk2] no PPK2 attached -- skipping toggle")
        return 0

    try:
        ppk2 = PPK2_API(dev, timeout=1, write_timeout=1, exclusive=True)
    except Exception as e:
        print(f"# [ppk2] failed to open {dev}: {e} -- skipping toggle")
        return 0

    try:
        _quiesce(ppk2)
        _install_safe_metadata(ppk2)
        ppk2.get_modifiers()
        # set_source_voltage() must be called at least once before any
        # further command is meaningful; also serves as the digital
        # reference for D0..D7 when we use ampere-meter mode.
        ppk2.set_source_voltage(args.supply_mv)
        ppk2.use_ampere_meter()
        ppk2.toggle_DUT_power("ON" if args.state == "on" else "OFF")
        print(f"# [ppk2] {dev}: DUT power {args.state.upper()}")
    except Exception as e:
        print(f"# [ppk2] toggle failed: {e}", file=sys.stderr)
        return 1
    finally:
        try:
            ppk2.ser.close()
        except Exception:
            pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
