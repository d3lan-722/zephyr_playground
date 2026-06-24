set QSPI_FLASHLOADER bsps/TARGET_APP_KIT_PSE84_EVAL_EPC2/config/GeneratedSource/PSE84_SMIF.FLM
source [find interface/kitprog3.cfg]


transport select swd
if {![info exists ENABLE_CM55]} { set ENABLE_CM55 1 }
source [find target/infineon/pse84xgxs2.cfg]
cat1d.cm33 configure -rtos auto -rtos-wipe-on-reset-halt 1
if {$ENABLE_CM55} {
    cat1d.cm55 configure -rtos auto -rtos-wipe-on-reset-halt 1
} elseif {[string length [info commands cat1d.cm55]] > 0} {
    # CM55 target exists but app holds PD1 OFF (e.g. apps/12_pm).  Skip
    # examine so OpenOCD never reads its powered-down debug regs and
    # never tears the CM33 session down with DAP ABORTs.
    cat1d.cm55 configure -defer-examine
}
gdb_breakpoint_override hard

if {$::ENABLE_ACQUIRE} {
    init
    reset init
    adapter speed 12000;
}