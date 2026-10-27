# A transaction queue for one I2C (Inter-Integrated Circuit) bus master:
# descriptors, earliest-deadline selection, timeouts, the idle-line check,
# the abort and the bus clear, without hardware.  The sources include the C
# standard headers and their own header, nothing else, so the folder builds
# in any project.  A driver implements i2c_txq_ops_t; rp2350/ holds the one
# for the RP2350's I2C blocks.
set(SRCS i2c_txq.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_i2c_txq STATIC ${SRCS})
    target_include_directories(rcbench_i2c_txq PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
