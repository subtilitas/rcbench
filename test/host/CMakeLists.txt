# Host build of everything in shared/ that the panel and the coprocessor also
# compile.  Nothing here links a vendor SDK, so it runs anywhere gcc does --
# which is what lets the link codec be fed corrupt frames long before there is
# a wire to feed it over.
cmake_minimum_required(VERSION 3.16)
project(rcbench_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_COVERAGE "Build with gcov instrumentation" OFF)

set(SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")

# Each module's CMakeLists answers to whichever build system is asking; out
# here that is the add_library() branch.
add_subdirectory("${SHARED}/link" link)

set(UNDER_TEST rcbench_link)

# -Wconversion is the one that earns its place: this code does arithmetic on
# register widths and frame offsets, and a silent narrowing there is a wrong
# number rather than a crash.
set(STRICT -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion)
foreach(lib ${UNDER_TEST})
    target_compile_options(${lib} PRIVATE ${STRICT})
    if(ENABLE_COVERAGE)
        target_compile_options(${lib} PRIVATE --coverage -O0 -g)
        target_link_options(${lib} PUBLIC --coverage)
    endif()
endforeach()

enable_testing()

foreach(suite link_crc link_frame)
    add_executable(test_${suite} test_${suite}.c)
    target_link_libraries(test_${suite} PRIVATE ${UNDER_TEST})
    target_include_directories(test_${suite} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${suite} COMMAND test_${suite})
endforeach()
