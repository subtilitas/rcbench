# A BLE central for a transparent-UART sensor (the ISSC/Microchip
# Transparent UART service): the advertising data parser, the scan list,
# connect, GATT discovery, subscribe, the write slot and the retries, as
# events in and actions out.  The sources include the C standard headers and
# each other, nothing else, so the folder builds in any project.  No pin
# driver and no radio code: the caller runs the host stack.
set(SRCS bleuart.c bleuart_adv.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_bleuart STATIC ${SRCS})
    target_include_directories(rcbench_bleuart PUBLIC include)
endif()
