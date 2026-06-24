# `initlevels.py` — list Zephyr init order

Workspace-local helper that wraps Zephyr's
[`check_init_priorities.py --initlevels`](https://docs.zephyrproject.org/latest/develop/west/zephyr-cmds.html)
so it can be run on any build directory without remembering the
script's path inside the Zephyr tree.

> **Note.** This is *not* registered as a `west` extension. The
> manifest project (`manifest-repo/`) lives outside the radar
> workspace, and `west` rejects `west-commands` paths that escape
> the project directory. Invoke the script directly as shown below.

## What it shows

`check_init_priorities.py --initlevels` disassembles the linked image
and prints every `SYS_INIT` / `DEVICE_DEFINE` callback grouped by
`init_level` (PRE_KERNEL_1, PRE_KERNEL_2, POST_KERNEL, APPLICATION,
SMP) and ordered within each level by `init_priority`. Useful when
debugging:

* "why does my driver initialise too late?"
* "is my `SYS_INIT(.., APPLICATION, 0)` actually the last hook?"
* PSE84 / sysbuild projects with several images, each with its own
  init list.

## Usage

```bash
# explicit build dir (most common — sysbuild projects have multiple)
python3 util/west_commands/initlevels.py \
    --build-dir apps/04_pse84_dual_core_rram/build/m55

python3 util/west_commands/initlevels.py \
    --build-dir apps/04_pse84_dual_core_rram/build/enable_cm55

# auto-detect: looks for ./build then ./client/build
python3 util/west_commands/initlevels.py
```

If `zephyr/` is not under the current directory, set
`WEST_TOPDIR` so the script can locate the Zephyr scripts:

```bash
WEST_TOPDIR=/home/ubuntu/zephyrproject \
    python3 /workspaces/radar/util/west_commands/initlevels.py \
    --build-dir apps/04_pse84_dual_core_rram/build/m55
```

## Requirements

* `ZEPHYR_BASE` exported, **or** the cwd / `WEST_TOPDIR` resolves to
  a directory that contains `zephyr/`. The devcontainer layout
  (`/home/ubuntu/zephyrproject/zephyr/`) satisfies this.
* The build must have been performed with debug info (the default).
