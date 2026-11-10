/**
 * @file test_servo_drive_supply.c
 * @brief The SET row and the plot of LIVE, SWEEP and RUDDER
 *        (servo_drive_ready.c): SET V and SET A open SUPPLY's keypad inside
 *        its OUTPUT LIMITS, a typed set point passes SUPPLY's question and
 *        the HV warning as on SUPPLY, a programming port holding the
 *        supply refuses them, the rbmod carrier's programming port shows
 *        its fixed 5V and plots the PROG 5V, and a new reading repaints the
 *        plot and the line under the card alone.
 *
 * Cases:
 * - live_sweep_and_rudder_show_set_v_set_a_and_the_plot
 * - a_tap_on_set_v_or_set_a_opens_the_keypad_inside_supplys_limits
 * - a_set_point_past_6v_waits_for_the_hv_hold
 * - on_a_live_output_a_typed_set_point_asks_supplys_question_armed_or_not
 * - a_programming_port_holding_the_supply_refuses_set_and_names_it
 * - on_the_carrier_port_the_row_reads_5v_fixed_and_the_plot_the_prog_5v
 * - a_new_reading_repaints_only_the_plot_and_the_line
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "out_bind.h"
#include "prog5v_box.h"
#include "servo_ready_int.h"
#include "ui_board.h"

/** @brief SET V's and SET A's middle row on @p fn. */
static int set_y(servo_fn_t fn)
{
    return (fn == SERVO_FN_RUDDER) ? SV_SET_RUD_Y : SV_SET_Y;
}

/** @brief SERVO fresh on LIVE, SUPPLY's voltage cap at @p v_max, the PD
 *         mini answering at 5.0V and 1.0A with its output @p on. */
static void fresh_pd(float v_max, bool on)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    fresh();
    ui_router_set_alert(NULL);
    settings_set(SET_SUPPLY_V_MAX, v_max);
    supply_screen_limits_changed();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(on);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = on;
    st.v = on ? 5.0f : 0.0f;
    st.set_v = 5.0f;
    st.set_i = 1.0f;
    servo_screen_supply(&st);
    go(SERVO_FN_LIVE);
    scr->tick(0.025f);
    supply_cmd_t c;
    while (supply_cmd(&c)) { }
}

/** @brief Whether @p r in buffer 0 holds a pixel of colour @p col. */
static bool has_color(gfx_rect_t r, gfx_color_t col)
{
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            if (fb[(size_t)y * W + x] == col) {
                return true;
            }
        }
    }
    return false;
}

/* Each of the three shows SET V and SET A, 80 x 48px, under its READY
 * rows, and the plot to the column's end; the READY rows keep their tags.
 * KST shows none of them. */
TEST_CASE(live_sweep_and_rudder_show_set_v_set_a_and_the_plot)
{
    fresh_pd(8.4f, false);
    static const servo_fn_t k_fn[] = { SERVO_FN_LIVE, SERVO_FN_SWEEP,
                                       SERVO_FN_RUDDER };
    for (size_t f = 0; f < sizeof(k_fn) / sizeof(k_fn[0]); ++f) {
        go(k_fn[f]);
        scr->tick(0.025f);
        const gfx_rect_t v = drive_set_rect(0);
        const gfx_rect_t a = drive_set_rect(1);
        const gfx_rect_t p = drive_plot_rect();
        CHECK_EQ(v.w, 80);
        CHECK_EQ(v.h, UI_TAP_MIN);
        CHECK(gfx_rect_contains(v, SV_SET_V_X, set_y(k_fn[f])));
        CHECK(gfx_rect_contains(a, SV_SET_A_X, set_y(k_fn[f])));
        CHECK(gfx_rect_contains(p, SV_PLOT_X, set_y(k_fn[f])));
        CHECK_EQ(p.w, 180);
        CHECK_EQ(p.x + p.w, SV_RC_X + SV_RC_W);
        /* Under the last row, above the type pill. */
        const gfx_rect_t last = ready_row_rect(ready_count() - 1);
        CHECK(v.y >= last.y + last.h);
        CHECK(v.y + v.h <= drive_tag_rect().y);
        CHECK(ready_tag_rect(0).x + ready_tag_rect(0).w <= SV_RC_X + SV_RC_W);
        scr->render(&cv, 0);
        CHECK(has_color(v, ui_theme_color(UI_C_VOLT)));     /* 5.00V */
        CHECK(has_color(a, ui_theme_color(UI_C_CURR)));     /* 1.00A */
        CHECK(has_color(v, ui_theme_color(UI_C_PANEL_SUNK)));
    }
    go(SERVO_FN_KST);
    CHECK_EQ(drive_set_rect(0).w, 0);
    CHECK_EQ(drive_plot_rect().w, 0);
}

/* SET V opens SUPPLY's VOLTAGE keypad from SUPPLY's caps narrowed by its
 * OUTPUT LIMITS, SET A its CURRENT LIMIT keypad.  A value past the cap is
 * refused, CANCEL keeps the set point, OK puts it on SUPPLY: one set point
 * on both screens. */
TEST_CASE(a_tap_on_set_v_or_set_a_opens_the_keypad_inside_supplys_limits)
{
    static const servo_fn_t k_fn[] = { SERVO_FN_LIVE, SERVO_FN_SWEEP,
                                       SERVO_FN_RUDDER };
    for (size_t f = 0; f < sizeof(k_fn) / sizeof(k_fn[0]); ++f) {
        fresh_pd(5.8f, false);
        go(k_fn[f]);
        scr->tick(0.025f);
        const supply_caps_t caps = supply_screen_caps();
        CHECK(fabsf(caps.v_max - 5.8f) < 1e-4f);
        tap(SV_SET_V_X, set_y(k_fn[f]));
        const ui_keypad_t *kp = servo_screen_keypad();
        CHECK(kp->open);
        CHECK_STR_EQ(kp->title, TR(SUP_VOLTAGE));
        CHECK_STR_EQ(kp->unit, "V");
        CHECK(fabsf(kp->min - caps.v_min) < 1e-4f);
        CHECK(fabsf(kp->max - caps.v_max) < 1e-4f);
        keys("5.9");                              /* past the cap */
        CHECK(kp->open);
        key(UI_KEY_CANCEL);
        CHECK(!kp->open);
        CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
        tap(SV_SET_V_X, set_y(k_fn[f]));
        keys("5.5");
        CHECK(!kp->open);
        CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
        tap(SV_SET_A_X, set_y(k_fn[f]));
        CHECK(kp->open);
        CHECK_STR_EQ(kp->title, TR(SUP_CURRENT_LIMIT));
        CHECK_STR_EQ(kp->unit, "A");
        CHECK(fabsf(kp->max - caps.i_max) < 1e-4f);
        keys("0.5");
        CHECK(fabsf(supply_screen_set_i() - 0.5f) < 1e-4f);
        CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
    }
}

/* A voltage raised past a standard servo's 6.0V waits for the HV warning's
 * 2s hold, as OUTPUT ON past it does; CANCEL keeps 5.0V. */
TEST_CASE(a_set_point_past_6v_waits_for_the_hv_hold)
{
    fresh_pd(8.4f, false);
    tap(SV_SET_V_X, SV_SET_Y);
    keys("7.2");
    CHECK(servo_screen_output_held());          /* the warning */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    tap(WARN_APPLY_X, WARN_Y);                  /* a tap is not the hold */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    hold_apply(2.3f);
    CHECK(!servo_screen_output_held());
    CHECK(fabsf(supply_screen_set_v() - 7.2f) < 1e-4f);
    supply_cmd_t c;
    while (supply_cmd(&c)) {
        CHECK(!c.on);                           /* a set point, no ON */
    }

    fresh_pd(8.4f, false);
    tap(SV_SET_V_X, SV_SET_Y);
    keys("6.5");
    CHECK(servo_screen_output_held());
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK(!servo_screen_output_held());
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* With the output live and SETTINGS, CONFIRM WHILE ON, KEYPAD on, a typed
 * set point waits for SUPPLY's question, ARMED or SAFE: APPLY puts it on,
 * CANCEL keeps the old one.  With the setting off it goes on at once. */
TEST_CASE(on_a_live_output_a_typed_set_point_asks_supplys_question_armed_or_not)
{
    for (int armed = 0; armed < 2; ++armed) {
        fresh_pd(8.4f, true);
        settings_set(SET_SUPPLY_CONFIRM_KEYS, 1.0f);
        servo_screen_set_armed(armed != 0);
        scr->tick(0.025f);
        tap(SV_SET_V_X, SV_SET_Y);
        CHECK(servo_screen_keypad()->open);
        keys("5.6");
        CHECK(servo_screen_output_held());      /* the question */
        CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
        tap(WARN_CANCEL_X, WARN_Y);
        CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
        tap(SV_SET_V_X, SV_SET_Y);
        keys("5.6");
        tap(WARN_APPLY_X, WARN_Y);              /* APPLY, a tap */
        CHECK(!servo_screen_output_held());
        CHECK(fabsf(supply_screen_set_v() - 5.6f) < 1e-4f);

        settings_set(SET_SUPPLY_CONFIRM_KEYS, 0.0f);
        tap(SV_SET_A_X, SV_SET_Y);
        keys("0.8");
        CHECK(!servo_screen_output_held());
        CHECK(fabsf(supply_screen_set_i() - 0.8f) < 1e-4f);
    }
}

/* While a programming port holds the PD mini the values are dim, a tap
 * opens no keypad and the alert band names the port's hold. */
TEST_CASE(a_programming_port_holding_the_supply_refuses_set_and_names_it)
{
    fresh_pd(8.4f, false);
    supply_screen_set_port_held(true);
    scr->tick(0.025f);
    tap(SV_SET_V_X, SV_SET_Y);
    CHECK(!servo_screen_keypad()->open);
    CHECK(ui_router_alert() != NULL);
    CHECK_STR_EQ(ui_router_alert(), TR(SUP_HELD_REFUSED));
    ui_router_set_alert(NULL);
    tap(SV_SET_A_X, SV_SET_Y);
    CHECK(!servo_screen_keypad()->open);
    CHECK_STR_EQ(ui_router_alert(), TR(SUP_HELD_REFUSED));
    scr->render(&cv, 0);
    CHECK(!has_color(drive_set_rect(0), ui_theme_color(UI_C_VOLT)));
    CHECK(has_color(drive_set_rect(0), ui_theme_color(UI_C_TEXT_FAINT)));
    supply_screen_set_port_held(false);
    scr->tick(0.025f);
    tap(SV_SET_V_X, SV_SET_Y);
    CHECK(servo_screen_keypad()->open);
    key(UI_KEY_CANCEL);
}

/** @brief Buffer 0 drawn, and whether the SET row reads PROG 5V over
 *         `5V FIXED` and nothing else. */
static bool row_reads_fixed(void)
{
    scr->render(&cv, 0);
    want_fresh();
    const gfx_rect_t r = drive_set_row_rect();
    gfx_fill_rect(&want_cv, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    gfx_text_in(&want_cv, (gfx_rect_t){ r.x, (int16_t)(r.y + 4), r.w, 16 },
                "PROG 5V", UI_FONT_FINE, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_LEFT);
    gfx_text_in(&want_cv, (gfx_rect_t){ r.x, (int16_t)(r.y + 24), r.w, 20 },
                TR(SV_P5_FIXED), &gfx_font_10x20, ui_theme_color(UI_C_TEXT),
                1, GFX_ALIGN_LEFT);
    return same_in(fb, want_fb, r.x, r.y, r.w, r.h);
}

/* On the rbmod carrier's programming port the servo runs from the fixed
 * PROG 5V: the SET row reads PROG 5V, 5V FIXED and takes no press, and the
 * plot and the line under the card are the channel's that watches the
 * PROG 5V, a point per window.  OUTPUTS again: SET V and the PD mini. */
TEST_CASE(on_the_carrier_port_the_row_reads_5v_fixed_and_the_plot_the_prog_5v)
{
    fresh_pd(8.4f, false);
    prog5v_box_publish(3u, 21u, (uint8_t)PROG5V_HELD_NONE);    /* ON */
    ui_board_set(OUTBIND_BOARD_RBMOD);
    servo_screen_set_drive(1u, 13u, false);
    scr->tick(0.025f);
    CHECK_EQ(drive_set_rect(0).w, 0);
    CHECK_EQ(drive_set_rect(1).w, 0);
    CHECK(row_reads_fixed());
    ui_text_set_language(UI_LANG_DE);
    servo_invalidate();
    CHECK(row_reads_fixed());
    ui_text_set_language(UI_LANG_EN);
    servo_invalidate();
    tap(SV_SET_V_X, SV_SET_Y);
    CHECK(!servo_screen_keypad()->open);
    CHECK(ui_router_alert() == NULL);

    /* Ten windows of 5.02V and 0.120A, then one of 0.240A: the line reads
     * the last, the plot repaints with it and nothing else does. */
    for (uint16_t n = 1u; n <= 10u; ++n) {
        prog5v_box_reading(true, 5020u, 120, n);
        scr->tick(0.05f);
    }
    CHECK(rail_line_reads("PROG 5V", NULL, "5.02V", "0.120A"));
    scr->render(&cv, 0);
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(was);
    memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
    prog5v_box_reading(true, 5020u, 240, 11u);
    scr->tick(0.05f);
    scr->render(&cv, 0);
    const gfx_rect_t p = drive_plot_rect();
    const gfx_rect_t l = ready_rail_rect();
    int in_plot = 0, in_line = 0, outside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] == was[(size_t)y * W + x]) {
                continue;
            }
            if (gfx_rect_contains(p, x, y)) {
                ++in_plot;
            } else if (gfx_rect_contains(l, x, y)) {
                ++in_line;
            } else {
                ++outside;
            }
        }
    }
    free(was);
    CHECK(in_plot > 0);
    CHECK(in_line > 0);
    CHECK_EQ(outside, 0);
    CHECK(rail_line_reads("PROG 5V", NULL, "5.02V", "0.240A"));
    /* The same window again is no new point: nothing repaints. */
    scr->render(&cv, 0);
    scr->render(&cv, 1);
    prog5v_box_reading(true, 5020u, 240, 11u);
    scr->tick(0.05f);
    gfx_color_t *same = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(same);
    memcpy(same, fb, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, same, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(same);

    /* OUTPUTS: the PD mini feeds the servo, SET V and SET A come back. */
    servo_screen_set_drive(0u, 0u, true);
    scr->tick(0.025f);
    CHECK_EQ(drive_set_rect(0).w, 80);
    ui_board_set(OUTBIND_BOARD_UNKNOWN);
    prog5v_box_reset();
}

/* On RUDDER, the PD mini the rail's meter: a sample repaints the plot and
 * the line under the card, each clipped, and nothing else. */
TEST_CASE(a_new_reading_repaints_only_the_plot_and_the_line)
{
    fresh_pd(8.4f, true);
    go(SERVO_FN_RUDDER);
    for (int i = 0; i < 20; ++i) {
        rail_supply(5.0f, 0.2f + 0.01f * (float)i);
        scr->tick(0.05f);
    }
    scr->render(&cv, 0);
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(was);
    memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
    rail_supply(5.0f, 1.5f);
    scr->render(&cv, 0);
    const gfx_rect_t p = drive_plot_rect();
    const gfx_rect_t l = ready_rail_rect();
    int in_plot = 0, in_line = 0, outside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] == was[(size_t)y * W + x]) {
                continue;
            }
            if (gfx_rect_contains(p, x, y)) {
                ++in_plot;
            } else if (gfx_rect_contains(l, x, y)) {
                ++in_line;
            } else {
                ++outside;
            }
        }
    }
    free(was);
    CHECK(in_plot > 0);
    CHECK(in_line > 0);
    CHECK_EQ(outside, 0);
    CHECK(line_supply("5.00V", "1.500A"));
}

int main(void)
{
    RUN(live_sweep_and_rudder_show_set_v_set_a_and_the_plot);
    RUN(a_tap_on_set_v_or_set_a_opens_the_keypad_inside_supplys_limits);
    RUN(a_set_point_past_6v_waits_for_the_hv_hold);
    RUN(on_a_live_output_a_typed_set_point_asks_supplys_question_armed_or_not);
    RUN(a_programming_port_holding_the_supply_refuses_set_and_names_it);
    RUN(on_the_carrier_port_the_row_reads_5v_fixed_and_the_plot_the_prog_5v);
    RUN(a_new_reading_repaints_only_the_plot_and_the_line);
    return test_summary("servo_drive_supply");
}
