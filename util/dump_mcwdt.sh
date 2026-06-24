#!/usr/bin/env bash
# dump_mcwdt.sh -- Attach to a running PSE84 and dump MCWDT0 + NVIC state.
#
# Usage:
#   ./util/dump_mcwdt.sh
#
# Non-intrusive: connects via KitProg3 without resetting the target and
# reads MCWDT0 / NVIC through the system AP (cat1d.sys).  The CPU is NOT
# halted, so this is safe to run while the application is in deep sleep.
#
# Useful for debugging the Zephyr LP-timer / kernel tick on PSE84:
#   - Is MCWDT0 enabled?  (CTL.ENABLED0)
#   - Is the counter advancing?  (run twice, compare CNTLOW.CTR0)
#   - Is MATCH programmed for a future tick?
#   - Is the IRQ unmasked at MCWDT (INTR_MASK) and at NVIC (ISER1 bit 23)?
#   - Is the IRQ pending but not being serviced?  (INTR / ISPR1)
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
MCWDT_TCL="${SCRIPT_DIR}/openocd/pse84/dump_mcwdt.tcl"

"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -c "set ENABLE_ACQUIRE 0" \
    -c "set ENABLE_CM55 0" \
    -f "$OPENOCD_CFG" \
    -c "init" \
    -c "adapter speed 12000" \
    -f "$MCWDT_TCL" \
    2>&1 | grep -vE '^(Open On-Chip|Licensed|For bug|Info :|Warn :|adapter speed)' || true
