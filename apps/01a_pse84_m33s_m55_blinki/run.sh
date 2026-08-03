#!/usr/bin/env bash
# Two standalone Zephyr images, no TF-M and no sysbuild.
#
# CM33-Secure sets up SAU (via soc_late_init_hook), then main() calls
# the local pse84_boot_local::app_pse84_cm55_startup() to configure
# MPC/PPC, release the CM55 core, and RETURN. CM33-S then enters its
# own blink loop on the red LED.
#
# CM55 is a plain non-secure Zephyr image blinking the green LED.
#
# Flash order: CM55 must be present in external flash before CM33-S
# releases it on reset.
set -euo pipefail

BOARD_CM33=kit_pse84_eval/pse846gps2dbzc4a/m33
BOARD_CM55=kit_pse84_eval/pse846gps2dbzc4a/m55

HERE=$(cd "$(dirname "$0")" && pwd)
CM33_DIR=$HERE/cm33_s
CM55_DIR=$HERE/cm55

cmd=${1:-all}

build() {
    west build -p always -b "$BOARD_CM55" -d "$CM55_DIR/build" "$CM55_DIR"
    west build -p always -b "$BOARD_CM33" -d "$CM33_DIR/build" "$CM33_DIR"
}

flash() {
    west flash -d "$CM55_DIR/build"
    west flash -d "$CM33_DIR/build"
}

clean() {
    rm -rf "$CM33_DIR/build" "$CM55_DIR/build"
}

case "$cmd" in
    build) build ;;
    flash) flash ;;
    clean) clean ;;
    all)   build; flash ;;
    *) echo "usage: $0 {build|flash|all|clean}" >&2; exit 1 ;;
esac
