# The Bluetooth bridge: a classic ESP32 (ESP32-WROOM-32) that answers the
# panel's HC-05 AT commands (protocols/hc05emu) and carries the BWT901CL's
# SPP (serial port profile) link to the panel's UART1 in data mode.  The
# original ESP32 is the only ESP32 with Bluetooth Classic.
cmake_minimum_required(VERSION 3.16)

# The emulator's core is the one ESP-IDF component from outside this folder.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../protocols/hc05emu")

# The application's version, MAJOR.MINOR.PATCH, from the header every build
# of a release reads; AT+VERSION? answers "btbridge-" and it.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../shared/link/include/rcbench_version.h"
     _rcbench_version_h)
foreach(part MAJOR MINOR PATCH)
    string(REGEX MATCH "#define RCBENCH_VERSION_${part} ([0-9]+)" _m
           "${_rcbench_version_h}")
    if(NOT _m)
        message(FATAL_ERROR "rcbench_version.h: no RCBENCH_VERSION_${part}")
    endif()
    set(_rcbench_${part} "${CMAKE_MATCH_1}")
endforeach()
set(PROJECT_VER "${_rcbench_MAJOR}.${_rcbench_MINOR}.${_rcbench_PATCH}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/../../shared/link/include/rcbench_version.h")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rcbench-btbridge)
