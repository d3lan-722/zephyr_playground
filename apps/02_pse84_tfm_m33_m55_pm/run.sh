#!/usr/bin/env bash
# Build and flash the PSE84 TF-M dual-core blinky.
# Order matters: CM33-NS is built first (CM55 consumes its PSA headers);
# CM55 is flashed first (CM33-NS jumps to it on boot).
set -euo pipefail

BOARD_CM33=kit_pse84_eval/pse846gps2dbzc4a/m33/ns
BOARD_CM55=kit_pse84_eval/pse846gps2dbzc4a/m55

HERE=$(cd "$(dirname "$0")" && pwd)
CM33_DIR=$HERE/cm33_ns
CM55_DIR=$HERE/cm55

cmd=${1:-all}

build() {
    rm -rf "$CM33_DIR/build"
    west build -b "$BOARD_CM33" -d "$CM33_DIR/build" "$CM33_DIR" -- -DDTC_OVERLAY_FILE=$CM33_DIR/boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay
    rm -rf  "$CM55_DIR/build"
   west build -b "$BOARD_CM55" -d "$CM55_DIR/build" "$CM55_DIR" -- \
  -DPSE84_CM33_BUILD_DIR="$CM33_DIR/build" \
  -DDTC_OVERLAY_FILE="$CM55_DIR/boards/kit_pse84_eval_pse846gps2dbzc4a_m55.overlay" 
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
