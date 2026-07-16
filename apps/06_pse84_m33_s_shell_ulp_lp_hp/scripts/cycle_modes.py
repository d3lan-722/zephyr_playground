#!/usr/bin/env python3
"""Cycle HP/LP/ULP mode commands on the CM33-S shell of project 06,
capture the console output and (optionally) the Nordic PPK2 current
and D7 GPIO trace, and write everything to a JSON file for
post-processing with scripts/postprocess.py.

The command sequence is an Euler circuit over the six directed
HP/LP/ULP transitions starting and ending at HP:
    ulp, lp, ulp, hp, lp, hp
so one loop exercises every edge of the graph exactly once and
lands back on HP for the next loop.

PPK2 integration is optional: if no PPK2 is attached (or --no-ppk
is passed) the script still cycles the modes and writes the JSON
without ppk2 fields.

Usage:
    ./cycle_modes.py                        # 3 loops, 5s period, autodetect PPK2
    ./cycle_modes.py --loops 5 --period 3
    ./cycle_modes.py --no-ppk               # skip ppk2 even if attached
    ./cycle_modes.py --output out.json      # override the auto-named output file

The output JSON is named `<strategy>_<timestamp>.json` where
<strategy> is derived from the firmware console output
(`pll_retune` from `[pll] ...` lines, `hf0_divider` from
`[div] ...` lines, `unknown` otherwise). Written into the
current directory by default.
"""

import argparse
import glob
import json
import os
import re
import signal
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any

import serial

# ------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------
SHELL_BAUD = 115200
COMMANDS = ["ulp", "lp", "ulp", "hp", "lp", "hp"]  # Euler cycle

KITPROG_GLOB = "/dev/serial/by-id/usb-Cypress_Semiconductor_KitProg3_CMSIS-DAP_*-if02"
PPK2_GLOB = "/dev/serial/by-id/usb-Nordic_Semiconductor_PPK2_*-if01"

# PPK2 hardware sample rate.
PPK2_SAMPLE_HZ = 100_000
PPK2_SAMPLE_PERIOD_S = 1.0 / PPK2_SAMPLE_HZ
# D7 receives the pm-busy signal from P3.1 on the DUT.
PPK2_GPIO_CHANNEL = 7
# Minimum D7-high segment considered a real pm-busy pulse.
PPK2_MIN_PULSE_US = 100.0
# Skip the first N ms of each idle segment when computing the mean
# current -- excludes UART flush and clock probe tail current from
# the "steady-state mode" estimate.
PPK2_IDLE_SETTLE_MS = 200.0


# ------------------------------------------------------------------
# Console-output parsing
# ------------------------------------------------------------------
RE_TRANSITION = re.compile(
    r"\[pm\] transition (?P<src>\S+)\s+\(\s*(?P<src_hz>\d+)\s*MHz\)\s*->\s*"
    r"(?P<tgt>\S+)\s+\(\s*(?P<tgt_hz>\d+)\s*MHz\)\s*:\s*(?P<cycles>\d+) cycles"
)
RE_PHASES = re.compile(r"\[pm\] (?P<label>\S+) phase cycles:\s*(?P<body>[^\n]+)")
RE_PHASE_ENTRY = re.compile(r"(\w+)=(\d+)")


def parse_console_output(text: str) -> dict[str, Any] | None:
    """Extract transition + phase data from a single command's console
    output. Returns None if no `[pm] transition` line was found."""
    m_trans = RE_TRANSITION.search(text)
    if not m_trans:
        return None
    result: dict[str, Any] = {
        "source": m_trans["src"],
        "source_hz": int(m_trans["src_hz"]) * 1_000_000,
        "target": m_trans["tgt"],
        "target_hz": int(m_trans["tgt_hz"]) * 1_000_000,
        "cycles_total": int(m_trans["cycles"]),
        "phases": {},
        "phase_label": None,
    }
    m_ph = RE_PHASES.search(text)
    if m_ph:
        result["phase_label"] = m_ph["label"]
        for name, val in RE_PHASE_ENTRY.findall(m_ph["body"]):
            if name != "total":
                result["phases"][name] = int(val)
    return result


def detect_strategy(text: str) -> str:
    """Detect DVFS strategy from status-line prefixes."""
    if "[pll]" in text:
        return "pll_retune"
    if "[div]" in text:
        return "hf0_divider"
    return "unknown"


# ------------------------------------------------------------------
# Shell (KitProg CDC) interaction
# ------------------------------------------------------------------
def find_kitprog(cli_dev: str | None) -> str:
    if cli_dev:
        return cli_dev
    matches = glob.glob(KITPROG_GLOB)
    if matches:
        return matches[0]
    sys.exit(
        f"KitProg CDC not found matching {KITPROG_GLOB!r} -- "
        "is the board plugged in?"
    )


class ShellDisconnected(Exception):
    """The KitProg CDC vanished mid-read -- typically because the
    DUT reset (which the KitProg mirrors to its CDC endpoint) or
    because a current transient during a mode-switch caused a brief
    USB brown-out. Raised by drain_shell for the caller to decide
    whether to reconnect or bail out."""


def drain_shell(ser: serial.Serial, quiet_ms: int = 500, max_ms: int = 3000) -> str:
    """Read from the shell until it has been quiet for ``quiet_ms``
    milliseconds or ``max_ms`` total elapsed.

    If the underlying tty is torn down mid-read (pyserial raises
    ``serial.SerialException`` -- device disconnected or multiple
    access on port), whatever bytes have already been buffered are
    returned and the exception is re-raised as ``ShellDisconnected``
    so the caller can decide whether to reopen the port and retry."""
    deadline_total = time.monotonic() + max_ms / 1000.0
    deadline_quiet = time.monotonic() + quiet_ms / 1000.0
    buf = b""
    while time.monotonic() < deadline_total:
        try:
            n = ser.in_waiting
            if n:
                chunk = ser.read(n)
                if chunk:
                    buf += chunk
                    deadline_quiet = time.monotonic() + quiet_ms / 1000.0
                else:
                    # in_waiting > 0 but read returned nothing -- kernel
                    # says data is there but the CDC is being torn down.
                    raise serial.SerialException(
                        "read returned 0 bytes despite in_waiting > 0"
                    )
            else:
                if time.monotonic() >= deadline_quiet:
                    break
                time.sleep(0.02)
        except (serial.SerialException, OSError) as e:
            raise ShellDisconnected(str(e)) from e
    return buf.decode("utf-8", errors="replace")


# ------------------------------------------------------------------
# PPK2 background capture (optional)
# ------------------------------------------------------------------

# Matches the pidfile written by scripts/ppk2_power.py -- see that
# script's docstring. If a keeper daemon is running, we need to stop
# it before opening the PPK2 ourselves; otherwise we hit an
# EAGAIN / 'Resource temporarily unavailable' on the exclusive lock.
PPK2_KEEPER_PIDFILE = f"/tmp/ppk2-power-{os.getuid()}.pid"


def _stop_ppk2_keeper() -> None:
    """If scripts/ppk2_power.py is holding the PPK2 port open (its
    keeper daemon), stop it and wait for the port to be released."""
    if not os.path.exists(PPK2_KEEPER_PIDFILE):
        return
    try:
        with open(PPK2_KEEPER_PIDFILE) as f:
            pid = int(f.read().strip())
    except Exception:
        return
    try:
        os.kill(pid, 0)
    except OSError:
        # Stale pidfile.
        try:
            os.remove(PPK2_KEEPER_PIDFILE)
        except FileNotFoundError:
            pass
        return
    print(f"# [ppk2] stopping keeper pid {pid} to take PPK2 port")
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError:
        return
    for _ in range(50):  # up to 5 s
        try:
            os.kill(pid, 0)
        except OSError:
            break
        time.sleep(0.1)
    # Extra settle so the kernel releases the CDC exclusive lock.
    time.sleep(0.3)


def _patch_ppk2_mp_del() -> None:
    """PPK2_MP.__del__ references self._quit_evt without checking
    that __init__ finished. If exclusive-open failed early in the
    base PPK2_API.__init__, _quit_evt is never set and the destructor
    raises AttributeError (harmless but noisy). Wrap it once."""
    try:
        from ppk2_api.ppk2_api import PPK2_MP as _PPK2_MP
    except ImportError:
        return
    if getattr(_PPK2_MP.__del__, "_patched", False):
        return
    _orig = _PPK2_MP.__del__

    def _safe_del(self):
        try:
            _orig(self)
        except AttributeError:
            pass
        except Exception:
            pass

    _safe_del._patched = True  # type: ignore[attr-defined]
    _PPK2_MP.__del__ = _safe_del  # type: ignore[method-assign]


class Ppk2Capture:
    """Optional PPK2 background capture. Silently becomes a no-op
    if ppk2-api isn't installed, no PPK2 is attached, or --no-ppk
    was passed."""

    def __init__(self, dev: str | None, supply_mv: int = 3300, disabled: bool = False):
        self.is_active = False
        self.samples: list[float] = []
        self.digital: list[int] = []
        self._ppk2 = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._sample_t0_monotonic: float | None = None

        if disabled:
            return

        try:
            from ppk2_api.ppk2_api import PPK2_MP  # type: ignore
        except ImportError:
            print("# [ppk2] ppk2-api not installed -- skipping current capture")
            return

        _patch_ppk2_mp_del()
        _stop_ppk2_keeper()

        if dev is None:
            matches = glob.glob(PPK2_GLOB)
            if not matches:
                print("# [ppk2] no PPK2 attached -- skipping current capture")
                return
            dev = matches[0]

        try:
            ppk2 = PPK2_MP(
                dev,
                timeout=1,
                write_timeout=1,
                exclusive=True,
                buffer_max_size_seconds=300.0,
            )
        except Exception as e:
            print(f"# [ppk2] failed to open {dev}: {e} -- skipping capture")
            return

        _quiesce(ppk2)
        _install_safe_metadata(ppk2)
        try:
            ppk2.get_modifiers()
            ppk2.set_source_voltage(supply_mv)
            ppk2.use_ampere_meter()
            ppk2.toggle_DUT_power("ON")
        except Exception as e:
            print(f"# [ppk2] setup failed: {e} -- skipping capture")
            return

        print(
            f"# [ppk2] opened {dev}, ampere-meter mode, " f"digital-ref {supply_mv} mV"
        )
        self._ppk2 = ppk2
        self.is_active = True

    def start(self) -> None:
        if not self.is_active:
            return
        assert self._ppk2 is not None
        self._sample_t0_monotonic = time.monotonic()
        self._ppk2.start_measuring()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self) -> None:
        assert self._ppk2 is not None
        while not self._stop.is_set():
            buf = self._ppk2.get_data()
            if buf:
                s, d = self._ppk2.get_samples(buf)
                self.samples.extend(s)
                self.digital.extend(d)
            else:
                time.sleep(0.005)

    def stop(self) -> None:
        if not self.is_active:
            return
        assert self._ppk2 is not None
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
        try:
            self._ppk2.stop_measuring()
        except Exception:
            pass
        # Leave DUT power ON -- cutting it now would reset the MCU.


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
    """Tolerant _read_metadata replacement (decode with errors='ignore')."""
    import types

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


# ------------------------------------------------------------------
# PPK2 sample post-processing (D7 segmentation)
# ------------------------------------------------------------------
@dataclass
class Segment:
    high: bool
    start_idx: int
    end_idx: int  # exclusive
    mean_ua: float

    @property
    def duration_s(self) -> float:
        return (self.end_idx - self.start_idx) * PPK2_SAMPLE_PERIOD_S

    @property
    def start_s(self) -> float:
        return self.start_idx * PPK2_SAMPLE_PERIOD_S


def segment_by_gpio(samples: list[float], digital: list[int]) -> list[Segment]:
    """Split the capture into maximal runs where D7 stays at one level."""
    n = min(len(samples), len(digital))
    if n == 0:
        return []
    segments: list[Segment] = []

    def d7(i: int) -> int:
        return (digital[i] >> PPK2_GPIO_CHANNEL) & 1

    seg_start = 0
    current_level = d7(0)
    running_sum = 0.0

    for i in range(n):
        level = d7(i)
        if level != current_level:
            length = i - seg_start
            segments.append(
                Segment(
                    high=bool(current_level),
                    start_idx=seg_start,
                    end_idx=i,
                    mean_ua=running_sum / length if length else 0.0,
                )
            )
            seg_start = i
            current_level = level
            running_sum = 0.0
        running_sum += samples[i]

    length = n - seg_start
    if length > 0:
        segments.append(
            Segment(
                high=bool(current_level),
                start_idx=seg_start,
                end_idx=n,
                mean_ua=running_sum / length,
            )
        )
    return segments


def idle_mean_settled(samples: list[float], seg: Segment) -> float:
    """Mean current in an idle segment, ignoring the first
    PPK2_IDLE_SETTLE_MS milliseconds."""
    settle_samples = int(PPK2_IDLE_SETTLE_MS / 1000.0 * PPK2_SAMPLE_HZ)
    a = min(seg.start_idx + settle_samples, seg.end_idx)
    b = seg.end_idx
    n = b - a
    if n <= 0:
        return seg.mean_ua
    return sum(samples[a:b]) / n


# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------
@dataclass
class TxnRecord:
    seq: int
    cmd: str
    sent_at_s: float
    console: str
    parsed: dict[str, Any] | None = field(default=None)


def default_output_name(strategy: str) -> str:
    ts = datetime.now().strftime("%Y%m%d-%H%M%S")
    return f"{strategy}_{ts}.json"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--dev", default=None, help="KitProg CDC device (default: autodetect)"
    )
    p.add_argument(
        "--ppk-dev", default=None, help="PPK2 CDC device (default: autodetect)"
    )
    p.add_argument(
        "--loops",
        type=int,
        default=3,
        help="how many times to cycle the 6-command sequence",
    )
    p.add_argument("--period", type=float, default=5.0, help="seconds between commands")
    p.add_argument(
        "--no-ppk", action="store_true", help="skip PPK2 capture even if attached"
    )
    p.add_argument(
        "--supply-mv",
        type=int,
        default=3300,
        help="PPK2 supply / digital-ref voltage in mV",
    )
    p.add_argument(
        "--output",
        default=None,
        help="output JSON path (default: <strategy>_<ts>.json)",
    )
    args = p.parse_args()

    dev = find_kitprog(args.dev)
    print(f"# opening shell {dev} @ {SHELL_BAUD} 8N1")
    ser = serial.Serial(dev, SHELL_BAUD, timeout=0.1)

    ppk = Ppk2Capture(dev=args.ppk_dev, supply_mv=args.supply_mv, disabled=args.no_ppk)

    # Flush any stale bytes queued in the tty (e.g. from a previous
    # aborted cycle_modes run or a stray console session) so the
    # first command is sent to a clean shell.
    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
    except Exception:
        pass
    # Nudge the shell to re-emit the prompt, then drain everything.
    try:
        ser.write(b"\n")
    except Exception:
        pass
    try:
        banner = drain_shell(ser, quiet_ms=500, max_ms=2000)
    except ShellDisconnected as e:
        print(f"# [shell] disconnected during initial drain ({e})",
              file=sys.stderr)
        banner = ""
    if banner:
        sys.stdout.write(banner)
        sys.stdout.flush()

    ppk.start()
    t0 = time.monotonic()
    txns: list[TxnRecord] = []
    total_cmds = args.loops * len(COMMANDS)

    try:
        for i in range(total_cmds):
            cmd = COMMANDS[i % len(COMMANDS)]
            sent_at = time.monotonic() - t0
            try:
                ser.write((cmd + "\n").encode())
                text = drain_shell(ser, quiet_ms=500, max_ms=3000)
            except ShellDisconnected as e:
                # KitProg CDC vanished mid-read (typically a DUT
                # reset or a brief USB brown-out from a mode-switch
                # current spike). Try to reopen once; if it works,
                # note the disruption in the record and keep going.
                # If it doesn't, save what we have and stop.
                print(f"\n# [shell] disconnected ({e}); trying to "
                      "reconnect...", flush=True)
                try:
                    ser.close()
                except Exception:
                    pass
                time.sleep(1.0)
                try:
                    ser = serial.Serial(dev, SHELL_BAUD, timeout=0.1)
                    text = f"<shell disconnected: {e}>"
                except Exception as reopen_err:
                    print(f"# [shell] reconnect failed: {reopen_err}; "
                          "stopping capture", flush=True)
                    txns.append(TxnRecord(
                        seq=i + 1, cmd=cmd, sent_at_s=sent_at,
                        console=f"<disconnected before send: {e}>",
                        parsed=None,
                    ))
                    break
            sys.stdout.write(text)
            sys.stdout.flush()

            txns.append(
                TxnRecord(
                    seq=i + 1,
                    cmd=cmd,
                    sent_at_s=sent_at,
                    console=text,
                    parsed=parse_console_output(text),
                )
            )

            remaining = args.period - (time.monotonic() - t0 - sent_at)
            if remaining > 0:
                time.sleep(remaining)
    except KeyboardInterrupt:
        print("\n# interrupted", flush=True)
    finally:
        ppk.stop()
        try:
            ser.close()
        except Exception:
            pass

    strategy = detect_strategy("\n".join(t.console for t in txns))
    output = args.output or default_output_name(strategy)

    doc: dict[str, Any] = {
        "meta": {
            "captured_at": datetime.now().isoformat(timespec="seconds"),
            "board": "kit_pse84_eval",
            "strategy": strategy,
            "supply_mv": args.supply_mv,
            "period_s": args.period,
            "loops": args.loops,
            "command_sequence": COMMANDS,
        },
        "transitions": [
            {
                "seq": t.seq,
                "cmd": t.cmd,
                "sent_at_s": round(t.sent_at_s, 6),
                "source": t.parsed["source"] if t.parsed else None,
                "target": t.parsed["target"] if t.parsed else None,
                "source_hz": t.parsed["source_hz"] if t.parsed else None,
                "target_hz": t.parsed["target_hz"] if t.parsed else None,
                "cycles_total": t.parsed["cycles_total"] if t.parsed else None,
                "phase_label": t.parsed["phase_label"] if t.parsed else None,
                "phases": t.parsed["phases"] if t.parsed else {},
            }
            for t in txns
        ],
    }

    if ppk.is_active:
        segs = segment_by_gpio(ppk.samples, ppk.digital)
        pulses = [s for s in segs if s.high and s.duration_s * 1e6 >= PPK2_MIN_PULSE_US]
        idles = [s for s in segs if not s.high]
        overall_n = sum(s.end_idx - s.start_idx for s in segs)
        overall_mean = (
            sum(s.mean_ua * (s.end_idx - s.start_idx) for s in segs) / overall_n
            if overall_n
            else 0.0
        )

        # Segments interleave idle,pulse,idle,pulse,...; the mode
        # inside idle #k is the target of pulse #k (1-indexed, matching
        # transition indexing). idle #0 has no preceding pulse -- it's
        # the boot state before the first command.
        idle_records = []
        pulses_seen = 0
        for seg in segs:
            if seg.high and seg.duration_s * 1e6 >= PPK2_MIN_PULSE_US:
                pulses_seen += 1
                continue
            if seg.high:
                continue  # glitch
            mode = None
            if pulses_seen == 0:
                mode = "HP"  # boot state
            elif pulses_seen <= len(txns):
                t = txns[pulses_seen - 1]
                if t.parsed:
                    mode = t.parsed["target"]
            idle_records.append(
                {
                    "start_s": round(seg.start_s, 6),
                    "duration_s": round(seg.duration_s, 6),
                    "mean_ua": round(seg.mean_ua, 3),
                    "settled_mean_ua": round(idle_mean_settled(ppk.samples, seg), 3),
                    "mode": mode,
                    "pulses_before": pulses_seen,
                }
            )

        pulse_records = []
        for i, seg in enumerate(pulses, start=1):
            txn = txns[i - 1] if i - 1 < len(txns) else None
            pulse_records.append(
                {
                    "seq": i,
                    "start_s": round(seg.start_s, 6),
                    "duration_s": round(seg.duration_s, 6),
                    "mean_ua": round(seg.mean_ua, 3),
                    "cmd": txn.cmd if txn else None,
                    "source": txn.parsed["source"] if txn and txn.parsed else None,
                    "target": txn.parsed["target"] if txn and txn.parsed else None,
                }
            )

        doc["ppk2"] = {
            "available": True,
            "sample_hz": PPK2_SAMPLE_HZ,
            "n_samples": overall_n,
            "duration_s": round(overall_n * PPK2_SAMPLE_PERIOD_S, 6),
            "overall_mean_ua": round(overall_mean, 3),
            "min_pulse_us": PPK2_MIN_PULSE_US,
            "idle_settle_ms": PPK2_IDLE_SETTLE_MS,
            "pulses": pulse_records,
            "idles": idle_records,
        }
    else:
        doc["ppk2"] = {"available": False}

    with open(output, "w") as f:
        json.dump(doc, f, indent=2)
    print(f"\n# wrote {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
