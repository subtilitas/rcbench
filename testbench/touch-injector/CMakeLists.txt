# The touch injector for a Waveshare RP2350-Zero: a GT911 emulator at 0x14
# on the panel's touch bus, driven over USB (Universal Serial Bus) serial.
# A standalone pico-sdk project: it needs PICO_SDK_PATH and nothing else of
# this repository but core/ and rp2350/.
#
#   export PICO_SDK_PATH=/path/to/pico-sdk        # 2.3.0
#   cmake -S . -B build -DPICO_BOARD=waveshare_rp2350_zero
#   cmake --build build
#
# Output: build/rcbench-touch-injector.uf2 (and .elf, .bin).
#
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD waveshare_rp2350_zero)
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350)
endif()

include(pico_sdk_import.cmake)
project(rcbench-touch-injector C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(rcbench-touch-injector
    rp2350/main.c
    core/gt911_emu.c
    core/inj_line_id.c
    core/injector.c
)
target_include_directories(rcbench-touch-injector PRIVATE core/include)
target_compile_options(rcbench-touch-injector PRIVATE -Wall -Wextra -Werror)
pico_generate_pio_header(rcbench-touch-injector
    ${CMAKE_CURRENT_LIST_DIR}/rp2350/ws2812.pio)

target_compile_definitions(rcbench-touch-injector PRIVATE
    # The name the Pi finds the port by:
    # /dev/serial/by-id/usb-Raspberry_Pi_rcbench_injector_<serial>-if00
    # At most 19 characters: pico-sdk cuts longer strings (rp2350/main.c
    # asserts it).
    USBD_PRODUCT="rcbench injector"
    # A host that stops reading costs a write at most 10ms, under the
    # 200ms watchdog.
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=10000
)

target_link_libraries(rcbench-touch-injector PRIVATE
    pico_stdlib
    pico_multicore
    pico_i2c_slave
    hardware_i2c
    hardware_pio
    hardware_watchdog
)

pico_enable_stdio_usb(rcbench-touch-injector 1)
pico_enable_stdio_uart(rcbench-touch-injector 0)
pico_add_extra_outputs(rcbench-touch-injector)
