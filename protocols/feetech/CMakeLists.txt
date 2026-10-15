# Feetech SCS and STS serial bus servos, without hardware: frames, the
# status decoder, the register tables of both series, the bus session and
# the arithmetic of the PIO (programmable input/output) pin driver.  The
# sources include the C standard headers and each other, nothing else, so
# the folder builds in any project.  A pin driver implements
# feetech_driver_t; rp2350/ holds the one for the RP2350.
set(SRCS feetech_wire.c feetech_reg.c feetech_bus.c feetech_pio.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_feetech STATIC ${SRCS})
    target_include_directories(rcbench_feetech PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
