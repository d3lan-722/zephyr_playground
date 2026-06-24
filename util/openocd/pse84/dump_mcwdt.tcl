# Dump PSE84 MCWDT0 + CM33 NVIC state for the kernel-tick / wake IRQ.
#
# Non-intrusive: uses the system AP (cat1d.sys), so the CPU is not halted
# and a deep-sleep CPU doesn't block the read.  All reads are wrapped in
# catch {} because parts of the bus may be partially powered down.
#
# References (MXS22SRSS variant on PSE84, see cyip_srss_v1_1.h
# MCWDT_STRUCT_Type @ secure base 0x5240D000, size 0x40):
#   +0x00 RESERVED
#   +0x04 CNTLOW       CTR0[15:0] | CTR1[31:16]
#   +0x08 CNTHIGH      CTR2 32-bit
#   +0x0C MATCH        MATCH0[15:0] | MATCH1[31:16]
#   +0x10 CONFIG       per-counter mode/cascade/match
#   +0x14 CTL          ENABLE/ENABLED per counter (CTR0..CTR2)
#   +0x18 INTR         pending bits for CTR0..2 (W1C)
#   +0x1C INTR_SET
#   +0x20 INTR_MASK    1 = enable IRQ propagation to NVIC
#   +0x24 INTR_MASKED
#   +0x28 LOCK         2 bits per counter (write-protect)
#   +0x2C LOWER_LIMIT
#
# CM33 NVIC (private to the core, only reachable via cat1d.cm33 -- the
# system AP cannot read 0xE000Exxx):
#   ISER0/1/2 @ 0xE000E100/+04/+08
#   ISPR0/1/2 @ 0xE000E200/+04/+08
# The MCWDT0 IRQ on PSE84 is 55 (ISER1 bit 23, ISPR1 bit 23).
# NVIC reads are attempted but expected to fail unless the CM33 AP is
# examined; failures are reported and the script continues.

set MCWDT0_BASE        0x5240D000
set MCWDT0_CNTLOW      [expr {$MCWDT0_BASE + 0x04}]
set MCWDT0_CNTHIGH     [expr {$MCWDT0_BASE + 0x08}]
set MCWDT0_MATCH       [expr {$MCWDT0_BASE + 0x0C}]
set MCWDT0_CONFIG      [expr {$MCWDT0_BASE + 0x10}]
set MCWDT0_CTL         [expr {$MCWDT0_BASE + 0x14}]
set MCWDT0_INTR        [expr {$MCWDT0_BASE + 0x18}]
set MCWDT0_INTR_SET    [expr {$MCWDT0_BASE + 0x1C}]
set MCWDT0_INTR_MASK   [expr {$MCWDT0_BASE + 0x20}]
set MCWDT0_INTR_MASKED [expr {$MCWDT0_BASE + 0x24}]
set MCWDT0_LOCK        [expr {$MCWDT0_BASE + 0x28}]

set NVIC_ISER0 0xE000E100
set NVIC_ISER1 0xE000E104
set NVIC_ISER2 0xE000E108
set NVIC_ISPR0 0xE000E200
set NVIC_ISPR1 0xE000E204
set NVIC_ISPR2 0xE000E208

set MCWDT0_IRQ 55

proc safe_r32 {addr} {
    if {[catch {read_memory $addr 32 1} v]} { return -1 }
    return $v
}

proc fmt_hex32 {v} {
    if {$v < 0} { return "<bus error>" }
    return [format "0x%08x" $v]
}

proc bit {v n} { return [expr {($v >> $n) & 1}] }

proc dump_mcwdt {} {
    global MCWDT0_CTL MCWDT0_CNTLOW MCWDT0_CNTHIGH MCWDT0_MATCH \
           MCWDT0_CONFIG MCWDT0_LOCK MCWDT0_INTR MCWDT0_INTR_MASK \
           MCWDT0_INTR_MASKED

    set ctl       [safe_r32 $MCWDT0_CTL]
    set cntlow    [safe_r32 $MCWDT0_CNTLOW]
    set cnthigh   [safe_r32 $MCWDT0_CNTHIGH]
    set match     [safe_r32 $MCWDT0_MATCH]
    set cfg       [safe_r32 $MCWDT0_CONFIG]
    set lock      [safe_r32 $MCWDT0_LOCK]
    set intr      [safe_r32 $MCWDT0_INTR]
    set mask      [safe_r32 $MCWDT0_INTR_MASK]
    set masked    [safe_r32 $MCWDT0_INTR_MASKED]

    puts "=== MCWDT0 @ 0x5240D000 ==="
    if {$ctl >= 0} {
        # CTL is byte-packed: each counter has ENABLE (bit n.0) and
        # ENABLED (bit n.1) at byte n.  Observed 0x00030303 = all three
        # counters enabled and active.
        puts [format "  CTL          = %s   CTR0:en=%d act=%d  CTR1:en=%d act=%d  CTR2:en=%d act=%d" \
                  [fmt_hex32 $ctl] \
                  [bit $ctl 0]  [bit $ctl 1] \
                  [bit $ctl 8]  [bit $ctl 9] \
                  [bit $ctl 16] [bit $ctl 17]]
    } else {
        puts [format "  CTL          = %s" [fmt_hex32 $ctl]]
    }
    if {$cntlow >= 0} {
        set ctr0 [expr {$cntlow & 0xFFFF}]
        set ctr1 [expr {($cntlow >> 16) & 0xFFFF}]
        puts [format "  CNTLOW       = %s   CTR0=0x%04x CTR1=0x%04x" \
                  [fmt_hex32 $cntlow] $ctr0 $ctr1]
    } else {
        puts [format "  CNTLOW       = %s" [fmt_hex32 $cntlow]]
    }
    puts [format "  CNTHIGH(CTR2)= %s" [fmt_hex32 $cnthigh]]
    if {$match >= 0} {
        set m0 [expr {$match & 0xFFFF}]
        set m1 [expr {($match >> 16) & 0xFFFF}]
        puts [format "  MATCH        = %s   MATCH0=0x%04x MATCH1=0x%04x" \
                  [fmt_hex32 $match] $m0 $m1]
    } else {
        puts [format "  MATCH        = %s" [fmt_hex32 $match]]
    }
    puts [format "  CONFIG       = %s" [fmt_hex32 $cfg]]
    if {$intr >= 0} {
        puts [format "  INTR         = %s   pending: CTR0=%d CTR1=%d CTR2=%d" \
                  [fmt_hex32 $intr] [bit $intr 0] [bit $intr 1] [bit $intr 2]]
    } else {
        puts [format "  INTR         = %s" [fmt_hex32 $intr]]
    }
    if {$mask >= 0} {
        puts [format "  INTR_MASK    = %s   enable:  CTR0=%d CTR1=%d CTR2=%d" \
                  [fmt_hex32 $mask] [bit $mask 0] [bit $mask 1] [bit $mask 2]]
    } else {
        puts [format "  INTR_MASK    = %s" [fmt_hex32 $mask]]
    }
    puts [format "  INTR_MASKED  = %s" [fmt_hex32 $masked]]
    puts [format "  LOCK         = %s   (2 bits/counter; non-zero = write-protected)" \
              [fmt_hex32 $lock]]
}

proc dump_nvic {} {
    global NVIC_ISER0 NVIC_ISER1 NVIC_ISER2 NVIC_ISPR0 NVIC_ISPR1 NVIC_ISPR2 \
           MCWDT0_IRQ

    puts ""
    puts "=== CM33 NVIC (read via core AP only; system AP cannot reach SCS) ==="

    set iser0 [safe_r32 $NVIC_ISER0]
    set iser1 [safe_r32 $NVIC_ISER1]
    set iser2 [safe_r32 $NVIC_ISER2]
    set ispr0 [safe_r32 $NVIC_ISPR0]
    set ispr1 [safe_r32 $NVIC_ISPR1]
    set ispr2 [safe_r32 $NVIC_ISPR2]

    puts [format "  ISER0 (IRQ  0..31)     = %s" [fmt_hex32 $iser0]]
    puts [format "  ISER1 (IRQ 32..63)     = %s" [fmt_hex32 $iser1]]
    puts [format "  ISER2 (IRQ 64..95)     = %s" [fmt_hex32 $iser2]]
    puts [format "  ISPR0 (pending  0..31) = %s" [fmt_hex32 $ispr0]]
    puts [format "  ISPR1 (pending 32..63) = %s" [fmt_hex32 $ispr1]]
    puts [format "  ISPR2 (pending 64..95) = %s" [fmt_hex32 $ispr2]]

    if {$iser1 >= 0} {
        set en [bit $iser1 [expr {$MCWDT0_IRQ - 32}]]
        set pe [expr {$ispr1 < 0 ? -1 : [bit $ispr1 [expr {$MCWDT0_IRQ - 32}]]}]
        puts [format "  -> MCWDT0 IRQ%d: enabled=%d pending=%d" \
                  $MCWDT0_IRQ $en $pe]
    }
}

# Two MCWDT snapshots a moment apart so we can see whether CTR0 advances.
catch { targets cat1d.sys }

puts "--- snapshot 1 ---"
dump_mcwdt
after 100
puts ""
puts "--- snapshot 2 (after 100 ms) ---"
dump_mcwdt
dump_nvic

shutdown
