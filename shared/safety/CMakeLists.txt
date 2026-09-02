# The safety heartbeat, both ends: the panel generates, the coprocessor
# judges, and they share a file so they share the numbers.
set(SRCS heartbeat.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_safety STATIC ${SRCS})
    target_include_directories(rcbench_safety PUBLIC include)
endif()
