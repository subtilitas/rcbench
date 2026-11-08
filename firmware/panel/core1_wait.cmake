# ESP-IDF's freertos component with core 1's wait for the scheduler slowed
# to one read about every 10us.  Included by CMakeLists.txt before
# project.cmake.
#
# At start-up core 1 waits in esp_startup_start_app_other_cores()
# (freertos/app_startup.c) until core 0's scheduler runs.  ESP-IDF v6.1
# reads port_xSchedulerRunning[0] in a loop of two instructions with no
# pause, while core 0 creates the main task and clears its stack in
# internal memory.  On the ESP32-P4 (chip revision 3.2) core 0's store or
# load then stalls in about 1 start of 5 in the layout of release 0.23.9.
# The start ends in a store access fault, or 10s later in a watchdog
# reset.  Core 1 was in that loop at every stall whose program counters
# the chip saved, more than 100 on the bench.  The same image with this
# loop read about every 10us started 60 times of 60, beside the unchanged
# image's 11 stalls of 60 in the same hour.
#
# The build takes a copy of the freertos component of the ESP-IDF it runs
# on, under the build directory, with that loop replaced, and lists it in
# EXTRA_COMPONENT_DIRS: a component found there takes precedence over
# ESP-IDF's own of the same name (as fatfs_exfat.cmake does for FatFs).
# Between two reads the loop counts 2000 down in registers and touches no
# memory.  The copy is made when CMake configures; everything else in the
# component is the running ESP-IDF's own.  The configure stops when
# app_startup.c no longer holds the loop exactly once, rather than build
# without the change.

set(_rcbench_idf_rtos "$ENV{IDF_PATH}/components/freertos")
set(_rcbench_rtos_root "${CMAKE_BINARY_DIR}/core1_wait")
set(_rcbench_rtos "${_rcbench_rtos_root}/freertos")
set(_rcbench_start_c "${_rcbench_idf_rtos}/app_startup.c")

if(NOT EXISTS "${_rcbench_start_c}")
    message(FATAL_ERROR "core 1 wait: no ${_rcbench_start_c}")
endif()

file(READ "${_rcbench_start_c}" _rcbench_start)
set(_rcbench_spin
    "    while (port_xSchedulerRunning[0] == 0) {\n        ;\n    }\n")
string(FIND "${_rcbench_start}" "${_rcbench_spin}" _at)
string(FIND "${_rcbench_start}" "${_rcbench_spin}" _at_rev REVERSE)
if(_at EQUAL -1 OR NOT _at EQUAL _at_rev)
    message(FATAL_ERROR
        "core 1 wait: ${_rcbench_start_c} does not hold core 1's wait loop "
        "exactly once; see core1_wait.cmake")
endif()
string(CONCAT _rcbench_spin_new
       "    // One read about every 10us: rcbench core1_wait.cmake\n"
       "    while (port_xSchedulerRunning[0] == 0) {\n"
       "        __asm__ volatile(\".option push\\n.option norvc\\n\"\n"
       "                         \"li t0, 2000\\n1: addi t0, t0, -1\\n\"\n"
       "                         \"bnez t0, 1b\\n.option pop\" ::: \"t0\");\n"
       "    }\n")
string(REPLACE "${_rcbench_spin}" "${_rcbench_spin_new}" _rcbench_start
       "${_rcbench_start}")

# The component without its tests and without app_startup.c: that one is
# written below, and only when it differs, so a configure that changes
# nothing recompiles nothing.
file(COPY "${_rcbench_idf_rtos}" DESTINATION "${_rcbench_rtos_root}"
     PATTERN "test_apps" EXCLUDE
     PATTERN "app_startup.c" EXCLUDE)
set(_rcbench_start_out "${_rcbench_rtos}/app_startup.c")
set(_rcbench_start_old "")
if(EXISTS "${_rcbench_start_out}")
    file(READ "${_rcbench_start_out}" _rcbench_start_old)
endif()
if(NOT _rcbench_start_old STREQUAL _rcbench_start)
    file(WRITE "${_rcbench_start_out}" "${_rcbench_start}")
endif()

list(APPEND EXTRA_COMPONENT_DIRS "${_rcbench_rtos}")
# An ESP-IDF update that changes the file configures again.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${_rcbench_start_c}")
