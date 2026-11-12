# Wrap-safe comparisons of a 32-bit millisecond counter (include/mstime.h).
# Header only: the library carries the include directory.
if(ESP_PLATFORM)
    idf_component_register(INCLUDE_DIRS include)
else()
    add_library(rcbench_mstime INTERFACE)
    target_include_directories(rcbench_mstime INTERFACE include)
endif()
