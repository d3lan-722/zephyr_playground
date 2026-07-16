cmake_minimum_required(VERSION 3.20.0)

# Register the app-local snippets/ dir so the `rram` snippet (used to
# link the image against the internal RRAM code region instead of the
# board-default SMIF XIP partition) is discoverable via
# `west build --snippet rram`. Must go BEFORE find_package(Zephyr) so
# snippets.cmake sees it during boilerplate include.
list(APPEND SNIPPET_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(pse84_m33_s_shell_ulp_lp_hp)

target_sources(app PRIVATE
    src/main.c
    src/power_manager.c
    src/power_manager_pll_retune.c
    src/power_manager_hf0_divider.c
    src/pm_phase_log.c
    src/pm_boot_optimize.c
    src/shell_cmds.c
    src/cmd_noidle.c
    src/cmd_sleep.c
    src/gpio_indicators.c
    src/diag.c
)

# When the `rram` snippet is selected, the SoC's signing helper
# (pse84_add_metadata_secure_hex) emits zephyr.signed.hex at the
# SMIF-SAHB base 0x60100000 for the m33-secure-only board variant
# (imgtool.py sign --hex-addr 0x60100000, derived from the board's
# `m33s_header` / `flash0_s` / `flash0_sahb` DT nodes).
# Rebase every IHEX extended-address record to RRAM-S 0x32011000
# (delta -0x2E0EF000) so OpenOCD's RRAM flash bank programs the
# correct memory. Payload bytes (vector table, MCUBoot header,
# hash, signature TLVs) are untouched, so SE-ROM signature
# verification still passes. Mirrors apps/00_hello_world, but note
# the base address differs by SoC variant: project 00 targets the
# m33/ns board where the signer picks 0x70100000; here we target
# the m33-secure-only board where it picks 0x60100000.
if("rram" IN_LIST SNIPPET_AS_LIST)
  set(_signed_hex ${ZEPHYR_BINARY_DIR}/${KERNEL_NAME}.signed.hex)
  set(_signed_hex_stamp ${_signed_hex}.rram.stamp)
  add_custom_command(
    OUTPUT ${_signed_hex_stamp}
    COMMAND ${CMAKE_COMMAND} -E echo
            "Shifting zephyr.signed.hex from SMIF-S 0x60100000 to RRAM-S 0x32011000"
    COMMAND ${CMAKE_OBJCOPY}
            --input-target=ihex
            --output-target=ihex
            --change-addresses=-0x2E0EF000
            ${_signed_hex}
            ${_signed_hex}.tmp
    COMMAND ${CMAKE_COMMAND} -E rename ${_signed_hex}.tmp ${_signed_hex}
    COMMAND ${CMAKE_COMMAND} -E touch ${_signed_hex_stamp}
    DEPENDS ${_signed_hex}
    COMMENT "Retargeting zephyr.signed.hex SMIF-S -> RRAM-S"
  )
  add_custom_target(retarget_signed_hex_to_rram ALL
    DEPENDS ${_signed_hex_stamp}
  )
endif()
