# The settings model: a typed schema, the values behind it, and the rules for
# clamping and defaulting them.  Persistence is a separate file with the
# panel, because NVS (non-volatile storage) is hardware; every rule here is
# tested without a chip.  The sensor bus's pins take the values the link
# page's rule and the board's catalogue allow, so the model reads both.  The
# labels, help lines, categories and option names are ui_text.def's English
# (settings_text.c).  settings_file.c writes and reads the settings as a text
# file, settings_backup.c keeps those files in the card's folder through the
# caller's functions: neither touches a card itself.
set(SRCS settings.c settings_schema.c settings_sensors.c settings_text.c
         settings_file.c settings_backup.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES link outputs PRIV_REQUIRES text)
else()
    add_library(rcbench_settings STATIC ${SRCS})
    target_include_directories(rcbench_settings PUBLIC include)
    target_link_libraries(rcbench_settings PUBLIC m
                          PRIVATE rcbench_link rcbench_outputs rcbench_text)
endif()
