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
 * - a_select_on_a_dim_panel_wakes_it_and_opens_nothing
 * - a_turn_on_a_dim_panel_wakes_it_and_moves_nothing
 * - a_hold_on_a_dim_armed_bench_disarms
 * - an_awake_panel_takes_the_dial_as_before
 * - a_long_list_keeps_its_strip
 * - a_press_lands_on_the_focused_item_everywhere
 * - a_press_after_a_finger_scrolled_opens_the_focused_row
 * - an_option_list_shows_its_options_range
 * - an_off_gap_takes_detents_alike_in_any_batch
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_rig.h"

#include "dial_panel.h"
#include "motor_screen.h"
#include "screen_dim.h"
#include "settings_sensors.h"
#include "ui_setlist.h"
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

/** @brief Facts of a bench at rest: every one idle. */
static shot_facts_t idle_facts(void)
{
    shot_facts_t f;
    memset(&f, 0, sizeof(f));
    bool *b = (bool *)&f;
    for (size_t i = 0; i < sizeof(f) / sizeof(bool); ++i) {
        b[i] = true;
    }
    return f;
}

/** @brief Dim after's state, dimmed at 0ms. */
static screen_dim_t dimmed(void)
{
    screen_dim_t d;
    screen_dim_init(&d, 0u);
    d.dimmed = true;
    return d;
}

/**
 * @brief One frame of the Dial's input through Dim after and the focus
 *        layer, as knob_apply() takes it.
 * @param d  Dim after's state.
 * @param in The input.
 * @param f  The bench's facts.
 * @return Whether the focus layer took it.
 */
static bool dial_frame(screen_dim_t *d, dial_input_t in,
                       const shot_facts_t *f)
{
    const dial_input_t act = dial_dim_filter(d, &in, ui_router_current(), f,
                                             60000u);
    return dial_navigate(ui_router_current(), &act, true, true);
}

TEST_CASE(a_select_on_a_dim_panel_wakes_it_and_opens_nothing)
{
    on(SCREEN_SETUP);
    CHECK(focus_on(ui_setting_category(SET_CAT_APP)));
    screen_dim_t d = dimmed();
    const shot_facts_t f = idle_facts();
    CHECK(screen_dim_idle(&f));
    dial_input_t in = { .presses = 1 };
    (void)dial_frame(&d, in, &f);
    CHECK(!screen_dim_is_dim(&d));
    CHECK_EQ(settings_screen_category(), -1);
    /* Awake: the next press opens. */
    CHECK(dial_frame(&d, in, &f));
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_APP);
}

TEST_CASE(a_turn_on_a_dim_panel_wakes_it_and_moves_nothing)
{
    on(SCREEN_OVERVIEW);
    CHECK_EQ(ui_focus_index(), 0);
    screen_dim_t d = dimmed();
    const shot_facts_t f = idle_facts();
    dial_input_t in = { .detents = 3 };
    (void)dial_frame(&d, in, &f);
    CHECK(!screen_dim_is_dim(&d));
    CHECK_EQ(ui_focus_index(), 0);
    /* A HOLD on a dim panel goes back nowhere either. */
    on(SCREEN_SETUP);
    d = dimmed();
    in = (dial_input_t){ .holds = 1 };
    (void)dial_frame(&d, in, &f);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(!screen_dim_is_dim(&d));
}

TEST_CASE(a_hold_on_a_dim_armed_bench_disarms)
{
    on(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    motor_cmd_t c;
    while (motor_screen_poll_cmd(&c)) {
    }
    screen_dim_t d;
    /* The facts of an ARMED bench, and once more facts that call it idle:
     * a HOLD on a bench screen acts either way. */
    shot_facts_t armed = idle_facts();
    armed.safe = false;
    const shot_facts_t rest = idle_facts();
    const shot_facts_t *k_facts[2] = { &armed, &rest };
    for (int k = 0; k < 2; ++k) {
        d = dimmed();
        dial_input_t in = { .holds = 1 };
        const dial_input_t act = dial_dim_filter(&d, &in, SCREEN_MOTOR,
                                                 k_facts[k], 60000u);
        CHECK(!screen_dim_is_dim(&d));
        CHECK_EQ(act.holds, 1u);
        CHECK(dial_gestures(SCREEN_MOTOR, &act, true));
        CHECK(motor_screen_poll_cmd(&c));
        CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    }
    /* A PRESS on the ARMED bench acts too: throttle 0%. */
    d = dimmed();
    dial_input_t in = { .presses = 1, .detents = 2 };
    const dial_input_t act = dial_dim_filter(&d, &in, SCREEN_MOTOR, &armed,
                                             60000u);
    CHECK_EQ(act.presses, 1u);
    CHECK_EQ(act.detents, 2);
}

TEST_CASE(an_awake_panel_takes_the_dial_as_before)
{
    on(SCREEN_OVERVIEW);
    screen_dim_t d;
    screen_dim_init(&d, 0u);
    const shot_facts_t f = idle_facts();
    dial_input_t in = { .detents = 1 };
    CHECK(dial_frame(&d, in, &f));
    CHECK_EQ(ui_focus_index(), 1);
    /* The minutes start again from the Dial's input. */
    CHECK_EQ(d.last_ms, 60000u);
    in = (dial_input_t){ .presses = 1 };
    CHECK(dial_frame(&d, in, &f));
    CHECK_EQ(ui_router_current(), overview_screen_tile(1));
    /* No input: nothing woken, nothing dropped. */
    d = dimmed();
    const dial_input_t none = { 0 };
    const dial_input_t act = dial_dim_filter(&d, &none, SCREEN_SETUP, &f,
                                             1000u);
    CHECK(screen_dim_is_dim(&d));
    CHECK_EQ(act.presses, 0u);
}

/** @brief The long list's row: a custom row that does nothing. */
static const ui_setlist_custom_t k_long_row = { .label = TX_DIAL_ROW_BIND };

/** @brief 70 rows. @return the count */
static int long_count(void) { return 70; }

/** @brief Row @p i. @param i The row. @return it */
static ui_setlist_row_t long_row(int i)
{
    return (i >= 0 && i < 70) ? (ui_setlist_row_t)UI_SL_ROW_CUSTOM(&k_long_row)
                              : ui_setlist_table_row(NULL, 0, 0);
}

/** @brief The long list's name. @return it */
static const char *long_name(void) { return "LONG"; }

TEST_CASE(a_long_list_keeps_its_strip)
{
    static const ui_setlist_host_t host = {
        .count = long_count, .row = long_row, .name = long_name,
    };
    on(SCREEN_SETUP);
    static ui_setlist_t sl;
    ui_setlist_init(&sl, &host);
    ui_setlist_open(&sl);
    static ui_focus_list_t out;
    char reset[48];
    snprintf(reset, sizeof(reset), TR(SET_RESET_ONE), "LONG");
    for (int dirty = 0; dirty < 2; ++dirty) {
        if (dirty) {
            settings_set(SET_DIM_AFTER, 15.0f);
        }
        const bool save = ui_setlist_save_offered();
        CHECK_EQ(save, dirty != 0);
        memset(&out, 0, sizeof(out));
        CHECK(ui_focus_add_setlist(&out, &sl));
        CHECK_EQ(out.count, UI_FOCUS_MAX);
        const int last = UI_FOCUS_MAX - 1;
        if (save) {
            CHECK_STR_EQ(out.item[last].label, TR(SET_SAVE));
            CHECK_STR_EQ(out.item[last - 1].label, reset);
            CHECK_EQ(out.item[last - 2].row, UI_FOCUS_MAX - 3);
        } else {
            CHECK_STR_EQ(out.item[last].label, reset);
            CHECK_EQ(out.item[last - 1].row, UI_FOCUS_MAX - 2);
        }
    }
}

/**
 * @brief Every item of the list on top, one by one: the point a SELECT
 *        taps lies on the focused item and where it can be seen, inside
 *        the body.
 * @param what The screen's name, for a failure.
 */
static void every_target_lands(const char *what)
{
    const int n = ui_focus_count();
    CHECK(n > 0);
    for (int i = 0; i < n; ++i) {
        ui_focus_target_t t;
        if (!ui_focus_target(&t)) {
            T_FAIL("%s: item %d has no target", what, i);
            return;
        }
        CHECK(gfx_rect_contains(t.rect, t.x, t.y));
        CHECK(gfx_rect_contains(t.seen, t.x, t.y));
        CHECK(t.seen.x >= 0 && t.seen.y >= 0
              && t.seen.x + t.seen.w <= UI_PANEL_W
              && t.seen.y + t.seen.h <= UI_BODY_H);
        CHECK(ui_focus_turn(1));
    }
}

TEST_CASE(a_press_lands_on_the_focused_item_everywhere)
{
    on(SCREEN_OVERVIEW);
    every_target_lands("overview");
    on(SCREEN_SETUP);
    settings_set(SET_DIM_AFTER, 5.0f);      /* SAVE offered */
    every_target_lands("SETUP tiles");
    open_category(SET_CAT_APP);
    every_target_lands("PANEL");
    open_sensor(SET_INA_SH(0));
    every_target_lands("SENSORS");
    on(SCREEN_SERVO);
    every_target_lands("SERVO");
    /* A choice list. */
    on(SCREEN_SETUP);
    dial_view_t dv;
    memset(&dv, 0, sizeof(dv));
    dv.link = DIAL_LINK_NONE;
    dv.scan_done = true;
    dv.found_n = 2;
    wireless_pane_set_dial(&dv);
    open_wireless(SET_DIAL_EN);
    CHECK(focus_on(ui_tr(TX_DIAL_ROW_BIND)));
    CHECK(ui_focus_press());
    CHECK_EQ(ui_focus_count(), 2);
    every_target_lands("choice list");
    /* The leave warning. */
    on(SCREEN_SETUP);
    open_category(SET_CAT_APP);
    settings_set(SET_DIM_AFTER, 10.0f);
    CHECK(ui_focus_hold());                 /* the tiles */
    CHECK(ui_focus_hold());                 /* leaving SETUP: the warning */
    CHECK_EQ(ui_focus_count(), 3);
    every_target_lands("leave warning");
}

TEST_CASE(a_press_after_a_finger_scrolled_opens_the_focused_row)
{
    on_dim_after();
    settings_set(SET_BRIGHTNESS, 80.0f);    /* dirty: SAVE in the strip */
    CHECK(ui_setlist_save_offered());
    CHECK(settings_screen_scroll(NULL) > 0);
    /* A finger drags the list back to its top, 300px a drag. */
    for (int drag = 0; drag < 4 && settings_screen_scroll(NULL) > 0; ++drag) {
        touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 1, .x = 300,
                                       .y = LIST_Y + 20 } };
        ui_router_event(&e);
        e.type = TOUCH_EVENT_MOVE;
        for (int k = 1; k <= 15; ++k) {
            e.point.y = (int16_t)(LIST_Y + 20 + 20 * k);
            ui_router_event(&e);
        }
        e.type = TOUCH_EVENT_UP;
        ui_router_event(&e);
    }
    CHECK_EQ(settings_screen_scroll(NULL), 0);
    CHECK(!settings_save_asked());
    /* The press opens DIM AFTER's keypad, never SAVE. */
    CHECK(ui_focus_press());
    CHECK(!settings_save_asked());
    CHECK(settings_screen_keypad()->open);
    CHECK_EQ(settings_screen_keypad_setting(), (int)SET_DIM_AFTER);
}

TEST_CASE(an_option_list_shows_its_options_range)
{
    on(SCREEN_SETUP);
    open_category(SET_CAT_APP);
    CHECK(focus_on(ui_setting_label(SET_TIME_ZONE)));
    const setting_def_t *d = settings_def(SET_TIME_ZONE);
    CHECK(d != NULL && d->option_count > 3);
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), 2);
    CHECK(ui_focus_press());
    dial_model_t m = model();
    CHECK_EQ(m.face, DK_FACE_VALUE);
    CHECK_EQ(m.meta.min, 0);
    CHECK_EQ(m.meta.max, (int32_t)d->option_count - 1);
    CHECK_EQ(m.value.value, 2);
    CHECK_EQ(m.value.was, 2);
    CHECK(ui_focus_turn(1));
    m = model();
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), 3);
    CHECK_EQ(m.value.value, 3);
    CHECK_EQ(m.value.was, 2);
    CHECK_EQ(m.meta.max, (int32_t)d->option_count - 1);
}

/**
 * @brief The shunt keypad's value after @p total detents in frames of
 *        @p batch, from @p from.
 * @param k     The keypad, open.
 * @param from  The value it starts on.
 * @param total The detents, signed.
 * @param batch Detents a frame.
 * @return The value.
 */
static float turned(ui_keypad_t *k, float from, int total, int batch)
{
    ui_keypad_set(k, from);
    const int dir = (total > 0) ? 1 : -1;
    int left = abs(total);
    while (left > 0) {
        const int n = (left < batch) ? left : batch;
        CHECK(ui_focus_turn(dir * n));
        left -= n;
    }
    return ui_keypad_value(k);
}

TEST_CASE(an_off_gap_takes_detents_alike_in_any_batch)
{
    on(SCREEN_SETUP);
    open_sensor(SET_INA_SH(0));
    CHECK(focus_on(ui_setting_label(SET_INA_SH(0))));
    CHECK(ui_focus_press());
    ui_keypad_t *k = (ui_keypad_t *)settings_screen_keypad();
    CHECK(k->open);
    /* Up from OFF: one detent to 5.0mΩ, then 0.1mΩ each. */
    CHECK_NEAR(turned(k, 0.0f, 1, 1), 5.0f, 0.001f);
    CHECK_NEAR(turned(k, 0.0f, 2, 1), 5.1f, 0.001f);
    for (int b = 1; b <= 5; ++b) {
        CHECK_NEAR(turned(k, 0.0f, 2, b), 5.1f, 0.001f);
        CHECK_NEAR(turned(k, 0.0f, 7, b), 5.6f, 0.001f);
        /* Down: to 5.0mΩ, one detent to OFF, then held at OFF. */
        CHECK_NEAR(turned(k, 5.3f, -3, b), 5.0f, 0.001f);
        CHECK_NEAR(turned(k, 5.3f, -4, b), 0.0f, 0.001f);
        CHECK_NEAR(turned(k, 5.3f, -7, b), 0.0f, 0.001f);
    }
    /* A long turn down in one frame: 39 steps of 0.1mΩ from 8.9mΩ end on
     * 5.0mΩ, not one detent early on OFF from the sum's rounding error. */
    for (int b = 1; b <= 39; b += 19) {
        CHECK_NEAR(turned(k, 8.9f, -39, b), 5.0f, 0.001f);
        CHECK_NEAR(turned(k, 8.9f, -40, b), 0.0f, 0.001f);
    }
    CHECK_NEAR(turned(k, 0.0f, 40, 40), 8.9f, 0.001f);
    CHECK(ui_focus_hold());
    CHECK(!k->open);
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
    RUN(a_select_on_a_dim_panel_wakes_it_and_opens_nothing);
    RUN(a_turn_on_a_dim_panel_wakes_it_and_moves_nothing);
    RUN(a_hold_on_a_dim_armed_bench_disarms);
    RUN(an_awake_panel_takes_the_dial_as_before);
    RUN(a_long_list_keeps_its_strip);
    RUN(a_press_lands_on_the_focused_item_everywhere);
    RUN(a_press_after_a_finger_scrolled_opens_the_focused_row);
    RUN(an_option_list_shows_its_options_range);
    RUN(an_off_gap_takes_detents_alike_in_any_batch);
    return test_summary("dial_nav");
}
