/**
 * @file test_dial_nav.c
 * @brief The M5Stack Dial navigates the panel (ui_focus.h): the focus moves
 *        and wraps, a SELECT opens what a tap opens, SET keeps and cancels,
 *        nothing shows while the Dial is off, nothing moves while ARMED,
 *        and the ring costs no repaint.
 *
 * Cases:
 * - the_focus_moves_and_wraps_on_the_overview
 * - a_select_on_a_tile_opens_its_screen
 * - setup_hub_and_list_move_wrap_and_come_back
 * - a_number_row_is_set_on_its_keypad_and_kept
 * - a_number_set_is_cancelled_by_a_hold
 * - an_on_off_row_steps_and_cancels
 * - a_choice_list_takes_the_focus_and_a_pick
 * - the_servo_hub_moves_and_opens_a_tile
 * - nothing_shows_while_the_dial_is_off
 * - armed_moves_no_focus
 * - the_dial_shows_nav_and_value
 * - a_ring_move_costs_its_pixels_and_restores_the_screen
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_rig.h"

#include "dial_panel.h"
#include "overview_screen.h"
#include "servo_screen.h"
#include "ui_focus.h"
#include "ui_keypad.h"

/** @brief The ring's colour as ui_focus.c draws it. */
static gfx_color_t ring_color(void)
{
    return (gfx_color_t)(ui_theme_color(UI_C_ACCENT) ^ 0x0001u);
}

/** @brief Pixels of the ring's colour in the body. */
static long ring_pixels(void)
{
    long n = 0;
    const gfx_color_t rc = ring_color();
    for (int i = UI_BAND_H * W; i < W * H; ++i) {
        n += (s_fb[i] == rc) ? 1 : 0;
    }
    return n;
}

/** @brief A frame into buffer @p b. */
static void frame(int b)
{
    ui_router_render(&s_c, b);
}

/** @brief The bench at its defaults on @p id, the Dial linked, SAFE. */
static void on(ui_screen_id_t id)
{
    fresh_hub();
    ui_router_goto(id);
    ui_focus_set_link(true, false);
    ui_text_set_language(UI_LANG_EN);
}

/**
 * @brief Turn until the focused item's label is @p label.
 * @param label The label.
 * @return true when found within one turn round the list.
 */
static bool focus_on(const char *label)
{
    const int n = ui_focus_count();
    for (int i = 0; i <= n; ++i) {
        if (strcmp(ui_focus_label(ui_focus_index()), label) == 0) {
            return true;
        }
        (void)ui_focus_turn(1);
    }
    return false;
}

/** @brief The Dial's model of this frame. */
static dial_model_t model(void)
{
    static dial_builder_t b;
    dial_model_in_t in;
    memset(&in, 0, sizeof(in));
    in.route = ui_router_current();
    in.title = ui_router_title(in.route);
    in.thr_tenths = 10u;
    in.srv_tenths = 10u;
    dial_model_t m;
    dial_model_build(&b, &in, &m);
    return m;
}

TEST_CASE(the_focus_moves_and_wraps_on_the_overview)
{
    on(SCREEN_OVERVIEW);
    CHECK(ui_focus_shown());
    const int n = ui_focus_count();
    CHECK_EQ(n, 8);
    CHECK_EQ(ui_focus_index(), 0);
    CHECK_STR_EQ(ui_focus_label(0), "MOTOR & ESC");
    CHECK(ui_focus_turn(1));
    CHECK_EQ(ui_focus_index(), 1);
    CHECK(ui_focus_turn(-2));
    CHECK_EQ(ui_focus_index(), n - 1);   /* wrapped back over the start */
    CHECK(ui_focus_turn(1));
    CHECK_EQ(ui_focus_index(), 0);       /* and forward over the end */
    CHECK(ui_focus_turn(3 * n + 2));
    CHECK_EQ(ui_focus_index(), 2);
    CHECK(ui_focus_turn(-(3 * n + 2)));
    CHECK_EQ(ui_focus_index(), 0);
    /* HOLD on the overview: no home tag, nothing goes. */
    CHECK(ui_focus_hold());
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(a_select_on_a_tile_opens_its_screen)
{
    for (int slot = 0; slot < 16; ++slot) {
        const ui_screen_id_t want = overview_screen_tile(slot);
        if (want == SCREEN_COUNT) {
            continue;
        }
        on(SCREEN_OVERVIEW);
        CHECK(focus_on(overview_screen_tile_name(slot)));
        CHECK(ui_focus_press());
        CHECK_EQ(ui_router_current(), want);
    }
}

TEST_CASE(setup_hub_and_list_move_wrap_and_come_back)
{
    on(SCREEN_SETUP);
    CHECK(ui_focus_shown());
    CHECK_EQ(ui_focus_count(), settings_hub_tiles() + 0);
    /* The tile a tap would open: PANEL's list. */
    CHECK(focus_on(ui_setting_category(SET_CAT_APP)));
    const int tile = ui_focus_index();
    CHECK(ui_focus_press());
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_APP);
    CHECK(ui_focus_shown());
    /* The list starts on its first row; the strip's RESET is last. */
    CHECK_EQ(ui_focus_index(), 0);
    const int n = ui_focus_count();
    CHECK(n > 4);
    CHECK(ui_focus_turn(-1));
    CHECK_EQ(ui_focus_index(), n - 1);
    CHECK(ui_focus_turn(1));
    CHECK_EQ(ui_focus_index(), 0);
    /* A row out of sight scrolls into it. */
    CHECK(ui_focus_turn(n - 2));
    CHECK(settings_screen_scroll(NULL) > 0);
    /* HOLD is the home tag: the tiles, on the tile left. */
    CHECK(ui_focus_hold());
    CHECK_EQ(settings_screen_category(), -1);
    CHECK(ui_focus_shown());
    CHECK_EQ(ui_focus_index(), tile);
    /* And once more: the overview. */
    CHECK(ui_focus_hold());
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/** @brief SETUP's PANEL list, the focus on DIM AFTER. */
static void on_dim_after(void)
{
    on(SCREEN_SETUP);
    open_category(SET_CAT_APP);
    CHECK(ui_focus_shown());
    CHECK(focus_on(ui_setting_label(SET_DIM_AFTER)));
}

TEST_CASE(a_number_row_is_set_on_its_keypad_and_kept)
{
    on_dim_after();
    CHECK_EQ(settings_get_int(SET_DIM_AFTER), 0);
    CHECK(ui_focus_press());
    const ui_keypad_t *k = settings_screen_keypad();
    CHECK(k->open);
    CHECK(ui_focus_shown());
    CHECK(ui_focus_turn(2));
    CHECK_NEAR(ui_keypad_value(k), 10.0f, 0.001f);
    ui_focus_value_t v;
    CHECK(ui_focus_value(&v));
    CHECK_NEAR(v.was, 0.0f, 0.001f);
    CHECK_NEAR(v.step, 5.0f, 0.001f);
    CHECK(!v.limit);
    /* Past the top: held at 60 and the limit said. */
    CHECK(ui_focus_turn(40));
    CHECK_NEAR(ui_keypad_value(k), 60.0f, 0.001f);
    CHECK(ui_focus_value(&v));
    CHECK(v.limit);
    CHECK(ui_focus_turn(-10));
    CHECK_NEAR(ui_keypad_value(k), 10.0f, 0.001f);
    /* PRESS is OK: the value kept, the keypad closed, the focus on the
     * row it came from. */
    CHECK(ui_focus_press());
    CHECK(!k->open);
    CHECK_EQ(settings_get_int(SET_DIM_AFTER), 10);
    CHECK_STR_EQ(ui_focus_label(ui_focus_index()),
                 ui_setting_label(SET_DIM_AFTER));
    CHECK(!ui_focus_value(&v));
}

TEST_CASE(a_number_set_is_cancelled_by_a_hold)
{
    on_dim_after();
    settings_set(SET_DIM_AFTER, 15.0f);
    CHECK(ui_focus_press());
    const ui_keypad_t *k = settings_screen_keypad();
    CHECK(k->open);
    CHECK(ui_focus_turn(3));
    CHECK_NEAR(ui_keypad_value(k), 30.0f, 0.001f);
    /* HOLD is CANCEL: the old value stays, the list stays. */
    CHECK(ui_focus_hold());
    CHECK(!k->open);
    CHECK_EQ(settings_get_int(SET_DIM_AFTER), 15);
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_APP);
}

TEST_CASE(an_on_off_row_steps_and_cancels)
{
    on(SCREEN_SETUP);
    open_category(SET_CAT_APP);
    CHECK(focus_on(ui_setting_label(SET_PLOT_PAN)));
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 0);
    ui_focus_value_t v;
    CHECK(!ui_focus_value(&v));
    CHECK(ui_focus_press());               /* step SET */
    CHECK(ui_focus_value(&v));
    CHECK(ui_focus_turn(1));
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 1);
    /* An on/off cycles, as its ">" key does on the panel. */
    CHECK(ui_focus_turn(1));
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 0);
    CHECK(ui_focus_turn(3));
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 1);
    CHECK(ui_focus_value(&v));
    CHECK_NEAR(v.value, 1.0f, 0.001f);
    CHECK_NEAR(v.was, 0.0f, 0.001f);
    CHECK(ui_focus_press());               /* KEEP */
    CHECK(!ui_focus_value(&v));
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 1);
    /* Again, then CANCEL: back to where SET began, after any count of
     * taps. */
    CHECK(ui_focus_press());
    CHECK(ui_focus_turn(-1));
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 0);
    CHECK(ui_focus_turn(-2));
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 0);
    CHECK(ui_focus_hold());
    CHECK_EQ(settings_get_int(SET_PLOT_PAN), 1);
    CHECK(!ui_focus_value(&v));
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_APP);
    /* The turn moves the focus again. */
    const int at = ui_focus_index();
    CHECK(ui_focus_turn(1));
    CHECK_EQ(ui_focus_index(), at + 1);
}

TEST_CASE(a_choice_list_takes_the_focus_and_a_pick)
{
    on(SCREEN_SETUP);
    dial_view_t dv;
    memset(&dv, 0, sizeof(dv));
    dv.link = DIAL_LINK_NONE;
    dv.scan_done = true;
    dv.found_n = 3;
    for (int i = 0; i < 3; ++i) {
        snprintf(dv.found[i].name, sizeof(dv.found[i].name), "DIAL-%d", i);
        dv.found[i].rssi = -50;
    }
    wireless_pane_set_dial(&dv);
    open_wireless(SET_DIAL_EN);
    dial_cmd_t cmd;
    while (wireless_pane_take_dial(&cmd)) {
    }
    CHECK(ui_focus_shown());
    CHECK(focus_on(ui_tr(TX_DIAL_ROW_BIND)));
    CHECK(ui_focus_press());
    /* The list's three rows, the focus on the first. */
    CHECK_EQ(ui_focus_count(), 3);
    CHECK_EQ(ui_focus_index(), 0);
    CHECK(ui_focus_turn(-1));
    CHECK_EQ(ui_focus_index(), 2);
    CHECK(ui_focus_press());
    CHECK(wireless_pane_take_dial(&cmd));
    CHECK_EQ(cmd.kind, DIAL_CMD_BIND);
    CHECK_EQ(cmd.arg, 2);
    /* Open again: HOLD closes the list and picks nothing. */
    CHECK(focus_on(ui_tr(TX_DIAL_ROW_BIND)));
    CHECK(ui_focus_press());
    CHECK_EQ(ui_focus_count(), 3);
    CHECK(ui_focus_hold());
    CHECK(!wireless_pane_take_dial(&cmd));
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_WIRELESS);
    CHECK_STR_EQ(ui_focus_label(ui_focus_index()), ui_tr(TX_DIAL_ROW_BIND));
}

TEST_CASE(the_servo_hub_moves_and_opens_a_tile)
{
    on(SCREEN_SERVO);
    CHECK(ui_focus_shown());
    CHECK_EQ(ui_focus_count(), SERVO_HUB_TILES + 1);
    CHECK(ui_focus_turn(-1));
    CHECK_STR_EQ(ui_focus_label(ui_focus_index()), TR(FOOT_SETTINGS));
    CHECK(ui_focus_turn(1));
    CHECK(ui_focus_press());
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIVE);
    /* A bench screen offers no focus: the Dial drives the horn there. */
    CHECK(!ui_focus_shown());
}

TEST_CASE(nothing_shows_while_the_dial_is_off)
{
    on(SCREEN_SETUP);
    frame(0);
    CHECK(ring_pixels() > 0);
    ui_focus_set_link(false, false);
    CHECK(!ui_focus_shown());
    CHECK(!ui_focus_turn(1));
    CHECK(!ui_focus_press());
    CHECK(!ui_focus_hold());
    CHECK_EQ(settings_screen_category(), -1);
    frame(0);
    CHECK_EQ(ring_pixels(), 0);
    /* A fresh panel with the Dial never linked: no ring at all. */
    fresh_hub();
    frame(0);
    frame(1);
    CHECK_EQ(ring_pixels(), 0);
    dial_input_t in = { .detents = 3, .presses = 1, .holds = 1 };
    CHECK(!dial_navigate(SCREEN_SETUP, &in, true, true));
    CHECK_EQ(settings_screen_category(), -1);
}

TEST_CASE(armed_moves_no_focus)
{
    on(SCREEN_SETUP);
    CHECK(ui_focus_turn(2));
    CHECK_EQ(ui_focus_index(), 2);
    ui_focus_set_link(true, true);
    CHECK(!ui_focus_shown());
    CHECK(!ui_focus_turn(1));
    CHECK(!ui_focus_press());
    CHECK_EQ(settings_screen_category(), -1);
    dial_input_t in = { .detents = 1, .presses = 1 };
    CHECK(!dial_navigate(SCREEN_SETUP, &in, true, true));
    frame(0);
    CHECK_EQ(ring_pixels(), 0);
    /* SAFE again: the list starts afresh. */
    ui_focus_set_link(true, false);
    CHECK(ui_focus_shown());
    CHECK_EQ(ui_focus_index(), 0);
    /* A bench screen keeps the knob's path, SAFE or ARMED: the Dial's
     * input is never the focus layer's there. */
    ui_router_goto(SCREEN_SERVO_LIVE);
    CHECK(!dial_navigate(SCREEN_SERVO_LIVE, &in, true, true));
    ui_focus_set_link(true, true);
    CHECK(!dial_navigate(SCREEN_SERVO_LIVE, &in, true, true));
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIVE);
}

TEST_CASE(the_dial_shows_nav_and_value)
{
    on(SCREEN_OVERVIEW);
    dial_model_t m = model();
    CHECK_EQ(m.face, DK_FACE_HOME);
    CHECK(m.has_nav);
    CHECK_EQ(m.flags & DK_SCR_NAVIGATES, DK_SCR_NAVIGATES);
    CHECK_STR_EQ(m.nav[DK_SLOT_FOCUS].item, "MOTOR & ESC");
    CHECK_STR_EQ(m.nav[DK_SLOT_PREV].item, "");
    CHECK_EQ(m.nav[DK_SLOT_FOCUS].count, 8);

    on_dim_after();
    m = model();
    CHECK_EQ(m.face, DK_FACE_NAV);
    CHECK_STR_EQ(m.title, ui_router_title(SCREEN_SETUP));
    CHECK_STR_EQ(m.nav[DK_SLOT_FOCUS].item, ui_setting_label(SET_DIM_AFTER));
    CHECK_EQ(m.nav[DK_SLOT_FOCUS].index, ui_focus_index());
    CHECK(ui_focus_press());
    CHECK(ui_focus_turn(1));
    m = model();
    CHECK_EQ(m.face, DK_FACE_VALUE);
    CHECK(m.has_value);
    CHECK_STR_EQ(m.meta.unit, "min");
    CHECK_EQ(m.meta.min, 0);
    CHECK_EQ(m.meta.max, 60);
    CHECK_EQ(m.meta.step, 5);
    CHECK_EQ(m.value.value, 5);
    CHECK_EQ(m.value.was, 0);
    CHECK_EQ(m.value.flags, DK_VAL_SET);

    /* Off: the old faces. */
    ui_focus_set_link(false, false);
    m = model();
    CHECK_EQ(m.face, DK_FACE_OTHER);
    CHECK(!m.has_nav);
    CHECK_EQ(m.flags, 0);
}

TEST_CASE(a_ring_move_costs_its_pixels_and_restores_the_screen)
{
    static gfx_color_t ref[W * H];
    on(SCREEN_OVERVIEW);
    ui_focus_set_link(false, false);
    frame(0);
    memcpy(ref, s_fb, sizeof(ref));
    ui_focus_set_link(true, false);
    frame(0);
    const uint32_t first = ui_focus_ring_writes();
    CHECK(first > 0u);
    /* An idle frame writes nothing. */
    frame(0);
    CHECK_EQ(ui_focus_ring_writes(), 0u);
    /* A detent: the old ring back and the new one drawn, nothing else.
     * Two tiles' rings, 2px wide, are far from a repaint of the body. */
    const gfx_rect_t a = overview_screen_tile_rect(0);
    const gfx_rect_t b = overview_screen_tile_rect(1);
    const uint32_t ring_a = (uint32_t)(4 * (a.w + a.h));
    const uint32_t ring_b = (uint32_t)(4 * (b.w + b.h));
    CHECK(ui_focus_turn(1));
    frame(0);
    CHECK(ui_focus_ring_writes() <= ring_a + ring_b);
    CHECK(ui_focus_ring_writes() * 50u < (uint32_t)(W * UI_BODY_H));
    /* Everything but the new ring is the screen as drawn without one. */
    long off = 0;
    const gfx_color_t rc = ring_color();
    for (int y = UI_BAND_H; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int i = y * W + x;
            if (s_fb[i] != ref[i] && s_fb[i] != rc) {
                ++off;
            }
        }
    }
    CHECK_EQ(off, 0);
    /* Off again: the screen exactly as before. */
    ui_focus_set_link(false, false);
    frame(0);
    CHECK(memcmp(s_fb + UI_BAND_H * W, ref + UI_BAND_H * W,
                 sizeof(gfx_color_t) * (size_t)(W * UI_BODY_H)) == 0);
}

int main(void)
{
    RUN(the_focus_moves_and_wraps_on_the_overview);
    RUN(a_select_on_a_tile_opens_its_screen);
    RUN(setup_hub_and_list_move_wrap_and_come_back);
    RUN(a_number_row_is_set_on_its_keypad_and_kept);
    RUN(a_number_set_is_cancelled_by_a_hold);
    RUN(an_on_off_row_steps_and_cancels);
    RUN(a_choice_list_takes_the_focus_and_a_pick);
    RUN(the_servo_hub_moves_and_opens_a_tile);
    RUN(nothing_shows_while_the_dial_is_off);
    RUN(armed_moves_no_focus);
    RUN(the_dial_shows_nav_and_value);
    RUN(a_ring_move_costs_its_pixels_and_restores_the_screen);
    return test_summary("dial_nav");
}
