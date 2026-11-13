# The wireless knob protocol of the M5Stack Dial over a custom BLE service:
# the packets in both directions with their bounds, the panel's turn
# tracker and link state, and the Dial's model of its face.  The sources
# include the C standard headers and each other, nothing else, so the folder
# builds in any project: the panel, the Dial's firmware and the host suite.
# No pin driver and no radio code.
set(SRCS dialknob.c dk_track.c dk_face.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_dialknob STATIC ${SRCS})
    target_include_directories(rcbench_dialknob PUBLIC include)
endif()
