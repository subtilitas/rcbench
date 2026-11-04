# The HC-05 Bluetooth module's AT set-up as an SPP (serial port profile)
# master: the exchange state machine, the answer lines and the address
# forms, without hardware.  The sources include the C standard headers and
# each other, nothing else, so the folder builds in any project.  No pin
# driver: the caller drives the UART and the KEY pin.
set(SRCS hc05.c hc05_line.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_hc05 STATIC ${SRCS})
    target_include_directories(rcbench_hc05 PUBLIC include)
endif()
