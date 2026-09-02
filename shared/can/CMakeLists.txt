# CAN (Controller Area Network) bit timing, shared by two controllers with
# different clocks and the same arithmetic, the MCP2515 register map, and the
# echo self-test.  Pure C: the host suite proves the segmentation before
# either controller is asked to accept it.
set(SRCS can_timing.c can_selftest.c mcp2515.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES link)
else()
    add_library(rcbench_can STATIC ${SRCS})
    target_include_directories(rcbench_can PUBLIC include)
    target_link_libraries(rcbench_can PUBLIC rcbench_link)
endif()
