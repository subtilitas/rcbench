# An HC-05 seen from its UART: the AT command set of an SPP (serial port
# profile) master answered from a state machine that a Bluetooth stack
# serves with radio results.  The sources include the C standard headers
# and each other, nothing else, so the folder builds in any project.  No
# pin driver and no radio code: the caller drives the UART, reads KEY and
# runs the radio.
set(SRCS hc05emu.c hc05emu_at.c hc05emu_radio.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_hc05emu STATIC ${SRCS})
    target_include_directories(rcbench_hc05emu PUBLIC include)
endif()
