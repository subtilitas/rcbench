# S.BUS: 16 channels in 25 bytes, with no checksum and a header byte that is
# also an ordinary channel value.  Pure C: the coprocessor's PIO (programmable
# input/output) receiver produces the bytes (that program is not written), and
# everything about what they mean is tested here.
set(SRCS sbus.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_sbus STATIC ${SRCS})
    target_include_directories(rcbench_sbus PUBLIC include)
endif()
