# KST servo programming protocol, without hardware: frames, the reply
# decoder, the register image, limits, write plans, the session and the
# arithmetic of the PIO (programmable input/output) pin driver.  The sources
# include the C standard headers and each other, nothing else, so the folder
# builds in any project.  A pin driver implements kst_driver_t; rp2350/
# holds the one for the RP2350.
set(SRCS kst_wire.c kst_reg.c kst_limits.c kst_plan.c kst_session.c
         kst_pio.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_kst STATIC ${SRCS})
    target_include_directories(rcbench_kst PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
