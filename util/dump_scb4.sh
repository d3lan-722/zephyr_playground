#!/usr/bin/env bash
# dump_scb4.sh -- Attach to a running PSE84 and dump SCB4 (UART4 / BT HCI)
# interrupt and FIFO state.
#
# Usage:
#   ./util/dump_scb4.sh
#
# Non-intrusive: connects via KitProg3 without resetting the target and
# reads SCB4 registers through the system AP (cat1d.sys).  The CPU is
# NOT halted, so this is safe to run while the h4.c bt_uart_isr while()
# loop is spinning; the returned values reflect the current hardware
# state that the ISR is polling.
#
# Prerequisites:
#   - Board already flashed and running
#   - KitProg3 probe connected via USB
#   - No other OpenOCD/GDB session active on the same probe

set -euo pipefail

OPENOCD="/usr/local/openocd/bin/openocd"
OPENOCD_SCRIPTS="/usr/local/openocd/scripts"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENOCD_CFG="${SCRIPT_DIR}/openocd/pse84/openocd.tcl"
SCB4_TCL="${SCRIPT_DIR}/openocd/pse84/dump_scb4.tcl"

"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -c "set ENABLE_ACQUIRE 0" \
    -c "set ENABLE_CM55 0" \
    -f "$OPENOCD_CFG" \
    -c "init" \
    -c "adapter speed 12000" \
    -f "$SCB4_TCL" \
    2>&1 | grep -vE '^(Open On-Chip|Licensed|For bug|Info :|Warn :|adapter speed)' || true
