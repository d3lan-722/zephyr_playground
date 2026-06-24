#!/usr/bin/env bash
# Read L1-boot status & key MMIO via vendor cat1d cfg with ENABLE_ACQUIRE=0
# (no Test-Mode trap, no reset). Requires open SYS-AP (DEVELOPMENT LCS OK).
#
# IMPORTANT: Run against a chip in the actual wake-hang state. Do NOT run
# `edgeprotecttools device-info` first; that forces Test Mode and overwrites
# 0x34000000 with the listen-window pattern.

set -e

OPENOCD=${OPENOCD:-/usr/local/openocd/bin/openocd}
SCRIPTS=${OPENOCD_SCRIPTS:-/usr/local/openocd/scripts}

"$OPENOCD" \
    --search "$SCRIPTS" \
    -c 'set ENABLE_ACQUIRE 0' \
    -c 'set ENABLE_POWER_SUPPLY 0' \
    -c 'set ENABLE_CM33 0; set ENABLE_CM55 0' \
    -c 'gdb_port disabled' \
    -c 'telnet_port disabled' \
    -c 'tcl_port disabled' \
    -c 'source [find interface/kitprog3.cfg]' \
    -c 'transport select swd' \
    -c 'source [find target/infineon/pse84xgxs2.cfg]' \
    -c 'adapter speed 1000' \
    -c 'init' \
    -c 'echo "--- L1-boot status (0x34000000..0x34000007) ---"' \
    -c 'cat1d.sys mdw 0x34000000 2' \
    -c 'echo "--- BACKUP.BREG_SET1 slots 0..3 (0x42421010) ---"' \
    -c 'cat1d.sys mdw 0x42421010 4' \
    -c 'echo "--- SRSS.RES_CAUSE / RES_CAUSE2 (0x52400440) ---"' \
    -c 'cat1d.sys mdw 0x52400440 2' \
    -c 'echo "--- SRSS.PWR_CTL / PWR_STATE (0x52400080) ---"' \
    -c 'cat1d.sys mdw 0x52400080 2' \
    -c 'shutdown'
