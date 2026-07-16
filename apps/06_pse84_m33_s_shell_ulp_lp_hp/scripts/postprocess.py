#!/usr/bin/env python3
"""Post-process a JSON capture from scripts/cycle_modes.py.

Prints:
  - per-mode baseline current statistics
  - per-direction transition statistics (duration, mean current,
    charge, energy)
  - per-direction firmware cycle counts vs PPK2 pulse widths
  - break-even residence time to justify a round-trip transition

Plots (if matplotlib is available and --no-plots is not passed):
  - current per mode          -> current_per_mode.png
  - transition duration       -> transition_duration.png
  - energy per transition     -> energy_per_transition.png
  - break-even chart          -> break_even.png

By default PNGs are written next to the input JSON (typically
<app-root>/measurements/<strategy>_<ts>/), matching the layout
cycle_modes.py produces.

Usage:
    ./postprocess.py measurements/pll_retune_20260716-093305/data.json
    ./postprocess.py <json> --show                # also open interactive windows
    ./postprocess.py <json> --no-plots            # report only
    ./postprocess.py <json> --save-plots outdir/  # override save location
"""

import argparse
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


def fmt_current(ua: float) -> str:
    if abs(ua) >= 1000.0:
        return f"{ua/1000.0:7.3f} mA"
    return f"{ua:7.1f} uA"


def fmt_duration(s: float) -> str:
    if s >= 1.0:
        return f"{s:6.3f} s"
    if s >= 1e-3:
        return f"{s*1e3:6.3f} ms"
    return f"{s*1e6:6.1f} us"


def fmt_energy(uj: float) -> str:
    """Format energy in the most human-readable unit for the range
    typical of this project (single-transition = a few mJ, idle
    residence over seconds = tens of mJ)."""
    if abs(uj) >= 1000.0:
        return f"{uj/1000.0:8.3f} mJ"
    return f"{uj:8.1f} uJ"


def energy_uj(charge_uc: float, supply_mv: int) -> float:
    """Convert charge (uC) at a given supply voltage (mV) to
    energy (uJ). Q(uC) * V(V) = E(uJ); V(V) = V_mV / 1000."""
    return charge_uc * supply_mv / 1000.0


def load(path: Path) -> dict[str, Any]:
    with open(path) as f:
        return json.load(f)


def summarize_modes(doc: dict[str, Any]) -> dict[str, dict[str, float]]:
    """Aggregate per-mode current stats across all idle segments in
    the capture, using the settled-mean-ua (post 200 ms) figure."""
    ppk = doc.get("ppk2", {})
    if not ppk.get("available"):
        return {}
    by_mode: dict[str, list[float]] = defaultdict(list)
    for idle in ppk["idles"]:
        mode = idle.get("mode")
        if mode is None:
            continue
        by_mode[mode].append(idle["settled_mean_ua"])
    out: dict[str, dict[str, float]] = {}
    for mode, vals in by_mode.items():
        out[mode] = {
            "n": len(vals),
            "mean_ua": statistics.mean(vals),
            "stdev_ua": statistics.stdev(vals) if len(vals) > 1 else 0.0,
            "min_ua": min(vals),
            "max_ua": max(vals),
        }
    return out


def summarize_transitions(
    doc: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    """Aggregate per-direction transition stats. Direction key is
    e.g. `HP->ULP`. Combines firmware cycles from the transitions
    list with PPK2 pulse widths/currents."""
    ppk = doc.get("ppk2", {})
    ppk_pulses = ppk.get("pulses", []) if ppk.get("available") else []
    by_dir: dict[str, dict[str, list[Any]]] = defaultdict(
        lambda: {"cycles": [], "duration_s": [], "mean_ua": []}
    )
    # Iterate the firmware transitions in order; pull the matching
    # PPK2 pulse by index.
    for i, t in enumerate(doc["transitions"]):
        if t["source"] is None or t["target"] is None:
            continue
        key = f"{t['source']}->{t['target']}"
        if t["cycles_total"] is not None:
            by_dir[key]["cycles"].append(t["cycles_total"])
        if i < len(ppk_pulses):
            p = ppk_pulses[i]
            by_dir[key]["duration_s"].append(p["duration_s"])
            by_dir[key]["mean_ua"].append(p["mean_ua"])
    out: dict[str, dict[str, Any]] = {}
    for key, buckets in by_dir.items():
        out[key] = {"n": len(buckets["cycles"])}
        for field, vals in buckets.items():
            if not vals:
                continue
            out[key][f"{field}_mean"] = statistics.mean(vals)
            out[key][f"{field}_min"] = min(vals)
            out[key][f"{field}_max"] = max(vals)
    return out


def break_even_seconds(
    src_ua: float, tgt_ua: float,
    pulse_dur_s_down: float, pulse_ua_down: float,
    pulse_dur_s_up: float, pulse_ua_up: float,
) -> tuple[float, float]:
    """For a round-trip src -> tgt -> src, return
    (transition_charge_uC, breakeven_s_at_target).

    The CPU does no useful work during either transition -- it
    busy-polls the PMU state machine, writes SRAM/RRAM trim
    registers, and waits for the PLL to relock. All the charge
    the SoC draws during those windows is pure overhead.

    To justify that overhead the SoC must reside in the low mode
    long enough that its lower steady-state current saves at least
    as much charge as the round-trip cost:

        Q_trans = pulse_ua_down * pulse_dur_s_down +
                  pulse_ua_up   * pulse_dur_s_up
        savings_rate_ua = src_ua - tgt_ua           (per second in tgt)
        breakeven_s     = Q_trans / savings_rate_ua

    Q_trans is always positive (raw charge, no baseline subtraction).
    breakeven_s is always positive too. If tgt >= src (no possible
    savings), breakeven_s is +infinity."""
    q_trans_uC = (pulse_ua_down * pulse_dur_s_down +
                  pulse_ua_up * pulse_dur_s_up)
    saving_ua = src_ua - tgt_ua
    if saving_ua <= 0:
        return q_trans_uC, float("inf")
    return q_trans_uC, q_trans_uC / saving_ua


def print_report(doc: dict[str, Any]) -> None:
    meta = doc["meta"]
    ppk = doc.get("ppk2", {})
    print(f"# capture: {meta['captured_at']}")
    print(f"# board:    {meta['board']}")
    print(f"# strategy: {meta['strategy']}")
    print(f"# supply:   {meta['supply_mv']} mV, "
          f"{meta['loops']} loops @ {meta['period_s']} s period")
    print(f"# transitions recorded: {len(doc['transitions'])}")
    if ppk.get("available"):
        print(f"# ppk2:     {ppk['n_samples']} samples "
              f"({ppk['duration_s']:.1f} s @ {ppk['sample_hz']} Hz), "
              f"overall mean = {fmt_current(ppk['overall_mean_ua'])}")
    else:
        print("# ppk2:     not captured")
    print()

    # --- per-mode baseline ---
    modes = summarize_modes(doc)
    if modes:
        print("== steady-state current per mode (post-200ms settling) ==")
        print(f"  {'mode':<4}  {'n':>3}  "
              f"{'mean':>10}  {'std':>10}  {'min':>10}  {'max':>10}")
        for m in ["HP", "LP", "ULP"]:
            if m not in modes:
                continue
            s = modes[m]
            print(f"  {m:<4}  {s['n']:>3}  "
                  f"{fmt_current(s['mean_ua']):>10}  "
                  f"{fmt_current(s['stdev_ua']):>10}  "
                  f"{fmt_current(s['min_ua']):>10}  "
                  f"{fmt_current(s['max_ua']):>10}")
        print()

    # --- per-direction transition ---
    txns = summarize_transitions(doc)
    supply_mv = doc["meta"].get("supply_mv", 3300)
    if txns:
        print(f"== per-direction transition stats "
              f"(supply {supply_mv} mV) ==")
        print(f"  {'direction':<12}  {'n':>3}  "
              f"{'cycles_mean':>12}  "
              f"{'ppk_dur_mean':>13}  {'ppk_mean_ua':>13}  "
              f"{'charge':>12}  {'energy':>12}")
        for key in ["HP->LP", "HP->ULP",
                    "LP->HP", "LP->ULP",
                    "ULP->HP", "ULP->LP"]:
            if key not in txns:
                continue
            s = txns[key]
            cyc = s.get("cycles_mean", 0)
            dur = s.get("duration_s_mean", 0.0)
            ua = s.get("mean_ua_mean", 0.0)
            charge_uC = ua * dur
            energy_uJ = energy_uj(charge_uC, supply_mv)
            print(f"  {key:<12}  {s['n']:>3}  "
                  f"{cyc:>12.0f}  "
                  f"{fmt_duration(dur):>13}  "
                  f"{fmt_current(ua):>13}  "
                  f"{charge_uC:>9.1f} uC  "
                  f"{fmt_energy(energy_uJ):>12}")
        print()

    # --- break-even for round-trips ---
    if modes and txns:
        print(f"== break-even residence for HP -> X -> HP "
              f"round-trips (supply {supply_mv} mV) ==")
        print(f"  {'target':<4}  {'HP_ua':>10}  {'X_ua':>10}  "
              f"{'q_trans':>10}  {'e_trans':>12}  {'breakeven':>12}")
        hp_ua = modes.get("HP", {}).get("mean_ua")
        if hp_ua is None:
            print("  (need HP baseline in the data)")
        else:
            for tgt in ["LP", "ULP"]:
                if tgt not in modes:
                    continue
                key_down = f"HP->{tgt}"
                key_up = f"{tgt}->HP"
                if key_down not in txns or key_up not in txns:
                    continue
                d = txns[key_down]
                u = txns[key_up]
                q, be = break_even_seconds(
                    src_ua=hp_ua,
                    tgt_ua=modes[tgt]["mean_ua"],
                    pulse_dur_s_down=d.get("duration_s_mean", 0),
                    pulse_ua_down=d.get("mean_ua_mean", 0),
                    pulse_dur_s_up=u.get("duration_s_mean", 0),
                    pulse_ua_up=u.get("mean_ua_mean", 0),
                )
                e = energy_uj(q, supply_mv)
                be_str = ("--" if be == float("inf")
                          else fmt_duration(be))
                print(f"  {tgt:<4}  {fmt_current(hp_ua):>10}  "
                      f"{fmt_current(modes[tgt]['mean_ua']):>10}  "
                      f"{q:>7.1f} uC  {fmt_energy(e):>12}  {be_str:>12}")
        print()


# ------------------------------------------------------------------
# Optional plots
# ------------------------------------------------------------------
def make_plots(doc: dict[str, Any], save_dir: Path | None) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("# matplotlib not installed -- skipping plots")
        return

    ppk = doc.get("ppk2", {})
    modes = summarize_modes(doc)
    txns = summarize_transitions(doc)

    if not modes and not txns:
        print("# nothing to plot")
        return

    figs = []

    # Plot 1: mean current per mode (bar chart with error bars).
    if modes:
        fig, ax = plt.subplots(figsize=(6, 4))
        keys = [m for m in ["HP", "LP", "ULP"] if m in modes]
        means = [modes[m]["mean_ua"] / 1000.0 for m in keys]
        stds = [modes[m]["stdev_ua"] / 1000.0 for m in keys]
        ax.bar(keys, means, yerr=stds, capsize=6,
               color=["#c33", "#c93", "#3c9"])
        ax.set_ylabel("mean current (mA)")
        ax.set_title(f"Steady-state current per mode -- "
                     f"{doc['meta']['strategy']}")
        ax.grid(axis="y", alpha=0.3)
        for i, (m, s) in enumerate(zip(means, stds)):
            ax.text(i, m + s, f"{m:.2f} mA", ha="center", va="bottom")
        fig.tight_layout()
        figs.append(("current_per_mode", fig))

    # Plot 2: transition duration per direction (bar chart).
    supply_mv = doc["meta"].get("supply_mv", 3300)
    if txns and ppk.get("available"):
        fig, ax = plt.subplots(figsize=(8, 4))
        keys = [k for k in ["HP->LP", "HP->ULP",
                            "LP->HP", "LP->ULP",
                            "ULP->HP", "ULP->LP"] if k in txns]
        durs_ms = [txns[k].get("duration_s_mean", 0) * 1000 for k in keys]
        mean_ua = [txns[k].get("mean_ua_mean", 0) / 1000.0 for k in keys]
        x = range(len(keys))
        ax.bar(x, durs_ms, color="#369")
        ax.set_xticks(list(x))
        ax.set_xticklabels(keys, rotation=45, ha="right")
        ax.set_ylabel("mean transition duration (ms)")
        ax.set_title(f"Transition wall time (PPK2 pm-busy pulse) -- "
                     f"{doc['meta']['strategy']}")
        ax.grid(axis="y", alpha=0.3)
        for i, (d, ua) in enumerate(zip(durs_ms, mean_ua)):
            ax.text(i, d, f"{d:.0f} ms\n{ua:.2f} mA",
                    ha="center", va="bottom", fontsize=8)
        fig.tight_layout()
        figs.append(("transition_duration", fig))

    # Plot 2b: energy per transition (mJ, computed via V_supply).
    if txns and ppk.get("available"):
        fig, ax = plt.subplots(figsize=(8, 4))
        keys = [k for k in ["HP->LP", "HP->ULP",
                            "LP->HP", "LP->ULP",
                            "ULP->HP", "ULP->LP"] if k in txns]
        energies_mj = []
        for k in keys:
            dur_s = txns[k].get("duration_s_mean", 0)
            ua = txns[k].get("mean_ua_mean", 0)
            charge_uc = ua * dur_s
            energies_mj.append(energy_uj(charge_uc, supply_mv) / 1000.0)
        x = range(len(keys))
        ax.bar(x, energies_mj, color="#c63")
        ax.set_xticks(list(x))
        ax.set_xticklabels(keys, rotation=45, ha="right")
        ax.set_ylabel("mean energy per transition (mJ)")
        ax.set_title(f"Transition energy at {supply_mv} mV -- "
                     f"{doc['meta']['strategy']}")
        ax.grid(axis="y", alpha=0.3)
        for i, e in enumerate(energies_mj):
            ax.text(i, e, f"{e:.2f} mJ",
                    ha="center", va="bottom", fontsize=8)
        fig.tight_layout()
        figs.append(("energy_per_transition", fig))

    # Plot 3: break-even chart.
    if modes and txns and "HP" in modes:
        fig, ax = plt.subplots(figsize=(6, 4))
        rows = []
        hp_ua = modes["HP"]["mean_ua"]
        for tgt in ["LP", "ULP"]:
            if tgt not in modes:
                continue
            kd = f"HP->{tgt}"
            ku = f"{tgt}->HP"
            if kd not in txns or ku not in txns:
                continue
            _, be = break_even_seconds(
                src_ua=hp_ua, tgt_ua=modes[tgt]["mean_ua"],
                pulse_dur_s_down=txns[kd].get("duration_s_mean", 0),
                pulse_ua_down=txns[kd].get("mean_ua_mean", 0),
                pulse_dur_s_up=txns[ku].get("duration_s_mean", 0),
                pulse_ua_up=txns[ku].get("mean_ua_mean", 0),
            )
            rows.append((tgt, be * 1000))  # ms
        if rows:
            ax.bar([r[0] for r in rows], [r[1] for r in rows],
                   color=["#c93", "#3c9"])
            ax.set_ylabel("break-even residence (ms)")
            ax.set_title(f"HP -> X -> HP break-even -- "
                         f"{doc['meta']['strategy']}")
            ax.grid(axis="y", alpha=0.3)
            for i, r in enumerate(rows):
                ax.text(i, r[1], f"{r[1]:.1f} ms",
                        ha="center", va="bottom")
            fig.tight_layout()
            figs.append(("break_even", fig))

    if save_dir is not None:
        save_dir.mkdir(parents=True, exist_ok=True)
        for name, fig in figs:
            path = save_dir / f"{name}.png"
            fig.savefig(path, dpi=120)
            print(f"# saved {path}")
    else:
        plt.show()


# ------------------------------------------------------------------
# CLI
# ------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("json", type=Path,
                    help="capture JSON produced by cycle_modes.py")
    ap.add_argument("--show", action="store_true",
                    help="also open the matplotlib windows interactively "
                    "instead of only saving them to disk")
    ap.add_argument("--no-plots", action="store_true",
                    help="don't generate plots (report only)")
    ap.add_argument("--save-plots", type=Path, default=None,
                    metavar="DIR",
                    help="write PNGs to DIR (default: alongside the JSON)")
    args = ap.parse_args()

    if not args.json.exists():
        sys.exit(f"file not found: {args.json}")

    doc = load(args.json)
    print_report(doc)

    if not args.no_plots:
        # Default: put PNGs next to the JSON. --save-plots overrides.
        save_dir = args.save_plots or args.json.resolve().parent
        make_plots(doc, save_dir if not args.show else None)
        if args.show:
            # If --show was passed we opened interactive windows and
            # deliberately skipped saving. Save too, for the record.
            make_plots(doc, save_dir)

    return 0


if __name__ == "__main__":
    sys.exit(main())
