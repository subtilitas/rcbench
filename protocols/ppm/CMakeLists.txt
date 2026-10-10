# PPM (pulse-position modulation): several channels on one pin, laid out as
# run lengths.  Pure C with no include outside this folder and the C
# standard headers: the PIO (programmable input/output) program in rp2350/
# toggles the pin at each run, and the arithmetic that decides whether a
# frame fits at all is tested on a host.
set(SRCS ppm.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_ppm STATIC ${SRCS})
    target_include_directories(rcbench_ppm PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
