# ESC (electronic speed controller) programming profiles: the tables compiled
# from shared/esc/profiles/*.json by tools/gen_esc_profiles.py, the parser
# for the same JSON read from the card, and the registry that puts the
# card's profiles in front of the built-in ones.  No file access, so the
# host suite feeds the parser hostile input without a card.
set(SRCS esc_profiles_gen.c esc_json.c esc_registry.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_esc STATIC ${SRCS})
    target_include_directories(rcbench_esc PUBLIC include)
endif()
