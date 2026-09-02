# The settings model: a typed schema, the values behind it, and the rules for
# clamping and defaulting them.  Persistence is a separate file with the
# panel, because NVS (non-volatile storage) is hardware; every rule here is
# tested without a chip.
set(SRCS settings.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_settings STATIC ${SRCS})
    target_include_directories(rcbench_settings PUBLIC include)
    target_link_libraries(rcbench_settings PUBLIC m)
endif()
