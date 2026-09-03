# DShot: the sixteen-bit frame out to an ESC (electronic speed controller)
# and the GCR (group-coded recording) burst back from one.  Pure C: the
# coprocessor's PIO (programmable input/output) programs put the edges on the
# pin and capture the samples, and everything about what the bits mean is
# tested here.
set(SRCS dshot_frame.c dshot_telem.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_dshot STATIC ${SRCS})
    target_include_directories(rcbench_dshot PUBLIC include)
endif()
