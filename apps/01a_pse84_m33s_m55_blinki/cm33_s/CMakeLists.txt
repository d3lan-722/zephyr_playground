cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cm33_s_blinki)

target_sources(app PRIVATE
	src/main.c
	src/pse84_boot_local.c
)
