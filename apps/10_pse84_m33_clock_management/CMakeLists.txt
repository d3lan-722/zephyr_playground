cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pse84_m33_clock_management)

add_clock_management_header(include/infineon_clock_management_header.h)
zephyr_include_directories(include)

target_sources(app PRIVATE
    src/main.c
    drivers/infineon_clock_root.c
    drivers/infineon_clock_mux.c
    drivers/infineon_clock_pll.c
    drivers/infineon_clock_hf.c
)

target_include_directories(app PRIVATE
    drivers/include
)