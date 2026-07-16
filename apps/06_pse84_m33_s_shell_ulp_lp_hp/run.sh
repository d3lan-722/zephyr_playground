#!/usr/bin/env bash
# Build and flash the PSE84 secure-only shell HP/LP/ULP demo (project 06).
#
# Single Zephyr image on kit_pse84_eval CM33-Secure — no TF-M, no CM55,
# no partitions. The mode-switch step sequences call the PDL syspm
# entries directly (see src/power_manager.c).
set -euo pipefail

BOARD=kit_pse84_eval/pse846gps2dbzc4a/m33

HERE=$(cd "$(dirname "$0")" && pwd)

# Snippet that redirects zephyr,flash from the board's m33s_xip SMIF
# partition to internal RRAM. Required for HP/LP/ULP switching: mode
# transitions change the ClkHf tree divider, which scales the SMIF
# peripheral clock. Without a corresponding SMIF-timing retune, the
# next XIP fetch after a transition hangs the CPU. Running from RRAM
# sidesteps that entirely -- the RRAM controller has its own
# Cy_RRAM_SetVoltageMode retune that pm_switch_to() already invokes.
#
# The snippet is app-local under ./snippets/rram/ and is discovered
# because CMakeLists.txt appends CMAKE_CURRENT_SOURCE_DIR to
# SNIPPET_ROOT before find_package(Zephyr). Use --snippet rram to
# activate.
SNIPPET_ARGS="--snippet rram"

cmd=${1:-all}

build() {
    west build -p always -b "$BOARD" -d "$HERE/build" "$HERE" $SNIPPET_ARGS
}

# If a PPK2 is attached in ampere-meter mode with its VIN/VOUT in
# series with the DUT's VDD rail, the DUT is powered off whenever the
# PPK2's internal FET switch is open (its default state on script exit
# and after USB re-enumeration). KitProg's DAP cannot program a
# powered-off target, so ensure the switch is closed before
# west flash.
#
# We deliberately do NOT power the DUT off after flashing:
# KitProg's DAP-acquire-in-test-mode sequence requires XRES-vs-SWD
# timing that is only reliable when KitProg itself controls power.
# With PPK2 in the VDD path, every power cycle leaves the ROM
# bootloader locking the DAP and 'kitprog3: failed to acquire the
# device' after the next attempted flash. Keeping the DUT powered
# after flashing preserves the DAP-enabled state (Zephyr does not
# lock it) so subsequent re-flashes work. If you specifically need
# to remove DUT power, run `scripts/ppk2_power.py off` manually.
flash() {
    "$HERE/scripts/ppk2_power.py" on || true
    sleep 0.3
    west flash -d "$HERE/build"
}

clean() {
    rm -rf "$HERE/build"
}

case "$cmd" in
    build) build ;;
    flash) flash ;;
    clean) clean ;;
    all)   build; flash ;;
    *) echo "usage: $0 {build|flash|all|clean}" >&2; exit 1 ;;
esac
