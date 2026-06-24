#!/usr/bin/env bash
# dump_ds_ram.sh -- Attach to a running PSE84 and dump everything
#                   needed to diagnose DEEPSLEEP_RAM behaviour.
#
# Usage:
#   ./util/dump_ds_ram.sh
#
# Non-intrusive: connects via KitProg3 without resetting the target.
# Reads SRSS power regs, RTC warm-boot token, and all 9 PPU PWSR
# values through the system AP (cat1d.sys) so it is safe to call
# while the CPU is in WFI / DS-RAM.
#
# Suggested workflow for diagnosing the "DS-RAM enters once then
# wakes to 2.6 mA spin" problem in apps/16_pse84_3img_rram_pm:
#
#   1. Flash the app, let it boot, watch console.
#   2. While the LED is cyan (chip is meant to be in DS-RAM), run:
#          ./util/dump_ds_ram.sh
#      Look at SRSS_PWR_CTL.DEBUG_SESSION and PPU PWSR.STA values.
#      MAIN/SRAM*/SOCMEM should read MEM_RET, SYSCPU should read OFF.
#   3. Run it again after the chip has visibly woken (red blinking).
#      Compare PPU states and check whether RES_CAUSE was latched.
#
# Prerequisites:
#   - Board already flashed and running.
#   - KitProg3 probe connected via USB.
#   - No other OpenOCD/GDB session active.

set -euo pipefail

OPENOCD="/usr/local/openocd/bin/openocd"
OPENOCD_SCRIPTS="/usr/local/openocd/scripts"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENOCD_CFG="${SCRIPT_DIR}/openocd/pse84/openocd.tcl"
DS_RAM_TCL="${SCRIPT_DIR}/openocd/pse84/dump_ds_ram.tcl"

# ACQUIRE=1: KitProg3 toggles XRES + sends the test-mode handshake so
# OpenOCD can attach even when CM33-NS just entered DS-RAM (DAP off
# in PD1) or is stuck spinning in a broken state. This costs us a
# reset — the dump will reflect post-reset state, NOT the live sleep
# state — but it's the only way to talk to the DAP when PD1 is down.
#
# Set ACQUIRE=0 manually if you want to read the live state of a
# chip that you know is awake (e.g. between sleep windows).
ACQUIRE="${ACQUIRE:-1}"

"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -c "set ENABLE_ACQUIRE ${ACQUIRE}" \
    -c "set ENABLE_CM55 0" \
    -f "$OPENOCD_CFG" \
    -c "init" \
    -c "adapter speed 12000" \
    -f "$DS_RAM_TCL" \
    2>&1 | grep -vE '^(Open On-Chip|Licensed|For bug|Info :|Warn :|adapter speed)' || true
