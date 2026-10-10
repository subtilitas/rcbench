# The phase tap: the edges of one ESC (electronic speed controller) motor
# phase, turned into beeps and their pitch.  tone.c is the detector,
# edge_ring.c reads the words the capture leaves in its DMA (direct memory
# access) ring, and tone_svc.c is the pass that feeds the detector with
# them.  Pure C with no include outside this folder and the C standard
# headers; rp2350/ holds the PIO (programmable input/output) capture.
set(SRCS tone.c edge_ring.c tone_svc.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_phase_tap STATIC ${SRCS})
    target_include_directories(rcbench_phase_tap PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
