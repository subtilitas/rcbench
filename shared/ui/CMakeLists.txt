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
    ui_setting_keypad.c
    ui_textkey.c
    ui_pinlist.c
    ui_board.c
    ui_router.c
    ui_help.c
    ui_alert_slot.c
    ui_text.c
    splash_screen.c
    overview_screen.c
    motor_screen.c
    motor_events.c
    motor_draw.c
    motor_workbench.c
    motor_settings_page.c
    motor_program_page.c
    supply_screen.c
    supply_events.c
    supply_draw.c
    servo_screen.c
    servo_screen_sweep.c
    servo_screen_link.c
    servo_screen_touch.c
    servo_screen_draw.c
    servo_settings_panel.c
    servo_settings_draw.c
    servo_test_panel.c
    servo_limit_panel.c
    servo_workbench.c
    kst_panel.c
    kst_panel_text.c
    kst_panel_warn.c
    kst_panel_edit.c
    kst_panel_touch.c
    kst_panel_run.c
    kst_panel_draw.c
    hitec_panel.c
    hitec_panel_text.c
    hitec_panel_edit.c
    hitec_panel_page.c
    hitec_panel_touch.c
    hitec_panel_draw.c
    hitec_panel_file.c
    analyser_screen.c
    balance_screen.c
    battery_screen.c
    esc_stick_pages.c
    esc_stick_panel.c
    esc_stick_list.c
    esc_stick_touch.c
    esc_stick_draw.c
    esc_stick_draw_run.c
    prog_models.c
    log_viewer_screen.c
    log_viewer_load.c
    log_viewer_draw.c
    log_viewer_events.c
    logs_strip.c
    log_select.c
    settings_screen.c
    settings_draw.c
    settings_rows.c
    prog5v_box.c
    sensors_pane.c
    sensors_rows.c
    outputs_screen.c
    outputs_view.c
    outputs_port.c
    outputs_port_entry.c
    port_view.c
    port_mail.c
    picker_screen.c
    picker_geometry.c
    picker_draw.c
    picker_cat.c
    ui_cats.c
    ui_cats_art.c
    picker_port.c
    board_drawing.c
    busfault_screen.c
    wifi_screen.c
    update_screen.c
    coproc_update_screen.c
    upd_screen.c
    files_screen.c
    layout_screen.c
    files_draw.c
    files_events.c
    files_text.c
    text_view.c
    text_view_draw.c
    text_view_keys.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES gfx touch bench logfile settings sbus link outputs servo can esc
                                    text files clock)
    # RCBENCH_TOUCH_INJECTOR comes with component text (shared/text).
else()
    add_library(rcbench_ui STATIC ${SRCS})
    target_include_directories(rcbench_ui PUBLIC include "${CMAKE_CURRENT_LIST_DIR}/../link/include" "${CMAKE_CURRENT_LIST_DIR}/../can/include")
    # RCBENCH_TOUCH_INJECTOR comes with rcbench_text (shared/text).
    target_link_libraries(rcbench_ui PUBLIC rcbench_gfx rcbench_touch rcbench_bench rcbench_logfile rcbench_settings rcbench_sbus rcbench_link rcbench_outputs rcbench_can rcbench_esc rcbench_servo rcbench_text rcbench_files rcbench_clock)
endif()
