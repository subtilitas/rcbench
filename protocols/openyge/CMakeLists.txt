# The OpenYGE ESC (electronic speed controller) protocol, written from
# docs/OpenYGE.md.  Pure C with no include outside this folder and the C
# standard headers, its CRC (cyclic redundancy check) included: frames,
# status, the parameter cache, the field map, the guard every outgoing
# frame passes, the session behind oy_driver_t, and the settings file.
# A host suite runs the session against a simulated ESC.
set(SRCS openyge_frame.c openyge_status.c openyge_params.c openyge_map.c
         openyge_guard.c openyge_session.c openyge_file.c)

# The pin driver: the arithmetic of its PIO (programmable input/output)
# program here, host-tested; the RP2350 driver in rp2350/.
list(APPEND SRCS openyge_pio.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_openyge STATIC ${SRCS})
    target_include_directories(rcbench_openyge PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
