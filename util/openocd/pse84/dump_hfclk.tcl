# Dump PSE84 HFCLK chains: source -> PLL -> CLK_PATH -> HFCLK.
#
# Non-intrusive: uses the system AP (cat1d.sys) so the CPU is not halted
# and deep sleep does not block reads.  All reads are wrapped in catch {}
# because parts of the SRSS bus may be partially powered down.
#
# References (PSE84 SVD, SRSS @ 0x42400000 / secure alias 0x52400000):
#   CLK_PATH_SELECT[k]   @ +0x1200  PATH_MUX[2:0]   selects PATH_k source
#   CLK_ROOT_SELECT[k]   @ +0x1240  ROOT_MUX[3:0],
#                                   ROOT_DIV[11:8],
#                                   ENABLE[31]      HFCLK_k mux+div+enable
#   CLK_DPLL_LP[i]       @ +0x1600 + i*0x20 (i = 0..1)
#       CONFIG  @+0x00: FEEDBACK_DIV[7:0], REFERENCE_DIV[12:8],
#                       OUTPUT_DIV[20:16], BYPASS_SEL[29:28], ENABLE[31]
#       CONFIG2 @+0x04: FRAC_DIV[23:0], FRAC_EN[31]
#       STATUS  @+0x1C: LOCKED[0]
#   CLK_DPLL_HP          @ +0x1800
#       CONFIG  @+0x00: NDIV_INT[7:0], PDIV[11:8], KDIV[15:12],
#                       BYPASS_SEL[29:28], ENABLE[31]
#       STATUS  @+0x1C: LOCKED[0]
#
# BYPASS_SEL semantics:
#   0 AUTO              - PLL output if locked, else PLL reference
#   1 LOCKED_OR_NOTHING - PLL output if locked, else 0
#   2 PLL_BYPASS        - always PLL reference
#   3 PLL_OUT           - always PLL output (even if unlocked)

set SRSS_BASE        0x52400000
set CLK_PATH_SELECT  [expr {$SRSS_BASE + 0x1200}]
set CLK_ROOT_SELECT  [expr {$SRSS_BASE + 0x1240}]
set CLK_DPLL_LP      [expr {$SRSS_BASE + 0x1600}]
set CLK_DPLL_HP      [expr {$SRSS_BASE + 0x1800}]
set NUM_HFCLK        14

# Assumed reference frequencies for PSE84 on-chip oscillators (Hz).
# EXTCLK / ECO / ALTHF are board-specific; left as 0 ("?").
set FREQ_IHO     50000000
set FREQ_IMO     8000000

proc safe_r32 {addr} {
    if {[catch {read_memory $addr 32 1} v]} { return -1 }
    return $v
}

proc fmt_hex32 {v} {
    if {$v < 0} { return "<bus error>" }
    return [format "0x%08x" $v]
}

proc fmt_freq {hz} {
    if {$hz <= 0} { return "?" }
    if {$hz >= 1000000} {
        return [format "%.3f MHz" [expr {$hz / 1000000.0}]]
    }
    if {$hz >= 1000} {
        return [format "%.3f kHz" [expr {$hz / 1000.0}]]
    }
    return [format "%d Hz" $hz]
}

# CLK_PATH_SELECT[k].PATH_MUX [2:0]
proc decode_path_src {sel} {
    switch -- $sel {
        0 { return "IHO" }
        1 { return "EXTCLK" }
        2 { return "ECO" }
        3 { return "IMO" }
        4 { return "ALTHF0" }
        5 { return "ALTHF1" }
        7 { return "DSI_MUX" }
        default { return [format "MUX=%d" $sel] }
    }
}

proc path_src_freq {sel} {
    global FREQ_IHO FREQ_IMO
    switch -- $sel {
        0 { return $FREQ_IHO }
        3 { return $FREQ_IMO }
        default { return 0 }
    }
}

# Standard PSE84 mapping: PATH0=DPLL_LP0, PATH1=DPLL_LP1, PATH2=DPLL_HP.
# All other paths feed the source through directly (no PLL).
proc path_pll {path} {
    switch -- $path {
        0 { return "DPLL_LP0" }
        1 { return "DPLL_LP1" }
        2 { return "DPLL_HP" }
        default { return "" }
    }
}

proc decode_bypass {bs} {
    switch -- $bs {
        0 { return "AUTO" }
        1 { return "LOCKED_OR_NOTHING" }
        2 { return "PLL_BYPASS" }
        3 { return "PLL_OUT" }
        default { return "?" }
    }
}

# ---- PLL readers ---------------------------------------------------------
# Each returns a dict-like list of named fields:
#   present  - 1 if the registers were readable
#   en       - ENABLE bit
#   locked   - STATUS.LOCKED bit
#   bypass   - BYPASS_SEL field (0..3)
#   fmult    - PLL multiplied output frequency (0 when not computable)
#   cfg_raw  - raw CONFIG register
#   desc     - short human description of the divider configuration

proc read_dpll_lp {idx ref_hz} {
    global CLK_DPLL_LP
    set base [expr {$CLK_DPLL_LP + $idx * 0x20}]
    set cfg  [safe_r32 [expr {$base + 0x00}]]
    set cfg2 [safe_r32 [expr {$base + 0x04}]]
    set st   [safe_r32 [expr {$base + 0x1C}]]
    if {$cfg < 0 || $cfg2 < 0 || $st < 0} {
        return [dict create present 0]
    }
    set en        [expr {($cfg >> 31) & 0x1}]
    set bypass    [expr {($cfg >> 28) & 0x3}]
    set out_div   [expr {($cfg >> 16) & 0x1F}]
    set ref_div   [expr {($cfg >> 8)  & 0x1F}]
    set fb_div    [expr { $cfg        & 0xFF}]
    set frac_en   [expr {($cfg2 >> 31) & 0x1}]
    set frac_div  [expr { $cfg2 & 0xFFFFFF}]
    set locked    [expr {  $st  & 0x1}]

    set fmult 0
    if {$ref_hz > 0 && $ref_div > 0 && $out_div > 0 && $fb_div > 0} {
        set fb_eff [expr {$fb_div + ($frac_en ? ($frac_div / 16777216.0) : 0.0)}]
        set fmult  [expr {int($ref_hz * $fb_eff / ($ref_div * $out_div))}]
    }
    set desc [format "FB=%d REF=%d OUT=%d" $fb_div $ref_div $out_div]
    return [dict create present 1 en $en locked $locked bypass $bypass \
                       fmult $fmult cfg_raw $cfg desc $desc]
}

proc read_dpll_hp {ref_hz} {
    global CLK_DPLL_HP
    set cfg [safe_r32 [expr {$CLK_DPLL_HP + 0x00}]]
    set st  [safe_r32 [expr {$CLK_DPLL_HP + 0x1C}]]
    if {$cfg < 0 || $st < 0} {
        return [dict create present 0]
    }
    set en      [expr {($cfg >> 31) & 0x1}]
    set bypass  [expr {($cfg >> 28) & 0x3}]
    set kdiv    [expr {($cfg >> 12) & 0xF}]
    set pdiv    [expr {($cfg >> 8)  & 0xF}]
    set ndiv    [expr { $cfg        & 0xFF}]
    set locked  [expr {$st & 0x1}]

    set fmult 0
    if {$ref_hz > 0 && $ndiv > 0 && $pdiv > 0} {
        set kd [expr {$kdiv + 1}]
        set fmult [expr {int(double($ref_hz) * $ndiv / ($pdiv * $kd))}]
    }
    set desc [format "N=%d P=%d K=%d" $ndiv $pdiv $kdiv]
    return [dict create present 1 en $en locked $locked bypass $bypass \
                       fmult $fmult cfg_raw $cfg desc $desc]
}

# Read whichever PLL sits on the given path; returns same dict shape.
# Adds:  name (string),  pll_present (0=no PLL on path),  reachable (0/1)
proc read_path_pll {path ref_hz} {
    set name [path_pll $path]
    if {$name eq ""} {
        return [dict create pll_present 0 name ""]
    }
    if {[string match "DPLL_LP*" $name]} {
        set idx  [string range $name end end]
        set d    [read_dpll_lp $idx $ref_hz]
    } else {
        set d    [read_dpll_hp $ref_hz]
    }
    dict set d pll_present 1
    dict set d name        $name
    return $d
}

# Given the PLL dict and the upstream reference, return the actual frequency
# at the PLL output (i.e. what feeds the CLK_PATH).  Honours BYPASS_SEL.
# Output: {fout effective_mode}   where effective_mode is one of:
#   PLL_OUT, PLL_BYPASS, ZERO, NO_PLL
proc pll_effective_output {pll_dict ref_hz} {
    if {![dict get $pll_dict pll_present]} {
        return [list $ref_hz NO_PLL]
    }
    if {![dict get $pll_dict present]} {
        return [list 0 UNREADABLE]
    }
    set en     [dict get $pll_dict en]
    set lck    [dict get $pll_dict locked]
    set bs     [dict get $pll_dict bypass]
    set fmult  [dict get $pll_dict fmult]

    # ENABLE=0 forces output to reference (LP behaviour, see SVD).  HP
    # mostly behaves the same.
    if {!$en} {
        switch -- $bs {
            1 { return [list 0 ZERO] }
            default { return [list $ref_hz PLL_BYPASS] }
        }
    }
    switch -- $bs {
        0 { ;# AUTO
            if {$lck} { return [list $fmult PLL_OUT] }
            return [list $ref_hz PLL_BYPASS]
        }
        1 { ;# LOCKED_OR_NOTHING
            if {$lck} { return [list $fmult PLL_OUT] }
            return [list 0 ZERO]
        }
        2 { return [list $ref_hz PLL_BYPASS] }
        3 { return [list $fmult PLL_OUT] }
    }
    return [list 0 ZERO]
}

# ---- Output --------------------------------------------------------------

proc dump_chain {} {
    global CLK_PATH_SELECT CLK_ROOT_SELECT NUM_HFCLK

    puts ""
    puts "PSE84 clock chains:  source -> PLL -> CLK_PATH -> HFCLK"
    puts "==================================================================="

    for {set k 0} {$k < $NUM_HFCLK} {incr k} {
        set root [safe_r32 [expr {$CLK_ROOT_SELECT + $k * 4}]]
        if {$root < 0} {
            puts [format "HFCLK%-2d  <bus error reading CLK_ROOT_SELECT>" $k]
            continue
        }

        set en        [expr {($root >> 31) & 0x1}]
        set hf_div    [expr {(($root >> 8) & 0xF) + 1}]
        set path      [expr {$root & 0xF}]

        # PATH source (oscillator).
        set path_raw  [safe_r32 [expr {$CLK_PATH_SELECT + $path * 4}]]
        if {$path_raw < 0} {
            set src_mux  -1
            set src_name "<err>"
            set src_hz   0
        } else {
            set src_mux  [expr {$path_raw & 0x7}]
            set src_name [decode_path_src $src_mux]
            set src_hz   [path_src_freq $src_mux]
        }

        # PLL on this path (or none).
        set pll  [read_path_pll $path $src_hz]
        lassign [pll_effective_output $pll $src_hz] path_hz mode

        # HFCLK output.  ENABLE=0 means the root is gated -> 0 Hz at the
        # tap, regardless of what the PLL/PATH would deliver upstream.
        if {$path_hz > 0 && $hf_div > 0} {
            set would_hz [expr {$path_hz / $hf_div}]
        } else {
            set would_hz 0
        }
        if {$en} {
            set hf_hz $would_hz
        } else {
            set hf_hz 0
        }

        set en_tag [expr {$en ? "ON " : "off"}]
        if {$k == 0 && !$en} { set en_tag "ON*" }

        puts ""
        if {$en} {
            puts [format "HFCLK%-2d  \[%s\]  %s" $k $en_tag [fmt_freq $hf_hz]]
        } else {
            puts [format "HFCLK%-2d  \[%s\]  gated (would be %s if enabled)" \
                $k $en_tag [fmt_freq $would_hz]]
        }

        # Build chain string.
        set src_str [format "%s (%s)" $src_name [fmt_freq $src_hz]]

        if {[dict get $pll pll_present]} {
            set pll_name [dict get $pll name]
            if {[dict get $pll present]} {
                set pll_en   [dict get $pll en]
                set pll_lck  [dict get $pll locked]
                set pll_bs   [dict get $pll bypass]
                set pll_desc [dict get $pll desc]
                set pll_mult [dict get $pll fmult]
                set pll_tag  [format "%s%s%s" \
                    [expr {$pll_en  ? "EN"  : "OFF"}] \
                    [expr {$pll_lck ? ",LOCKED" : ",unlocked"}] \
                    [format ",bypass=%s" [decode_bypass $pll_bs]]]
                set pll_label [format "%s \[%s\] %s -> %s" \
                    $pll_name $pll_tag $pll_desc [fmt_freq $pll_mult]]
            } else {
                set pll_label [format "%s <unreadable>" $pll_name]
            }
        } else {
            set pll_label "(no PLL on this path)"
        }

        # Effective output of PLL stage.
        switch -- $mode {
            PLL_OUT     { set stage_note "PLL output" }
            PLL_BYPASS  { set stage_note "PLL BYPASSED -> reference passes through" }
            ZERO        { set stage_note "PLL gated -> 0 Hz at PATH" }
            NO_PLL      { set stage_note "" }
            UNREADABLE  { set stage_note "PLL unreadable" }
            default     { set stage_note $mode }
        }
        if {$stage_note ne ""} {
            set after_pll [format "  (effective: %s = %s)" \
                $stage_note [fmt_freq $path_hz]]
        } else {
            set after_pll ""
        }

        puts [format "         %s" $src_str]
        if {[dict get $pll pll_present]} {
            puts [format "      -> %s%s" $pll_label $after_pll]
        }
        puts [format "      -> CLK_PATH%-2d (%s)" $path [fmt_freq $path_hz]]
        if {$en} {
            puts [format "      -> CLK_HF%-3d  (div=/%d) = %s" \
                $k $hf_div [fmt_freq $hf_hz]]
        } else {
            puts [format "      -> CLK_HF%-3d  ENABLE=0  (gated, no clock at the tap)" $k]
        }
    }

    puts ""
    puts "==================================================================="
    puts "  HFCLK status: \[ON\] enabled, \[off\] gated, \[ON*\] HFCLK0 (always on)"
    puts "  PLL bypass modes: AUTO (locked->out, else ref),"
    puts "                    LOCKED_OR_NOTHING (locked->out, else 0 Hz),"
    puts "                    PLL_BYPASS (always ref), PLL_OUT (always out)"
    puts "  Oscillator references: IHO=50 MHz, IMO=8 MHz."
    puts "  EXTCLK/ECO/ALTHF are board-dependent (shown as '?')."
}

targets cat1d.sys
dump_chain
shutdown
