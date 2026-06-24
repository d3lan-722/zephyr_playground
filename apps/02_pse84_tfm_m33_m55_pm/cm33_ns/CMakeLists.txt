cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cm33_ns_blinki)

# Drop the SoC's default pm_state_set + its PRE_KERNEL_1 SYS_INIT
# (which bus-faults from NS under TF-M when CONFIG_PM=y). We provide
# our own pm_state_set in src/power.c.
get_target_property(zephyr_sources zephyr SOURCES)
list(REMOVE_ITEM zephyr_sources
     ${ZEPHYR_BASE}/soc/infineon/edge/pse84/power.c)
set_property(TARGET zephyr PROPERTY SOURCES ${zephyr_sources})

target_sources(app PRIVATE
    src/main.c
    src/indicator.c
    src/power.c
)
