# Hitec servo programming protocols, without hardware: frames, transactions,
# the reply decoders and the arithmetic of the PIO (programmable
# input/output) pin driver.  The sources include the C standard headers and
# each other, nothing else, so the folder builds in any project.  A pin
# driver implements hitec_driver_t; rp2350/ holds the one for the RP2350.
set(SRCS hitec_wire.c hitec_pio.c hitec_pre.c hitec_reg.c hitec_session.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_hitec STATIC ${SRCS})
    target_include_directories(rcbench_hitec PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
