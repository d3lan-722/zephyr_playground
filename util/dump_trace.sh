#!/usr/bin/env bash
# dump_trace.sh — Attach to a running PSE84, read PM breadcrumbs + dump trace.
#
# Usage:
#   ./util/dump_trace.sh <path/to/zephyr.elf> [seconds] [output.bin]
#   ./util/dump_trace.sh --read-only                       # just read BREGs, no trace dump
#
# Example:
#   ./util/dump_trace.sh apps/04_ppp/build/zephyr/zephyr.elf 10
#   ./util/dump_trace.sh --read-only
#
# Prerequisites:
#   - Board already flashed and running (west flash / power cycle)
#   - KitProg3 probe connected via USB
#   - No other OpenOCD/GDB session active

set -euo pipefail

# ---------- tools ----------------------------------------------------------
GDB="/usr/local/zephyr-sdk-0.17.4/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"
OPENOCD="/usr/local/openocd/bin/openocd"
OPENOCD_SCRIPTS="/usr/local/openocd/scripts"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENOCD_CFG="${SCRIPT_DIR}/openocd/pse84/openocd.tcl"

# ---------- mode -----------------------------------------------------------
READ_ONLY=0
if [[ "${1:-}" == "--read-only" ]]; then
    READ_ONLY=1
    ELF=""
    WAIT_SEC="${2:-2}"
    OUTPUT=""
else
    ELF="${1:?Usage: $0 <zephyr.elf> [seconds] [output.bin]  OR  $0 --read-only}"
    WAIT_SEC="${2:-10}"
    ELF_DIR="$(cd "$(dirname "$ELF")/.." && pwd)"
    OUTPUT="${3:-${ELF_DIR}/trace_$(date +%Y%m%d_%H%M%S).bin}"
    if [[ ! -f "$ELF" ]]; then
        echo "Error: ELF file not found: $ELF" >&2
        exit 1
    fi
fi

# ---------- start OpenOCD WITHOUT reset ------------------------------------
# Key: set ENABLE_ACQUIRE 0 BEFORE sourcing the board config, so the
#      if {$::ENABLE_ACQUIRE} { init; reset init } block is skipped.
#      This connects the DAP without resetting the target, preserving
#      BACKUP_BREG values and the CPU's current state.
OPENOCD_LOG=$(mktemp /tmp/openocd_trace_XXXXXX.log)

if [[ "$READ_ONLY" -eq 1 ]]; then
    # ---- READ-ONLY: run OpenOCD inline, halt, read registers, exit --------
    # When CM33 is in DEEPSLEEP_OFF the CPU core and its MEM-AP are powered
    # down — we can't halt or read through cat1d.cm33.  Instead we use
    # cat1d.sys (System AP, always-on mem_ap) to read backup registers and
    # SRSS directly, no halt required.  All reads are wrapped in catch {}
    # because the bus may be partially powered down.
    echo "[1/2] Connecting to target (no-reset) and reading state ..."
    BREG_RAW=$(mktemp /tmp/breg_raw_XXXXXX.txt)
    TCL_SCRIPT=$(mktemp /tmp/ocd_read_XXXXXX.tcl)
    cat > "$TCL_SCRIPT" <<'TCLEOF'
proc safe_mdw {addr {count 1}} {
    if {[catch {mdw $addr $count} result]} {
        echo "  mdw $addr FAILED: $result"
    }
}

echo ">>> BREG"
targets cat1d.sys
echo "  BREG SET0 (0x52421000) - PM breadcrumbs:"
safe_mdw 0x52421000 4
echo "  BREG SET1 (0x52421010) - main sentinels:"
safe_mdw 0x52421010 4

echo ">>> RES_CAUSE"
echo "  Secure:"
safe_mdw 0x52401BD0 2
echo "  Non-secure:"
safe_mdw 0x42401BD0 2

echo ">>> PWR_CTL2"
safe_mdw 0x52401004

echo ">>> CM33_STATE"
if {[catch {targets cat1d.cm33} err]} {
    echo "  CM33 target not available: $err"
} else {
    if {[catch {halt} err]} {
        echo "  halt failed (CPU likely powered off): $err"
    } else {
        catch {reg pc}
        echo ">>> SCB_SCR"
        safe_mdw 0xE000ED10
        catch {resume}
    }
}

shutdown
TCLEOF

    "$OPENOCD" \
        -s "$OPENOCD_SCRIPTS" \
        -c "set ENABLE_ACQUIRE 0" \
        -c "set ENABLE_CM55 0" \
        -f "$OPENOCD_CFG" \
        -c "init" \
        -c "adapter speed 12000" \
        -f "$TCL_SCRIPT" \
        2>&1 | tee "$BREG_RAW" || true

    rm -f "$TCL_SCRIPT"

    echo ""
    echo "============================================"
    echo "=== PM Breadcrumbs (BACKUP_BREG_SET0)    ==="
    echo "============================================"
    # SET0 at RTC+0x1000 = 0x52421000
    BREG_LINE=$(grep -E '^0x[45]2421000: [0-9a-f]' "$BREG_RAW" | head -1 || echo "")
    if [[ -n "$BREG_LINE" ]]; then
        echo "  Raw: $BREG_LINE"
        WORDS=( $(echo "$BREG_LINE" | sed 's/.*: //') )
        echo "  [0] total pm_state_set calls : $((16#${WORDS[0]:-0}))"
        echo "  [1] last state (1=idle,2=standby,3=ram,4=soft-off): $((16#${WORDS[1]:-0}))"
        echo "  [2] SOFT_OFF entry count     : $((16#${WORDS[2]:-0}))"
        echo "  [3] last DeepSleep retval    : 0x${WORDS[3]:-0}"
    else
        echo "  (could not read SET0)"
    fi
    # SET1 at RTC+0x1010 = 0x52421010
    SENT_LINE=$(grep -E '^0x[45]2421010: [0-9a-f]' "$BREG_RAW" | head -1 || echo "")
    if [[ -n "$SENT_LINE" ]]; then
        SWORDS=( $(echo "$SENT_LINE" | sed 's/.*: //') )
        echo "  SET1[0] init sentinel        : 0x${SWORDS[0]:-0}"
        echo "  SET1[1] main-loop counter    : 0x${SWORDS[1]:-0}"
    else
        echo "  (could not read SET1)"
    fi

    echo ""
    echo "=== CPU State ==="
    grep ">>> CM33_STATE" -A3 "$BREG_RAW" | grep -v ">>> CM33_STATE" || echo "  (CPU powered off — DEEPSLEEP_OFF)"
    echo ""
    echo "=== Reset Cause (SRSS_RES_CAUSE / RES_CAUSE2) ==="
    grep "0x52401bd0" "$BREG_RAW" || echo "  (not read)"
    echo ""
    echo "=== PWR_CTL2 (SRSS+0x1004) ==="
    grep "0x52401004" "$BREG_RAW" || echo "  (not read — power domain may be off)"
    echo ""
    echo "=== SCB_SCR (0xE000ED10) ==="
    grep "0xe000ed10" "$BREG_RAW" || echo "  (not read — CPU may be powered down)"
    echo ""

    rm -f "$BREG_RAW"
    echo "[2/2] Done (read-only)."
    exit 0
fi

# ---- FULL MODE: start OpenOCD in background for GDB trace dump -----------
echo "[1/4] Starting OpenOCD (no-reset attach, log: $OPENOCD_LOG) ..."
"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -c "set ENABLE_ACQUIRE 0" \
    -c "set ENABLE_CM55 0" \
    -f "$OPENOCD_CFG" \
    -c "init" \
    -c "adapter speed 12000" \
    > "$OPENOCD_LOG" 2>&1 &
OPENOCD_PID=$!

# Give OpenOCD time to bind its GDB port
sleep 2
if ! kill -0 "$OPENOCD_PID" 2>/dev/null; then
    echo "Error: OpenOCD failed to start. Check $OPENOCD_LOG" >&2
    cat "$OPENOCD_LOG"
    exit 1
fi

cleanup() {
    echo "[*] Cleaning up OpenOCD (PID $OPENOCD_PID) ..."
    kill "$OPENOCD_PID" 2>/dev/null || true
    wait "$OPENOCD_PID" 2>/dev/null || true
}
trap cleanup EXIT

# ---------- wait for the application to run --------------------------------
echo "[2/4] Letting the application run for ${WAIT_SEC}s ..."
sleep "$WAIT_SEC"

# ---------- dump trace via GDB ---------------------------------------------
echo "[3/4] Attaching GDB, halting core, dumping trace to: $OUTPUT"

GDB_COMMANDS=$(mktemp /tmp/gdb_trace_XXXXXX.gdb)
cat > "$GDB_COMMANDS" <<EOF
set confirm off
set pagination off
set mem inaccessible-by-default off
target remote :3333

# Set a HW breakpoint on pm_state_set — fires just before deep sleep,
# while SRAM (and the Percepio ring buffer) is still intact.
hbreak pm_state_set
continue

# Breakpoint hit — SRAM is valid, dump trace
dump binary value ${OUTPUT} *RecorderDataPtr

detach
quit
EOF

"$GDB" -batch -nx -x "$GDB_COMMANDS" "$ELF" 2>&1 | \
    grep -v "^Reading symbols\|^Remote debugging\|^warning:" || true

rm -f "$GDB_COMMANDS"

# ---------- done -----------------------------------------------------------
if [[ -f "$OUTPUT" ]]; then
    SIZE=$(stat -c%s "$OUTPUT" 2>/dev/null || stat -f%z "$OUTPUT" 2>/dev/null)
    echo "[4/4] Done! Trace dumped: $OUTPUT ($SIZE bytes)"
    echo "      Open in Tracealyzer: File → Open → Trace File"
else
    echo "[4/4] Error: trace file was not created." >&2
    exit 1
fi
