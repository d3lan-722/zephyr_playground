# Dump PSE84 PPU state for all 9 power domains.
#
# Non-intrusive: uses the system AP (cat1d.sys) so the CPU is not halted
# and active deep sleep is not disturbed.  Each register read is wrapped
# in catch{}; a PPU sitting in OFF (e.g. PD1 children when PD1 is OFF)
# will return "<bus>" rather than crashing the script.
#
# Decoded fields:
#   PWPR  @+0x000  PWR_POLICY[3:0], DYNAMIC_EN[8], OP_POLICY[19:16],
#                  OP_DYN_EN[24]
#   PWSR  @+0x008  PWR_STATUS[3:0] (actual current mode)
#                  bit 8 = LAST_HARD_RESET_TYPE,
#                  bit 9 = PWR_DYN_STATUS  (1 = transition in progress)
#   STSR  @+0x018  Stored DEVDENY signals from the most recent denied
#                  static transition.  One bit per dynamic device the
#                  PPU manages.  Reserved/zero on P-Channel-only PPUs.
#   ISR   @+0x038  Interrupt status:
#                    bit  0 = STA_POLICY_TRN_IRQ   transition started
#                    bit  1 = STA_ACCEPT_IRQ       transition accepted
#                    bit  2 = STA_DENY_IRQ         transition DENIED
#                    bit  3 = EMU_ACCEPT_IRQ
#                    bit  4 = EMU_DENY_IRQ         emulated DENIED
#                    bit  6 = LOCKED_IRQ
#                    bit  7 = OTHER_IRQ
#                    bit  8 = PWR_ACTIVE_IRQ
#                    bits 9..15 = OP_*_ACTIVE_IRQ
#   AISR  @+0x03C  Additional interrupt status:
#                    bit  0 = DYN_POLICY_MIN_IRQ
#                    bit  1 = DYN_ACCEPT_IRQ
#                    bit  2 = DYN_DENY_IRQ         dynamic DENIED
#                    bits 4..15 = STA_*/DYN_* per-mode flags
#   PWCR  @+0x020  Q-channel handshake enables.  PWR_DEV_ACTIVE_EN[18:8],
#                  OP_DEV_ACTIVE_EN[31:24], DEV_REQ_EN[0],
#                  DEV_ACTIVE_DISABLE[7:0].  PWCR=0 disables every
#                  handshake bit and breaks the SRSS DS-RAM wake path
#                  (see pse84-pd1-ppu-pwcr-breaks-deepsleep-wake.md).
#
# A power domain can REFUSE a programmed transition.  The PPU
# communicates with domain components via Q-Channel/P-Channel low-power
# interfaces; a component asserts QDENY/DEVPDENY when it cannot enter
# the requested mode (e.g. busy, illegal mode, transition unsupported).
# Software must compare PWPR.PWR_POLICY (requested) with
# PWSR.PWR_STATUS (actual).  If the policy is denied:
#     * for static transitions, hardware reverts PWPR.PWR_POLICY back
#       to the current PWSR.PWR_STATUS value,
#     * ISR.STA_DENY_IRQ is set,
#     * STSR latches per-device denial bits (Q-Channel PPUs only),
#     * for dynamic transitions, AISR.DYN_DENY_IRQ is set,
#     * for emulated transitions, ISR.EMU_DENY_IRQ is set.
# This script flags every PWPR/PWSR mismatch and every set DENY bit.
#
# Power policy / status mode encoding (ppu_v1_mode):
#   0=OFF  1=OFF_EMU  2=MEM_RET  3=MEM_RET_EMU  4=LOGIC_RET
#   5=FULL_RET 6=MEM_OFF 7=FUNC_RET 8=ON 9=WARM_RST 10=DBG_RECOV
#
# Datasheet Table 2 (PPU configurations vs deep-sleep mode), expected
# programmed PWR_POLICY for each mode:
#                   DEEPSLEEP   DEEPSLEEP_RAM   DEEPSLEEP_OFF
#   MAIN              5 FULL     2 MEM_RET       0 OFF
#   SRAM0             5 FULL     2 MEM_RET       0 OFF
#   SRAM1             5 FULL     2 MEM_RET       0 OFF
#   SYSCPU            5 FULL     0 OFF           0 OFF
#   PD1               2 MEM_RET  2 MEM_RET       0 OFF
#   APPCPUSS          0 OFF      0 OFF           0 OFF
#   APPCPU            0 OFF      0 OFF           0 OFF
#   SOCMEM            5 FULL     2 MEM_RET       0 OFF
#   U55               0 OFF      0 OFF           0 OFF
#
# All entries above must match before the SRSS DS-RAM/DS-OFF state
# machine will execute the deeper power-down (Table 2 footnote 1);
# any mismatch silently demotes to plain DEEPSLEEP.

# NS-SBUS-alias base addresses.  PPC1 marks the App-domain PPUs as
# NS-only for PC=6, so the S-SBUS aliases (0x5xxxxxxx) silently return
# 0 (RAZ) when the cat1d.sys probe is not Secure-authenticated.  Using
# the NS-SBUS aliases (0x4xxxxxxx) gives real data through cat1d.sys.
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
        9  { return "WARM_RST" }
        10 { return "DBG_RECOV" }
        default { return [format "?%d" $m] }
    }
}

# Expected PWR_POLICY per Table 2, indexed by domain name.  Used to flag
# rows that would block the SRSS sequencer.
proc expected_policy {domain mode} {
    # mode: DEEPSLEEP | DEEPSLEEP_RAM | DEEPSLEEP_OFF
    array set t_DEEPSLEEP {
        MAIN 5  SRAM0 5  SRAM1 5  SYSCPU 5
        PD1  2  APPCPUSS 0 APPCPU 0
        SOCMEM 5 U55 0
    }
    array set t_DEEPSLEEP_RAM {
        MAIN 2  SRAM0 2  SRAM1 2  SYSCPU 0
        PD1  2  APPCPUSS 0 APPCPU 0
        SOCMEM 2 U55 0
    }
    array set t_DEEPSLEEP_OFF {
        MAIN 0  SRAM0 0  SRAM1 0  SYSCPU 0
        PD1  0  APPCPUSS 0 APPCPU 0
        SOCMEM 0 U55 0
    }
    return [set t_${mode}($domain)]
}

puts "PSE84 PPU dump (non-intrusive, system AP)"
puts ""
puts [format "%-9s %-10s %-10s %-3s %-3s %-3s %-9s %-9s %-10s   %-7s %-7s %-7s" \
      "DOMAIN" "ADDR" "PWPR" "DYN" "OP" "ODE" "POLICY" "STATUS" "PWCR" \
      "exp.DS" "exp.RAM" "exp.OFF"]
puts [string repeat "-" 110]

foreach {name addr_str} $PPU_BASES {
    set addr  [expr {$addr_str + 0}]
    set pwpr  [safe_r32 [expr {$addr + 0x000}]]
    set pwsr  [safe_r32 [expr {$addr + 0x008}]]
    set pwcr  [safe_r32 [expr {$addr + 0x020}]]

    if {$pwpr < 0} {
        puts [format "%-9s %-10s <bus error - domain probably OFF>" \
              $name [format "0x%08x" $addr]]
        continue
    }

    set policy  [expr {  $pwpr        & 0xF}]
    set dyn_en  [expr { ($pwpr >> 8)  & 0x1}]
    set op_pol  [expr { ($pwpr >> 16) & 0xF}]
    set op_dyn  [expr { ($pwpr >> 24) & 0x1}]

    if {$pwsr < 0} {
        set status_str "<bus>"
    } else {
        set status     [expr {$pwsr & 0xF}]
        set status_str [format "%d %s" $status [mode_name $status]]
    }

    if {$pwcr < 0} {
        set pwcr_str "<bus>"
    } else {
        set pwcr_str [format "0x%08x" $pwcr]
    }

    set exp_ds   [expected_policy $name DEEPSLEEP]
    set exp_ram  [expected_policy $name DEEPSLEEP_RAM]
    set exp_off  [expected_policy $name DEEPSLEEP_OFF]

    set policy_str [format "%d %s" $policy [mode_name $policy]]

    puts [format "%-9s 0x%08x %-10s %-3d %-3d %-3d %-9s %-9s %-10s   %-7s %-7s %-7s" \
          $name $addr [format "0x%08x" $pwpr] \
          $dyn_en $op_pol $op_dyn \
          $policy_str $status_str $pwcr_str \
          [mode_name $exp_ds] [mode_name $exp_ram] [mode_name $exp_off]]
}

puts ""
puts "Legend:"
puts "  DYN  = DYNAMIC_EN bit (Q-channel transitions enabled)"
puts "  OP   = OP_POLICY field (operating mode policy)"
puts "  ODE  = OP_DYN_EN bit"
puts "  POLICY = programmed PWR_POLICY (target mode for next DS entry)"
puts "  STATUS = current PWSR.PWR_STATUS (actual mode now)"
puts {  PWCR   = Q-channel handshake enables (PWR_DEV_ACTIVE_EN bits 8-18,}
puts {           OP_DEV_ACTIVE_EN bits 24-31, DEV_REQ_EN bit 0).  Must be}
puts {           non-zero - PWCR=0 breaks the SRSS DS-RAM wake path.}
puts "  exp.DS / RAM / OFF = required PWR_POLICY per Table 2"

# ---- Per-PPU denial / transition diagnostics -----------------------------
# For each PPU dump STSR (stored DEVDENY), ISR (static/emulated denial),
# AISR (dynamic denial) and PWSR.PWR_DYN_STATUS.  Then:
#   * mismatch flag if PWPR.PWR_POLICY != PWSR.PWR_STATUS
#   * call out STA_DENY / EMU_DENY / DYN_DENY if set
# Per Arm PPU spec: when a static transition is denied, hardware reverts
# PWPR.PWR_POLICY to PWSR.PWR_STATUS.  So a "no mismatch" line plus a
# set ISR.STA_DENY bit means the request was tried, denied, and the
# policy field has been silently rolled back -- i.e. the program-time
# value is gone; only ISR/STSR carries the evidence.
puts ""
puts "Per-PPU denial diagnostics (STSR / ISR / AISR / PWSR.DYN)"
puts [string repeat "-" 110]
puts [format "%-9s %-10s %-10s %-10s %-10s  %-3s  %-9s %-9s  %s" \
      "DOMAIN" "STSR" "ISR" "AISR" "PWSR" "DYN" "POLICY" "STATUS" "FLAGS"]

foreach {name addr_str} $PPU_BASES {
    set addr  [expr {$addr_str + 0}]
    set pwpr  [safe_r32 [expr {$addr + 0x000}]]
    set pwsr  [safe_r32 [expr {$addr + 0x008}]]
    set stsr  [safe_r32 [expr {$addr + 0x018}]]
    set isr   [safe_r32 [expr {$addr + 0x038}]]
    set aisr  [safe_r32 [expr {$addr + 0x03C}]]

    if {$pwpr < 0 || $pwsr < 0} {
        puts [format "%-9s <bus error - PPU domain unreachable>" $name]
        continue
    }

    set policy     [expr {  $pwpr        & 0xF}]
    set status     [expr {  $pwsr        & 0xF}]
    set dyn_status [expr { ($pwsr >> 9)  & 0x1}]

    set flags [list]
    if {$policy != $status} {
        lappend flags [format "MISMATCH(req=%d %s != cur=%d %s)" \
            $policy [mode_name $policy] $status [mode_name $status]]
    }
    if {$isr >= 0} {
        if {$isr & 0x4} { lappend flags "STA_DENY" }
        if {$isr & 0x10} { lappend flags "EMU_DENY" }
        if {$isr & 0x40} { lappend flags "LOCKED" }
    }
    if {$aisr >= 0} {
        if {$aisr & 0x4} { lappend flags "DYN_DENY" }
    }
    if {$dyn_status} { lappend flags "TRANSITION_IN_PROGRESS" }
    if {$stsr > 0}   { lappend flags [format "STSR=0x%08x(devs denied)" $stsr] }
    if {[llength $flags] == 0} { lappend flags "ok" }

    puts [format "%-9s %-10s %-10s %-10s %-10s  %-3d  %-9s %-9s  %s" \
        $name \
        [fmt_hex32 $stsr] \
        [fmt_hex32 $isr] \
        [fmt_hex32 $aisr] \
        [fmt_hex32 $pwsr] \
        $dyn_status \
        [format "%d %s" $policy [mode_name $policy]] \
        [format "%d %s" $status [mode_name $status]] \
        [join $flags ", "]]
}

# ---- Table 2 conformance summary -----------------------------------------
# For each candidate target mode (DEEPSLEEP, DEEPSLEEP_RAM, DEEPSLEEP_OFF),
# count how many PPUs have PWSR.PWR_STATUS matching the Table 2 expected
# value.  This is the pass/fail check the user actually cares about: did
# the chip really reach the programmed system state, or did one or more
# domains silently refuse?
puts ""
puts "Table 2 conformance: PWSR.PWR_STATUS vs expected per system state"
puts [string repeat "-" 110]
puts [format "%-9s   %-12s   %-12s   %-12s" \
      "DOMAIN" "now" "vs DS" "vs DS-RAM   vs DS-OFF"]

set ds_match  0
set ds_total  0
set ram_match 0
set ram_total 0
set off_match 0
set off_total 0

foreach {name addr_str} $PPU_BASES {
    set addr [expr {$addr_str + 0}]
    set pwsr [safe_r32 [expr {$addr + 0x008}]]
    if {$pwsr < 0} {
        # Unreachable PPU usually means its parent domain is OFF, which
        # for DS-OFF is the *expected* result.  Count as match for OFF.
        incr off_total; incr off_match
        puts [format "%-9s   <bus error>    n/a            n/a            UNREACHABLE (counted as OFF)" $name]
        continue
    }
    set status  [expr {$pwsr & 0xF}]
    set exp_ds  [expected_policy $name DEEPSLEEP]
    set exp_ram [expected_policy $name DEEPSLEEP_RAM]
    set exp_off [expected_policy $name DEEPSLEEP_OFF]

    incr ds_total
    incr ram_total
    incr off_total
    if {$status == $exp_ds}  { incr ds_match;  set m_ds  "ok" } else { set m_ds  [format "MISS(want %s)" [mode_name $exp_ds]] }
    if {$status == $exp_ram} { incr ram_match; set m_ram "ok" } else { set m_ram [format "MISS(want %s)" [mode_name $exp_ram]] }
    if {$status == $exp_off} { incr off_match; set m_off "ok" } else { set m_off [format "MISS(want %s)" [mode_name $exp_off]] }

    puts [format "%-9s   %-12s   %-12s   %-12s   %s" \
        $name [mode_name $status] $m_ds $m_ram $m_off]
}

puts [string repeat "-" 110]
puts [format "TOTALS:                        DS %d/%d   DS-RAM %d/%d   DS-OFF %d/%d" \
      $ds_match $ds_total $ram_match $ram_total $off_match $off_total]
puts ""
if {$off_match == $off_total} {
    puts "  -> All PPUs match Table 2 DEEPSLEEP_OFF expected state."
} elseif {$ram_match == $ram_total} {
    puts "  -> All PPUs match Table 2 DEEPSLEEP_RAM expected state."
} elseif {$ds_match == $ds_total} {
    puts "  -> All PPUs match Table 2 DEEPSLEEP (plain) expected state."
} else {
    puts "  -> No exact Table 2 match.  Chip is in an INTERMEDIATE state."
    puts "     Look for STA_DENY / DYN_DENY / EMU_DENY in the table above."
}

# ---- BREG instrumentation ------------------------------------------------
# RTC backup register region 1 is in the always-on backup domain and is
# reachable via the system AP even when SOCMEM/U55 are unreachable.  The
# app's force_app_domain_off() reads each programmed PPU mode back after
# the cy_pd_ppu_set_power_mode() writes and stashes the values here.
#
# SIDDSO Step-2 layout (apps/14_pse84_siddso_exact, m55/src/main.c):
#   BREG_SET1[0]  sentinel 0xC0FFEE55 (CM55 reached this code)
#   BREG_SET1[1]  APPCPUSS PWPR pre-PDL  (CM55-NS read)
#   BREG_SET1[2]  APPCPU   PWPR pre-PDL
#   BREG_SET1[3]  SOCMEM   PWPR pre-PDL  (control)
#   BREG_SET1[4]  PD1      PWPR pre-PDL
#   BREG_SET1[5]  APPCPUSS PWPR post-cy_pd_ppu_set_power_mode(APPCPUSS,OFF)
#   BREG_SET1[6]  APPCPU   PWPR post-cy_pd_ppu_set_power_mode(APPCPU,OFF)
#   BREG_SET1[7]  SOCMEM   PWPR post-cy_pd_ppu_set_power_mode(SOCMEM,OFF)
#   BREG_SET1[8]  PD1      PWPR post-cy_pd_ppu_set_power_mode(PD1,OFF)
#   BREG_SET1[9]  step bitmap (1=pre,2=APPCPU,4=APPCPUSS,8=SOCMEM,16=PD1,32=pause)
set RTC_BREG_SET1_BASE 0x42421010

set b0 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x00}]]
set b1 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x04}]]
set b2 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x08}]]
set b3 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x0C}]]
set b4 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x10}]]
set b5 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x14}]]
set b6 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x18}]]
set b7 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x1C}]]
set b8 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x20}]]
set b9 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x24}]]
set b10 [safe_r32 [expr {$RTC_BREG_SET1_BASE + 0x28}]]

puts ""
puts "BREG_SET1 readback (SIDDSO Step-2 CM55 self-test)"
puts [string repeat "-" 110]
puts [format {  [0] sentinel        = %s   %s} [fmt_hex32 $b0] \
      [expr {$b0 == 0xC0FFEE55 ? {(CM55 reached the BREG block - grant works)} : \
             {<-- CM55 NEVER WROTE THIS BREG (PPC0 grant missing OR CM55 never got past Step 4)}}]]
puts [format {  [9] step bitmap     = %s} [fmt_hex32 $b9]]
puts [format {  [10] last STAGE     = %s   %s} [fmt_hex32 $b10] \
      [switch -- $b10 {
          1 {format "after __enable_irq, BEFORE Cy_SysPm_SetDeepSleepMode"}
          2 {format "after SetDeepSleepMode, BEFORE PeriGroup0 deinit"}
          3 {format "after PeriGroup0 deinit, BEFORE PeriGroup1"}
          4 {format "after PeriGroup1 deinit, BEFORE PeriGroup2"}
          5 {format "after PeriGroup2 deinit, BEFORE HF disables"}
          6 {format "after HF disables, BEFORE pdcm_clear_dependency"}
          7 {format "after pdcm_clear_dependency, BEFORE PPU OFF block"}
          default {format "(unknown / not yet reached)"}
      }]]
if {$b9 >= 0 && $b9 != 0} {
    set steps {}
    if {$b9 & 1}  { lappend steps "pre-snapshot" }
    if {$b9 & 2}  { lappend steps "APPCPU OFF call done" }
    if {$b9 & 4}  { lappend steps "APPCPUSS OFF call done" }
    if {$b9 & 8}  { lappend steps "SOCMEM OFF call done" }
    if {$b9 & 16} { lappend steps "PD1 OFF call done" }
    if {$b9 & 32} { lappend steps "reached pause spin" }
    puts [format {      executed: %s} [join $steps ", "]]
}
puts ""
puts [format {  PRE  APPCPUSS PWPR  = %s} [fmt_hex32 $b1]]
puts [format {  PRE  APPCPU   PWPR  = %s} [fmt_hex32 $b2]]
puts [format {  PRE  SOCMEM   PWPR  = %s   (control)} [fmt_hex32 $b3]]
puts [format {  PRE  PD1      PWPR  = %s} [fmt_hex32 $b4]]
puts [format {  POST APPCPUSS PWPR  = %s} [fmt_hex32 $b5]]
puts [format {  POST APPCPU   PWPR  = %s} [fmt_hex32 $b6]]
puts [format {  POST SOCMEM   PWPR  = %s   (control)} [fmt_hex32 $b7]]
puts [format {  POST PD1      PWPR  = %s} [fmt_hex32 $b8]]

if {$b0 == 0xC0FFEE55 && $b9 >= 0 && [expr {$b9 & 0x1F}]} {
    puts ""
    puts "  CM55-side PWPR delta analysis:"
    foreach {name pre post} [list \
        APPCPUSS $b1 $b5 \
        APPCPU   $b2 $b6 \
        SOCMEM   $b3 $b7 \
        PD1      $b4 $b8] {
        if {$pre < 0 || $post < 0} { continue }
        set pre_pol  [expr {$pre  & 0xF}]
        set post_pol [expr {$post & 0xF}]
        if {$pre == $post} {
            set verdict "*** UNCHANGED *** PDL write to PWPR was DROPPED"
        } elseif {$post_pol == 0} {
            set verdict "ok: write landed, POLICY -> 0 OFF"
        } else {
            set verdict [format "write landed but POLICY -> %d %s (not OFF)" \
                                $post_pol [mode_name $post_pol]]
        }
        puts [format "    %-9s  %s -> %s   %s" $name [fmt_hex32 $pre] \
              [fmt_hex32 $post] $verdict]
    }
}

# ---- SRSS_PWR_CTL  (DEBUG_SESSION + LPM_READY) ----------------------------
# SRSS @ 0x52400000, PWR_CTL @ +0x1000.  Bit 4 = DEBUG_SESSION (CDBGPWRUPREQ
# is asserted - this BLOCKS DS-RAM/DS-OFF entry and silently demotes to
# plain DEEPSLEEP).  Bit 5 = LPM_READY (SRSS power circuits are ready
# for low-power entry).
set SRSS_PWR_CTL_ADDR 0x42401000
set pwr_ctl [safe_r32 $SRSS_PWR_CTL_ADDR]

puts ""
puts "SRSS_PWR_CTL @ 0x42401000"
puts [string repeat "-" 110]
puts [format "  raw            = %s" [fmt_hex32 $pwr_ctl]]
if {$pwr_ctl >= 0} {
    set dbg_session [expr {($pwr_ctl >> 4) & 0x1}]
    set lpm_ready   [expr {($pwr_ctl >> 5) & 0x1}]
    puts [format "  DEBUG_SESSION  = %d   %s" $dbg_session \
          [expr {$dbg_session ? {<-- DEBUGGER ACTIVE - DS-RAM/DS-OFF BLOCKED} : {(no debug session)}}]]
    puts [format "  LPM_READY      = %d   %s" $lpm_ready \
          [expr {$lpm_ready ? {(SRSS power circuits ready)} : {<-- LPM NOT READY - DS entry will fail}}]]
}

# Read SRSS_PWR_HIBERNATE LIVE - shows if token=0xFB was written by PDL
set SRSS_PWR_HIBERNATE_ADDR 0x42401014
set pwr_hib_live [safe_r32 $SRSS_PWR_HIBERNATE_ADDR]
puts ""
puts [format "  SRSS_PWR_HIBERNATE (live) = %s" [fmt_hex32 $pwr_hib_live]]
if {$pwr_hib_live >= 0} {
    set token_live [expr {$pwr_hib_live & 0xFF}]
    puts [format "    TOKEN (live) = 0x%02X" $token_live]
    if {$token_live == 0xFB} {
        puts "      -> PDL wrote DS_OFF token before WFI (DS-OFF was attempted)"
    } elseif {$token_live == 0xF8} {
        puts "      -> HIBERNATE token (HIB was attempted)"
    } else {
        puts "      -> No DS-OFF token written - PDL took plain DS path"
    }
}

puts ""
puts "DS-RAM mismatch check: any 'POLICY' that doesn't match 'exp.RAM'"
puts "blocks the SRSS DS-RAM sequencer and forces silent demote to plain"
puts "DEEPSLEEP.  Also DEBUG_SESSION=1 has the same effect."

# ---- BREG_SET2[0]  ULP-entry diagnostic ----------------------------------
# Layout: [31:24]=0xAA, [23:16]=entry rc, [15:8]=pm_status>>8 byte
#         (bit 11 = SYSTEM_ULP), [7:0]=pm_status low byte
set RTC_BREG_SET2_BASE 0x42421020
set b2_0 [safe_r32 $RTC_BREG_SET2_BASE]
set b2_1 [safe_r32 [expr {$RTC_BREG_SET2_BASE + 0x04}]]
set b2_2 [safe_r32 [expr {$RTC_BREG_SET2_BASE + 0x08}]]
set b2_3 [safe_r32 [expr {$RTC_BREG_SET2_BASE + 0x0C}]]
set b2_4 [safe_r32 [expr {$RTC_BREG_SET2_BASE + 0x10}]]
set b2_5 [safe_r32 [expr {$RTC_BREG_SET2_BASE + 0x14}]]
puts ""
puts "BREG_SET2 readback (enter_system_ulp_profile instrumentation)"
puts [string repeat "-" 110]
puts [format {  [0] = %s} [fmt_hex32 $b2_0]]
puts [format {  [1] = %s   (raw Cy_SysPm_ReadStatus)} [fmt_hex32 $b2_1]]
puts [format {  [2] = %s   (last pm_state_set: 0xCN_xxxxxx)} [fmt_hex32 $b2_2]]
puts [format {  [3] = %s   (SRSS_RES_CAUSE captured at boot)} [fmt_hex32 $b2_3]]
puts [format {  [4] = %s   (SRSS_PWR_HIBERNATE captured at boot - low byte = TOKEN)} [fmt_hex32 $b2_4]]
puts [format {  [5] = %s   (DS-OFF wakeup count - only ++ when token == 0xFB)} [fmt_hex32 $b2_5]]
if {$b2_0 >= 0 && (($b2_0 >> 24) & 0xFF) == 0xAA} {
    set entry_rc [expr { $b2_0 & 0xFF}]
    set is_lp    [expr {  $b2_1                          & 0x00000080}]
    set is_ulp   [expr {  $b2_1                          & 0x00000800}]
    set is_hp    [expr {  $b2_1                          & 0x08000000}]
    set cm33_act [expr {  $b2_1                          & 0x00010000}]
    set cm33_slp [expr {  $b2_1                          & 0x00020000}]
    set cm33_dsp [expr {  $b2_1                          & 0x00040000}]
    puts ""
    puts [format "  Cy_SysPm_SystemEnterUlp() rc = %d %s" $entry_rc \
          [expr {$entry_rc == 0 ? {(SUCCESS)} : {<-- FAIL}}]]
    puts [format "  SYSTEM_LP    = %d" [expr {$is_lp ? 1 : 0}]]
    puts [format "  SYSTEM_ULP   = %d %s" [expr {$is_ulp ? 1 : 0}] \
          [expr {$is_ulp ? {(0.7 V Vccd - good)} : {<-- NOT IN ULP}}]]
    puts [format "  SYSTEM_HP    = %d" [expr {$is_hp ? 1 : 0}]]
    puts [format "  CM33_ACTIVE  = %d" [expr {$cm33_act ? 1 : 0}]]
    puts [format "  CM33_SLEEP   = %d" [expr {$cm33_slp ? 1 : 0}]]
    puts [format "  CM33_DSLEEP  = %d" [expr {$cm33_dsp ? 1 : 0}]]
} else {
    puts ""
    puts "  (0xAA sentinel not present - enter_system_ulp_profile didn't run)"
}

if {$b2_2 >= 0 && (($b2_2 >> 28) & 0xF) == 0xC} {
    set state_code [expr {($b2_2 >> 24) & 0xF}]
    set count      [expr {  $b2_2        & 0x00FFFFFF}]
    set names [list "?" "CPU_SLEEP" "SYSTEM_DEEP_SLEEP" "CPU_DEEP_SLEEP" \
                    "SUSPEND_TO_RAM" "SOFT_OFF"]
    set name [expr {$state_code < [llength $names] ? \
                    [lindex $names $state_code] : "?"}]
    puts ""
    puts [format "  Last PM state entered: %s  (count=%d)" $name $count]
} else {
    puts ""
    puts "  (no PM state has been entered yet - residency policy never fired)"
}

# Decode reset reason at last boot
if {$b2_4 >= 0} {
    set token [expr {$b2_4 & 0xFF}]
    puts ""
    puts [format "  PWR_HIBERNATE.TOKEN = 0x%02X" $token]
    if {$token == 0xFB} {
        puts "    -> Last reset was DS-OFF WAKEUP (DS-OFF really worked)"
    } elseif {$token == 0xF8} {
        puts "    -> Last reset was HIBERNATE WAKEUP"
    } else {
        puts "    -> Last reset was POR / XRES / WDT / fault (NOT a DS-OFF wake)"
    }
}
if {$b2_5 >= 0} {
    puts [format "  DS-OFF wakeup counter = %d" $b2_5]
    if {$b2_5 == 0} {
        puts "    -> NEVER successfully entered DS-OFF (always silent-demoted to plain DEEPSLEEP)"
    } else {
        puts "    -> Chip HAS been doing DS-OFF cycles successfully"
    }
}

# =========================================================================
# PPC1 register-state dump (Step-1 instrumentation companion).
#
# Reads PPC1 NS_ATT and PC_MASK[r] for every region in the
# apps/14_pse84_siddso_exact PPC1 grant list, via system AP.
#
# Each slot must show NS=1 PC_MASK=0xFF after `cm33s_ppc_apply_phase2()`
# completes.  If NS=0 or PC_MASK!=0xFF, that region's CM55-NS access
# rights were not actually applied -> grant silently failed.
# =========================================================================
puts ""
puts "PPC1 grant-state dump (S-SBUS @ 0x54020000)"
puts "--------------------------------------------------------------------------------------------------------------"
set ppc1_base 0x54020000
set ppc1_pcmask [expr {$ppc1_base + 0x1000}]
set ppc1_nsatt  [expr {$ppc1_base + 0x2000}]
set ppc1_nspatt [expr {$ppc1_base + 0x4000}]

# slot list from m33_s/src/cm33s_ppc_regions/ppc1_led_demo.inc
set ppc1_slots {
    {0x000 PROT_PERI1_MAIN}
    {0x00F PROT_PERI1_M55APPCPUSS}
    {0x010 PROT_PERI1_MXCM55_CM55}
    {0x012 PROT_PERI1_MXCM55_CM55_NS}
    {0x01A PROT_PERI1_APPCPUSS_ALL_PC}
    {0x01B PROT_PERI1_APPCPUSS_CM33_NS}
    {0x022 PROT_PERI1_MS_CTL_MS19_MAIN}
    {0x02B PROT_PERI1_MS_CTL_MS_PC19_PRIV}
    {0x033 PROT_PERI1_MS_CTL_MS_PC19_PRIV_MIR}
    {0x002 PROT_PERI1_GR1_GROUP}
    {0x099 PROT_PERI1_SOCMEM_PPU_SOCMEM_PPU}
    {0x0A2 PROT_PERI1_ITCM}
    {0x0A3 PROT_PERI1_DTCM}
}

puts [format "%-6s %-36s %-12s %-3s %-5s %-5s %s" "SLOT" "NAME" "PC_MASK" "NS" "NS_P" "S_P" "VERDICT"]
foreach entry $ppc1_slots {
    set slot [lindex $entry 0]
    set name [lindex $entry 1]
    set s    [expr {$slot}]
    set word [expr {$s >> 5}]
    set bit  [expr {$s & 0x1f}]
    set pcm  [safe_r32 [expr {$ppc1_pcmask + $s * 4}]]
    set nsw  [safe_r32 [expr {$ppc1_nsatt  + $word * 4}]]
    set nspw [safe_r32 [expr {$ppc1_nspatt + $word * 4}]]
    set spw  [safe_r32 [expr {$ppc1_base + 0x2400 + $word * 4}]]
    if {$pcm < 0 || $nsw < 0} {
        puts [format "0x%03x %-36s <bus>" $s $name]
        continue
    }
    set ns   [expr {($nsw  >> $bit) & 1}]
    set nsp  [expr {($nspw >> $bit) & 1}]
    set sp   [expr {($spw  >> $bit) & 1}]
    set verdict "ok"
    # PPC v2_1 PC_MASK width is 8 (PC=0..7).  PC=0 is reserved and
    # always reads 0; canonical "open to all PCs" is therefore 0xFE.
    # The PDL writes 0xFF but the hardware retains only 0xFE.
    if {$ns != 1}                              { set verdict "FAIL: NS bit not set" }
    if {[expr {$pcm & 0xFE}] != 0xFE}          { set verdict "FAIL: PC_MASK missing PC1..7" }
    puts [format "0x%03x %-36s 0x%08x   %d   %d     %d     %s" \
        $s $name $pcm $ns $nsp $sp $verdict]
}

puts ""
puts "PPC1 word dump (only words with NS!=0):"
for {set w 0} {$w < 32} {incr w} {
    set nsw  [safe_r32 [expr {$ppc1_nsatt  + $w * 4}]]
    set nspw [safe_r32 [expr {$ppc1_nspatt + $w * 4}]]
    set spw  [safe_r32 [expr {$ppc1_base + 0x2400 + $w * 4}]]
    if {$nsw == 0 && $nspw == 0} { continue }
    puts [format "  word\[%2d\] (regions 0x%03x..0x%03x): NS=0x%08x NS_P=0x%08x S_P=0x%08x" \
        $w [expr {$w * 32}] [expr {$w * 32 + 31}] $nsw $nspw $spw]
}

shutdown
