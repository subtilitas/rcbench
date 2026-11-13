# The balance measurement in pure C.  vibe_demod.c and vibe_edge.c are the
# balance node's signal processing: the index pulse found in its ADC
# (analogue-to-digital converter) channel and the vibration channel
# demodulated against it, integer only for the ESP32-C6's RV32IMAC core.
# vibe_solve.c turns measured runs into a correction and runs on the panel,
# in float.  No image compiles the module yet; the host suite holds it
# (test_vibe, test_vibe_solve).
set(SRCS vibe_demod.c vibe_edge.c vibe_solve.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_vibe STATIC ${SRCS})
    target_include_directories(rcbench_vibe PUBLIC include)
    target_link_libraries(rcbench_vibe PUBLIC m)
endif()
