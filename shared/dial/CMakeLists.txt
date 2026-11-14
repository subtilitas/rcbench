# The M5Stack Dial's faces: what its round 240 x 240 display shows, drawn
# from the panel's model (protocols/dialknob) with the bench's rasteriser
# (shared/gfx), its palette (shared/ui/ui_theme.c) and its words
# (shared/text).  Pure C, so the host suite renders and checks every face
# and tools/render_dial.py writes the committed images; the Dial's
# firmware (firmware/dial) draws with the same code.
set(SRCS dial_scene.c dial_draw.c dial_text.c dial_rings.c)

if(ESP_PLATFORM)
    # The Dial builds the theme alone of shared/ui: ui_theme.c needs gfx.h
    # and ui_layout.h only.
    idf_component_register(
        SRCS ${SRCS} "../ui/ui_theme.c"
        INCLUDE_DIRS include "../ui/include"
        REQUIRES gfx dialknob text)
else()
    add_library(rcbench_dial STATIC ${SRCS})
    target_include_directories(rcbench_dial PUBLIC include)
    # The host suite links rcbench_ui, which holds ui_theme.c.
    target_link_libraries(rcbench_dial PUBLIC rcbench_gfx rcbench_dialknob
                          rcbench_text rcbench_ui m)
endif()
