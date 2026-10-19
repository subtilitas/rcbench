# The Hitec bench harness: protocols/hitec's pin driver on one RP2350 pin,
# driven over USB (Universal Serial Bus) serial.  A standalone pico-sdk
# project: it needs PICO_SDK_PATH, core/, rp2350/ and protocols/hitec.
#
#   export PICO_SDK_PATH=/path/to/pico-sdk        # 2.3.0
#   cmake -S . -B build -G Ninja -DPICO_BOARD=pico2
#   cmake --build build
#
# Output: build/rcbench-hitec-harness.uf2 (and .elf, .bin).  The line pin is
# a run-time argument (`open`), so one image runs on any RP2350 board whose
# PICO_BOARD matches its flash.
#
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350)
endif()

include(pico_sdk_import.cmake)
project(rcbench-hitec-harness C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

# The protocol module, core and pin driver, from the repository.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../protocols/hitec hitec)

add_executable(rcbench-hitec-harness
    rp2350/main.c
    core/hitec_harness.c
)
target_include_directories(rcbench-hitec-harness PRIVATE core/include)
target_compile_options(rcbench-hitec-harness PRIVATE -Wall -Wextra -Werror)

target_compile_definitions(rcbench-hitec-harness PRIVATE
    # The name the Pi finds the port by:
    # /dev/serial/by-id/usb-Raspberry_Pi_rcbench_hitec_<serial>-if00
    # At most 19 characters: pico-sdk cuts longer strings (rp2350/main.c
    # asserts it).
    USBD_PRODUCT="rcbench hitec"
    # A host that stops reading costs a write at most 10 ms, under the
    # 1 s watchdog.
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=10000
)

target_link_libraries(rcbench-hitec-harness PRIVATE
    pico_stdlib
    hardware_watchdog
    rcbench_hitec
    rcbench_hitec_rp2350
)

pico_enable_stdio_usb(rcbench-hitec-harness 1)
pico_enable_stdio_uart(rcbench-hitec-harness 0)
pico_add_extra_outputs(rcbench-hitec-harness)
