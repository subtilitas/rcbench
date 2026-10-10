# The RP2350 pin driver for the PD mini's UART (universal asynchronous
# receiver-transmitter).
# An INTERFACE library: the source compiles in the target that links it,
# with that target's options and definitions, and the PIO (programmable
# input/output) program is assembled into a header in the build tree.
add_library(rcbench_pdmini_rp2350 INTERFACE)
target_sources(rcbench_pdmini_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/pd_uart.c")
target_include_directories(rcbench_pdmini_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
pico_generate_pio_header(rcbench_pdmini_rp2350
    "${CMAKE_CURRENT_LIST_DIR}/pd_uart.pio")
target_link_libraries(rcbench_pdmini_rp2350 INTERFACE
    rcbench_pdmini hardware_pio hardware_gpio hardware_clocks)
