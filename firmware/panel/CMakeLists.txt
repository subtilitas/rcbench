# The panel firmware: screen, touch, card, settings and the host end of the
# link.  Nothing with a deadline runs here.
cmake_minimum_required(VERSION 3.16)

# shared/ modules are ordinary ESP-IDF (Espressif Internet-of-Things
# Development Framework) components from here.  Each one's CMakeLists calls
# idf_component_register() under ESP_PLATFORM and add_library() otherwise, so
# the same directory serves this build, the IOmcu's and the host
# suite's with one source list.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../shared")
# The protocol modules are named one by one.  Only a module's core is compiled
# here; its rp2350/ folder belongs to a pico-sdk build.  openyge is listed
# for its planned use; no component requires it and the panel does not link it.
foreach(module dshot hitec kst openyge pdmini phase_tap ppm sbus)
    list(APPEND EXTRA_COMPONENT_DIRS
         "${CMAKE_CURRENT_LIST_DIR}/../../protocols/${module}")
endforeach()

# The application's version, as esp_app_desc_t carries it and an update
# compares it: MAJOR.MINOR.PATCH from the one header every build reads.
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

# The touch injector's end of the panel (testbench/touch-injector): the read
# of an I2C device at 0x14 beside the GT911, and INJECT on the band.  Default
# ON; the one place the default lives, read by components gt911 and ui.
#   idf.py -D RCBENCH_TOUCH_INJECTOR=OFF build
# builds a panel that never addresses 0x14 and holds none of that code.
option(RCBENCH_TOUCH_INJECTOR "Read a touch injector at I2C 0x14" ON)

# The DSI (Display Serial Interface) host's colour bars for 3 s at start-up,
# for a first look at a board (components/display).
#   idf.py -D RCBENCH_DISPLAY_BARS=ON build
option(RCBENCH_DISPLAY_BARS "Show the DSI host's colour bars at start-up" OFF)

# ESP-IDF's fatfs component with exFAT on, in place of its own; the reason
# and the mechanism are in the file.
include("${CMAKE_CURRENT_LIST_DIR}/fatfs_exfat.cmake")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rcbench-panel)
