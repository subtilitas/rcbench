# Coordinate mapping, the press/move/release state machine, and the merge of
# the glass's contacts with a touch injector's.  The GT911 driver that feeds
# this is hardware and lives under firmware/panel.
set(SRCS touch_map.c touch_merge.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_touch STATIC ${SRCS})
    target_include_directories(rcbench_touch PUBLIC include)
endif()
