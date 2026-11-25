/*
 * The stages of programming for YGE ESCs' screens (yge_panel*.c) on the bench
 * of yge_panel_rig.h: a stop parks the line and leaves CLEAR STOP and PIN
 * BACK; ABORT acts on the press with another finger down; a link that goes
 * closes the editors and the pages; another writer's pin is released through
 * a page held 2s; BY HAND waits for POWER IS ON; a frame at rest changes no
 * revision; and every text of the session fits its field in English and
 * German.
 *
 * SPDX-License-Identifier: MIT
 */
#include "gfx.h"
#include "yge_panel_rig.h"

/* The bench run until no flow is out. */
static void settle(void)
{
    for (int i = 0; i < 8000 && yge_prog_busy(yge_panel_model()); ++i) {
        bench_run(5u);
    }
    bench_run(300u);
}

TEST_CASE(a_stop_parks_the_line_and_leaves_clear_and_pin_back)
{
    session_up();
    connected();
    yge_link_stop(&g_l);
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_STOPPED);
    CHECK_ALL_OFF();
    CHECK(yge_panel_holds_port());
    /* No WRITE and no hold under a stop. */
    CHECK_EQ(yge_panel_hold_rect().w, 0);
    /* CLEAR STOP, then the pin held with nothing going: ENTER AGAIN would
     * switch the supply on, and after a stop only PIN BACK is offered. */
    yg_tap(yge_panel_action_rect(0));
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_PIN_HELD);
    CHECK_EQ(yge_panel_hold_rect().w, 0);
    yg_tap(yge_panel_action_rect(2));
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_CONNECT);
    CHECK(!yge_panel_holds_port());
}

TEST_CASE(the_panels_stop_leaves_pin_back_only)
{
    session_up();
    connected();
    g_stopped = true;
    yge_link_stop(&g_l);
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_STOPPED);
    /* CLEAR STOP is the page's: under the panel's stop it is not offered. */
    const int before = g_commands;

    yg_tap(yge_panel_action_rect(0));
    settle();
    CHECK_EQ(g_commands, before);
    yg_tap(yge_panel_action_rect(1));
    settle();
    CHECK_EQ(g_cmd_op[before], LINK_YGE_OP_CLOSE);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_CONNECT);
    g_stopped = false;
}

TEST_CASE(abort_acts_on_the_press_with_another_finger_down)
{
    session_up();
    ygp_s.boot_ms = 20000u;
    yg_hold_ms(2100u);
    bench_run(500u);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_ENTERING);
    /* A finger elsewhere, then ABORT pressed: the cut needs no release. */
    touch_event_t other = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 2, .x = 600, .y = 200,
                                       .strength = 40 } };
    const gfx_rect_t a = yge_panel_action_rect(0);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 3, .x = (int16_t)(a.x + 4),
                                   .y = (int16_t)(a.y + 4), .strength = 40 } };

    yge_pages.event(&other);
    CHECK(yge_pages.safe_at(e.point.x, e.point.y));
    yge_pages.event(&e);
    settle();
    CHECK_ALL_OFF();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_PIN_HELD);
    CHECK_STR_EQ(yge_panel_message(),
                 "ABORT ended the operation: the pin remains held.");
    /* No ABORT where none shows. */
    CHECK(!yge_pages.safe_at(e.point.x, e.point.y));
}

TEST_CASE(link_loss_closes_the_editors_and_the_pages)
{
    session_up();
    connected();
    const int b = row_named(1, "BEC VOLTAGE");

    yg_tap(yge_panel_value_rect(b));
    CHECK(yge_panel_keypad()->open);
    g_link_down = true;
    bench_run(10u);
    CHECK(!yge_panel_keypad()->open);
    g_link_down = false;
    bench_run(10u);
    /* A list and a page go the same way. */
    const int d = row_named(1, "DIRECTION");

    yg_tap(yge_panel_value_rect(d));
    CHECK_EQ(ygp_s.ch_what, YGP_CH_FIELD);
    g_link_down = true;
    bench_run(10u);
    CHECK_EQ(ygp_s.ch_what, YGP_CH_NONE);
    g_link_down = false;
    bench_run(10u);
    yg_tap(yge_panel_value_rect(d));
    choose("REVERSED");
    yg_tap(yge_panel_action_rect(3));
    CHECK_EQ(yge_panel_page(), YGP_PAGE_LEAVE);
    g_link_down = true;
    bench_run(10u);
    CHECK_EQ(yge_panel_page(), YGP_PAGE_NONE);
    CHECK_STR_EQ(yge_panel_message(), "No response from the IOmcu.");
    g_link_down = false;
}

TEST_CASE(another_writers_pin_is_released_by_a_page_held_2s)
{
    session_up();
    /* The bench console's session holds port 1; the page is read every
     * 500ms while this panel holds none. */
    open_port(0u);
    bench_run(600u);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_OTHER);
    CHECK(yge_panel_holds_port());
    yg_tap(yge_panel_action_rect(0));
    CHECK_EQ(yge_panel_page(), YGP_PAGE_RELEASE);
    CHECK_STR_EQ(yge_panel_page_line(1), "This pin: PORT 1 GPIO 13.");
    yg_hold_ms(2100u);
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_CONNECT);
    CHECK(!yge_panel_holds_port());
}

TEST_CASE(by_hand_waits_for_power_is_on)
{
    session_up();
    yg_tap(yge_panel_value_rect(YGP_C_POWER));
    choose("BY HAND");
    yg_hold_ms(2100u);
    for (int i = 0; i < 400 && yge_panel_stage() != YGE_PANEL_HAND; ++i) {
        bench_run(5u);
    }
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_HAND);
    CHECK(yge_panel_by_hand());
    CHECK_EQ(g.ons, 0);
    g.battery = true;
    yg_tap(yge_panel_action_rect(1));
    for (int i = 0; i < 6000 && yge_panel_stage() != YGE_PANEL_SESSION; ++i) {
        bench_run(5u);
    }
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_SESSION);
    CHECK_EQ(g.ons, 0);
    CHECK(yge_panel_by_hand());
}

TEST_CASE(a_frame_at_rest_changes_no_revision)
{
    session_up();
    connected();
    bench_run(500u);
    const uint32_t rev = yge_panel_revision();

    /* Time goes on, the page is read again and again: nothing shown moves. */
    bench_run(2000u);
    CHECK_EQ(yge_panel_revision(), rev);
    /* A press on a row moves only the hold's revision, not the page's. */
    tab(1);
    const uint32_t rev2 = yge_panel_revision();
    const gfx_rect_t r = yge_panel_value_rect(0);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + 4),
                                   .y = (int16_t)(r.y + 4), .strength = 40 } };

    yge_pages.event(&e);
    CHECK_EQ(yge_panel_revision(), rev2);
    e.type = TOUCH_EVENT_UP;
    e.point.x = 0;
    e.point.y = 0;
    yge_pages.event(&e);
}

/* Every UI_TEXT the session draws in a field: its cells in each language
 * against the field's, in the session's 10px face. */
TEST_CASE(the_texts_fit_their_fields_in_english_and_german)
{
    static const struct {
        ui_text_id_t id;
        int px;          /* the field's width */
    } k_field[] = {
        { TX_YG_LEAVE, 243 },        { TX_YG_ROW_PORT, 265 },
        { TX_YG_ROW_BOOT, 265 },     { TX_YG_CONNECT_TITLE, 880 },
        { TX_KP_HOLD_LEAVE_DROP, 420 }, { TX_YG_HELD_1, 487 },
        { TX_YG_STRIP_HAND, 300 },   { TX_YG_TARGET_PORT, 232 },
        { TX_EW_WRITE_N, 243 },      { TX_EW_READ, 243 },
        { TX_EW_FILES, 243 },        { TX_EW_HOLD_CONNECT_BTN, 420 },
    };

    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (size_t i = 0u; i < sizeof(k_field) / sizeof(k_field[0]); ++i) {
            const char *s = ui_tr(k_field[i].id);

            CHECK(gfx_text_cells(s) * 10 <= k_field[i].px - 8);
        }
    }
    /* Every row's name and value on every tab, in German too. */
    session_up();
    connected();
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (int t = 1; t < YGP_TABS; ++t) {
            tab(t);
            for (int i = 0; yge_panel_row_name(i) != NULL; ++i) {
                CHECK(gfx_text_cells(yge_panel_row_name(i)) * 10
                      <= 497 - YGP_VAL_W - 8);
                CHECK(gfx_text_cells(yge_panel_row_value(i)) * 10
                      <= YGP_VAL_W - 8);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_stop_parks_the_line_and_leaves_clear_and_pin_back);
    RUN(the_panels_stop_leaves_pin_back_only);
    RUN(abort_acts_on_the_press_with_another_finger_down);
    RUN(link_loss_closes_the_editors_and_the_pages);
    RUN(another_writers_pin_is_released_by_a_page_held_2s);
    RUN(by_hand_waits_for_power_is_on);
    RUN(a_frame_at_rest_changes_no_revision);
    RUN(the_texts_fit_their_fields_in_english_and_german);
    return test_summary("yge_panel_stages");
}
