# The RP2350 driver for the I2C queue: one of the chip's two I2C blocks,
# its interrupt and its two pins behind i2c_txq_ops_t.  No PIO
# (programmable input/output) program.  An INTERFACE library: the source
# compiles in the target that links it, with that target's options and
# definitions.
add_library(rcbench_i2c_txq_rp2350 INTERFACE)
target_sources(rcbench_i2c_txq_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/i2c_txq_block.c")
target_include_directories(rcbench_i2c_txq_rp2350 INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(rcbench_i2c_txq_rp2350 INTERFACE
    rcbench_i2c_txq hardware_i2c hardware_gpio hardware_irq hardware_sync
    hardware_timer)
