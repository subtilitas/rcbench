# DShot: the sixteen-bit frame out to an ESC (electronic speed controller)
# and the GCR (group-coded recording) burst back from one.  Pure C with no
# include outside this folder and the C standard headers: the PIO
# (programmable input/output) programs in rp2350/ put the edges on the pin
# and capture the samples, and everything about what the bits mean is
# tested on a host.
set(SRCS dshot_frame.c dshot_telem.c dshot_edt.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_dshot STATIC ${SRCS})
    target_include_directories(rcbench_dshot PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
