cmake_minimum_required(VERSION 3.20.0)
#list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../)
list(APPEND SNIPPET_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/main.c)

# When the `rram` snippet is selected, the SoC's signing helper
# (pse84_add_metadata_secure_hex) still emits zephyr.signed.hex at
# the SMIF-SAHB address 0x70100000 because it derives --hex-addr
# from the board's `m33s_header`/`flash0_s`/`flash0_sahb` DT nodes.
# Rebase every IHEX extended-address record to RRAM-S 0x32011000
# (delta -0x3E0EF000) so OpenOCD's RRAM flash bank programs the
# correct memory. Payload bytes (vector table, MCUBoot header,
# hash, signature TLVs) are untouched, so SE-ROM signature
# verification still passes.
#
# We attach via add_custom_target(... ALL) with a file-level
# DEPENDS on signed.hex instead of `extra_post_build_commands`
# because that property is snapshotted by Zephyr's top-level
# CMakeLists during find_package(Zephyr).
if("rram" IN_LIST SNIPPET_AS_LIST)
  set(_signed_hex ${ZEPHYR_BINARY_DIR}/${KERNEL_NAME}.signed.hex)
  set(_signed_hex_stamp ${_signed_hex}.rram.stamp)
  add_custom_command(
    OUTPUT ${_signed_hex_stamp}
    COMMAND ${CMAKE_COMMAND} -E echo
            "Shifting zephyr.signed.hex from SMIF-S 0x70100000 to RRAM-S 0x32011000"
    COMMAND ${CMAKE_OBJCOPY}
            --input-target=ihex
            --output-target=ihex
            --change-addresses=-0x3E0EF000
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


