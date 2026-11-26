# The QR scanner's serial side in pure C: scan_line.c splits the bytes of
# the scanner's UART (universal asynchronous receiver-transmitter) into
# code lines and command answer frames; gm861.c is the command protocol of
# a GM861 scanner module (CRC, frames, the zones the bench holds it to) and
# the sequencer that finds the module, sets it and watches it.  No include
# outside this folder and the C standard headers.  The panel's UART task
# moves the bytes; the host suite holds both files (test_scan_line,
# test_gm861) against a simulated module.
set(SRCS scan_line.c gm861.c scan_port.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_scanner STATIC ${SRCS})
    target_include_directories(rcbench_scanner PUBLIC include)
endif()
