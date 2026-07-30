# Declare the current directory as a Zephyr library
# If no name is provided, the library name will be the same as the directory name
zephyr_library()

# Add source files for this library
zephyr_library_sources(
  bgt60tr13c.c
)

# Add header files to the CMake search directories
zephyr_include_directories(
  .
)