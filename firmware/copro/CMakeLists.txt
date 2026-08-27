# The coprocessor firmware: protocol timing, capture, sampling and the power
# path.  Everything with a deadline; nothing with an opinion.
cmake_minimum_required(VERSION 3.13)

# The RP2350B, not the RP2350A.  The pin budget is what drove the split --
# servo outputs, servo inputs, the ESC channels, the one-wire programming
# lines, the host link, CAN, I2C, the encoder, the load gates and the safety
# line come to 27-32 GPIO -- and a Pico 2 brings out 26.  The board default
# below is a guess at which RP2350B module arrives; override it with
# -DPICO_BOARD=... and correct it here once that is settled.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pimoroni_pico_plus2_rp2350)
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350)
endif()

include(pico_sdk_import.cmake)
project(rcbench-copro C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

# The same directory the panel registers as an IDF component and the host
# suite builds with plain gcc.  One source list, three build systems.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/link"  link)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/bench" bench)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/safety" safety)
# Not called yet -- it has nothing to drive until there is PWM and a sensor per
# output -- but built here so the ARM toolchain compiles it alongside the host
# one.  --gc-sections drops it from the image.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared/servo"  servo)

add_executable(rcbench-copro
    src/main.c
    src/link_uart.c
)
target_include_directories(rcbench-copro PRIVATE include src)
target_link_libraries(rcbench-copro
    pico_stdlib
    hardware_pio
    hardware_uart
    hardware_gpio
    rcbench_link
    rcbench_bench
    rcbench_safety
    rcbench_servo
)
pico_add_extra_outputs(rcbench-copro)
