# The PPA (pixel-processing accelerator) driver with the DIG-734 workaround
# always on.  Included by CMakeLists.txt before project.cmake.
#
# Screen rotation 180 copies what each frame drew into the frame buffer
# through the PPA's SRM (scale, rotate, mirror) engine
# (components/display).  The ESP32-P4's SRM engine can hang on hardware
# bug DIG-734.  ESP-IDF v5.5's esp_driver_ppa, src/ppa_srm.c, turns its
# workaround (the macro-block order bypass) on only for some block sizes.
# Espressif's esp_lvgl_adapter component, version 0.6.4, patches that
# block to turn the bypass on for every transaction
# (0001-bugfix-ppa-Temporary-fix-for-the-PPA-hang-issue.patch, 2026-02-11).
# This file makes the same change.
#
# The build takes a copy of the esp_driver_ppa component of the ESP-IDF it
# runs on, under the build directory, with the block between the two
# markers below replaced by the one call, and lists it in
# EXTRA_COMPONENT_DIRS: a component found there takes precedence over
# ESP-IDF's own of the same name (as fatfs_exfat.cmake does for FatFs).
# The copy is made when CMake configures; everything else in the component
# is the running ESP-IDF's own.  The configure stops when ppa_srm.c no
# longer holds each marker exactly once, rather than build without the
# workaround.

set(_rcbench_idf_ppa "$ENV{IDF_PATH}/components/esp_driver_ppa")
set(_rcbench_ppa_root "${CMAKE_BINARY_DIR}/ppa_dig734")
set(_rcbench_ppa "${_rcbench_ppa_root}/esp_driver_ppa")
set(_rcbench_srm_c "${_rcbench_idf_ppa}/src/ppa_srm.c")

if(NOT EXISTS "${_rcbench_srm_c}")
    message(FATAL_ERROR "PPA: no ${_rcbench_srm_c}")
endif()

file(READ "${_rcbench_srm_c}" _rcbench_srm)
set(_rcbench_first "    // Hardware bug workaround (DIG-734)\n")
set(_rcbench_last
    "    ppa_ll_srm_bypass_mb_order(platform->hal.dev, bypass_mb_order);\n")
foreach(marker first last)
    string(FIND "${_rcbench_srm}" "${_rcbench_${marker}}" _at)
    string(FIND "${_rcbench_srm}" "${_rcbench_${marker}}" _at_rev REVERSE)
    if(_at EQUAL -1 OR NOT _at EQUAL _at_rev)
        message(FATAL_ERROR
            "PPA: ${_rcbench_srm_c} does not hold the DIG-734 ${marker} "
            "marker exactly once; see ppa_dig734.cmake")
    endif()
    set(_rcbench_at_${marker} ${_at})
endforeach()
if(NOT _rcbench_at_first LESS _rcbench_at_last)
    message(FATAL_ERROR "PPA: the DIG-734 markers in ${_rcbench_srm_c} are "
                        "out of order; see ppa_dig734.cmake")
endif()
string(LENGTH "${_rcbench_last}" _rcbench_last_len)
math(EXPR _rcbench_tail "${_rcbench_at_last} + ${_rcbench_last_len}")
string(SUBSTRING "${_rcbench_srm}" 0 ${_rcbench_at_first} _rcbench_head)
string(SUBSTRING "${_rcbench_srm}" ${_rcbench_tail} -1 _rcbench_rest)
string(CONCAT _rcbench_srm
       "${_rcbench_head}"
       "    // Hardware bug workaround (DIG-734), always on: "
       "rcbench ppa_dig734.cmake\n"
       "    ppa_ll_srm_bypass_mb_order(platform->hal.dev, true);\n"
       "${_rcbench_rest}")

# The component without its tests and without ppa_srm.c: that one is
# written below, and only when it differs, so a configure that changes
# nothing recompiles nothing.
file(COPY "${_rcbench_idf_ppa}" DESTINATION "${_rcbench_ppa_root}"
     PATTERN "test_apps" EXCLUDE
     PATTERN "ppa_srm.c" EXCLUDE)
set(_rcbench_srm_out "${_rcbench_ppa}/src/ppa_srm.c")
set(_rcbench_srm_old "")
if(EXISTS "${_rcbench_srm_out}")
    file(READ "${_rcbench_srm_out}" _rcbench_srm_old)
endif()
if(NOT _rcbench_srm_old STREQUAL _rcbench_srm)
    file(WRITE "${_rcbench_srm_out}" "${_rcbench_srm}")
endif()

list(APPEND EXTRA_COMPONENT_DIRS "${_rcbench_ppa}")
# An ESP-IDF update that changes the file configures again.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${_rcbench_srm_c}")
