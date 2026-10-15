# The RP2350 pin driver for an EX Bus line: one PIO (programmable
# input/output) state machine and two DMA (direct memory access) channels
# behind exbus_driver_t.  An INTERFACE library: the source compiles in the
# target that links it, with that target's options and definitions, and the
# PIO program is assembled into a header in the build tree.
add_library(rcbench_exbus_rp2350 INTERFACE)
target_sources(rcbench_exbus_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/exbus_line.c")
target_include_directories(rcbench_exbus_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
pico_generate_pio_header(rcbench_exbus_rp2350
    "${CMAKE_CURRENT_LIST_DIR}/exbus_line.pio")
target_link_libraries(rcbench_exbus_rp2350 INTERFACE
    rcbench_exbus hardware_pio hardware_gpio hardware_dma hardware_clocks
    hardware_timer)
