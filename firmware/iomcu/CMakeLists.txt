# The coprocessor firmware: protocol timing, capture, sampling and the power
# path.  Everything with a deadline runs here.
cmake_minimum_required(VERSION 3.13)

# PICO_BOARD defaults to pimoroni_pico_plus2_rp2350 because the SDK (software
# development kit) has no board file for the Waveshare RP2350-CAN module the
# bring-up runs on (RP2350A, 4 MB flash); the default builds and runs on it.
# Override with -DPICO_BOARD=... for another module.  The final board needs an
# RP2350B: servo outputs, servo inputs, the ESC (electronic speed controller)
# channels, the one-wire programming lines, the host link, CAN (Controller
# Area Network), I2C (Inter-Integrated Circuit), the encoder, the load gates
# and the safety line come to 27 to 32 GPIO (general-purpose input/output),
# and a Pico 2 brings out 26.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pimoroni_pico_plus2_rp2350)
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350)
endif()

include(pico_sdk_import.cmake)
project(rcbench-iomcu C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

# The same directories the panel registers as ESP-IDF (Espressif
# Internet-of-Things Development Framework) components and the host suite
# builds with plain gcc.  One source list, three build systems.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/link"  link)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/bench" bench)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/outputs" outputs)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/dshot"  dshot)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/ppm"    ppm)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/safety" safety)
# Not called: the servo procedures need a current sensor per output, and none
# is fitted.  Built so the ARM toolchain compiles it as the host suite does;
# --gc-sections drops it from the image.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/servo"  servo)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/openyge" openyge)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/can"     can)

add_executable(rcbench-iomcu
    src/main.c
    src/xl2515.c
    src/out_pwm.c
    src/out_ppm.c
    src/out_dshot.c
    src/outputs_hw.c
)
target_include_directories(rcbench-iomcu PRIVATE include src)

# The PIO (programmable input/output) programs are assembled into headers at
# build time, so the .pio file is the only copy of a program and nothing can
# drift from it.  The generated headers land in the build tree.
pico_generate_pio_header(rcbench-iomcu
    "${CMAKE_CURRENT_LIST_DIR}/src/ppm.pio")
pico_generate_pio_header(rcbench-iomcu
    "${CMAKE_CURRENT_LIST_DIR}/src/dshot.pio")

# The coprocessor's own sources are held to the same warnings as the host
# build, scoped to this target so the SDK's headers are not judged by them.
# -Wconversion is off here: the SDK's register macros trip it, and the shared
# arithmetic that needs it is held to it by the host suite.
target_compile_options(rcbench-iomcu PRIVATE
    -Wall -Wextra -Werror -Wshadow)
target_link_libraries(rcbench-iomcu
    pico_stdlib
    hardware_pio
    hardware_pwm
    hardware_dma
    hardware_spi
    hardware_gpio
    rcbench_link
    rcbench_outputs
    rcbench_dshot
    rcbench_ppm
    rcbench_bench
    rcbench_safety
    rcbench_servo
    rcbench_openyge
    rcbench_can
)
#
# Diagnostics go out over USB (Universal Serial Bus) CDC (communications
# device class), not over a UART (universal asynchronous
# receiver-transmitter).  The SDK's default is stdio on uart0; both lines are
# stated explicitly so no diagnostic output lands on a UART pin.
#
pico_enable_stdio_usb(rcbench-iomcu 1)
pico_enable_stdio_uart(rcbench-iomcu 0)

pico_add_extra_outputs(rcbench-iomcu)
