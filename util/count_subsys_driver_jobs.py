#!/usr/bin/env python3
"""
Count ZDP-style "jobs" per thread across Zephyr's core subsystems and
drivers (not sample apps). Sibling of util/count_sample_jobs.py.

Units of analysis:
- Every first-level directory under /home/ubuntu/zephyrproject/zephyr/subsys
- Every first-level directory under /home/ubuntu/zephyrproject/zephyr/drivers
Each such directory is treated as one "module".

Definitions:
- Thread: any static K_THREAD_DEFINE or runtime k_thread_create.
- Job boundary: any blocking Zephyr primitive (see BLOCKING_PRIMITIVES).
  Call sites containing K_NO_WAIT on the same line are excluded.
"""

from __future__ import annotations

import csv
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ZEPHYR = Path("/home/ubuntu/zephyrproject/zephyr")
ROOTS = [ZEPHYR / "subsys", ZEPHYR / "drivers"]

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

THREAD_MARKERS = ["K_THREAD_DEFINE", "k_thread_create"]

CALL_RE = {p: re.compile(rf"\b{p}\s*\(") for p in BLOCKING_PRIMITIVES}
NO_WAIT_RE = re.compile(r"\bK_NO_WAIT\b")
THREAD_RE = {m: re.compile(rf"\b{m}\s*\(") for m in THREAD_MARKERS}


def analyse_file(path: Path) -> tuple[Counter, int, list[str]]:
    """Return (blocking counter, thread declaration count, files with threads)."""
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return Counter(), 0, []
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)

    counter: Counter[str] = Counter()
    for line in text.splitlines():
        if NO_WAIT_RE.search(line):
            continue
        for prim, rx in CALL_RE.items():
            counter[prim] += len(rx.findall(line))
    tcount = sum(len(rx.findall(text)) for rx in THREAD_RE.values())
    return counter, tcount, ([str(path)] if tcount else [])


def analyse_module(mod_dir: Path) -> dict:
    total: Counter = Counter()
    threads = 0
    thread_files: list[str] = []
    n_files = 0
    for c in mod_dir.rglob("*.c"):
        if "build" in c.parts:
            continue
        n_files += 1
        counter, tcount, tf = analyse_file(c)
        total.update(counter)
        threads += tcount
        thread_files.extend(tf)
    blk = sum(total.values())
    return {
        "module": str(mod_dir.relative_to(ZEPHYR)),
        "threads_declared": threads,
        "blocking_sites_total": blk,
        "avg_jobs_per_thread": round(blk / max(threads + 1, 1), 2),
        "c_files": n_files,
        "thread_files": ";".join(
            str(Path(p).relative_to(ZEPHYR)) for p in thread_files
        ),
        **{f"n_{p}": total[p] for p in BLOCKING_PRIMITIVES},
    }


def main() -> int:
    modules: list[Path] = []
    for root in ROOTS:
        modules.extend(sorted(p for p in root.iterdir() if p.is_dir()))
    print(
        f"Analysing {len(modules)} modules under subsys/ and drivers/", file=sys.stderr
    )

    rows = [analyse_module(m) for m in modules]

    fieldnames = list(rows[0].keys())
    writer = csv.DictWriter(sys.stdout, fieldnames=fieldnames)
    writer.writeheader()
    for r in rows:
        writer.writerow(r)

    # ---- Summary ----
    print("\n=== Summary ===", file=sys.stderr)
    total_threads = sum(r["threads_declared"] for r in rows)
    total_blk = sum(r["blocking_sites_total"] for r in rows)
    with_thread = [r for r in rows if r["threads_declared"] > 0]
    print(f"Modules analysed:           {len(rows)}", file=sys.stderr)
    print(f"Modules that spawn threads: {len(with_thread)}", file=sys.stderr)
    print(f"Total declared threads:     {total_threads}", file=sys.stderr)
    print(f"Total blocking sites:       {total_blk}", file=sys.stderr)

    # Split by root.
    print("\n=== By area ===", file=sys.stderr)
    for area in ("subsys", "drivers"):
        rs = [r for r in rows if r["module"].startswith(area + "/")]
        rst = [r for r in rs if r["threads_declared"] > 0]
        print(
            f"{area:8}  modules={len(rs):3}  with_threads={len(rst):3}  "
            f"threads={sum(r['threads_declared'] for r in rs):4}  "
            f"blocking={sum(r['blocking_sites_total'] for r in rs):5}",
            file=sys.stderr,
        )

    # Top-N by thread count.
    print("\n=== Top 20 modules by declared threads ===", file=sys.stderr)
    for r in sorted(rows, key=lambda r: -r["threads_declared"])[:20]:
        print(
            f"  threads={r['threads_declared']:3}  "
            f"blk={r['blocking_sites_total']:4}  "
            f"avg_jobs={r['avg_jobs_per_thread']:>5}  {r['module']}",
            file=sys.stderr,
        )

    # Top-N by blocking sites.
    print("\n=== Top 20 modules by blocking-site count ===", file=sys.stderr)
    for r in sorted(rows, key=lambda r: -r["blocking_sites_total"])[:20]:
        print(
            f"  blk={r['blocking_sites_total']:4}  "
            f"threads={r['threads_declared']:3}  "
            f"avg_jobs={r['avg_jobs_per_thread']:>5}  {r['module']}",
            file=sys.stderr,
        )

    # Primitive frequency.
    prim_total: Counter[str] = Counter()
    for r in rows:
        for p in BLOCKING_PRIMITIVES:
            prim_total[p] += r[f"n_{p}"]
    print("\n=== Primitive frequency (subsys+drivers combined) ===", file=sys.stderr)
    for p, c in prim_total.most_common():
        if c:
            print(f"  {p:20} {c}", file=sys.stderr)

    # Histogram of blocking sites per module (only modules with threads).
    print(
        "\n=== Blocking-site histogram (thread-owning modules only) ===",
        file=sys.stderr,
    )
    buckets = [(0, 0), (1, 3), (4, 10), (11, 30), (31, 100), (101, 300), (301, 10**9)]
    hist: Counter[str] = Counter()
    for r in with_thread:
        v = r["blocking_sites_total"]
        for lo, hi in buckets:
            if lo <= v <= hi:
                key = f"{lo}" if lo == hi else f"{lo}-{hi}" if hi < 10**9 else f">={lo}"
                hist[key] += 1
                break
    for lo, hi in buckets:
        key = f"{lo}" if lo == hi else f"{lo}-{hi}" if hi < 10**9 else f">={lo}"
        print(f"  {key:>10} {hist[key]:>4}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
