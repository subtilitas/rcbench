# The remote servo node's pass-through: pass_decode.c turns the states the
# capture pushes into pulse widths, frame periods, missed frames, glitches
# and lost signals; pass_ring.c reads the capture's DMA (direct memory
# access) ring into it and finds a lap.  Pure C with no include outside this
# folder and the C standard headers; rp2350/ holds the PIO (programmable
# input/output) programs MIRROR and CAPTURE and their driver.
set(SRCS pass_decode.c pass_ring.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_passthru STATIC ${SRCS})
    target_include_directories(rcbench_passthru PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
