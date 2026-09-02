# The panel-to-coprocessor protocol, compiled by all three build systems: the
# panel polls with it, the coprocessor answers with it, and the host suite
# runs both ends against each other over a bus the suite controls.
#
# link_crc serves OpenYGE, which uses the same polynomial with a different
# seed.  The CAN (Controller Area Network) link carries no CRC (cyclic
# redundancy check) in its payload: CAN has a CRC, an acknowledge slot and
# retransmission in silicon.
set(SRCS link_crc.c link_can.c link_dev.c link_host.c link_bringup.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_link STATIC ${SRCS})
    target_include_directories(rcbench_link PUBLIC include)
endif()
