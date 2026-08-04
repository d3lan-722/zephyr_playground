# Dump PSE84 SCB4 (UART4) state — the BT HCI UART on kit_pse84_ai.
#
# Non-intrusive: uses the system AP (cat1d.sys) so the CPU is not halted
# and we can inspect the UART while the CM33 ISR is spinning.
# All reads are wrapped in catch {} because parts of the bus may be
# partially powered down.
#
# References (MXS22SCB, CySCB_Type in
#   modules/hal/infineon/mtb-dsl-pse8xxgp/pdl/devices/include/ip/cyip_scb.h):
#
#   +0x000 CTRL            block enable + mode (bit 24=UART, bit 31=ENABLED)
#   +0x004 STATUS          bus-level status
#   +0x040 UART_CTRL       UART mode (bit 24=RX_ENABLED)
#   +0x048 UART_RX_CTRL    parity/stop/break/polarity
#   +0x04C UART_RX_STATUS  break-detect latch
#   +0x050 UART_FLOW_CTRL  CTS/RTS enable, RTS trigger level
#   +0x200 TX_CTRL         data-width, MSB-first, open-drain
#   +0x204 TX_FIFO_CTRL    trigger level, shift-reg clear
#   +0x208 TX_FIFO_STATUS  USED[15:0] (bytes still in TX FIFO)
#   +0x300 RX_CTRL         data-width, MSB-first, MEDIAN
#   +0x304 RX_FIFO_CTRL    trigger level, shift-reg clear
#   +0x308 RX_FIFO_STATUS  USED[15:0] (bytes still in RX FIFO)
#   +0xE00 INTR_CAUSE      bit0=M bit1=S bit2=TX bit3=RX bit4=I2C_EC bit5=SPI_EC
#   +0xF80 INTR_TX         raw TX interrupt sources (W1C for latched)
#   +0xF88 INTR_TX_MASK    enabled TX sources
#   +0xF8C INTR_TX_MASKED  INTR_TX & INTR_TX_MASK
#   +0xFC0 INTR_RX         raw RX interrupt sources
#   +0xFC8 INTR_RX_MASK    enabled RX sources
#   +0xFCC INTR_RX_MASKED  INTR_RX & INTR_RX_MASK
#
# TX interrupt bits (level=L, latched=T):
#   0  0x001  TRIGGER          L
#   1  0x002  NOT_FULL         L
#   4  0x010  EMPTY            L   <-- h4.c irq_tx_ready looks for this
#   5  0x020  OVERFLOW         T
#   6  0x040  UNDERFLOW        T
#   7  0x080  BLOCKED          T
#   8  0x100  UART_DONE        T
#   9  0x200  UART_NACK        T
#  10  0x400  UART_ARB_LOST    T
#
# RX interrupt bits:
#   0  0x001  TRIGGER          L
#   2  0x004  NOT_EMPTY        L   <-- h4.c irq_rx_ready checks FIFO count instead
#   4  0x010  FULL             L
#   5  0x020  OVERFLOW         T
#   6  0x040  UNDERFLOW        T
#   8  0x100  UART_FRAME_ERROR T
#   9  0x200  UART_PARITY_ERR  T
#  10  0x400  UART_BREAK_DET   T
#
# A trap for h4.c's bt_uart_isr while(1) loop is any bit set in
# INTR_TX_MASKED or INTR_RX_MASKED that neither irq_tx_ready (only
# EMPTY/NOT_FULL) nor irq_rx_ready (only FIFO count > 0) recognizes.

set SCB4_BASE 0x529C0000

set SCB4_CTRL           [expr {$SCB4_BASE + 0x000}]
set SCB4_STATUS         [expr {$SCB4_BASE + 0x004}]
set SCB4_UART_CTRL      [expr {$SCB4_BASE + 0x040}]
set SCB4_UART_RX_CTRL   [expr {$SCB4_BASE + 0x048}]
set SCB4_UART_RX_STATUS [expr {$SCB4_BASE + 0x04C}]
set SCB4_UART_FLOW_CTRL [expr {$SCB4_BASE + 0x050}]
set SCB4_TX_CTRL        [expr {$SCB4_BASE + 0x200}]
set SCB4_TX_FIFO_CTRL   [expr {$SCB4_BASE + 0x204}]
set SCB4_TX_FIFO_STATUS [expr {$SCB4_BASE + 0x208}]
set SCB4_RX_CTRL        [expr {$SCB4_BASE + 0x300}]
set SCB4_RX_FIFO_CTRL   [expr {$SCB4_BASE + 0x304}]
set SCB4_RX_FIFO_STATUS [expr {$SCB4_BASE + 0x308}]
set SCB4_INTR_CAUSE     [expr {$SCB4_BASE + 0xE00}]
set SCB4_INTR_TX        [expr {$SCB4_BASE + 0xF80}]
set SCB4_INTR_TX_MASK   [expr {$SCB4_BASE + 0xF88}]
set SCB4_INTR_TX_MASKED [expr {$SCB4_BASE + 0xF8C}]
set SCB4_INTR_RX        [expr {$SCB4_BASE + 0xFC0}]
set SCB4_INTR_RX_MASK   [expr {$SCB4_BASE + 0xFC8}]
set SCB4_INTR_RX_MASKED [expr {$SCB4_BASE + 0xFCC}]

proc safe_r32 {addr} {
    if {[catch {read_memory $addr 32 1} v]} { return -1 }
    return $v
}

proc fmt_hex32 {v} {
    if {$v < 0} { return "<bus error>" }
    return [format "0x%08x" $v]
}

proc bit {v n} { return [expr {($v >> $n) & 1}] }

proc decode_intr_cause {v} {
    if {$v < 0} { return "<bus error>" }
    set flags [list]
    if {[bit $v 0]} { lappend flags "M" }
    if {[bit $v 1]} { lappend flags "S" }
    if {[bit $v 2]} { lappend flags "TX" }
    if {[bit $v 3]} { lappend flags "RX" }
    if {[bit $v 4]} { lappend flags "I2C_EC" }
    if {[bit $v 5]} { lappend flags "SPI_EC" }
    if {[llength $flags] == 0} { return "(idle)" }
    return [join $flags " | "]
}

proc decode_tx_intr {v} {
    if {$v < 0} { return "<bus error>" }
    set flags [list]
    if {[bit $v  0]} { lappend flags "TRIGGER" }
    if {[bit $v  1]} { lappend flags "NOT_FULL" }
    if {[bit $v  4]} { lappend flags "EMPTY" }
    if {[bit $v  5]} { lappend flags "OVERFLOW*" }
    if {[bit $v  6]} { lappend flags "UNDERFLOW*" }
    if {[bit $v  7]} { lappend flags "BLOCKED*" }
    if {[bit $v  8]} { lappend flags "UART_DONE*" }
    if {[bit $v  9]} { lappend flags "UART_NACK*" }
    if {[bit $v 10]} { lappend flags "UART_ARB_LOST*" }
    if {[llength $flags] == 0} { return "(none)" }
    return [join $flags " | "]
}

proc decode_rx_intr {v} {
    if {$v < 0} { return "<bus error>" }
    set flags [list]
    if {[bit $v  0]} { lappend flags "TRIGGER" }
    if {[bit $v  2]} { lappend flags "NOT_EMPTY" }
    if {[bit $v  4]} { lappend flags "FULL" }
    if {[bit $v  5]} { lappend flags "OVERFLOW*" }
    if {[bit $v  6]} { lappend flags "UNDERFLOW*" }
    if {[bit $v  8]} { lappend flags "UART_FRAME_ERROR*" }
    if {[bit $v  9]} { lappend flags "UART_PARITY_ERROR*" }
    if {[bit $v 10]} { lappend flags "UART_BREAK_DETECT*" }
    if {[llength $flags] == 0} { return "(none)" }
    return [join $flags " | "]
}

proc dump_scb4 {} {
    global SCB4_CTRL SCB4_STATUS SCB4_UART_CTRL SCB4_UART_RX_CTRL \
           SCB4_UART_RX_STATUS SCB4_UART_FLOW_CTRL \
           SCB4_TX_CTRL SCB4_TX_FIFO_CTRL SCB4_TX_FIFO_STATUS \
           SCB4_RX_CTRL SCB4_RX_FIFO_CTRL SCB4_RX_FIFO_STATUS \
           SCB4_INTR_CAUSE \
           SCB4_INTR_TX SCB4_INTR_TX_MASK SCB4_INTR_TX_MASKED \
           SCB4_INTR_RX SCB4_INTR_RX_MASK SCB4_INTR_RX_MASKED

    set ctrl           [safe_r32 $SCB4_CTRL]
    set status         [safe_r32 $SCB4_STATUS]
    set uart_ctrl      [safe_r32 $SCB4_UART_CTRL]
    set uart_rx_ctrl   [safe_r32 $SCB4_UART_RX_CTRL]
    set uart_rx_status [safe_r32 $SCB4_UART_RX_STATUS]
    set uart_flow_ctrl [safe_r32 $SCB4_UART_FLOW_CTRL]
    set tx_fifo_status [safe_r32 $SCB4_TX_FIFO_STATUS]
    set rx_fifo_status [safe_r32 $SCB4_RX_FIFO_STATUS]
    set intr_cause     [safe_r32 $SCB4_INTR_CAUSE]
    set intr_tx        [safe_r32 $SCB4_INTR_TX]
    set intr_tx_mask   [safe_r32 $SCB4_INTR_TX_MASK]
    set intr_tx_masked [safe_r32 $SCB4_INTR_TX_MASKED]
    set intr_rx        [safe_r32 $SCB4_INTR_RX]
    set intr_rx_mask   [safe_r32 $SCB4_INTR_RX_MASK]
    set intr_rx_masked [safe_r32 $SCB4_INTR_RX_MASKED]

    puts "=== SCB4 (UART4, BT HCI) @ 0x529C0000 ==="
    puts [format "  CTRL            = %s  ENABLED=%d" \
             [fmt_hex32 $ctrl] \
             [expr {$ctrl < 0 ? -1 : [bit $ctrl 31]}]]
    puts [format "  STATUS          = %s" [fmt_hex32 $status]]
    puts [format "  UART_CTRL       = %s" [fmt_hex32 $uart_ctrl]]
    puts [format "  UART_RX_CTRL    = %s" [fmt_hex32 $uart_rx_ctrl]]
    puts [format "  UART_RX_STATUS  = %s" [fmt_hex32 $uart_rx_status]]
    puts [format "  UART_FLOW_CTRL  = %s" [fmt_hex32 $uart_flow_ctrl]]
    if {$tx_fifo_status >= 0} {
        puts [format "  TX_FIFO_STATUS  = %s  USED=%d" \
                 [fmt_hex32 $tx_fifo_status] \
                 [expr {$tx_fifo_status & 0xFFFF}]]
    } else {
        puts [format "  TX_FIFO_STATUS  = %s" [fmt_hex32 $tx_fifo_status]]
    }
    if {$rx_fifo_status >= 0} {
        puts [format "  RX_FIFO_STATUS  = %s  USED=%d" \
                 [fmt_hex32 $rx_fifo_status] \
                 [expr {$rx_fifo_status & 0xFFFF}]]
    } else {
        puts [format "  RX_FIFO_STATUS  = %s" [fmt_hex32 $rx_fifo_status]]
    }

    puts ""
    puts "  -- Interrupt state --"
    puts [format "  INTR_CAUSE      = %s  \[%s\]" \
             [fmt_hex32 $intr_cause] \
             [decode_intr_cause $intr_cause]]
    puts [format "  INTR_TX         = %s  \[%s\]" \
             [fmt_hex32 $intr_tx] \
             [decode_tx_intr $intr_tx]]
    puts [format "  INTR_TX_MASK    = %s  \[%s\]" \
             [fmt_hex32 $intr_tx_mask] \
             [decode_tx_intr $intr_tx_mask]]
    puts [format "  INTR_TX_MASKED  = %s  \[%s\]" \
             [fmt_hex32 $intr_tx_masked] \
             [decode_tx_intr $intr_tx_masked]]
    puts [format "  INTR_RX         = %s  \[%s\]" \
             [fmt_hex32 $intr_rx] \
             [decode_rx_intr $intr_rx]]
    puts [format "  INTR_RX_MASK    = %s  \[%s\]" \
             [fmt_hex32 $intr_rx_mask] \
             [decode_rx_intr $intr_rx_mask]]
    puts [format "  INTR_RX_MASKED  = %s  \[%s\]" \
             [fmt_hex32 $intr_rx_masked] \
             [decode_rx_intr $intr_rx_masked]]

    puts ""
    puts "  Trap check for h4.c bt_uart_isr while() loop:"
    puts "    * = latched bit; not cleared by process_tx/process_rx."
    if {$intr_tx_masked > 0} {
        set trap_tx [expr {$intr_tx_masked & ~0x0013}]
        if {$trap_tx != 0} {
            puts [format "    !! TX trap bits set: 0x%08x  \[%s\]" \
                     $trap_tx [decode_tx_intr $trap_tx]]
        }
    }
    if {$intr_rx_masked > 0 && $rx_fifo_status >= 0} {
        set fifo_used [expr {$rx_fifo_status & 0xFFFF}]
        set trap_rx [expr {$intr_rx_masked & ~0x0005}]
        if {$trap_rx != 0 && $fifo_used == 0} {
            puts [format "    !! RX trap bits set with empty FIFO: 0x%08x  \[%s\]" \
                     $trap_rx [decode_rx_intr $trap_rx]]
        }
    }
}

dump_scb4
shutdown
