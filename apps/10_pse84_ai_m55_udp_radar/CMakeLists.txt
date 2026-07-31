cmake_minimum_required(VERSION 3.20.0)

set(ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/bgt60tr13c")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pse84_ai_m55_udp_radar)

target_sources(app PRIVATE
    src/main.c
    src/scb3_clock_fix.c
    src/wifi_connect.c
)