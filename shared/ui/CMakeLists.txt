# The widget layer, the router and the screens: theme, primitives-drawn
# icons, and the controls the screens are assembled from.
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
    ui_slider.c
    ui_tabs.c
    ui_workbench.c
    ui_target_row.c
    prog_registry.c
    ui_keypad.c
    ui_textkey.c
    ui_pinlist.c
    ui_router.c
    ui_text.c
    ui_text_de.c
    splash_screen.c
    overview_screen.c
    motor_screen.c
    motor_workbench.c
    motor_settings_page.c
    supply_screen.c
    servo_screen.c
    servo_settings_panel.c
    servo_test_panel.c
    servo_limit_panel.c
    servo_workbench.c
    kst_panel.c
    analyser_screen.c
    balance_screen.c
    battery_screen.c
    programmer_screen.c
    esc_stick_panel.c
    log_viewer_screen.c
    log_select.c
    settings_screen.c
    outputs_screen.c
    picker_screen.c
    board_drawing.c
    busfault_screen.c
    wifi_screen.c
    coproc_update_screen.c
    upd_screen.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES gfx touch bench logfile settings sbus link outputs servo can esc)
    # INJECT's mode name and bubble text, in the touch injector's bench build
    # only (idf.py -D RCBENCH_TOUCH_INJECTOR=ON build).  PUBLIC, as the
    # include path is: the define changes ui_text.def's IDs, and every file
    # that includes ui_text.h has to see the same set.
    if(RCBENCH_TOUCH_INJECTOR)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC RCBENCH_TOUCH_INJECTOR=1)
    endif()
else()
    add_library(rcbench_ui STATIC ${SRCS})
    target_include_directories(rcbench_ui PUBLIC include "${CMAKE_CURRENT_LIST_DIR}/../link/include" "${CMAKE_CURRENT_LIST_DIR}/../can/include")
    # The host suite tests INJECT's bubble, so it builds with the injector's
    # texts; PUBLIC for the reason above.
    target_compile_definitions(rcbench_ui PUBLIC RCBENCH_TOUCH_INJECTOR=1)
    target_link_libraries(rcbench_ui PUBLIC rcbench_gfx rcbench_touch rcbench_bench rcbench_logfile rcbench_settings rcbench_sbus rcbench_link rcbench_outputs rcbench_can rcbench_esc rcbench_servo)
endif()
