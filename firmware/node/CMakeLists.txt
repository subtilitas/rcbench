# The sensor nodes' firmware: one ESP-IDF (Espressif Internet-of-Things
# Development Framework) project for the ESP32-C6 on a Waveshare
# ESP32-C6-Zero.  The role (power or balance) is chosen at build time,
# main/Kconfig.projbuild:
#   idf.py build                                   the power node
#   idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.balance" build
#                                                  the balance node
cmake_minimum_required(VERSION 3.16)

# The packets (protocols/rcnode), the balance measurement (shared/vibe) and
# the INA228 driver of shared/sense (components/ina228 compiles its two
# files).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../protocols/rcnode"
                         "${CMAKE_CURRENT_LIST_DIR}/../../shared/vibe")

# The application's version, as the panel's: MAJOR.MINOR.PATCH from the one
# header every build reads.
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
project(rcbench-node)
