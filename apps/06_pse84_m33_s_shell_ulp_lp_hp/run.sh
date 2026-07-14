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

flash() {
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
