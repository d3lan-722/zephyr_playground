#!/usr/bin/env python3
"""Simple PPK2 (Nordic Power Profiler Kit II) probe for project 06.

Captures current + digital-channel samples for a fixed duration while
the device under test cycles through HP / LP / ULP transitions, then
prints:

  1) The duration and mean current of every high-pulse detected on
     D7 (the pm-busy scope trigger, wired to the PPK2's D7 input).
     Each pulse corresponds to one pm_strategy_transition() call.
  2) The duration and mean current of each low-segment between
     pulses. These are the residence periods in HP, LP or ULP.
  3) A short summary of overall mean current.

Sample rate is a fixed 100 kHz (10 us per sample).

Usage:
    ./ppk2_probe.py                  # default: auto-detect, 12s capture, ampere meter
    ./ppk2_probe.py --source 3300    # source meter mode, 3.3 V DUT supply
    ./ppk2_probe.py --dur 30         # 30 second capture
    ./ppk2_probe.py --dev /dev/ttyACM1

The PPK2 has two CDC interfaces (-if01 and -if03); the API talks over
-if01. The default auto-detect picks that one by USB serial ID.

Requires: `pip install ppk2-api pyserial` (both already present in
the dev container's venv).
"""

import argparse
import glob
import sys
import time
import types
from dataclasses import dataclass

from ppk2_api.ppk2_api import PPK2_API, PPK2_MP

SAMPLE_HZ = 100_000  # PPK2 hardware sample rate
SAMPLE_PERIOD_S = 1.0 / SAMPLE_HZ
GPIO_CHANNEL = 7  # D7 receives the pm-busy signal
# Main-thread poll interval when draining the background reader's
# queue. Small enough to keep the queue shallow (bounded by
# buffer_max_size_seconds inside PPK2_MP), but not so small that we
# spin uselessly.
POLL_INTERVAL_S = 0.005

# Stable udev id for the PPK2 control CDC interface.
PPK2_GLOB = "/dev/serial/by-id/usb-Nordic_Semiconductor_PPK2_*-if01"


def quiesce_ppk2(ppk2: PPK2_API, quiet_ms: int = 500, max_ms: int = 3000) -> None:
    """Send stop-measurement, then discard incoming bytes until the port
    has been silent for ``quiet_ms`` milliseconds (or ``max_ms`` total
    elapses). Needed after a dirty shutdown left the PPK2 streaming
    sample bytes -- those bytes would otherwise corrupt the next
    get_modifiers() call."""
    try:
        ppk2.stop_measuring()
    except Exception:
        pass
    deadline_total = time.monotonic() + max_ms / 1000.0
    last_data = time.monotonic()
    while time.monotonic() < deadline_total:
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


def _safe_read_metadata(self):
    """Drop-in replacement for PPK2_API._read_metadata that tolerates
    stray non-UTF-8 bytes. The metadata reply is pure ASCII terminated
    by the "END" sentinel, so errors='ignore' cannot lose meaning; it
    just discards any leftover sample bytes that slipped through the
    quiesce."""
    for _ in range(10):
        read = self.ser.read(self.ser.in_waiting)
        time.sleep(0.1)
        if read:
            decoded = read.decode("utf-8", errors="ignore")
            if "END" in decoded:
                return decoded
    return ""


@dataclass
class Segment:
    """A contiguous run of samples where D7 stayed at one level."""

    high: bool
    start_idx: int
    end_idx: int  # exclusive
    mean_ua: float

    @property
    def duration_us(self) -> float:
        return (self.end_idx - self.start_idx) * SAMPLE_PERIOD_S * 1e6

    @property
    def duration_ms(self) -> float:
        return self.duration_us / 1000.0


def find_ppk2(cli_dev: str | None) -> str:
    if cli_dev:
        return cli_dev
    matches = glob.glob(PPK2_GLOB)
    if not matches:
        sys.exit(f"no PPK2 found matching {PPK2_GLOB!r} -- " "is it plugged in?")
    return matches[0]


def capture(ppk2: PPK2_API, dur_s: float) -> tuple[list[float], list[int]]:
    """Collect samples and raw digital bytes for ``dur_s`` seconds."""
    samples: list[float] = []
    digital: list[int] = []
    deadline = time.monotonic() + dur_s
    ppk2.start_measuring()
    try:
        while time.monotonic() < deadline:
            buf = ppk2.get_data()
            if buf:
                s, d = ppk2.get_samples(buf)
                samples.extend(s)
                digital.extend(d)
            time.sleep(POLL_INTERVAL_S)
    finally:
        ppk2.stop_measuring()
    return samples, digital


def segment_by_gpio(samples: list[float], digital: list[int]) -> list[Segment]:
    """Split the capture into segments where D7 stays at one level."""
    if not samples or not digital:
        return []

    # digital[i] is a byte packing D0..D7 in bits[0..7]
    def d7(i: int) -> int:
        return (digital[i] >> GPIO_CHANNEL) & 1

    # PPK2 sometimes delivers analog samples with no matching digital
    # entry (or vice versa). Trim to the common length.
    n = min(len(samples), len(digital))

    segments: list[Segment] = []
    seg_start = 0
    current_level = d7(0)
    running_sum = 0.0

    for i in range(n):
        level = d7(i)
        if level != current_level:
            # boundary -- close the previous segment
            length = i - seg_start
            mean = running_sum / length if length else 0.0
            segments.append(
                Segment(
                    high=bool(current_level),
                    start_idx=seg_start,
                    end_idx=i,
                    mean_ua=mean,
                )
            )
            seg_start = i
            current_level = level
            running_sum = 0.0
        running_sum += samples[i]

    # tail
    length = n - seg_start
    if length > 0:
        mean = running_sum / length
        segments.append(
            Segment(
                high=bool(current_level),
                start_idx=seg_start,
                end_idx=n,
                mean_ua=mean,
            )
        )

    return segments


def fmt_current(ua: float) -> str:
    if ua >= 1000.0:
        return f"{ua/1000.0:7.3f} mA"
    return f"{ua:7.1f} uA"


def print_report(segments: list[Segment], min_pulse_us: float = 100.0):
    """Print per-segment breakdown and summary stats."""
    if not segments:
        print("# no samples captured -- is the PPK2 wired correctly?")
        return

    pulses = [s for s in segments if s.high and s.duration_us >= min_pulse_us]
    lows = [s for s in segments if not s.high]

    n = sum(s.end_idx - s.start_idx for s in segments)
    total_ua = sum(s.mean_ua * (s.end_idx - s.start_idx) for s in segments)
    overall_mean = total_ua / n if n else 0.0

    print(
        f"# captured {n} samples "
        f"({n * SAMPLE_PERIOD_S:.3f} s @ {SAMPLE_HZ} Hz), "
        f"overall mean = {fmt_current(overall_mean)}"
    )
    print(
        f"# {len(pulses)} pulses (pm-busy, D7 high) " f"and {len(lows)} idle segments"
    )
    if overall_mean < 100.0:  # < 100 uA is implausibly low for a running MCU
        print(
            "# WARNING: overall current is very low. In ampere-meter "
            "mode PPK2's VIN and VOUT must be inserted in series with "
            "the DUT's VDD rail. Also check ground continuity."
        )
    if not pulses:
        print(
            "# WARNING: no D7 high pulses detected. Confirm P3.1 (pm-busy) "
            "is wired to the PPK2 D7 input AND ground on the PPK2 logic "
            "header is tied to DUT ground, and that the DUT is actually "
            "issuing mode transitions during the capture window."
        )
    print()

    print("# per-segment listing:")
    print(f"# {'idx':>4}  {'kind':>5}  {'duration':>12}  {'mean current':>14}")
    for i, s in enumerate(segments):
        if s.duration_us < min_pulse_us and s.high:
            # too short to be a real pm-busy assertion; probably noise
            kind = "glitch"
        else:
            kind = "PULSE" if s.high else "idle"
        if s.duration_ms >= 1.0:
            dur = f"{s.duration_ms:9.3f} ms"
        else:
            dur = f"{s.duration_us:9.1f} us"
        print(f"  {i:>4}  {kind:>5}  {dur:>12}  {fmt_current(s.mean_ua):>14}")
    print()

    if pulses:
        durs = [s.duration_us for s in pulses]
        mas = [s.mean_ua for s in pulses]
        print("# pulse-only summary (transitions):")
        print(
            f"    n={len(pulses)}  "
            f"min={min(durs):.1f} us  "
            f"max={max(durs):.1f} us  "
            f"mean={sum(durs)/len(durs):.1f} us"
        )
        print(
            f"    current  min={fmt_current(min(mas))}  "
            f"max={fmt_current(max(mas))}  "
            f"mean={fmt_current(sum(mas)/len(mas))}"
        )
        print()

    if lows:
        print(
            "# idle-only summary (mode residence -- correlate order "
            "with the shell command sequence you sent):"
        )
        for i, s in enumerate(lows):
            print(
                f"    idle #{i:>2}  "
                f"dur={s.duration_ms:8.3f} ms  "
                f"mean={fmt_current(s.mean_ua)}"
            )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--dev",
        default=None,
        help="serial device (default: auto-detect PPK2 control CDC)",
    )
    ap.add_argument(
        "--dur",
        type=float,
        default=12.0,
        help="capture duration in seconds (default: 12)",
    )
    ap.add_argument(
        "--source",
        type=int,
        default=None,
        metavar="MV",
        help="use source-meter mode and supply MV millivolts to the DUT "
        "(default: ampere-meter mode, DUT externally powered)",
    )
    ap.add_argument(
        "--logic-mv",
        type=int,
        default=3300,
        help="digital-input reference voltage in mV. Required even in "
        "ampere-meter mode -- the PPK2 uses it as the logic threshold "
        "for D0..D7 (default: 3300 for the PSE84 eval kit's 3.3 V I/O)",
    )
    ap.add_argument(
        "--min-pulse-us",
        type=float,
        default=100.0,
        help="ignore D7-high segments shorter than this (glitch filter)",
    )
    args = ap.parse_args()

    dev = find_ppk2(args.dev)
    print(f"# opening PPK2 at {dev}", flush=True)
    # PPK2_MP is the PPK2_API subclass that runs a background reader
    # thread. It's needed here because the raw 100 kHz sample stream
    # (400 kB/s) overruns kernel-side USB buffering if the main
    # thread pauses more than a few dozen ms -- symptom is asking
    # for a 30 s capture and getting ~13 s of samples. Size the
    # ring buffer at least as large as the capture window so no
    # chunks get dropped even if the main thread pauses to process.
    buffer_max_size_seconds = max(args.dur + 2.0, 10.0)
    ppk2 = PPK2_MP(
        dev,
        timeout=1,
        write_timeout=1,
        exclusive=True,
        buffer_max_size_seconds=buffer_max_size_seconds,
    )

    # If the previous session ended uncleanly the PPK2 may still be
    # streaming sample bytes into its USB TX buffer. Reading those as
    # if they were the ASCII metadata reply blows up get_modifiers()
    # with UnicodeDecodeError, and a single reset_input_buffer() is
    # not enough because more streaming bytes keep arriving from
    # kernel-side USB buffering. Do a proper quiesce: send the
    # AVERAGE_STOP command, then drain until the port has been
    # silent for a sustained interval.
    quiesce_ppk2(ppk2, quiet_ms=500, max_ms=3000)

    # Monkey-patch _read_metadata so a stray non-UTF-8 byte does not
    # abort the whole run -- the metadata reply is ASCII, so
    # errors='ignore' is safe and matches the intent of the search
    # for the "END" sentinel.
    ppk2._read_metadata = types.MethodType(_safe_read_metadata, ppk2)

    ppk2.get_modifiers()

    if args.source is not None:
        print(
            f"# source-meter mode, DUT supply = {args.source} mV",
            flush=True,
        )
        ppk2.set_source_voltage(args.source)
        ppk2.use_source_meter()
    else:
        print(
            f"# ampere-meter mode (DUT externally powered), "
            f"digital threshold ref = {args.logic_mv} mV",
            flush=True,
        )
        # set_source_voltage() must be called even in ampere-meter mode:
        # the PPK2 firmware guards start_measuring() with an
        # "Input voltage not set!" exception otherwise. The value is
        # also used as the reference for the D0..D7 logic threshold.
        ppk2.set_source_voltage(args.logic_mv)
        ppk2.use_ampere_meter()

    # PPK2 has an internal FET switch in the VIN -> VOUT path -- this
    # is true in BOTH source-meter mode (PPK2 supplies power) AND
    # ampere-meter mode (external supply, PPK2 measures current in
    # series). It defaults OPEN, so the DUT gets no power until we
    # explicitly close it here. Symptom of forgetting this call:
    # MCU LEDs never light, PPK2 reads ~0.3 uA (leakage into the
    # measurement front-end).
    ppk2.toggle_DUT_power("ON")
    time.sleep(0.05)  # let VDD settle before start_measuring

    print(f"# capturing for {args.dur} s...", flush=True)
    samples, digital = capture(ppk2, args.dur)
    print(
        f"# got {len(samples)} analog samples, " f"{len(digital)} digital samples",
        flush=True,
    )

    # Deliberately leave toggle_DUT_power("ON") state alone on exit:
    # cutting DUT power would reset the MCU and drop the console
    # session, which is exactly what the user does NOT want between
    # back-to-back probe runs. If you need to hard-cut, run
    # `python3 -c "from ppk2_api.ppk2_api import PPK2_API; ..."` by
    # hand.

    segments = segment_by_gpio(samples, digital)
    print_report(segments, min_pulse_us=args.min_pulse_us)
    return 0


if __name__ == "__main__":
    sys.exit(main())
