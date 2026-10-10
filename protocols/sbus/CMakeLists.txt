# S.BUS: 16 channels in 25 bytes, with no checksum and a header byte that is
# also an ordinary channel value.  Pure C with no include outside this
# folder and the C standard headers.  The module has no pin driver: a PIO
# (programmable input/output) receiver that produces the bytes is not
# written, and everything about what the bytes mean is tested on a host.
set(SRCS sbus.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_sbus STATIC ${SRCS})
    target_include_directories(rcbench_sbus PUBLIC include)
endif()
