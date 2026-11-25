# The OpenYGE ESC (electronic speed controller) protocol, written from
# docs/OpenYGE.md.  Pure C with no include outside this folder and the C
# standard headers, its CRC (cyclic redundancy check) included: frames,
# status, the parameter cache, the field map, the guard every outgoing
# frame passes, and the session behind oy_driver_t.  The module has no pin
# driver; a host suite runs the session against a simulated ESC.
set(SRCS openyge_frame.c openyge_status.c openyge_params.c openyge_map.c
         openyge_guard.c openyge_session.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_openyge STATIC ${SRCS})
    target_include_directories(rcbench_openyge PUBLIC include)
endif()
