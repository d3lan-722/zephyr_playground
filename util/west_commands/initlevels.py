#!/usr/bin/env python3
# Copyright (c) 2026 Infineon Technologies AG
# SPDX-License-Identifier: Apache-2.0

"""West extension: `west initlevels`.

Wraps Zephyr's ``check_init_priorities.py --initlevels`` so it can be
invoked from anywhere in the workspace without remembering the script
path. Loads the Zephyr base from ``ZEPHYR_BASE`` (or, failing that,
from the manifest workspace).

Usage::

    west initlevels                              # most recent build
    west initlevels --build-dir client/build
    west initlevels --build-dir apps/04_pse84_dual_core_rram/build/m55

The selected build directory must contain ``zephyr/zephyr.elf``.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path
from typing import Optional

try:
    from west.commands import WestCommand
except ImportError:  # allow direct `python3 initlevels.py ...` invocation
    WestCommand = None  # type: ignore[assignment,misc]


# Default search order when --build-dir is not supplied.
_DEFAULT_BUILD_DIRS = (
    "build",
    "client/build",
)


def _find_zephyr_base(west_topdir: Path) -> Optional[Path]:
    """Return Zephyr base directory, or None if it cannot be located."""

    env_base = os.environ.get("ZEPHYR_BASE")
    if env_base:
        return Path(env_base)

    candidate = west_topdir / "zephyr"
    if (candidate / "scripts" / "build" / "check_init_priorities.py").exists():
        return candidate

    return None


def _resolve_build_dir(arg: Optional[str], topdir: Path) -> Optional[Path]:
    """Pick the build dir to inspect."""

    if arg:
        p = Path(arg)
        if p.is_absolute():
            return p
        # Try cwd first, then west topdir.
        cwd_cand = Path.cwd() / p
        if (cwd_cand / "zephyr" / "zephyr.elf").exists():
            return cwd_cand
        return topdir / p

    for rel in _DEFAULT_BUILD_DIRS:
        cand = topdir / rel
        if (cand / "zephyr" / "zephyr.elf").exists():
            return cand

    return None


class InitLevels(WestCommand if WestCommand is not None else object):
    def __init__(self) -> None:
        if WestCommand is None:
            return
        super().__init__(
            "initlevels",
            "list SYS_INIT / DEVICE_DEFINE init order from a build",
            "Run Zephyr's check_init_priorities.py --initlevels on the "
            "given build directory and print the result.",
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name, help=self.help, description=self.description
        )
        parser.add_argument(
            "-d",
            "--build-dir",
            default=None,
            help="build directory to inspect (default: ./build, " "./client/build)",
        )
        return parser

    def do_run(self, args, _unknown):
        topdir = Path(self.topdir)
        _run(topdir, args.build_dir, log=self.inf, fail=self.die)


def _run(topdir: Path, build_dir_arg: Optional[str], *, log, fail) -> None:
    zephyr_base = _find_zephyr_base(topdir)
    if zephyr_base is None:
        fail(
            "Cannot find Zephyr base. Set ZEPHYR_BASE or run from a "
            "west workspace that contains zephyr/."
        )

    build_dir = _resolve_build_dir(build_dir_arg, topdir)
    if build_dir is None:
        fail(
            "No build directory found. Pass --build-dir <path> or "
            "build the project first."
        )

    elf = build_dir / "zephyr" / "zephyr.elf"
    if not elf.exists():
        fail(f"{elf} not found \u2014 build the project first.")

    script = zephyr_base / "scripts" / "build" / "check_init_priorities.py"
    cmd = [
        sys.executable,
        str(script),
        "-f",
        str(elf),
        "--initlevels",
    ]

    log(f"running {' '.join(cmd)}")
    subprocess.check_call(cmd)


def _main_standalone(argv):
    parser = argparse.ArgumentParser(
        description="List SYS_INIT / DEVICE_DEFINE init order from a Zephyr build."
    )
    parser.add_argument("-d", "--build-dir", default=None)
    args = parser.parse_args(argv)

    topdir = Path(os.environ.get("WEST_TOPDIR", os.getcwd()))

    def _log(msg):
        print(msg, file=sys.stderr)

    def _die(msg):
        print(f"error: {msg}", file=sys.stderr)
        sys.exit(1)

    _run(topdir, args.build_dir, log=_log, fail=_die)
    return 0


if __name__ == "__main__":
    sys.exit(_main_standalone(sys.argv[1:]))
