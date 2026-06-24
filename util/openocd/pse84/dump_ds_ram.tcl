# Dump PSE84 state relevant to DEEPSLEEP_RAM diagnosis.
#
# Non-intrusive: reads through the system AP (cat1d.sys), CPU is NOT
# halted, so we can sample while the chip is parked in WFI / DS-RAM.
#
# What we are trying to answer:
#   * Did DS-RAM actually engage? (PWSR of MAIN/SRAM0/SRAM1/SYSCPU
#     should be MEM_RET/OFF; if they read ON/FULL_RET the SRSS
#     downgraded the request to plain CPU DeepSleep.)
#   * Did the SRSS detect a debug session? (PWR_CTL.DEBUG_SESSION=1
#     is the bit that silently demotes DS-RAM/DS-OFF -> DeepSleep.)
#   * Is the warm-boot token planted? (RTC->BREG_SET1[1] ==
#     0x16D5DA01 if power.c reached its pre-WFI store.)
#   * Was a reset cause latched? (SRSS_RES_CAUSE should be 0 right
#     after a fresh DS-RAM warm boot; non-zero after XRES.)
#
# References:
#   SRSS_PWR_CTL          @ 0x42401000  bit 4  DEBUG_SESSION
#                                       bit 5  LPM_READY
#   SRSS_PWR_CTL2         @ 0x42401004
#   SRSS_RES_CAUSE        @ 0x42401BD0
#   SRSS_RES_CAUSE2       @ 0x42401BD4
#   SRSS_RES_CAUSE_EXTEND @ 0x42401C04
#   RTC base              @ 0x42420000  BREG_SET1[0..3] @ +0x1010
#   PPU bases             see dump_ppu.tcl

proc safe_r32 {addr} {
    if {[catch {read_memory $addr 32 1} v]} { return -1 }
    return $v
}

proc fmt_hex32 {v} {
    if {$v < 0} { return "<bus error>" }
    return [format "0x%08x" $v]
}

proc mode_name {m} {
    switch -- $m {
        0  { return "OFF" }
        1  { return "OFF_EMU" }
        2  { return "MEM_RET" }
        3  { return "MEM_RET_EMU" }
        4  { return "LOGIC_RET" }
        5  { return "FULL_RET" }
        6  { return "MEM_OFF" }
        7  { return "FUNC_RET" }
        8  { return "ON" }
        default { return [format "?%d" $m] }
    }
}

init
targets cat1d.sys

puts ""
puts "=== SRSS power state ==="
set pwr_ctl   [safe_r32 0x42401000]
set pwr_ctl2  [safe_r32 0x42401004]
set res_cause [safe_r32 0x42401BD0]
set res_cause2 [safe_r32 0x42401BD4]
set res_cause_ext [safe_r32 0x42401C04]

puts [format "  SRSS_PWR_CTL          = %s" [fmt_hex32 $pwr_ctl]]
if {$pwr_ctl >= 0} {
    set dbg  [expr {($pwr_ctl >> 4) & 1}]
    set lpm  [expr {($pwr_ctl >> 5) & 1}]
    puts [format "    DEBUG_SESSION (bit4) = %d   <-- 1 => DS-RAM/DS-OFF silently downgraded to DeepSleep" $dbg]
    puts [format "    LPM_READY     (bit5) = %d" $lpm]
}
puts [format "  SRSS_PWR_CTL2         = %s" [fmt_hex32 $pwr_ctl2]]
if {$pwr_ctl2 >= 0} {
    set bgref_lp [expr {($pwr_ctl2 >> 28) & 1}]
    set frz_dpslp [expr {($pwr_ctl2 >> 30) & 1}]
    set frz_pd1   [expr {($pwr_ctl2 >> 31) & 1}]
    puts [format "    BGREF_LPMODE  (bit28) = %d" $bgref_lp]
    puts [format "    FREEZE_DPSLP  (bit30) = %d" $frz_dpslp]
    puts [format "    FREEZE_DPSLP_PD1 (b31)= %d" $frz_pd1]
}
puts [format "  SRSS_RES_CAUSE        = %s" [fmt_hex32 $res_cause]]
puts [format "  SRSS_RES_CAUSE2       = %s" [fmt_hex32 $res_cause2]]
puts [format "  SRSS_RES_CAUSE_EXTEND = %s" [fmt_hex32 $res_cause_ext]]

puts ""
puts "=== Warm-boot token (RTC->BREG_SET1) ==="
for {set i 0} {$i < 4} {incr i} {
    set v [safe_r32 [expr {0x42421010 + $i*4}]]
    set tag ""
    if {$i == 1 && $v == 0x16D5DA01} {
        set tag "  <-- WARM_BOOT_TOKEN_DS_RAM"
    }
    puts [format "  BREG_SET1\[%d\] @0x%08x = %s%s" $i [expr {0x42421010 + $i*4}] [fmt_hex32 $v] $tag]
}

puts ""
puts "=== PPU PWPR/PWSR (current power mode per domain) ==="
set PPU_BASES {
    MAIN     0x42411000
    SRAM0    0x42220000
    SRAM1    0x42221000
    SYSCPU   0x42225000
    PD1      0x42413000
    APPCPUSS 0x44100000
    APPCPU   0x44101000
    SOCMEM   0x44660000
    U55      0x44602000
}

puts [format "  %-9s %-10s   %-9s %-9s   verdict" "DOMAIN" "ADDR" "PWPR.POL" "PWSR.STA"]
puts [string repeat "-" 70]

foreach {name addr_str} $PPU_BASES {
    set addr  [expr {$addr_str + 0}]
    set pwpr  [safe_r32 [expr {$addr + 0x000}]]
    set pwsr  [safe_r32 [expr {$addr + 0x008}]]
    if {$pwpr < 0 || $pwsr < 0} {
        puts [format "  %-9s 0x%08x   <bus error - domain probably OFF>" $name $addr]
        continue
    }
    set policy [expr {$pwpr & 0xF}]
    set status [expr {$pwsr & 0xF}]
    set verdict ""
    if {$policy != $status} { set verdict "  <-- policy != status (transition or DENIED)" }
    puts [format "  %-9s 0x%08x   %-9s %-9s%s" \
          $name $addr [mode_name $policy] [mode_name $status] $verdict]
}

puts ""
puts "=== DS-RAM expected vs actual (PWSR.STA) ==="
puts "  For DEEPSLEEP_RAM the SRSS sequencer requires:"
puts "    MAIN=MEM_RET  SRAM0=MEM_RET  SRAM1=MEM_RET  SYSCPU=OFF"
puts "    PD1=MEM_RET   APPCPUSS=OFF   APPCPU=OFF     SOCMEM=MEM_RET"
puts "    U55=OFF"
puts "  If any row above shows PWSR.STA=ON/FULL_RET, DS-RAM was DOWNGRADED"
puts "  to plain CPU DeepSleep. The most common cause is SRSS_PWR_CTL.DEBUG_SESSION=1."

shutdown
