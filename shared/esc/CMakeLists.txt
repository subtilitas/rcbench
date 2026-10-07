# ESC (electronic speed controller) programming profiles: the tables compiled
# from shared/esc/profiles/*.json by tools/gen_esc_profiles.py, the parser
# for the same JSON read from the card, and the registry that puts the
# card's profiles in front of the built-in ones; the stick programmer that
# runs a profile against the supply current, and a simulated ESC for it.  No
# file access and no I/O, so the host suite feeds the parser hostile input
# without a card and runs the programmer against the simulation.
set(SRCS esc_profiles_gen.c esc_json.c esc_registry.c esc_stick.c esc_sim.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_esc STATIC ${SRCS})
    target_include_directories(rcbench_esc PUBLIC include)
endif()
