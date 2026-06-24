#!/usr/bin/env bash
# dump_ds_state.sh -- Post-WFI state dump for PSE84 DS-RAM diagnosis.
# Each mdw is wrapped in catch so a single bad address (e.g. PD1 off)
# does not abort subsequent reads.

set -euo pipefail

OPENOCD="/usr/local/openocd/bin/openocd"
OPENOCD_SCRIPTS="/usr/local/openocd/scripts"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENOCD_CFG="${SCRIPT_DIR}/openocd/pse84/openocd.tcl"

ACQUIRE="${ACQUIRE:-0}"

# Tcl helper: try-read one word and label it; never abort on error.
TRY='proc tryword {label addr} { if {[catch {mem2array tmp 32 $addr 1} msg]} { echo "[format {%-32s 0x%08X} $label $addr]: ERR ($msg)" } else { echo "[format {%-32s 0x%08X = 0x%08X} $label $addr $tmp(0)]" } }'

"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -c "set ENABLE_ACQUIRE ${ACQUIRE}" \
    -c "set ENABLE_CM55 0" \
    -f "$OPENOCD_CFG" \
    -c "init" \
    -c "adapter speed 4000" \
    -c "targets cat1d.sys" \
    -c "$TRY" \
    -c "echo {==== BREG_SET1[0..3] ====}" \
    -c "tryword {BREG_SET1[0] mainStamp} 0x42421010" \
    -c "tryword {BREG_SET1[1] warmTok}   0x42421014" \
    -c "tryword {BREG_SET1[2] PWR_CTL pre} 0x42421018" \
    -c "tryword {BREG_SET1[3] PWR_CTL post} 0x4242101C" \
    -c "echo {==== BREG_SET2 PPU snapshots ====}" \
    -c "tryword {SET2[0] MAIN}     0x42421020" \
    -c "tryword {SET2[1] SRAM0}    0x42421024" \
    -c "tryword {SET2[2] SRAM1}    0x42421028" \
    -c "tryword {SET2[3] SYSCPU}   0x4242102C" \
    -c "tryword {SET2[4] PD1}      0x42421030" \
    -c "tryword {SET2[5] APPCPU}   0x42421034" \
    -c "tryword {SET2[6] APPCPUSS} 0x42421038" \
    -c "tryword {SET2[7] SOCMEM}   0x4242103C" \
    -c "echo {==== Live SRSS ====}" \
    -c "tryword {SRSS.PWR_CTL}   0x42401000" \
    -c "tryword {SRSS.PWR_CTL2}  0x42401004" \
    -c "tryword {SRSS.RES_CAUSE} 0x42401BD0" \
    -c "echo {==== Live PPU PWPR (policy) / PWSR (status) ====}" \
    -c "tryword {MAIN.PWPR}     0x42411000" \
    -c "tryword {MAIN.PWSR}     0x42411008" \
    -c "tryword {SYSCPU.PWPR}   0x42225000" \
    -c "tryword {SYSCPU.PWSR}   0x42225008" \
    -c "tryword {SRAM0.PWPR}    0x42220000" \
    -c "tryword {SRAM0.PWSR}    0x42220008" \
    -c "tryword {SRAM1.PWPR}    0x42221000" \
    -c "tryword {SRAM1.PWSR}    0x42221008" \
    -c "tryword {PD1.PWPR}      0x42413000" \
    -c "tryword {PD1.PWSR}      0x42413008" \
    -c "tryword {APPCPU.PWPR}   0x44101000" \
    -c "tryword {APPCPU.PWSR}   0x44101008" \
    -c "tryword {APPCPUSS.PWPR} 0x44100000" \
    -c "tryword {APPCPUSS.PWSR} 0x44100008" \
    -c "tryword {SOCMEM.PWPR}   0x44660000" \
    -c "tryword {SOCMEM.PWSR}   0x44660008" \
    -c "echo {==== Halt CM33 and read SCR/ICSR/SCS_CPPWR ====}" \
    -c "targets cat1d.cm33" \
    -c "catch {halt}" \
    -c "tryword {SCB.SCR  (bit2=SLEEPDEEP)} 0xE000ED10" \
    -c "tryword {SCB.ICSR}                  0xE000ED04" \
    -c "tryword {SCS.CPPWR (bit20/22=SU10/SU11)} 0xE000E00C" \
    -c "catch {resume}" \
    -c "shutdown" \
    2>&1 | grep -vE '^(Open On-Chip|Licensed|For bug|adapter speed|Info :|Warn :)' || true
