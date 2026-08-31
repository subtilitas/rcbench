# Host build of everything in shared/ that the panel and the coprocessor also
# compile.  Nothing here links a vendor SDK, so it runs anywhere gcc does --
# which is what makes the coverage number in the README mean something, and
# what lets the link codec be fed corrupt frames long before there is a wire.
cmake_minimum_required(VERSION 3.16)
project(rcbench_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_COVERAGE "Build with gcov instrumentation" OFF)

set(SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")

# Each module's CMakeLists answers to whichever build system is asking; out
# here that is the add_library() branch.
add_subdirectory("${SHARED}/gfx"      gfx)
add_subdirectory("${SHARED}/touch"    touch)
add_subdirectory("${SHARED}/ui"       ui)
add_subdirectory("${SHARED}/settings" settings)
add_subdirectory("${SHARED}/logfile"  logfile)
add_subdirectory("${SHARED}/link"     link)
add_subdirectory("${SHARED}/bench"    bench)
add_subdirectory("${SHARED}/outputs"  outputs)
add_subdirectory("${SHARED}/safety"   safety)
add_subdirectory("${SHARED}/servo"    servo)
add_subdirectory("${SHARED}/openyge"  openyge)
add_subdirectory("${SHARED}/can"      can)
add_subdirectory("${SHARED}/sbus"     sbus)

set(UNDER_TEST
    rcbench_gfx
    rcbench_touch
    rcbench_ui
    rcbench_settings
    rcbench_logfile
    rcbench_link
    rcbench_bench
    rcbench_outputs
    rcbench_safety
    rcbench_servo
    rcbench_openyge
    rcbench_can
    rcbench_sbus
)

# -Wconversion is the one that earns its place: this code does arithmetic on
# pixel coordinates and register widths, and a silent narrowing there is a
# wrong number rather than a crash.
set(STRICT -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion)

# ASan + UBSan, for the runtime faults -Werror cannot see: a parser fed a
# hostile frame reading off the end of a buffer, a signed shift overflowing, an
# unaligned access.  This is a separate CI job from the -Werror one, and it
# drops -Werror on purpose: UBSan's instrumentation makes GCC re-issue
# -Wconversion on shift expressions whose casts are already correct, and those
# false positives would fail the build before a single test ran.  The warnings
# still print; the enforcing -Werror lives in the ordinary job.
option(ENABLE_SANITIZERS "Build the suite under AddressSanitizer + UBSan" OFF)
if(ENABLE_SANITIZERS)
    list(REMOVE_ITEM STRICT -Werror)
    set(SAN -fsanitize=address,undefined -fno-sanitize-recover=all
            -fno-omit-frame-pointer -g)
    add_compile_options(${SAN})
    add_link_options(${SAN})
endif()

foreach(lib ${UNDER_TEST})
    target_compile_options(${lib} PRIVATE ${STRICT})
    if(ENABLE_COVERAGE)
        target_compile_options(${lib} PRIVATE --coverage -O0 -g)
        target_link_options(${lib} PUBLIC --coverage)
    endif()
endforeach()

# No fakes left.  The one that existed simulated a byte wire between the two
# ends; the CAN loopback puts the real host and the real device against each
# other over a bus this suite controls, which needs nothing pretended.
set(FAKES "")

enable_testing()

foreach(suite gfx touch_map nav widgets bench motor logview logwriter settings logfile link_crc link_can link_pages link_watchdog link_loopback link_bringup outputs heartbeat servo_limit servo_sync openyge_frame openyge_status openyge_params can_timing can_selftest mcp2515 sbus servo analyser programmer balance battery)
    add_executable(test_${suite} test_${suite}.c ${FAKES})
    target_link_libraries(test_${suite} PRIVATE ${UNDER_TEST})
    target_include_directories(test_${suite} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${suite} COMMAND test_${suite})
    # The CSV fixtures are the ones logwiju is held to; the suite finds them
    # relative to its own source directory, not the build tree.
    set_tests_properties(${suite} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
