# Custom Zephyr function that imports the bgt60tr13c/ subdirectory if the Kconfig option BGT60TR13C is enabled
add_subdirectory_ifdef(
  CONFIG_BGT60TR13C
  bgt60tr13c
)