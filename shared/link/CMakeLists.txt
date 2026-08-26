# The panel-to-coprocessor codec.  The only module all three build systems
# compile: the panel polls with it, the coprocessor answers with it, and the
# host suite feeds it truncated and bit-flipped frames to prove it never
# accepts one.
set(SRCS link_crc.c link_frame.c link_dev.c link_host.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_link STATIC ${SRCS})
    target_include_directories(rcbench_link PUBLIC include)
endif()
