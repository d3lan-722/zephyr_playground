#!/usr/bin/env bash
# dump_pc.sh -- Halt the PSE84 cores via SWD and print PC/LR/xPSR
#               for CM33-NS (cat1d.cpu) so we can see what code the
#               core is executing when the chip wedges at ~2.7 mA
#               after a DS-RAM cycle.
#
# Two modes:
#   ACQUIRE=0 (default) — try to attach to the running chip without
#                         reset/test-mode. Use this when you BELIEVE
#                         the chip is alive but stuck (the post-DS-RAM
#                         hang is exactly that case).
#   ACQUIRE=1           — reset+test-mode handshake. Use if the live
#                         attach above fails ("DAP initialization
#                         failed"). Recovers the DAP but reflects
#                         post-reset state, not the live PC.
#
# Usage:
#   ./util/dump_pc.sh           # live attach
#   ACQUIRE=1 ./util/dump_pc.sh # reset+attach (only if live fails)
#
# Prerequisites: KitProg3 connected, no other OpenOCD/GDB session.

set -euo pipefail

OPENOCD="/usr/local/openocd/bin/openocd"
OPENOCD_SCRIPTS="/usr/local/openocd/scripts"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENOCD_CFG="${SCRIPT_DIR}/openocd/pse84/openocd.tcl"

ACQUIRE="${ACQUIRE:-0}"

"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -c "set ENABLE_ACQUIRE ${ACQUIRE}" \
    -c "set ENABLE_CM55 0" \
    -f "$OPENOCD_CFG" \
    -c "init" \
    -c "adapter speed 4000" \
    -c "targets cat1d.cm33" \
    -c "halt" \
    -c "echo {==== CM33-NS halted ====}" \
    -c "echo {PC :}" -c "reg pc"   \
    -c "echo {LR :}" -c "reg lr"   \
    -c "echo {SP :}" -c "reg sp"   \
    -c "echo {MSP:}" -c "reg msp"  \
    -c "echo {PSP:}" -c "reg psp"  \
    -c "echo {xPSR:}" -c "reg xpsr" \
    -c "echo {CONTROL:}" -c "reg control" \
    -c "echo {==== Fault status (SCB) ====}" \
    -c "echo {CFSR @ 0xE000ED28:}" -c "mdw 0xE000ED28 1" \
    -c "echo {HFSR @ 0xE000ED2C:}" -c "mdw 0xE000ED2C 1" \
    -c "echo {MMFAR @ 0xE000ED34:}" -c "mdw 0xE000ED34 1" \
    -c "echo {BFAR  @ 0xE000ED38:}" -c "mdw 0xE000ED38 1" \
    -c "echo {ICSR  @ 0xE000ED04:}" -c "mdw 0xE000ED04 1" \
    -c "echo {VTOR  @ 0xE000ED08:}" -c "mdw 0xE000ED08 1" \
    -c "echo {SHCSR @ 0xE000ED24:}" -c "mdw 0xE000ED24 1" \
    -c "echo {==== NVIC pending (ISPR[0..15] @ 0xE000E200) ====}" \
    -c "mdw 0xE000E200 16" \
    -c "echo {==== NVIC enabled (ISER[0..15] @ 0xE000E100) ====}" \
    -c "mdw 0xE000E100 16" \
    -c "echo {==== NVIC active  (IABR[0..15] @ 0xE000E300) ====}" \
    -c "mdw 0xE000E300 16" \
    -c "echo {==== PRIMASK/BASEPRI/FAULTMASK ====}" \
    -c "reg primask" -c "reg basepri" -c "reg faultmask" \
    -c "resume" \
    -c "shutdown" \
    2>&1 | grep -vE '^(Open On-Chip|Licensed|For bug|adapter speed|Info :)' || true
