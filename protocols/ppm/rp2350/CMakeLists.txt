# The RP2350 pin driver for PPM (pulse-position modulation) out.
# An INTERFACE library: the source compiles in the target that links it,
# with that target's options and definitions, and the PIO (programmable
# input/output) program is assembled into a header in the build tree.
add_library(rcbench_ppm_rp2350 INTERFACE)
target_sources(rcbench_ppm_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/out_ppm.c")
target_include_directories(rcbench_ppm_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
pico_generate_pio_header(rcbench_ppm_rp2350
    "${CMAKE_CURRENT_LIST_DIR}/ppm.pio")
target_link_libraries(rcbench_ppm_rp2350 INTERFACE
    rcbench_ppm hardware_pio hardware_gpio hardware_dma hardware_clocks)
