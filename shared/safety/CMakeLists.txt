# The safety heartbeat, both ends: the panel generates, the coprocessor
# judges, and they share a file so they share the numbers.  arming.c is the
# panel's policy for when the bench may be armed, kept here because it is the
# same subject and because it has to be testable on the host.  safety_gate.c
# is the coprocessor's decision to arm, from the monitor and the link's
# watchdog together.
set(SRCS heartbeat.c arming.c touch_loss.c safety_gate.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES link)
else()
    add_library(rcbench_safety STATIC ${SRCS})
    target_include_directories(rcbench_safety PUBLIC include)
    target_link_libraries(rcbench_safety PUBLIC rcbench_link)
endif()
