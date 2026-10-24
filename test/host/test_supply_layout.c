/**
 * @file test_supply_layout.c
 * @brief SUPPLY at the 1024 x 600 sizes: OUTPUT OFF is a finger tall,
 *        acts where it is drawn and lies above the alert band; every
 *        control a finger taps is UI_TAP_MIN tall from its first row to its
 *        last; a finger that misses OUTPUT OFF to the left changes no set
 *        point; no text of LIVE, TABLE, SETTINGS, the keypad or the question
 *        reaches the alert band, in English or German at the longest texts.
 *
 * Cases:
 * - output_off_acts_where_it_is_drawn_and_is_a_finger_tall
 * - every_tapped_control_takes_a_finger_from_edge_to_edge
 * - a_finger_that_misses_output_off_to_the_left_changes_no_set_point
 * - no_text_lies_under_the_alert_band
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen_rig.h"
#include "ui_slider.h"

/** @brief The body row 4px over the alert band: no text reaches it. */
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

/*
 * On, OUTPUT OFF is the one stop of the screen: UI_TAP_MIN or taller,
 * where its danger-red fill is drawn, at the CONTROL card's foot, and its
 * bottom row 4px or more above the alert band.  Off, OUTPUT ON is no stop.
 */
TEST_CASE(output_off_acts_where_it_is_drawn_and_is_a_finger_tall)
{
    fresh();
    CHECK_EQ(stop_bounds().w, 0);
    supply_screen_set_output(true);
    const gfx_rect_t r = stop_bounds();
    CHECK_EQ(r.x, SU_RC_X);
    CHECK_EQ(r.w, SU_RC_W);
    CHECK_EQ(r.y, SU_OUT_T);
    CHECK_EQ(r.h, SU_OUT_H);
    CHECK(r.h >= UI_TAP_MIN && r.w >= UI_TAP_MIN);
    CHECK(r.y + r.h <= TEXT_FLOOR);

    /* Drawn where it acts: past the flash, the fill across its middle row
     * from its left edge to its right, and down a column top to bottom. */
    tick_for(10);
    supply_invalidate();
    scr->render(&cv, 0);
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    int lo = -1, hi = -1;
    for (int x = r.x - 20; x < r.x + r.w + 10; ++x) {
        if (fb[(size_t)(r.y + r.h / 2) * W + x] == red) {
            lo = (lo < 0) ? x : lo;
            hi = x;
        }
    }
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

/** @brief Whether a tap at (@p x, @p y) on LIVE opens the keypad; it is
 *         closed again.
 *  @return true when it opened */
static bool opens_keypad(int x, int y)
{
    tap(x, y);
    const bool open = scr->back();
    return open;
}

/*
 * The step keys, the values and SETTINGS' values take a press from their
 * first row to their last, SU_CTRL_H (UI_TAP_MIN) apart, and none a row
 * outside them.
 */
TEST_CASE(every_tapped_control_takes_a_finger_from_edge_to_edge)
{
    CHECK(SU_CTRL_H >= UI_TAP_MIN);
    const int rows[2] = { SU_V_ROW_Y, SU_I_ROW_Y };
    for (int k = 0; k < 2; ++k) {
        const int t = rows[k] - SU_CTRL_H / 2;
        const int b = t + SU_CTRL_H - 1;
        fresh();
        float (*get)(void) = (k == 0) ? supply_screen_set_v
                                      : supply_screen_set_i;
        const float v0 = get();
        tap(SU_UP_X, t);
        CHECK_NEAR(get(), v0 + 0.1f, 1e-4f);
        tap(SU_UP_X, b);
        CHECK_NEAR(get(), v0 + 0.2f, 1e-4f);
        tap(SU_DOWN_X, t);
        tap(SU_DOWN_X, b);
        CHECK_NEAR(get(), v0, 1e-4f);
        /* A row outside: the label's line over it, the gap under it. */
        tap(SU_UP_X, t - 1);
        tap(SU_UP_X, b + 1);
        CHECK_NEAR(get(), v0, 1e-4f);
        /* The value: its top and bottom rows and its right column open the
         * keypad; the row over it does not. */
        CHECK(opens_keypad(SU_VAL_X, t));
        CHECK(opens_keypad(SU_VAL_X, b));
        CHECK(opens_keypad(SU_VAL_R, rows[k]));
        CHECK(!opens_keypad(SU_VAL_X, t - 1));
        CHECK(!opens_keypad(SU_VAL_R + 1, rows[k]));
        CHECK(b + UI_SLIDER_OVERHANG + 2 <= TEXT_FLOOR);
    }

    /* SETTINGS: VOLTAGE MAX's value from its first row to its last. */
    fresh();
    tap(SET_TAB_X, STRIP_Y);
    const int t = SU_VMAX_Y - UI_TAP_MIN / 2;
    CHECK(opens_keypad(SU_ROW_L_X, t));
    CHECK(opens_keypad(SU_ROW_L_X, t + UI_TAP_MIN - 1));
    CHECK(!opens_keypad(SU_ROW_L_X, t - 1));
    CHECK(!opens_keypad(SU_ROW_L_X, t + UI_TAP_MIN));
}

/*
 * Between the values and OUTPUT OFF lies the rail's margin, on the rows
 * they share: with the output on, a tap anywhere in it commands nothing,
 * opens nothing and is no stop, while the value's right column opens the
 * keypad and changes no set point.
 */
TEST_CASE(a_finger_that_misses_output_off_to_the_left_changes_no_set_point)
{
    fresh();
    supply_screen_set_output(true);
    drain();
    const float v = supply_screen_set_v();
    const float i = supply_screen_set_i();
    const int y = SU_I_ROW_Y;                   /* the limit's and OUTPUT's */
    CHECK(y >= SU_OUT_T && y < SU_OUT_T + SU_OUT_H);
    CHECK(scr->safe_at(SU_RC_X, y));
    CHECK(!scr->safe_at(SU_RC_X - 1, y));
    for (int x = SU_VAL_R + 1; x < SU_RC_X; ++x) {
        tap(x, y);
        CHECK(!supply_screen_poll_cmd(NULL));
        CHECK(!scr->back());
    }
    CHECK(opens_keypad(SU_VAL_R, y));
    CHECK_NEAR(supply_screen_set_v(), v, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), i, 1e-4f);
    CHECK(supply_screen_output_on());
}

/** @brief The pixels of the left column and the rail from TEXT_FLOOR down
 *         to the body's foot, clear of the cards' edges and chamfers, that
 *         are not the panels' fill.  Drawn whole first.
 *  @return the count */
static int ink_under_the_alert_band(void)
{
    supply_invalidate();
    scr->render(&cv, 0);
    const gfx_color_t fill = ui_theme_color(UI_C_PANEL);
    int n = 0;
    for (int y = TEXT_FLOOR; y < UI_BODY_H - UI_MARGIN - 12; ++y) {
        for (int x = UI_MARGIN + 2; x < UI_MARGIN + 620 - 2; ++x) {
            n += fb[(size_t)y * W + x] != fill;
        }
        for (int x = SU_RC_X - 10; x < SU_RC_X + SU_RC_W + 10; ++x) {
            n += fb[(size_t)y * W + x] != fill;
        }
    }
    return n;
}

/*
 * In both languages nothing is drawn under the alert band: LIVE on the
 * model with the output on, PLOT and TABLE; a trip; a programming port
 * holding the supply; SETTINGS with the PD mini (RESET PD MINI) and
 * SETUP's change not saved, the longest save line; the keypad; and the
 * question with its two-line foot.
 */
TEST_CASE(no_text_lies_under_the_alert_band)
{
    const float cells = settings_get(SET_PACK_CELLS);
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh();
        supply_screen_set_model(true);
        supply_screen_set_output(true);
        supply_state_t st = reading(6.0f, 2.0f);
        st.output = true;
        st.mode = SUPPLY_MODE_CC;
        for (int k = 0; k < 40; ++k) {
            supply_screen_push(&st);
        }
        tick_for(10);
        CHECK_EQ(ink_under_the_alert_band(), 0);
        tap(SU_TABLE_TAB_X, SU_PANE_Y);
        CHECK_EQ(ink_under_the_alert_band(), 0);
        tap(SU_PLOT_TAB_X, SU_PANE_Y);

        /* +0.1V on the live output: the question. */
        tap(SU_UP_X, SU_V_ROW_Y);
        CHECK_EQ(ink_under_the_alert_band(), 0);
        tap(DISCARD_X, ASK_Y);
        tap(CARD_X, CARD_V_Y);                    /* the keypad */
        CHECK_EQ(ink_under_the_alert_band(), 0);
        CHECK(scr->back());

        supply_screen_set_port_held(true);
        CHECK_EQ(ink_under_the_alert_band(), 0);
        supply_screen_set_port_held(false);

        st.output = false;
        st.trip = SUPPLY_TRIP_VOLTAGE;
        supply_screen_set_output(false);
        supply_screen_push(&st);
        CHECK_EQ(ink_under_the_alert_band(), 0);

        supply_screen_set_model(false);
        tap(SET_TAB_X, STRIP_Y);
        settings_set(SET_PACK_CELLS, cells + 1.0f);   /* SETUP's, unsaved */
        CHECK(settings_dirty());
        CHECK_EQ(ink_under_the_alert_band(), 0);
        /* The save line is there, above the band. */
        int ink = 0;
        for (int y = SU_SAVE_LINE_Y; y < SU_SAVE_LINE_Y + 20; ++y) {
            for (int x = UI_MARGIN + 10; x < UI_MARGIN + 400; ++x) {
                ink += fb[(size_t)y * W + x] != ui_theme_color(UI_C_PANEL);
            }
        }
        CHECK(ink > 100);
        settings_set(SET_PACK_CELLS, cells);          /* as stored */
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(output_off_acts_where_it_is_drawn_and_is_a_finger_tall);
    RUN(every_tapped_control_takes_a_finger_from_edge_to_edge);
    RUN(a_finger_that_misses_output_off_to_the_left_changes_no_set_point);
    RUN(no_text_lies_under_the_alert_band);
    return test_summary("supply_layout");
}
