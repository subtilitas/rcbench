# PPM (pulse-position modulation): several channels on one pin, laid out as
# run lengths.  Pure C: the coprocessor's PIO (programmable input/output)
# program toggles the pin at each run, and the arithmetic that decides whether
# a frame fits at all is tested here.
set(SRCS ppm.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES outputs)
else()
    add_library(rcbench_ppm STATIC ${SRCS})
    target_include_directories(rcbench_ppm PUBLIC include)
    target_link_libraries(rcbench_ppm PUBLIC rcbench_outputs)
endif()
