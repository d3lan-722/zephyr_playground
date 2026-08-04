#!/usr/bin/env bash
# Build and flash the PSE84 CM33-S + CM55 power-shell demo (project 07a).
#
# Two standalone Zephyr images, no TF-M, no sysbuild.
#
# CM33-Secure runs from internal RRAM (`--snippet rram`), hosts the
# Zephyr shell, drives HP/LP/ULP + sleep/deep_sleep, and releases
# CM55 from a local copy of pse84_boot.c.
#
# CM55 runs a parking image (Cy_SysPm_SetDeepSleepMode(DEEPSLEEP) +
# masked WFI loop) so the SoC has a second DEEPSLEEP requestor for
# the system-DS path.
#
# Flash order: CM55 must be present in external flash before CM33-S
# releases it on reset.
set -euo pipefail

BOARD_CM33=kit_pse84_eval/pse846gps2dbzc4a/m33
BOARD_CM55=kit_pse84_eval/pse846gps2dbzc4a/m55

HERE=$(cd "$(dirname "$0")" && pwd)
CM33_DIR=$HERE/cm33_s
CM55_DIR=$HERE/cm55

SNIPPET_ARGS="--snippet rram"

cmd=${1:-all}

build() {
    west build -p always -b "$BOARD_CM55" -d "$CM55_DIR/build" "$CM55_DIR"
    west build -p always -b "$BOARD_CM33" -d "$CM33_DIR/build" "$CM33_DIR" $SNIPPET_ARGS
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
