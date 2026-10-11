# The panel firmware: screen, touch, card, settings and the host end of the
# link.  Nothing with a deadline runs here.
cmake_minimum_required(VERSION 3.16)

# shared/ modules are ordinary ESP-IDF (Espressif Internet-of-Things
# Development Framework) components from here.  Each one's CMakeLists calls
# idf_component_register() under ESP_PLATFORM and add_library() otherwise, so
# the same directory serves this build, the coprocessor's and the host
# suite's with one source list.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../shared")
# The protocol modules are named one by one: a module the panel has no use
# for is not a component of this build.  Only a module's core is compiled
# here; its rp2350/ folder belongs to a pico-sdk build.
foreach(module dshot kst openyge pdmini phase_tap ppm sbus)
    list(APPEND EXTRA_COMPONENT_DIRS
         "${CMAKE_CURRENT_LIST_DIR}/../../protocols/${module}")
endforeach()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rcbench-panel)
