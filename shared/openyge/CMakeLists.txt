# The OpenYGE ESC protocol, written from docs/OpenYGE.md.  Pure C: the panel
# never speaks it, the coprocessor will, and the host suite feeds it corrupt
# frames long before there is an ESC on the bench.
set(SRCS openyge_frame.c openyge_status.c openyge_params.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES link)
else()
    add_library(rcbench_openyge STATIC ${SRCS})
    target_include_directories(rcbench_openyge PUBLIC include)
    target_link_libraries(rcbench_openyge PUBLIC rcbench_link)
endif()
