# The RP2350B driver for the pass-through: MIRROR and CAPTURE in PIO0, two
# chained DMA (direct memory access) channels and the ring.  An INTERFACE
# library: the source compiles in the target that links it, with that
# target's options and definitions, and the PIO (programmable input/output)
# programs are assembled into a header in the build tree.
add_library(rcbench_passthru_rp2350 INTERFACE)
target_sources(rcbench_passthru_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/pass_cap.c")
target_include_directories(rcbench_passthru_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
pico_generate_pio_header(rcbench_passthru_rp2350
    "${CMAKE_CURRENT_LIST_DIR}/pass.pio")
target_link_libraries(rcbench_passthru_rp2350 INTERFACE
    rcbench_passthru pico_stdlib hardware_pio hardware_gpio hardware_dma
    hardware_timer)
