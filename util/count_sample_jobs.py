#!/usr/bin/env python3
"""
Count ZDP-style "jobs" per thread in Zephyr's upstream samples.

A ZDP job is the code a thread runs between two consecutive blocking
primitives (see doc/dvfs_pm.md §4.1). A blocking primitive is any
Zephyr kernel call that suspends the caller until an event or timeout
fires. This script does a lexical (grep-style) count of call sites
across every sample under /home/ubuntu/zephyrproject/zephyr/samples.

Caveats:
- Call sites are counted regardless of the timeout argument, so a
  k_sem_take(&s, K_NO_WAIT) is counted like a blocking one. To keep
  the count meaningful we still exclude *_get/take/put lines that
  literally contain "K_NO_WAIT".
- Blocking calls hidden in helper functions or libraries are missed.
- A "thread" is anything declared with K_THREAD_DEFINE or created
  with k_thread_create. The implicit main thread is not counted.

Output: CSV to stdout, plus a summary block to stderr.
"""

from __future__ import annotations

import csv
import os
import re
import sys
from collections import Counter
from pathlib import Path

SAMPLES_ROOT = Path("/home/ubuntu/zephyrproject/zephyr/samples")

# Blocking primitives Zephyr threads can call. Each is a job boundary
# unless followed by a K_NO_WAIT timeout arg.
BLOCKING_PRIMITIVES = [
    "k_msleep",
    "k_usleep",
    "k_sleep",
    "k_sem_take",
    "k_mutex_lock",
    "k_msgq_get",
    "k_msgq_put",
    "k_queue_get",
    "k_condvar_wait",
    "k_event_wait",
    "k_event_wait_all",
    "k_poll",
    "k_thread_join",
    "k_pipe_get",
    "k_pipe_put",
    "k_pipe_read",
    "k_pipe_write",
    "k_stack_pop",
    "k_mbox_get",
    "k_fifo_get",
    "k_lifo_get",
    "k_futex_wait",
    "k_thread_suspend",
]

THREAD_MARKERS = [
    "K_THREAD_DEFINE",
    "k_thread_create",
]

# Compiled patterns: word-boundary + open paren + no K_NO_WAIT on the
# same line. Not perfect, but filters the majority of non-blocking
# poll/sem_take call sites.
CALL_RE = {prim: re.compile(rf"\b{prim}\s*\(") for prim in BLOCKING_PRIMITIVES}
NO_WAIT_RE = re.compile(r"\bK_NO_WAIT\b")
THREAD_RE = {marker: re.compile(rf"\b{marker}\s*\(") for marker in THREAD_MARKERS}


def analyse_c_file(path: Path) -> tuple[Counter, int]:
    """Return (blocking_primitive_counter, thread_declaration_count) for one C/H file."""
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return Counter(), 0

    # Strip block comments and line comments to reduce false positives.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)

    counter: Counter[str] = Counter()
    for line in text.splitlines():
        if NO_WAIT_RE.search(line):
            continue
        for prim, rx in CALL_RE.items():
            counter[prim] += len(rx.findall(line))

    thread_count = 0
    for marker, rx in THREAD_RE.items():
        thread_count += len(rx.findall(text))
    return counter, thread_count


def find_samples() -> list[Path]:
    """Return every directory that contains sample.yaml (the sample marker)."""
    return sorted(p.parent for p in SAMPLES_ROOT.rglob("sample.yaml"))


def analyse_sample(sample_dir: Path) -> dict:
    """Aggregate blocking-primitive and thread counts across all C files below."""
    total = Counter()
    threads = 0
    c_files = list(sample_dir.rglob("*.c")) + list(sample_dir.rglob("*.h"))
    for c in c_files:
        # Skip vendored/build artefacts.
        if "build" in c.parts or "twister-out" in c.parts:
            continue
        counter, tcount = analyse_c_file(c)
        total.update(counter)
        threads += tcount

    blocking_total = sum(total.values())
    return {
        "sample": str(sample_dir.relative_to(SAMPLES_ROOT)),
        "threads_declared": threads,
        "blocking_sites_total": blocking_total,
        # Job-count proxy: if there are threads, average blocking sites per
        # thread (+ 1 for the main thread). If no threads declared, all
        # blocking sites belong to main => that's the job count of main.
        "avg_jobs_per_thread": (round(blocking_total / max(threads + 1, 1), 2)),
        "c_and_h_files": len(c_files),
        **{f"n_{prim}": total[prim] for prim in BLOCKING_PRIMITIVES},
    }


def main() -> int:
    samples = find_samples()
    print(f"Found {len(samples)} samples under {SAMPLES_ROOT}", file=sys.stderr)

    rows = [analyse_sample(s) for s in samples]

    fieldnames = list(rows[0].keys())
    writer = csv.DictWriter(sys.stdout, fieldnames=fieldnames)
    writer.writeheader()
    for r in rows:
        writer.writerow(r)

    # Summary to stderr so it does not pollute the CSV.
    print("\n=== Summary ===", file=sys.stderr)
    total_threads = sum(r["threads_declared"] for r in rows)
    total_blocks = sum(r["blocking_sites_total"] for r in rows)
    print(f"Total samples analysed:     {len(rows)}", file=sys.stderr)
    print(f"Total declared threads:     {total_threads}", file=sys.stderr)
    print(f"Total blocking-site count:  {total_blocks}", file=sys.stderr)
    print(
        f"Avg blocking sites per sample: {total_blocks / max(len(rows), 1):.2f}",
        file=sys.stderr,
    )

    # Histogram of blocking_sites_total.
    hist: Counter[str] = Counter()
    buckets = [(0, 0), (1, 1), (2, 3), (4, 7), (8, 15), (16, 31), (32, 63), (64, 10**9)]
    for r in rows:
        v = r["blocking_sites_total"]
        for lo, hi in buckets:
            if lo <= v <= hi:
                key = f"{lo}" if lo == hi else f"{lo}-{hi}" if hi < 10**9 else f">={lo}"
                hist[key] += 1
                break
    print("\nBlocking-site histogram (samples per bucket):", file=sys.stderr)
    for lo, hi in buckets:
        key = f"{lo}" if lo == hi else f"{lo}-{hi}" if hi < 10**9 else f">={lo}"
        print(f"  {key:>6}: {hist[key]}", file=sys.stderr)

    # Top-N samples by blocking sites.
    print("\nTop 20 samples by blocking-site count:", file=sys.stderr)
    rows_sorted = sorted(rows, key=lambda r: r["blocking_sites_total"], reverse=True)
    for r in rows_sorted[:20]:
        print(
            f"  {r['blocking_sites_total']:4}  threads={r['threads_declared']:3}  {r['sample']}",
            file=sys.stderr,
        )

    # Primitive frequency.
    prim_total: Counter[str] = Counter()
    for r in rows:
        for prim in BLOCKING_PRIMITIVES:
            prim_total[prim] += r[f"n_{prim}"]
    print("\nBlocking primitive frequency across all samples:", file=sys.stderr)
    for prim, cnt in prim_total.most_common():
        if cnt == 0:
            continue
        print(f"  {prim:20} {cnt}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
