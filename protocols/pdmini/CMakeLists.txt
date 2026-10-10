# The PD mini: a USB-C PD (Power Delivery) trigger and buck module,
# commanded over a UART (universal asynchronous receiver-transmitter).
# Pure C with no include outside this folder and the C standard headers:
# the driver talks through three callbacks, rp2350/ supplies them on two
# PIO (programmable input/output) state machines, and a host suite runs
# the driver against a modelled module.
set(SRCS pdmini.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_pdmini STATIC ${SRCS})
    target_include_directories(rcbench_pdmini PUBLIC include)
    # The pin driver, in a pico-sdk build only.
    if(COMMAND pico_generate_pio_header)
        add_subdirectory(rp2350)
    endif()
endif()
