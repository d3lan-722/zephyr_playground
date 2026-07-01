#!/usr/bin/env bash
# Copyright (c) 2026 Infineon Technologies AG
# SPDX-License-Identifier: Apache-2.0
#
# Apply Option D from doc/TFM_tutorial.md §29 to the in-tree TF-M
# PSE84 platform port. Flips 8 CYCFG_PPC_SECURED_* bits from 1U to
# 0U so that SRSS_MAIN, PWRMODE, RAMC0/1_RAM_PWR, M33SYSCPUSS, and
# the APPCPUSS group all become NS-accessible.
#
# After running this and rebuilding TF-M, PDL syspm APIs called from
# CM33-NS (Cy_SysPm_Set{Sys,App,SOCMEM}DeepSleepMode,
# Cy_SysPm_CoreBuck*, SRSS_PWR_CTL2 writes, etc.) succeed directly
# instead of bus-faulting. Enables Phase 6 (Layer-B static bias) and
# Phase 7 (per-transition PPU config) without a project-local secure
# partition.
#
# Idempotent: safe to run multiple times.
# Revert: reflip manually, or `west update -f`, or wipe the module tree.
#
# Isolation cost (see doc/TFM_tutorial.md §29 Option D + §30):
# any NS code can now reprogram SRSS clocks, hibernate, PWRMODE PPU,
# SRAM PPUs, APPCPUSS-domain PPUs, and CM33 SYSCPU (also opens
# MSC/DDFT/AP debug windows). Acceptable for dev-board bring-up;
# revert before production.

set -euo pipefail

# There are TWO cycfg_ppc.h files that both need to change:
#
#   [1] TF-M platform port (consumed by the TF-M-Secure build)
#   [2] Zephyr hal_infineon module (consumed by the CM33-NS Zephyr
#       build for the NS-side PDL syspm compile)
#
# If only [1] is patched, NS-side cy_syspm_v4.c is still compiled
# with CY_PDL_SYSPM_ENABLE_SRF_INTEG=1 (because [2]'s bits are still
# 1U) and takes the SRF branch instead of the direct-register branch.
# The SRF branch still works, but Cy_SysPm_SetSysDeepSleepMode et al.
# (which have no SRF branch at all) still bus-fault. Both files must
# be flipped in lock-step.
CYCFG_TFM="${HOME}/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h"
CYCFG_HAL="${HOME}/zephyrproject/modules/hal/infineon/zephyr-ifx-cycfg/pse84/kit_pse84_eval/cycfg_ppc.h"

for f in "$CYCFG_TFM" "$CYCFG_HAL"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: $f not found" >&2
        exit 1
    fi
done

# The 8 bits to flip. The first 5 (SRSS_MAIN through M55APPCPUSS)
# MUST move together — cy_syspm_srf.h enforces this with a #error.
# The other 3 are what makes Phase 7's SetSysDeepSleepMode succeed
# (SRAM0/1 PPU writes + CPUSS PPU write).
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
        if grep -qE "^${prefix}[[:space:]]+0U$" "$f"; then
            printf '    already 0U: %s\n' "$bit"
            continue
        fi
        if ! grep -qE "^${prefix}[[:space:]]+1U$" "$f"; then
            printf '    WARN: %s not found or unexpected value; skipping\n' "$bit" >&2
            continue
        fi
        sed -i "s|^${prefix}[[:space:]]\+1U\$|${prefix} 0U|" "$f"
        changed=$((changed + 1))
        printf '    flipped %s: 1U -> 0U\n' "$bit"
    done
done

echo ""
if [[ $changed -eq 0 ]]; then
    echo "Option D already applied — no changes made."
else
    echo "Option D applied ($changed bits changed across 2 files)."
    echo "Rebuild TF-M for the change to take effect:"
    echo "  cd $(dirname "$(realpath "$0")")/.. && ./run.sh build"
fi
