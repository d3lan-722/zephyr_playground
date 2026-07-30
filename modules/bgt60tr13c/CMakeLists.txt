# Include the required subdirectories
add_subdirectory(drivers)

# Include the drivers directory for header files
zephyr_include_directories(drivers)