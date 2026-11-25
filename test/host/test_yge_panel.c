/*
 * The screens of programming for YGE ESCs (yge_panel*.c) on the whole bench of
 * yge_panel_rig.h: the CONNECT page with SUPPLY's values and the port it
 * names, its 2s hold released at 1999ms and held to 2000ms, BY HAND in the
 * strip, BOOT WAIT's keypad at its limits; the tabs and their rows, CHECK
 * rows in the warning colour, raw rows read-only; the list on a choice and
 * the keypad on a number refusing a value outside the writable set; the line
 * that names a CHECK field with its new value; and the row in the registry.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_panel_rig.h"

/* A hold of the session's 2s control, the frames as the router gives them:
 * 7 of 0.25s and @p last_s more, nothing else of the bench running. */
static void hold_frames(float last_s)
{
    const gfx_rect_t r = yge_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };

    yge_pages.event(&e);
    for (int i = 0; i < 7; ++i) {
        yge_pages.tick(0.25f);
    }
    yge_pages.tick(last_s);
    e.type = TOUCH_EVENT_UP;
    yge_pages.event(&e);
}

TEST_CASE(connect_names_the_port_and_takes_a_hold_of_2000ms)
{
    char s[40];
    yge_prog_cmd_t c;

    session_up();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_CONNECT);
    CHECK(!yge_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, "PORT 1 GPIO 13");
    CHECK(!yge_panel_by_hand());
    /* Released at 1999ms: nothing goes. */
    hold_frames(0.249f);
    CHECK(!yge_panel_next_cmd(&c));
    CHECK(!yge_panel_holds_port());
    bench_run(50u);
    CHECK_EQ(g_commands, 0);
    /* Held to 2000ms: OPEN goes, on the hold and not on its release. */
    hold_frames(0.250f);
    CHECK(yge_panel_holds_port());
    CHECK(yge_panel_next_cmd(&c));
    CHECK_EQ(c.kind, YGE_PROG_CMD_OPEN);
    CHECK_EQ(c.port, 0u);
    carry_out(&c);
    for (int i = 0; i < 6000 && yge_panel_stage() != YGE_PANEL_SESSION; ++i) {
        bench_run(5u);
    }
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_SESSION);
    CHECK_EQ(g_cmd_op[1], LINK_YGE_OP_ENTER);
    CHECK_EQ(g_cmd_key[1], LINK_YGE_KEY);
    /* SUPPLY's values, and the pin the page holds. */
    CHECK_EQ(g.on_mv, 7400u);
    CHECK_EQ(g.on_ma, 1000u);
    CHECK(!yge_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, "PORT 1 GPIO 13");
    CHECK_EQ(yge_pages.pin(), 13u);
}

TEST_CASE(the_port_and_the_power_are_chosen_on_the_page)
{
    char s[40];

    session_up();
    yg_tap(yge_panel_value_rect(YGP_C_PORT));
    CHECK_EQ(ygp_s.ch_what, YGP_CH_PORT);
    CHECK_STR_EQ(yge_panel_choice_label(2), "PORT 3 GPIO 15");
    choose("PORT 3 GPIO 15");
    CHECK(!yge_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, "PORT 3 GPIO 15");
    /* BY HAND: no voltage and no limit, and the strip says so. */
    yg_tap(yge_panel_value_rect(YGP_C_POWER));
    choose("BY HAND");
    CHECK(yge_panel_by_hand());
    yg_tap(yge_panel_value_rect(YGP_C_VOLT));
    CHECK(!yge_panel_keypad()->open);
    yg_tap(yge_panel_value_rect(YGP_C_POWER));
    choose("SWITCHED");
    CHECK(!yge_panel_by_hand());
    /* A port whose pin is not read names the port alone, in the warning
     * colour. */
    port_view_t v;

    port_view_unknown(&v);
    port_view_set(&v);
    CHECK(yge_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, "PORT 3");
}

TEST_CASE(the_values_follow_supply_until_typed_and_boot_wait_keeps_its_limits)
{
    session_up();
    CHECK_EQ(ygp_s.mv, 7400u);
    supply_screen_put(5.0f, 0.5f);
    bench_run(5u);
    CHECK_EQ(ygp_s.mv, 5000u);
    CHECK_EQ(ygp_s.ma, 500u);
    yg_tap(yge_panel_value_rect(YGP_C_VOLT));
    CHECK(yge_panel_keypad()->open);
    kp_type("6");
    CHECK_EQ(ygp_s.mv, 6000u);
    supply_screen_put(5.0f, 0.5f);
    bench_run(5u);
    CHECK_EQ(ygp_s.mv, 6000u);
    /* BOOT WAIT: 1.0s to 20.0s; 0.9s and 20.1s are refused. */
    yg_tap(yge_panel_value_rect(YGP_C_BOOT));
    kp_type("0.9");
    CHECK(yge_panel_keypad()->open);
    yg_tap(ui_keypad_key_rect(yge_panel_keypad(), UI_KEY_CANCEL));
    yg_tap(yge_panel_value_rect(YGP_C_BOOT));
    kp_type("20.1");
    CHECK(yge_panel_keypad()->open);
    yg_tap(ui_keypad_key_rect(yge_panel_keypad(), UI_KEY_CANCEL));
    yg_tap(yge_panel_value_rect(YGP_C_BOOT));
    kp_type("20");
    CHECK(!yge_panel_keypad()->open);
    CHECK_EQ(ygp_s.boot_ms, 20000u);
    yg_tap(yge_panel_value_rect(YGP_C_BOOT));
    kp_type("1");
    CHECK_EQ(ygp_s.boot_ms, 1000u);
}

TEST_CASE(a_supply_value_that_moves_under_the_hold_ends_it)
{
    yge_prog_cmd_t c;
    const gfx_rect_t r = yge_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + 4),
                                   .y = (int16_t)(r.y + 4), .strength = 40 } };

    session_up();
    yge_pages.event(&e);
    yge_pages.tick(1.0f);
    supply_screen_put(6.0f, 1.0f);
    feed_now();
    for (int i = 0; i < 8; ++i) {
        yge_pages.tick(0.25f);
    }
    CHECK(!yge_panel_next_cmd(&c));
}

TEST_CASE(the_tabs_show_the_fields_and_their_access)
{
    session_up();
    connected();
    /* IDENTITY holds no row. */
    CHECK(yge_panel_row_name(0) == NULL);
    /* MODE & BEC: mode, BEC and direction are CHECK; F3C is edited; the
     * BEC's 12V bit and keep mAh are read-only. */
    tab(1);
    CHECK_STR_EQ(yge_panel_row_name(0), "ESC MODE");
    CHECK(yge_panel_row_check(0));
    CHECK(yge_panel_row_live(0));
    CHECK_STR_EQ(yge_panel_row_name(1), "BEC VOLTAGE");
    CHECK_STR_EQ(yge_panel_row_value(1), "7.4V");
    CHECK(yge_panel_row_check(1));
    CHECK_STR_EQ(yge_panel_row_name(2), "BEC UP TO 12V");
    CHECK(!yge_panel_row_live(2));
    CHECK_STR_EQ(yge_panel_row_name(3), "DIRECTION");
    CHECK_STR_EQ(yge_panel_row_value(3), "NORMAL");
    CHECK(yge_panel_row_check(3));
    CHECK(yge_panel_row_live(3));
    CHECK_STR_EQ(yge_panel_row_name(4), "F3C AUTOROTATION");
    CHECK(!yge_panel_row_check(4));
    CHECK(yge_panel_row_live(4));
    CHECK(!yge_panel_row_live(5));
    /* PROTECTION: the current limit in A with its 2 decimals. */
    tab(2);
    CHECK_STR_EQ(yge_panel_row_name(2), "CURRENT LIMIT");
    CHECK_STR_EQ(yge_panel_row_value(2), "120.00A");
    CHECK(yge_panel_row_check(2));
    /* START: startup response is raw. */
    tab(3);
    CHECK_STR_EQ(yge_panel_row_value(0), "5%");
    CHECK_STR_EQ(yge_panel_row_name(5), "STARTUP RESPONSE");
    CHECK(!yge_panel_row_live(5));
    /* GOVERNOR & RPM: pole pairs read-only. */
    tab(4);
    CHECK_STR_EQ(yge_panel_row_name(4), "POLE PAIRS");
    CHECK(!yge_panel_row_live(4));
    /* STICK: microseconds as us. */
    tab(5);
    CHECK_STR_EQ(yge_panel_row_value(0), "1100us");
    CHECK_STR_EQ(yge_panel_row_value(1), "800us");
    /* RAW: every row read-only; the telemetry type never written. */
    tab(6);
    for (int i = 0; yge_panel_row_name(i) != NULL; ++i) {
        CHECK(!yge_panel_row_live(i));
    }
    CHECK_STR_EQ(yge_panel_row_name(2), "TELEMETRY TYPE");
    CHECK_STR_EQ(yge_panel_row_value(3), "0x0000");
    CHECK(yge_panel_row_name(8) == NULL);
}

TEST_CASE(a_choice_lists_the_writable_set_and_a_number_is_held_to_it)
{
    session_up();
    connected();
    /* MOTOR TIMING: 0 to 6 and 17 to 19; 16 is never offered. */
    const int t = row_named(3, "MOTOR TIMING");

    CHECK(t >= 0);
    yg_tap(yge_panel_value_rect(t));
    CHECK_EQ(ygp_s.ch_count, 10);
    CHECK_STR_EQ(yge_panel_choice_label(1), "0°");
    CHECK_STR_EQ(yge_panel_choice_label(7), "AUTO EFFICIENT");
    choose("12°");
    CHECK_STR_EQ(yge_panel_row_value(t), "12°");
    CHECK_EQ(yge_panel_changed(), 1);
    /* PINION TEETH: 1 to 255; 0 and 256 refused, the draft kept. */
    const int p = row_named(4, "PINION TEETH");

    yg_tap(yge_panel_value_rect(p));
    kp_type("0");
    CHECK(yge_panel_keypad()->open);
    yg_tap(ui_keypad_key_rect(yge_panel_keypad(), UI_KEY_CANCEL));
    yg_tap(yge_panel_value_rect(p));
    kp_type("256");
    CHECK(yge_panel_keypad()->open);
    yg_tap(ui_keypad_key_rect(yge_panel_keypad(), UI_KEY_CANCEL));
    yg_tap(yge_panel_value_rect(p));
    kp_type("255");
    CHECK(!yge_panel_keypad()->open);
    CHECK_STR_EQ(yge_panel_row_value(p), "255");
    /* BEC VOLTAGE: 5.5V to 8.4V while bit 3 of index 26 is clear. */
    const int b = row_named(1, "BEC VOLTAGE");

    yg_tap(yge_panel_value_rect(b));
    kp_type("8.5");
    CHECK(yge_panel_keypad()->open);
    yg_tap(ui_keypad_key_rect(yge_panel_keypad(), UI_KEY_CANCEL));
    yg_tap(yge_panel_value_rect(b));
    kp_type("8.4");
    CHECK_STR_EQ(yge_panel_row_value(b), "8.4V");
    CHECK_EQ(yge_panel_changed(), 3);
}

TEST_CASE(the_check_line_names_each_check_field_and_its_new_value)
{
    session_up();
    connected();
    CHECK_STR_EQ(yge_panel_check_line(), "");
    /* An edited field is no CHECK: the line stays empty. */
    const int f = row_named(1, "F3C AUTOROTATION");

    yg_tap(yge_panel_value_rect(f));
    choose("ON");
    CHECK_STR_EQ(yge_panel_check_line(), "");
    const int b = row_named(1, "BEC VOLTAGE");

    yg_tap(yge_panel_value_rect(b));
    kp_type("8.4");
    CHECK_STR_EQ(yge_panel_check_line(), "CHECK BEC VOLTAGE 8.4V");
    const int d = row_named(1, "DIRECTION");

    yg_tap(yge_panel_value_rect(d));
    choose("REVERSED");
    CHECK_STR_EQ(yge_panel_check_line(), "CHECK BEC VOLTAGE 8.4V, +1 MORE");
    /* A value set back to the one read is no change. */
    yg_tap(yge_panel_value_rect(b));
    kp_type("7.4");
    CHECK_STR_EQ(yge_panel_check_line(), "CHECK DIRECTION REVERSED");
    CHECK_EQ(yge_panel_changed(), 1);
    /* Both flag bits are one index. */
    yg_tap(yge_panel_value_rect(d));
    choose("NORMAL");
    CHECK_EQ(yge_panel_changed(), 1);
    CHECK_STR_EQ(yge_panel_check_line(), "");
}

TEST_CASE(the_row_dims_until_the_iomcu_reports_the_page)
{
    size_t n = 0u;
    const prog_reg_row_t *rows = prog_registry(&n);
    const prog_reg_row_t *yge = NULL;
    size_t at = 0u;
    char out[48];

    for (size_t i = 0u; i < n; ++i) {
        if (rows[i].page_set == &yge_pages) {
            yge = &rows[i];
            at = i;
        }
    }
    CHECK(yge != NULL);
    if (yge == NULL) {
        return;
    }
    CHECK_EQ(yge->device, PROG_DEV_MOTOR);
    CHECK_EQ(yge->needs_capability, LINK_CAP_YGE);
    CHECK_STR_EQ(ui_tr(yge->name), "YGE ESCs");
    CHECK_STR_EQ(ui_tr(yge->group), "TELEMETRY WIRE");
    CHECK_STR_EQ(ui_tr(yge->transport), "OPENYGE");
    CHECK_EQ(prog_reg_state(rows, n, at, LINK_CAP_PROGRAM), PROG_ROW_NO_CAP);
    CHECK(prog_reg_detail(rows, n, at, LINK_CAP_PROGRAM, out, sizeof(out)));
    CHECK_STR_EQ(out, "IOMCU HAS NO YGE");
    CHECK_EQ(prog_reg_state(rows, n, at, LINK_CAP_PROGRAM | LINK_CAP_YGE),
             PROG_ROW_LIVE);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ui_tr(yge->group), "TELEMETRIEKABEL");
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(connect_names_the_port_and_takes_a_hold_of_2000ms);
    RUN(the_port_and_the_power_are_chosen_on_the_page);
    RUN(the_values_follow_supply_until_typed_and_boot_wait_keeps_its_limits);
    RUN(a_supply_value_that_moves_under_the_hold_ends_it);
    RUN(the_tabs_show_the_fields_and_their_access);
    RUN(a_choice_lists_the_writable_set_and_a_number_is_held_to_it);
    RUN(the_check_line_names_each_check_field_and_its_new_value);
    RUN(the_row_dims_until_the_iomcu_reports_the_page);
    return test_summary("yge_panel");
}
