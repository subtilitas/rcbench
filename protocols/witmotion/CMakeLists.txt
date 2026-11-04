# WitMotion serial protocol: the 11-byte frames of the BWT901CL and its
# family decoded to units, and the 5-byte commands.  Pure C with no include
# outside this folder and the C standard headers.  The module has no pin
# driver: the caller moves the bytes, and everything about what they mean is
# tested on a host.
set(SRCS witmotion.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_witmotion STATIC ${SRCS})
    target_include_directories(rcbench_witmotion PUBLIC include)
endif()
