# Servo procedures: what a servo does when it is installed, rather than on a
# desk.  Pure state machines fed measurements, so the host suite runs them
# against a modelled servo and the coprocessor runs the same code against a
# real one (not wired in).
set(SRCS servo_limit.c servo_sync.c servo_sim.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_servo STATIC ${SRCS})
    target_include_directories(rcbench_servo PUBLIC include)
endif()
