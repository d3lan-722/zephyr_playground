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
# series with the DUT's VDD rail, the DUT is powered off whenever
# the PPK2 is not actively "measuring" -- the toggle_DUT_power("ON")
# command only asserts the internal FET switch WHILE start_measuring
# is running. So we spawn scripts/ppk2_power.py's keeper daemon
# (which holds the port open with start_measuring active) for the
# duration of `west flash`, then stop it afterwards. Downstream
# scripts (scripts/cycle_modes.py) are then responsible for their
# own PPK2 lifecycle -- flash and cycle_modes are independent.
#
# If no PPK2 is attached the helper is a no-op.
#
# Note on DAP acquire: KitProg's acquire-in-test-mode needs XRES vs
# SWD timing that only works when KitProg itself owns power. If the
# DUT ends up in a DAP-locked state after a PPK2 power cycle, this
# wrapper alone won't recover it -- see the README for the one-time
# bypass procedure.
flash() {
    "$HERE/scripts/ppk2_power.py" on --daemon || true
    sleep 0.5
    local rc=0
    west flash -d "$HERE/build" || rc=$?
    "$HERE/scripts/ppk2_power.py" off || true
    return $rc
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
