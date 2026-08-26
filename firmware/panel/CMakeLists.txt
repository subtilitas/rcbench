# The panel firmware: screen, touch, card, settings, and the host end of the
# link.  Everything with an opinion; nothing with a deadline.
cmake_minimum_required(VERSION 3.16)

# shared/ modules are ordinary IDF components from here.  Each one's
# CMakeLists calls idf_component_register() under ESP_PLATFORM and
# add_library() otherwise, so the same directory serves this build, the
# coprocessor's and the host suite's with no source list written twice.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../shared")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rcbench-panel)
