# Reset the board, let it run briefly, halt CM33 right after it configures
# PPC/MPC but (hopefully) before it enters deep sleep. Then dump regs.

proc d32 {name addr} {
    if {[catch {read_memory $addr 32 1} v]} {
        puts [format "  %-40s @0x%08x = <bus error>" $name $addr]
    } else {
        puts [format "  %-40s @0x%08x = 0x%08x" $name $addr $v]
    }
}

proc hdr {title} { puts ""; puts "=== $title ===" }

proc dump_ppc {name base} {
    hdr "$name @ [format 0x%08x $base]"
    d32 "${name}_CTL"            [expr {$base + 0x0}]
    for {set i 0} {$i < 8} {incr i} {
        d32 "${name}_ATT\[$i\]"  [expr {$base + 0x40 + $i*4}]
    }
    for {set i 0} {$i < 16} {incr i} {
        d32 "${name}_PC_MASK\[$i\]" [expr {$base + 0x1000 + $i*4}]
    }
}

proc dump_mpc {name base} {
    hdr "$name @ [format 0x%08x $base]"
    d32 "${name}_CTL"          [expr {$base + 0x0}]
    d32 "${name}_BLK_MAX"      [expr {$base + 0x10}]
    d32 "${name}_BLK_CFG"      [expr {$base + 0x14}]
    d32 "${name}_BLK_IDX"      [expr {$base + 0x18}]
    d32 "${name}_BLK_LUT"      [expr {$base + 0x1C}]
    d32 "${name}_ROT_CTL"      [expr {$base + 0x100}]
    d32 "${name}_ROT_BLK_MAX"  [expr {$base + 0x110}]
    d32 "${name}_ROT_BLK_CFG"  [expr {$base + 0x114}]
    d32 "${name}_ROT_BLK_IDX"  [expr {$base + 0x118}]
    d32 "${name}_ROT_BLK_PC"   [expr {$base + 0x11C}]
    d32 "${name}_ROT_BLK_LUT"  [expr {$base + 0x120}]
}

# Acquire + reset halt on CM33. Stay halted — do NOT resume. The CM33
# reset vector runs CY_PDL init, which programs PPC/MPC and *then*
# enters sleep. If we halt at reset, we capture BEFORE config, which
# is useless. Instead: resume, wait 50ms, halt again. Many power demos
# go to sleep ~100ms in.

init
reset init
targets cat1d.cm33
# Stay halted. CM33 is at reset vector — cores are awake, debug AP is
# fully responsive. We are BEFORE app code runs, so this captures the
# state immediately after boot-ROM hand-off to application.

hdr "CM33 core state"
if {[catch {reg pc} pcstr]} { puts "pc unreadable: $pcstr" } else { puts "  $pcstr" }

hdr "CM55 core regs (MXCM55 NS @ 0x44160000)"
d32 "CM55_CTL"                   0x44160000
d32 "CM55_CMD"                   0x44160004
d32 "CM55_STATUS"                0x44160008
d32 "CM55_S_VECTOR_TABLE_BASE"   0x44161000
d32 "CM55_NS_VECTOR_TABLE_BASE"  0x44161004
d32 "CM55_EVENT_CTL"             0x44161008
d32 "CM55_CPU_PWR_CTL"           0x4416100C

hdr "Bus master PCs MS 19..22"
for {set ms 19} {$ms <= 22} {incr ms} {
    d32 "MS\[$ms\].CTL" [expr {0x541C4000 + $ms*0x10}]
    d32 "MS\[$ms\].PC"  [expr {0x541C5000 + $ms*0x10}]
}

dump_ppc "PPC0" 0x42020000
dump_ppc "PPC1" 0x44020000

dump_mpc "RRAMC0_MPC0" 0x42211000
dump_mpc "RRAMC0_MPC1" 0x42212000
dump_mpc "MXCM55_ITCM_MPC" 0x44162000
dump_mpc "MXCM55_DTCM_MPC" 0x44163000

hdr "FAULT_STRUCT0/1"
d32 "FS0.CTL"        0x422B0000
d32 "FS0.STATUS"     0x422B000C
d32 "FS0.DATA[0]"    0x422B0010
d32 "FS0.DATA[1]"    0x422B0014
d32 "FS0.DATA[2]"    0x422B0018
d32 "FS0.DATA[3]"    0x422B001C
d32 "FS0.MASK0"      0x422B0040
d32 "FS0.MASK1"      0x422B0044
d32 "FS0.PENDING0"   0x422B0050
d32 "FS0.PENDING1"   0x422B0054
d32 "FS1.CTL"        0x422B0100
d32 "FS1.STATUS"     0x422B010C
d32 "FS1.DATA[0]"    0x422B0110
d32 "FS1.DATA[1]"    0x422B0114
d32 "FS1.DATA[2]"    0x422B0118
d32 "FS1.DATA[3]"    0x422B011C
d32 "FS1.MASK0"      0x422B0140
d32 "FS1.MASK1"      0x422B0144
d32 "FS1.PENDING0"   0x422B0150
d32 "FS1.PENDING1"   0x422B0154

hdr "RRAM image head"
d32 "RRAM_NS@0x22030000"   0x22030000
d32 "RRAM_NS@0x22030004"   0x22030004

shutdown
