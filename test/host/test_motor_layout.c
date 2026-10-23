/**
 * @file test_motor_layout.c
 * @brief MOTOR & ESC at the 1024 x 600 sizes: DISARM is a finger tall,
 *        acts where it is drawn and lies above the alert band, apart from
 *        the other controls; no text of LIVE, TABLE or SETTINGS reaches
 *        the alert band, in English or German at the longest texts.
 *
 * Cases:
 * - disarm_acts_where_it_is_drawn_and_is_a_finger_tall
 * - the_controls_stand_apart_and_above_the_alert_band
 * - a_finger_that_misses_disarm_to_the_left_moves_no_throttle
 * - no_text_lies_under_the_alert_band
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_rig.h"

#include "link_pages.h"

/** @brief The body row 4 px over the alert band: no text reaches it. */
#define TEXT_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)

/** @brief The rectangle of the body that safe_at() names as a stop,
 *         scanned every pixel; empty when none.
 *  @return its bounds */
static gfx_rect_t stop_bounds(void)
{
    int x0 = W, y0 = UI_BODY_H, x1 = -1, y1 = -1, n = 0;
    for (int y = 0; y < UI_BODY_H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!scr->safe_at(x, y)) {
                continue;
            }
            ++n;
            x0 = (x < x0) ? x : x0;
            y0 = (y < y0) ? y : y0;
            x1 = (x > x1) ? x : x1;
            y1 = (y > y1) ? y : y1;
        }
    }
    if (n == 0) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    /* Solid: every point inside the bounds is the stop. */
    CHECK_EQ(n, (x1 - x0 + 1) * (y1 - y0 + 1));
    return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0 + 1),
                         (int16_t)(y1 - y0 + 1) };
}

/** @brief The extent of colour @p col along row @p y, x from @p xa to
 *         @p xb; @p lo and @p hi get the first and last x, -1 for none. */
static void row_extent(int y, int xa, int xb, gfx_color_t col, int *lo,
                       int *hi)
{
    *lo = -1;
    *hi = -1;
    for (int x = xa; x < xb; ++x) {
        if (fb[(size_t)y * W + x] == col) {
            *lo = (*lo < 0) ? x : *lo;
            *hi = x;
        }
    }
}

/*
 * Armed, DISARM is the one stop of the screen: UI_TAP_MIN or taller, where
 * its danger-red fill is drawn, at the CONTROL card's foot, and its bottom
 * row 4 px or more above the alert band.  Disarmed, ARM is no stop.
 */
TEST_CASE(disarm_acts_where_it_is_drawn_and_is_a_finger_tall)
{
    fresh();
    CHECK_EQ(stop_bounds().w, 0);
    motor_screen_set_armed(true);
    const gfx_rect_t r = stop_bounds();
    CHECK_EQ(r.x, MO_RC_X);
    CHECK_EQ(r.w, MO_RC_W);
    CHECK_EQ(r.y, MO_ARM_T);
    CHECK_EQ(r.h, MO_ARM_H);
    CHECK(r.h >= UI_TAP_MIN && r.w >= UI_TAP_MIN);
    CHECK(r.y + r.h <= TEXT_FLOOR);

    /* Drawn where it acts: the fill across its middle row from its left
     * edge to its right, and down its middle column from top to bottom. */
    tick_for(10);                         /* past the arm's flash */
    scr->render(&cv, 0);
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    int lo = 0, hi = 0;
    row_extent(r.y + r.h / 2, r.x - 20, r.x + r.w + 10, red, &lo, &hi);
    CHECK_EQ(lo, r.x);
    CHECK_EQ(hi, r.x + r.w - 1);
    int top = -1, bottom = -1;
    for (int y = r.y - 20; y < r.y + r.h + 20; ++y) {
        if (fb[(size_t)y * W + r.x + r.w / 4] == red) {
            top = (top < 0) ? y : top;
            bottom = y;
        }
    }
    CHECK_EQ(top, r.y);
    CHECK_EQ(bottom, r.y + r.h - 1);
}

/*
 * Every control of the CONTROL card and the throttle keys stand apart from
 * DISARM: a press on RESET PEAKS, the TARGET row or a step key is no stop,
 * and every one of them ends above the alert band.
 */
TEST_CASE(the_controls_stand_apart_and_above_the_alert_band)
{
    fresh();
    motor_screen_set_armed(true);
    /* RESET PEAKS' bottom row and the TARGET row's: no stop. */
    CHECK(!scr->safe_at(MO_RESET_X, MO_RESET_Y + UI_TAP_MIN / 2 - 1));
    CHECK(!scr->safe_at(MO_TARGET_X, MO_TARGET_B));
    CHECK(!scr->safe_at(MO_UP_X, MO_TRACK_Y));
    /* RESET PEAKS stops 10 px over DISARM, and takes its press. */
    CHECK(MO_RESET_Y + UI_TAP_MIN / 2 + 10 <= MO_ARM_T);
    tap(MO_RESET_X, MO_RESET_Y + UI_TAP_MIN / 2 - 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_RESET_PEAKS);
    /* The step keys, the hint and the track end above the alert band. */
    CHECK(MO_HINT_Y + 10 <= TEXT_FLOOR);
    CHECK(MO_TRACK_Y + 28 <= TEXT_FLOOR);
}

/*
 * Between +1 and DISARM lies a finger's width, UI_TAP_MIN, on the rows
 * they share: armed, a tap anywhere in it commands nothing and is no stop,
 * while +1's right column steps the throttle.
 */
TEST_CASE(a_finger_that_misses_disarm_to_the_left_moves_no_throttle)
{
    fresh();
    motor_screen_set_armed(true);
    tick_for(10);
    (void)last_cmd();
    const int y = MO_ARM_T + 10;                  /* +1's rows and DISARM's */
    CHECK(y < MO_TRACK_Y + 28);
    CHECK(MO_RC_X - (MO_UP_R + 1) >= UI_TAP_MIN);
    CHECK(scr->safe_at(MO_RC_X, y));
    CHECK(!scr->safe_at(MO_RC_X - 1, y));
    for (int x = MO_UP_R + 1; x < MO_RC_X; ++x) {
        tap(x, y);
        CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    }
    tap(MO_UP_R, y);
    const motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 1.0f, 0.001f);
}

/** @brief The pixels of the cards from TEXT_FLOOR down to the bodies'
 *         foot, clear of the cards' edges and chamfers, that are not the
 *         cards' fill.  Buffer 0 drawn whole first.
 *  @return the count */
static int ink_under_the_alert_band(void)
{
    motor_invalidate();
    scr->render(&cv, 0);
    const gfx_color_t fill = ui_theme_color(UI_C_PANEL);
    int n = 0;
    for (int y = TEXT_FLOOR; y < UI_BODY_H - UI_MARGIN - 12; ++y) {
        for (int x = UI_MARGIN + 2; x < UI_MARGIN + 620 - 2; ++x) {
            n += fb[(size_t)y * W + x] != fill;
        }
        for (int x = MO_RC_X - 10; x < MO_RC_X + MO_RC_W + 10; ++x) {
            n += fb[(size_t)y * W + x] != fill;
        }
    }
    return n;
}

/** @brief A bench whose INA228 clips, with the ESC's own readings: TABLE's
 *         fullest page. */
static void push_sensed(void)
{
    telemetry_sim_t sim;
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    for (int i = 0; i < 60; ++i) {
        telemetry_sim_step(&sim, 80.0f, 0.05f, &b);
        b.flags = (uint16_t)((b.flags & (uint16_t)~LINK_BN_SIMULATED)
                             | LINK_BN_SENSED);
        b.clipped = true;
        bench_state_set_esc(&b, true, b.voltage + 0.18f, true,
                            b.current * 1.12f, false);
        motor_screen_push(&b);
    }
}

/*
 * In both languages nothing is drawn under the alert band: LIVE armed and
 * disarmed (the hint, or ARM FIRST), TABLE at its fullest (the INA228
 * clipping and the ESC's two rows), and SETTINGS armed with SETUP's change
 * not saved (the lock's reason and the longest save line).
 */
TEST_CASE(no_text_lies_under_the_alert_band)
{
    const float mah = settings_get(SET_PACK_MAH);
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh();
        CHECK_EQ(ink_under_the_alert_band(), 0);      /* ARM FIRST */
        motor_screen_set_armed(true);
        tick_for(10);
        push_sensed();
        CHECK_EQ(ink_under_the_alert_band(), 0);      /* the hint */
        tap(MO_TABLE_TAB_X, MO_PANE_Y);
        CHECK_EQ(ink_under_the_alert_band(), 0);
        tap(MO_TAB_X(2), MO_TAB_Y);                   /* SETTINGS */
        CHECK_EQ(motor_screen_task(), 3);
        settings_set(SET_PACK_MAH, mah + 100.0f);     /* not saved */
        CHECK(settings_dirty());
        tick_for(1);
        CHECK_EQ(ink_under_the_alert_band(), 0);
        /* The save line is there, above the band. */
        int ink = 0;
        for (int y = MO_SAVE_LINE_Y; y < MO_SAVE_LINE_Y + 20; ++y) {
            for (int x = UI_MARGIN + 10; x < UI_MARGIN + 400; ++x) {
                ink += fb[(size_t)y * W + x] != ui_theme_color(UI_C_PANEL);
            }
        }
        CHECK(ink > 100);
        motor_screen_set_armed(false);
        settings_set(SET_PACK_MAH, mah);              /* as stored */
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(disarm_acts_where_it_is_drawn_and_is_a_finger_tall);
    RUN(the_controls_stand_apart_and_above_the_alert_band);
    RUN(a_finger_that_misses_disarm_to_the_left_moves_no_throttle);
    RUN(no_text_lies_under_the_alert_band);
    return test_summary("motor_layout");
}
