# The settings model: a typed schema, the values behind it, and the rules for
# clamping and defaulting them.  Persistence is a separate file with the
# panel, because NVS (non-volatile storage) is hardware; every rule here is
# tested without a chip.  The sensor bus's pins take the values the link
# page's rule and the board's catalogue allow, so the model reads both.
set(SRCS settings.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES link outputs)
else()
    add_library(rcbench_settings STATIC ${SRCS})
    target_include_directories(rcbench_settings PUBLIC include)
    target_link_libraries(rcbench_settings PUBLIC m
                          PRIVATE rcbench_link rcbench_outputs)
endif()
