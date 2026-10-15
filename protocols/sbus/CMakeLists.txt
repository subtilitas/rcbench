# S.BUS and S.BUS2: the 25-byte frame, decoded and encoded; the S.BUS2
# telemetry slots read after a frame; the arithmetic of the PIO
# (programmable input/output) pin driver; and the output that sends a frame
# each period on a driver.  The sources include the C standard headers and
# each other, nothing else, so the folder builds in any project.  A pin
# driver implements sbus_driver_t; rp2350/ holds the one for the RP2350.
set(SRCS sbus.c sbus2.c sbus_pio.c sbus_out.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_sbus STATIC ${SRCS})
    target_include_directories(rcbench_sbus PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
