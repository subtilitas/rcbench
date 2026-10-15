# Protocol 2.0 of ROBOTIS Dynamixel servos, without hardware: packets, the
# CRC and byte stuffing, the X-series control table, one transaction at a
# time with its deadline, the ID scan and the arithmetic of the PIO
# (programmable input/output) pin driver.  The sources include the C
# standard headers and each other, nothing else, so the folder builds in any
# project.  A pin driver implements dxl_driver_t; rp2350/ holds the one for
# the RP2350.
set(SRCS dxl_packet.c dxl_table.c dxl_bus.c dxl_pio.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_dynamixel STATIC ${SRCS})
    target_include_directories(rcbench_dynamixel PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
