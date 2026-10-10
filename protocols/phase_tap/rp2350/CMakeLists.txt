# The RP2350 pin driver for the phase tap: the edge capture and its ring.
# An INTERFACE library: the source compiles in the target that links it,
# with that target's options and definitions, and the PIO (programmable
# input/output) program is assembled into a header in the build tree.
add_library(rcbench_phase_tap_rp2350 INTERFACE)
target_sources(rcbench_phase_tap_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/tone_cap.c")
target_include_directories(rcbench_phase_tap_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
pico_generate_pio_header(rcbench_phase_tap_rp2350
    "${CMAKE_CURRENT_LIST_DIR}/tone_cap.pio")
target_link_libraries(rcbench_phase_tap_rp2350 INTERFACE
    rcbench_phase_tap pico_stdlib hardware_pio hardware_gpio hardware_dma hardware_clocks)
