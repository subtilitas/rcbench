# The panel-to-coprocessor protocol.  The only module all three build systems
# compile: the panel polls with it, the coprocessor answers with it, and the
# host suite runs both ends against each other over a fake bus.
#
# link_crc is still here for OpenYGE, which needs the same polynomial with a
# different seed.  Nothing in this link uses it any more -- CAN has a CRC, an
# acknowledge slot and retransmission in silicon.
set(SRCS link_crc.c link_can.c link_dev.c link_host.c link_bringup.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_link STATIC ${SRCS})
    target_include_directories(rcbench_link PUBLIC include)
endif()
