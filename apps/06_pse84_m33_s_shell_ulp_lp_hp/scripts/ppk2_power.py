#!/usr/bin/env python3
"""Hold the DUT powered via the Nordic PPK2's internal FET switch.

The PPK2's `toggle_DUT_power("ON")` command only asserts the VIN->VOUT
switch WHILE the device is in the measuring state -- as soon as
`stop_measuring()` is called (or the serial port is closed) the switch
opens and the DUT loses power. So keeping the DUT alive requires a
process that holds the port open with `start_measuring()` active.

This script does exactly that:

    ./ppk2_power.py on              # foreground, Ctrl+C to turn off
    ./ppk2_power.py on --daemon     # fork to background, return
    ./ppk2_power.py off             # stop any running keeper, DUT off
    ./ppk2_power.py status          # is a keeper running?

Used by run.sh to make sure the KitProg DAP sees a powered target
before west flash. Also handy manually when you want to keep the
board alive between flash / interactive-shell sessions.

If ppk2-api is not installed or no PPK2 is attached, `on` and `off`
both exit 0 with a diagnostic -- run.sh can call them
unconditionally.
"""

import argparse
import glob
import os
import signal
import sys
import time
import types

PPK2_GLOB = "/dev/serial/by-id/usb-Nordic_Semiconductor_PPK2_*-if01"
PIDFILE = f"/tmp/ppk2-power-{os.getuid()}.pid"


# ------------------------------------------------------------------
# PPK2 low-level helpers (shared with cycle_modes.py)
# ------------------------------------------------------------------
def _quiesce(ppk2, quiet_ms: int = 500, max_ms: int = 3000) -> None:
    """Stop any in-flight streaming and drain the port until silent."""
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
    """Replace _read_metadata with a decode(errors='ignore') variant."""

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


def open_and_configure(dev: str, supply_mv: int):
    """Open the PPK2 in ampere-meter mode and return the API handle.
    Caller is responsible for toggle_DUT_power + start/stop_measuring."""
    from ppk2_api.ppk2_api import PPK2_API

    ppk2 = PPK2_API(dev, timeout=1, write_timeout=1, exclusive=True)
    _quiesce(ppk2)
    _install_safe_metadata(ppk2)
    ppk2.get_modifiers()
    ppk2.set_source_voltage(supply_mv)
    ppk2.use_ampere_meter()
    return ppk2


# ------------------------------------------------------------------
# pidfile handling
# ------------------------------------------------------------------
def _is_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def read_pidfile() -> int | None:
    if not os.path.exists(PIDFILE):
        return None
    try:
        with open(PIDFILE) as f:
            pid = int(f.read().strip())
    except Exception:
        return None
    return pid if _is_alive(pid) else None


def write_pidfile() -> None:
    with open(PIDFILE, "w") as f:
        f.write(str(os.getpid()))


def remove_pidfile() -> None:
    try:
        os.remove(PIDFILE)
    except FileNotFoundError:
        pass


# ------------------------------------------------------------------
# on / off / status commands
# ------------------------------------------------------------------
def _cleanup_and_exit(ppk2, exit_code: int = 0) -> None:
    """Turn DUT off cleanly then release the port."""
    try:
        ppk2.toggle_DUT_power("OFF")
        # Have to briefly start_measuring for the OFF toggle to be
        # asserted by the PPK2 firmware, mirror of the ON path.
        try:
            ppk2.start_measuring()
            time.sleep(0.1)
        finally:
            try:
                ppk2.stop_measuring()
            except Exception:
                pass
    except Exception:
        pass
    try:
        ppk2.ser.close()
    except Exception:
        pass
    remove_pidfile()
    sys.exit(exit_code)


def cmd_on(args) -> int:
    existing = read_pidfile()
    if existing is not None:
        print(f"# [ppk2] already ON (keeper pid {existing})")
        return 0

    try:
        import ppk2_api.ppk2_api  # noqa: F401
    except ImportError:
        print("# [ppk2] ppk2-api not installed -- skipping (no-op)")
        return 0

    dev = find_ppk2(args.dev)
    if dev is None:
        print("# [ppk2] no PPK2 attached -- skipping (no-op)")
        return 0

    try:
        ppk2 = open_and_configure(dev, args.supply_mv)
    except Exception as e:
        print(f"# [ppk2] failed to open {dev}: {e}", file=sys.stderr)
        return 1

    try:
        ppk2.toggle_DUT_power("ON")
        # start_measuring is what actually asserts the FET switch.
        # We must hold it -- as soon as stop_measuring runs (or the
        # port closes), the switch opens again.
        ppk2.start_measuring()
    except Exception as e:
        print(f"# [ppk2] failed to enable DUT power: {e}", file=sys.stderr)
        try:
            ppk2.ser.close()
        except Exception:
            pass
        return 1

    if args.daemon:
        # Simple double-fork daemonization: parent returns immediately,
        # child re-execs stdout/stderr and lives forever.
        pid = os.fork()
        if pid > 0:
            # Parent -- wait a moment then return so the child has
            # time to write its pidfile.
            time.sleep(0.05)
            print(f"# [ppk2] {dev}: DUT power ON (keeper daemonized)")
            return 0
        os.setsid()

    write_pidfile()

    if not args.daemon:
        print(f"# [ppk2] {dev}: DUT power ON (Ctrl+C to turn off)")

    # Install signal handlers so both --daemon and foreground exit
    # cleanly (turn DUT off, remove pidfile).
    def _handler(signum, frame):
        _cleanup_and_exit(ppk2, 0)

    signal.signal(signal.SIGTERM, _handler)
    signal.signal(signal.SIGINT, _handler)
    signal.signal(signal.SIGHUP, _handler)

    # Keep the PPK2 firmware happy: periodically drain any samples
    # it has buffered so its TX pipe doesn't back up. We don't
    # care about the sample values -- the port just needs to be
    # active.
    try:
        while True:
            try:
                _ = ppk2.get_data()
            except Exception:
                pass
            time.sleep(0.5)
    except SystemExit:
        raise
    except Exception:
        pass
    _cleanup_and_exit(ppk2, 0)
    return 0  # unreachable


def cmd_off(args) -> int:
    """Turn the DUT power off. Kill any running keeper via the
    pidfile, then (in either case) reopen the PPK2 and issue an
    explicit OFF toggle so we're sure of the final state."""
    keeper = read_pidfile()
    if keeper is not None:
        os.kill(keeper, signal.SIGTERM)
        for _ in range(50):  # up to 5 seconds
            if not _is_alive(keeper):
                break
            time.sleep(0.1)
        remove_pidfile()
        # The keeper's SIGTERM handler already issued the OFF toggle.
        print(f"# [ppk2] stopped keeper pid {keeper}, DUT power OFF")
        return 0

    # No keeper -- open the PPK2 fresh and toggle off.
    try:
        import ppk2_api.ppk2_api  # noqa: F401
    except ImportError:
        print("# [ppk2] ppk2-api not installed -- skipping (no-op)")
        return 0
    dev = find_ppk2(args.dev)
    if dev is None:
        print("# [ppk2] no PPK2 attached -- skipping (no-op)")
        return 0
    try:
        ppk2 = open_and_configure(dev, args.supply_mv)
    except Exception as e:
        print(f"# [ppk2] failed to open {dev}: {e}", file=sys.stderr)
        return 1
    try:
        ppk2.toggle_DUT_power("OFF")
        try:
            ppk2.start_measuring()
            time.sleep(0.1)
        finally:
            try:
                ppk2.stop_measuring()
            except Exception:
                pass
    finally:
        try:
            ppk2.ser.close()
        except Exception:
            pass
    print(f"# [ppk2] {dev}: DUT power OFF")
    return 0


def cmd_status(args) -> int:
    keeper = read_pidfile()
    if keeper is not None:
        print(f"# [ppk2] keeper running (pid {keeper}) -- DUT ON")
        return 0
    print("# [ppk2] no keeper running -- DUT OFF (or unknown)")
    return 1


# ------------------------------------------------------------------
# CLI
# ------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("state", choices=["on", "off", "status"])
    ap.add_argument(
        "--dev", default=None, help="PPK2 CDC device (default: autodetect -if01)"
    )
    ap.add_argument(
        "--supply-mv", type=int, default=3300, help="digital-ref voltage in mV"
    )
    ap.add_argument(
        "--daemon", action="store_true", help="only valid with 'on': fork to background"
    )
    args = ap.parse_args()

    if args.state == "on":
        return cmd_on(args)
    if args.state == "off":
        return cmd_off(args)
    return cmd_status(args)


if __name__ == "__main__":
    sys.exit(main())
