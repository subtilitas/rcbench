# rcbench sensor nodes: the Bluetooth LE (Low Energy) packets of the power
# node and the balance node, the control writes, the UUIDs and the
# advertising, and the power node's 20ms window arithmetic.  Pure C with no
# include outside this folder and the C standard headers.  The module has no
# pin driver: the node firmware (firmware/node) encodes, the panel decodes,
# and the host suite holds both to one layout.
set(SRCS rcnode.c rcnode_win.c rcnode_adv.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_rcnode STATIC ${SRCS})
    target_include_directories(rcbench_rcnode PUBLIC include)
endif()
