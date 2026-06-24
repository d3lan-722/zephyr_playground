# Dump PSE84 configuration registers relevant to CM55 boot from RRAM.
# Assumes the board is already running the reference project (CM55 active,
# red LED blinking). We halt CM33 non-destructively to read regs, then resume.

proc d32 {name addr} {
    if {[catch {read_memory $addr 32 1} v]} {
        puts [format "  %-40s @0x%08x = <bus error>" $name $addr]
    } else {
        puts [format "  %-40s @0x%08x = 0x%08x" $name $addr $v]
    }
}

proc hdr {title} {
    puts ""
    puts "=== $title ==="
}

proc dump_ppc {name base} {
    hdr "$name @ [format 0x%08x $base]"
    d32 "${name}_CTL"            [expr {$base + 0x0}]
    d32 "${name}_ATT[0]"         [expr {$base + 0x40}]
    d32 "${name}_ATT[1]"         [expr {$base + 0x44}]
    d32 "${name}_ATT[2]"         [expr {$base + 0x48}]
    d32 "${name}_ATT[3]"         [expr {$base + 0x4C}]
    d32 "${name}_ATT[4]"         [expr {$base + 0x50}]
    d32 "${name}_ATT[5]"         [expr {$base + 0x54}]
    d32 "${name}_ATT[6]"         [expr {$base + 0x58}]
    d32 "${name}_ATT[7]"         [expr {$base + 0x5C}]
    # PC_MASK array starts 0x1000 offset, 32 entries (first handful only)
    for {set i 0} {$i < 8} {incr i} {
        d32 "${name}_PC_MASK\[$i\]" [expr {$base + 0x1000 + $i*4}]
    }
}

proc dump_mpc {name base} {
    hdr "$name @ [format 0x%08x $base]"
    d32 "${name}_CTL"            [expr {$base + 0x0}]
    d32 "${name}_BLK_MAX"        [expr {$base + 0x10}]
    d32 "${name}_BLK_CFG"        [expr {$base + 0x14}]
    d32 "${name}_BLK_IDX"        [expr {$base + 0x18}]
    d32 "${name}_BLK_LUT"        [expr {$base + 0x1C}]
    d32 "${name}_ROT_CTL"        [expr {$base + 0x100}]
    d32 "${name}_ROT_BLK_MAX"    [expr {$base + 0x110}]
    d32 "${name}_ROT_BLK_CFG"    [expr {$base + 0x114}]
    d32 "${name}_ROT_BLK_IDX"    [expr {$base + 0x118}]
    d32 "${name}_ROT_BLK_PC"     [expr {$base + 0x11C}]
    d32 "${name}_ROT_BLK_LUT"    [expr {$base + 0x120}]
}

init
# Use the system AP (independent of CPU power domains) for non-intrusive
# reads. Does not halt cores, so deep sleep won't block us.
targets cat1d.sys

hdr "CM55 core regs (MXCM55 @ 0x542A0000)"
d32 "CM55_CTL"                   0x542A0000
d32 "CM55_CMD"                   0x542A0004
d32 "CM55_STATUS"                0x542A0008
d32 "CM55_S_VECTOR_TABLE_BASE"   0x542A1000
d32 "CM55_NS_VECTOR_TABLE_BASE"  0x542A1004
d32 "CM55_EVENT_CTL"             0x542A1008
d32 "CM55_CPU_PWR_CTL"           0x542A100C

hdr "APPCPU / APPCPUSS power"
# APPCPU PPU base (SVD: APPCPUSS @ 0x42040000, PPU offset TBD) — cover a range
for {set off 0} {$off < 0x100} {incr off 4} {
    d32 [format "APPCPU_PPU+0x%03x" $off] [expr {0x44080000 + $off}]
}

hdr "Bus master PCs MS[19..22] @ 0x541C4000/0x541C5000"
for {set ms 19} {$ms <= 22} {incr ms} {
    d32 [format "MS\[%d\].CTL" $ms] [expr {0x541C4000 + $ms*0x10}]
    d32 [format "MS\[%d\].PC"  $ms] [expr {0x541C5000 + $ms*0x10}]
}

# Protection PPCs
dump_ppc "PPC0" 0x50420000
dump_ppc "PPC1" 0x50460000

# RRAM MPC blocks
dump_mpc "RRAMC0_MPC0" 0x52211000
dump_mpc "RRAMC0_MPC1" 0x52212000

# CM55 TCM MPC
dump_mpc "MXCM55_ITCM_MPC" 0x542A2000
dump_mpc "MXCM55_DTCM_MPC" 0x542A3000

# Fault structures
hdr "FAULT_STRUCT0 @ 0x522B0000"
d32 "FS0.CTL"        0x522B0000
d32 "FS0.STATUS"     0x522B000C
d32 "FS0.DATA[0]"    0x522B0010
d32 "FS0.DATA[1]"    0x522B0014
d32 "FS0.DATA[2]"    0x522B0018
d32 "FS0.DATA[3]"    0x522B001C
d32 "FS0.MASK0"      0x522B0040
d32 "FS0.MASK1"      0x522B0044
d32 "FS0.MASK2"      0x522B0048
d32 "FS0.PENDING0"   0x522B0050
d32 "FS0.PENDING1"   0x522B0054
d32 "FS0.PENDING2"   0x522B0058

hdr "FAULT_STRUCT1 @ 0x522B0100"
d32 "FS1.CTL"        0x522B0100
d32 "FS1.STATUS"     0x522B010C
d32 "FS1.DATA[0]"    0x522B0110
d32 "FS1.DATA[1]"    0x522B0114
d32 "FS1.DATA[2]"    0x522B0118
d32 "FS1.DATA[3]"    0x522B011C
d32 "FS1.MASK0"      0x522B0140
d32 "FS1.MASK1"      0x522B0144
d32 "FS1.MASK2"      0x522B0148

# ITCM content at the default Secure vector base — this is critical:
# if the reference leaves real code here, H5 framing was wrong.
hdr "ITCM first 16 words via different aliases"
puts "  (read each alias; note any bus error)"
foreach {nm base} {ITCM_S_SBUS 0x58000000 ITCM_NS_SBUS 0x48000000 ITCM_S_CBUS 0x78000000 ITCM_NS_CBUS 0x68000000 ROM_M0_S 0x10000000 ROM_M0_NS 0x00000000} {
    hdr "$nm @ $base"
    for {set i 0} {$i < 16} {incr i} {
        d32 [format "%s\[%02d\]" $nm $i] [expr {$base + $i*4}]
    }
}

# RRAM NS boot image first words — sanity
hdr "RRAM boot image"
d32 "RRAM_NS@0x22030000"        0x22030000
d32 "RRAM_NS@0x22030004"        0x22030004
d32 "RRAM_NS@0x22030008"        0x22030008
d32 "RRAM_NS@0x2203000C"        0x2203000C

shutdown
