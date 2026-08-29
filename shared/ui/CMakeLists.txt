# The widget layer: theme, primitives-drawn icons, and the controls the
# screens are assembled from.  The router and the screens themselves land
# here as they are re-cut against the two-processor model.
#
# Icons are drawn from primitives rather than stored as bitmaps: a few hundred
# bytes of code each, scalable, and no second asset to keep in sync.
set(SRCS
    ui_theme.c
    ui_widgets.c
    ui_icons.c
    ui_band.c
    ui_watermark.c
    ui_plot.c
    ui_hero.c
    ui_slider.c
    ui_tabs.c
    ui_router.c
    splash_screen.c
    overview_screen.c
    stub_screen.c
    motor_screen.c
    servo_screen.c
    analyser_screen.c
    log_viewer_screen.c
    settings_screen.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES gfx touch bench logfile settings sbus)
else()
    add_library(rcbench_ui STATIC ${SRCS})
    target_include_directories(rcbench_ui PUBLIC include)
    target_link_libraries(rcbench_ui PUBLIC rcbench_gfx rcbench_touch rcbench_bench rcbench_logfile rcbench_settings rcbench_sbus)
endif()
