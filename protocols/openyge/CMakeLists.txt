# The OpenYGE ESC (electronic speed controller) protocol, written from
# docs/OpenYGE.md.  Pure C with no include outside this folder and the C
# standard headers, its CRC (cyclic redundancy check) included.  The
# module has no pin driver; a host suite feeds it corrupt frames without
# an ESC connected.
set(SRCS openyge_frame.c openyge_status.c openyge_params.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_openyge STATIC ${SRCS})
    target_include_directories(rcbench_openyge PUBLIC include)
endif()
