# Dump PSE84 SOCMEM PWR_PARTITION_CTL + PWR_PARTITION_CTL_LOCK + PWR_STATUS
# via the system AP (cat1d.sys). Non-intrusive: no halt, no acquire.
#
# Addresses (cyip_socmem.h + pse846gps2dbzc4a.h):
#   SOCMEM base               = 0x44640000
#   PWR_PARTITION_CTL[0..15]  @ +0x200 .. +0x23c (16 x 32-bit, R/W via lock)
#   PWR_PARTITION_CTL_LOCK    @ +0x280 (R/W)
#   PWR_STATUS                @ +0x288 (R, bit0 = PWR_DONE)
#
# PWR_PARTITION_CTL fields:
#   ACT_OFF[1:0] @ bit 0  -- partition power-off in ACTIVE  (00 = on)
#   RET_OFF[1:0] @ bit 8  -- partition power-off in DS      (00 = retain, 11 = off)
#
# PWR_PARTITION_CTL_LOCK[1:0]:
#   00 = unlocked, write 0x1 = CLR0, 0x2 = CLR1, 0x3 = SET01 (locked)
#
# Lock-gated reads of PWR_PARTITION_CTL return 0 when locked.

proc safe_r32 {addr} {
    if {[catch {read_memory $addr 32 1} v]} { return -1 }
    return $v
}

proc safe_w32 {addr val} {
    if {[catch {write_memory $addr 32 [list $val]} _]} { return 0 }
    return 1
}

set SOCMEM_BASE         0x44640000
set PART_CTL_BASE       [expr {$SOCMEM_BASE + 0x200}]
set PART_LOCK           [expr {$SOCMEM_BASE + 0x280}]
set PWR_STATUS          [expr {$SOCMEM_BASE + 0x288}]
set SOCMEM_PPU          0x44660000
set PD1_PPU             0x42413000

puts ""
puts "==== PSE84 SOCMEM PWR snapshot (system AP, NS-SBUS aliases) ===="
puts ""

# PD1 + SOCMEM PPU policy/status -- read-only, no lock
set pd1_pwpr    [safe_r32 [expr {$PD1_PPU + 0x000}]]
set pd1_pwsr    [safe_r32 [expr {$PD1_PPU + 0x008}]]
set socppu_pwpr [safe_r32 [expr {$SOCMEM_PPU + 0x000}]]
set socppu_pwsr [safe_r32 [expr {$SOCMEM_PPU + 0x008}]]
puts [format "PD1     PPU  PWPR=0x%08x  PWSR=0x%08x" $pd1_pwpr $pd1_pwsr]
puts [format "SOCMEM  PPU  PWPR=0x%08x  PWSR=0x%08x" $socppu_pwpr $socppu_pwsr]
puts ""

# Lock state + lock-gated PARTITION_CTL[0..9] before unlock
set lock0 [safe_r32 $PART_LOCK]
set sta0  [safe_r32 $PWR_STATUS]
puts [format "PARTITION_CTL_LOCK (before unlock) = 0x%08x" $lock0]
puts [format "PWR_STATUS                         = 0x%08x  (bit0 = PWR_DONE)" $sta0]
puts ""
puts "PARTITION_CTL[0..9] BEFORE unlock (expect 0 if lock blocks reads):"
for {set i 0} {$i < 10} {incr i} {
    set v [safe_r32 [expr {$PART_CTL_BASE + $i * 4}]]
    puts [format "  [%d] = 0x%08x  ACT_OFF=%d RET_OFF=%d" $i $v [expr {$v & 0x3}] [expr {($v >> 8) & 0x3}]]
}

puts ""
puts "Sending CLR0 + CLR1 to PARTITION_CTL_LOCK from system AP..."
set ok0 [safe_w32 $PART_LOCK 0x1]
set ok1 [safe_w32 $PART_LOCK 0x2]
puts [format "  CLR0 write returned %d, CLR1 write returned %d" $ok0 $ok1]
set lock1 [safe_r32 $PART_LOCK]
puts [format "PARTITION_CTL_LOCK (after unlock)  = 0x%08x" $lock1]
puts ""
puts "PARTITION_CTL[0..9] AFTER unlock (real values):"
for {set i 0} {$i < 10} {incr i} {
    set v [safe_r32 [expr {$PART_CTL_BASE + $i * 4}]]
    puts [format "  [%d] = 0x%08x  ACT_OFF=%d RET_OFF=%d" $i $v [expr {$v & 0x3}] [expr {($v >> 8) & 0x3}]]
}

# Re-lock so we leave the chip as we found it
safe_w32 $PART_LOCK 0x3

puts ""
puts "Decoding RET_OFF column:"
puts "  RET_OFF=0 -> partition RETAINED through DS  (high leakage)"
puts "  RET_OFF=1 -> partition POWERED DOWN in DS   (low leakage)"
puts ""
puts "If all RET_OFF=0 here despite RETAIN_SOCMEM_MASK=0x000, the NS"
puts "Cy_SysPm_SetSOCMemPartDsPwrMode writes from enter_ds_ram() are"
puts "being silently dropped (PD1/SOCMEM peripheral NS-write hazard)."
puts ""

shutdown
