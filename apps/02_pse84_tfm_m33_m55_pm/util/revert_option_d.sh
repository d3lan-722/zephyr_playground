#!/usr/bin/env bash
# Copyright (c) 2026 Infineon Technologies AG
# SPDX-License-Identifier: Apache-2.0
#
# Revert Option D (apply_option_d.sh) — flip the 8 CYCFG_PPC_SECURED_*
# bits back from 0U to 1U in both cycfg_ppc.h copies. Use when Option D
# turned out not to do what we expected (it only gates the PDL's SRF-
# integ compile-time headers; the runtime PPC config in cycfg_system.c
# is separate and still enforces the original Secure-only attribution).
#
# Idempotent: safe to run multiple times.

set -euo pipefail

CYCFG_TFM="${HOME}/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h"
CYCFG_HAL="${HOME}/zephyrproject/modules/hal/infineon/zephyr-ifx-cycfg/pse84/kit_pse84_eval/cycfg_ppc.h"

for f in "$CYCFG_TFM" "$CYCFG_HAL"; do
    [[ -f "$f" ]] || { echo "ERROR: $f not found" >&2; exit 1; }
done

BITS=(
    SRSS_MAIN
    SRSS_HIB_DATA
    PWRMODE_PWRMODE
    APPCPUSS_AP
    M55APPCPUSS
    RAMC0_RAM_PWR
    RAMC1_RAM_PWR
    M33SYSCPUSS
)

changed=0
for f in "$CYCFG_TFM" "$CYCFG_HAL"; do
    label="$(basename "$(dirname "$(dirname "$f")")")/$(basename "$(dirname "$f")")/$(basename "$f")"
    echo "-- $label"
    for bit in "${BITS[@]}"; do
        prefix="#define CYCFG_PPC_SECURED_${bit}"
        if grep -qE "^${prefix}[[:space:]]+1U$" "$f"; then
            printf '    already 1U: %s\n' "$bit"
            continue
        fi
        if ! grep -qE "^${prefix}[[:space:]]+0U$" "$f"; then
            printf '    WARN: %s not found or unexpected value; skipping\n' "$bit" >&2
            continue
        fi
        sed -i "s|^${prefix}[[:space:]]\+0U\$|${prefix} 1U|" "$f"
        changed=$((changed + 1))
        printf '    reverted %s: 0U -> 1U\n' "$bit"
    done
done

echo ""
if [[ $changed -eq 0 ]]; then
    echo "Option D already reverted — no changes made."
else
    echo "Option D reverted ($changed bits changed). Rebuild TF-M:"
    echo "  cd $(dirname "$(realpath "$0")")/.. && ./run.sh build"
fi
