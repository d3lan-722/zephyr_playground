# Minimum CMake required version
cmake_minimum_required(VERSION 3.20.0)

# Get Zephyr directory
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

add_clock_management_header(../../include/infineon_clock_management_header.h)
zephyr_include_directories(../../include)

# Project and source files
project(clock_management_test)
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE
    ../../drivers/infineon_clock_root.c
    ../../drivers/infineon_clock_mux.c
    ../../drivers/infineon_clock_pll.c
    ../../drivers/infineon_clock_hf.c
    ../../drivers/infineon_clock_consumer.c
    ../../drivers/infineon_clock_div.c
)
