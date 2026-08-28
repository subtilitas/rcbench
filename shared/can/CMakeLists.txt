# CAN bit timing, shared by two controllers with different clocks and the same
# arithmetic.  Pure C: the host suite proves the segmentation before either
# controller is asked to accept it.
set(SRCS can_timing.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_can STATIC ${SRCS})
    target_include_directories(rcbench_can PUBLIC include)
endif()
