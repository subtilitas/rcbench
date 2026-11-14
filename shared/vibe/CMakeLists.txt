# The balance measurement in pure C.  vibe_frame.c, vibe_edge.c and
# vibe_demod.c are the balance node's signal processing: its ADC
# (analogue-to-digital converter) frames split into channels and scaled,
# the index pulse found in its channel, and the vibration channel
# demodulated against it, integer only for the ESP32-C6's RV32IMAC core.
# vibe_solve.c turns measured runs into a correction for the panel, in
# float.  The balance node's image (firmware/node) compiles the module and
# links the first three; the host suite holds all four (test_vibe,
# test_vibe_frame, test_vibe_solve).
set(SRCS vibe_demod.c vibe_edge.c vibe_frame.c vibe_solve.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_vibe STATIC ${SRCS})
    target_include_directories(rcbench_vibe PUBLIC include)
    target_link_libraries(rcbench_vibe PUBLIC m)
endif()
