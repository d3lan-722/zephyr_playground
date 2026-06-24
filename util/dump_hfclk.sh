#!/usr/bin/env bash
# dump_hfclk.sh -- Attach to a running PSE84 and dump HFCLK configuration.
#
# Usage:
#   ./util/dump_hfclk.sh
#
# Non-intrusive: connects via KitProg3 without resetting the target and
# reads SRSS clock registers through the system AP (cat1d.sys).  The CPU
# is NOT halted, so this is safe to run while the application is in
# deep sleep.
#
# Prerequisites:
#   - Board already flashed and running
#   - KitProg3 probe connected via USB
#   - No other OpenOCD/GDB session active

set -euo pipefail

OPENOCD="/usr/local/openocd/bin/openocd"
OPENOCD_SCRIPTS="/usr/local/openocd/scripts"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENOCD_CFG="${SCRIPT_DIR}/openocd/pse84/openocd.tcl"
HFCLK_TCL="${SCRIPT_DIR}/openocd/pse84/dump_hfclk.tcl"

"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -c "set ENABLE_ACQUIRE 0" \
    -c "set ENABLE_CM55 0" \
    -f "$OPENOCD_CFG" \
    -c "init" \
    -c "adapter speed 12000" \
    -f "$HFCLK_TCL" \
    2>&1 | grep -vE '^(Open On-Chip|Licensed|For bug|Info :|Warn :|adapter speed)' || true
