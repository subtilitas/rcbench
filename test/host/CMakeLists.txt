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

set(UNDER_TEST
    rcbench_gfx
    rcbench_touch
    rcbench_ui
    rcbench_settings
    rcbench_logfile
    rcbench_link
)

# -Wconversion is the one that earns its place: this code does arithmetic on
# pixel coordinates and register widths, and a silent narrowing there is a
# wrong number rather than a crash.
set(STRICT -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion)
foreach(lib ${UNDER_TEST})
    target_compile_options(${lib} PRIVATE ${STRICT})
    if(ENABLE_COVERAGE)
        target_compile_options(${lib} PRIVATE --coverage -O0 -g)
        target_link_options(${lib} PUBLIC --coverage)
    endif()
endforeach()

enable_testing()

foreach(suite gfx touch_map settings logfile link_crc link_frame)
    add_executable(test_${suite} test_${suite}.c)
    target_link_libraries(test_${suite} PRIVATE ${UNDER_TEST})
    target_include_directories(test_${suite} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${suite} COMMAND test_${suite})
    # The CSV fixtures are the ones logwiju is held to; the suite finds them
    # relative to its own source directory, not the build tree.
    set_tests_properties(${suite} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
