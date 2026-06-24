#!/usr/bin/env bash
# Read L1-boot status & key MMIO via raw SYS-AP, no chip reset,
# no vendor cat1d target script (which requires ENABLE_ACQUIRE = Test Mode).
#
# Strategy: declare a plain swj-dp + mem_ap on AP #0 (the always-on AHB-AP
# alias the PSE84 SYS-AP exposes), open the debug power domain via the
# generic 'dap' command, then do raw mdw reads.

set -e

OPENOCD=${OPENOCD:-/usr/local/openocd/bin/openocd}
SCRIPTS=${OPENOCD_SCRIPTS:-/usr/local/openocd/scripts}

"$OPENOCD" \
    --search "$SCRIPTS" \
    -c 'gdb_port disabled' \
    -c 'telnet_port disabled' \
    -c 'tcl_port disabled' \
    -c 'source [find interface/kitprog3.cfg]' \
    -c 'transport select swd' \
    -c 'adapter speed 1000' \
    -c 'swd newdap pse84 cpu -irlen 4 -expected-id 0x4c013477' \
    -c 'dap create pse84.dap -chain-position pse84.cpu' \
    -c 'target create pse84.ap mem_ap -dap pse84.dap -ap-num 0' \
    -c 'init' \
    -c 'pse84.dap dpreg 0x4 0x50000000' \
    -c 'echo "--- DP CTRL/STAT ---"' \
    -c 'pse84.dap dpreg 0x4' \
    -c 'echo "--- L1-boot status at 0x34000000..0x34000007 ---"' \
    -c 'pse84.ap mdw 0x34000000 2' \
    -c 'echo "--- BACKUP.BREG_SET1[0..3] (0x42421010) ---"' \
    -c 'pse84.ap mdw 0x42421010 4' \
    -c 'echo "--- SRSS.RES_CAUSE / RES_CAUSE2 (0x52400440) ---"' \
    -c 'pse84.ap mdw 0x52400440 2' \
    -c 'echo "--- SRSS.PWR_CTL / PWR_STATE (0x52400080) ---"' \
    -c 'pse84.ap mdw 0x52400080 2' \
    -c 'shutdown'
