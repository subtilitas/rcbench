# The M5Stack Dial v1.1's firmware: the wireless knob for the panel.  An
# ESP32-S3FN8 (8MB flash, no PSRAM) with a GC9A01 round display, an FT3267
# touch controller, a rotary encoder, a button and a buzzer, serving the
# dialknob BLE service (protocols/dialknob) as a NimBLE peripheral.
cmake_minimum_required(VERSION 3.16)

# The bench's rasteriser and the knob protocol are components from here;
# each one's CMakeLists registers an IDF component under ESP_PLATFORM.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/gfx"
    "${CMAKE_CURRENT_LIST_DIR}/../../protocols/dialknob")

# The application's version is the bench's: MAJOR.MINOR.PATCH from the one
# header every build reads, as esp_app_desc_t and INFO carry it.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../shared/link/include/rcbench_version.h"
     _rcbench_version_h)
foreach(part MAJOR MINOR PATCH)
    string(REGEX MATCH "#define RCBENCH_VERSION_${part} ([0-9]+)" _m
           "${_rcbench_version_h}")
    if(NOT _m)
        message(FATAL_ERROR "rcbench_version.h: no RCBENCH_VERSION_${part}")
    endif()
    set(RCBENCH_DIAL_${part} "${CMAKE_MATCH_1}")
endforeach()
set(PROJECT_VER "${RCBENCH_DIAL_MAJOR}.${RCBENCH_DIAL_MINOR}.${RCBENCH_DIAL_PATCH}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/../../shared/link/include/rcbench_version.h")

# The console's test hooks (main/dial_console.c, docs/Dial.md): inject a
# turn, a press, a hold, a tap, a battery reading and a dropped link, and
# raise PORT.B G1 (GPIO1) for 1ms per injected detent.  A debug tool, off
# in the released image.
#   idf.py -D RCBENCH_DIAL_HOOKS=ON build
option(RCBENCH_DIAL_HOOKS "The console injects input (test build)" OFF)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rcbench-dial)
