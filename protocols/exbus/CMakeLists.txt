# EX Bus master, without hardware: the channel packet, the telemetry request,
# the reply check, the EX telemetry decoder, the cycle scheduler and the
# arithmetic of the PIO (programmable input/output) pin driver.  The sources
# include the C standard headers and each other, nothing else, so the folder
# builds in any project.  A pin driver implements exbus_driver_t; rp2350/
# holds the one for the RP2350.
set(SRCS exbus_wire.c exbus_telem.c exbus_sched.c exbus_pio.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_exbus STATIC ${SRCS})
    target_include_directories(rcbench_exbus PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
