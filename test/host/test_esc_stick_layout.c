/**
 * @file test_esc_stick_layout.c
 * @brief ESC STICK's layout on the 1024 x 540 px body: tap targets, the
 *        alert band, the fullest pages and the longest labels, through
 *        the session's private seam (esc_stick_panel_int.h).
 *
 * Cases:
 * - every_control_is_a_tap_target_above_the_alert_band
 * - no_two_controls_of_a_page_overlap
 * - the_steppers_keep_a_finger_between_rows
 * - the_fullest_pages_end_above_their_footers
 * - the_longest_labels_fit_their_controls_in_every_language
 * - the_spots_the_suites_tap_lie_on_their_controls
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "esc_stick_panel.h"
#include "ui_text.h"

#include "stick_spots.h"

/* Last: its layout macros have short names (W, H, PAD) and s names the
 * session's state. */
#include "esc_stick_panel_int.h"

/** @brief The alert band's top in body pixels, and where a control at the
 *         foot ends at the latest: 4 px above it. */
#define BAND_TOP (UI_ALERT_Y - UI_BAND_H)
#define FOOT     (BAND_TOP - 4)

/** @brief The session as MOTOR & ESC sets it up: the body under the
 *         output strip. */
static void fresh(void)
{
    esc_stick_pages.init((gfx_rect_t){ 0, PROG_FULL_Y, W,
                                       (int16_t)(H - PROG_FULL_Y) });
}

static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h
           && b.y < a.y + a.h;
}

/** @brief @p r is a tap target: UI_TAP_MIN each way at least, inside the
 *         body's margins, under the output strip and ending 4 px above the
 *         alert band. */
static void target(gfx_rect_t r)
{
    CHECK(r.w >= UI_TAP_MIN);
    CHECK(r.h >= UI_TAP_MIN);
    CHECK(r.x >= PAD);
    CHECK(r.x + r.w <= W - PAD);
    CHECK(r.y >= PROG_FULL_Y);
    CHECK(r.y + r.h <= FOOT);
}

/** @brief TIMING's value of row @p i, its keypad's tap: the value column
 *         over the row's height. */
static gfx_rect_t value_rect(int i)
{
    return (gfx_rect_t){ CTRL_X, (int16_t)(ROW_Y0 + i * ROW_P), CTRL_W,
                         ROW_H };
}

/** @brief A profile's page's and TIMING's controls into @p out: BACK,
 *         TIMING, the page keys, MANUAL INTERVENTION REQUIRED, every row
 *         with its value and keys, DEFAULTS and RUN.  Returns the count. */
static int page_controls(gfx_rect_t *out)
{
    int n = 0;
    out[n++] = pg.back;
    out[n++] = s.st.timing_btn;
    out[n++] = pg.page_up;
    out[n++] = pg.page_dn;
    out[n++] = s.st.hand_btn;
    for (int i = 0; i < ROWS_MAX; ++i) {
        out[n++] = pg.row[i];
        out[n++] = value_rect(i);
        out[n++] = pg.down[i];
        out[n++] = pg.up[i];
    }
    out[n++] = pg.read_btn;
    out[n++] = pg.write_btn;
    return n;
}

/** @brief The list's controls into @p out: BACK, the search field, "^",
 *         "v" and the rows.  Returns the count. */
static int list_controls(gfx_rect_t *out)
{
    int n = 0;
    out[n++] = pg.back;
    out[n++] = s.st.find_box;
    out[n++] = s.st.list_up;
    out[n++] = s.st.list_dn;
    for (int i = 0; i < SP_ROWS; ++i) {
        out[n++] = s.st.rows[i];
    }
    return n;
}

TEST_CASE(every_control_is_a_tap_target_above_the_alert_band)
{
    gfx_rect_t c[64];
    fresh();
    int n = page_controls(c);
    for (int i = 0; i < n; ++i) {
        target(c[i]);
    }
    n = list_controls(c);
    for (int i = 0; i < n; ++i) {
        target(c[i]);
    }
    target(s.st.find_clr);
    target(s.st.hand_res);
    target(s.st.hold_btn);
    target(s.st.steps_btn);
    target(s.st.cancel_btn);
    /* RUN, ABORT and OK end 4 px above the alert band; the panels' HOLD
     * TO RUN, DONE, CANCEL and ABORT end 4 px above the foot row, so a
     * second tap on RUN's place lands on none of them. */
    CHECK_EQ(pg.write_btn.y + pg.write_btn.h, FOOT);
    CHECK_EQ(pg.write_btn.h, UI_BTN_H_TALL);
    CHECK_EQ(s.st.cancel_btn.y + s.st.cancel_btn.h + 4, pg.write_btn.y);
    CHECK_EQ(s.st.hold_btn.y, s.st.cancel_btn.y);
    CHECK(!overlap(s.st.cancel_btn, pg.write_btn));
    CHECK(!overlap(s.st.hold_btn, pg.write_btn));
}

TEST_CASE(no_two_controls_of_a_page_overlap)
{
    gfx_rect_t c[64];
    fresh();
    int n = page_controls(c);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            CHECK(!overlap(c[i], c[j]));
        }
    }
    n = list_controls(c);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            CHECK(!overlap(c[i], c[j]));
        }
    }
    /* X lies inside the search field, at its right end. */
    CHECK_EQ(s.st.find_clr.x + s.st.find_clr.w,
             s.st.find_box.x + s.st.find_box.w);
    CHECK_EQ(s.st.find_clr.y, s.st.find_box.y);
    /* A result's foot row: its MANUAL INTERVENTION REQUIRED left of OK. */
    CHECK(!overlap(s.st.hand_res, pg.write_btn));
    CHECK(s.st.hand_res.x + s.st.hand_res.w + 8 <= pg.write_btn.x);
    /* The panels' three buttons. */
    CHECK(!overlap(s.st.hold_btn, s.st.steps_btn));
    CHECK(!overlap(s.st.steps_btn, s.st.cancel_btn));
}

/* Between two rows' keys, and between a row's name, value and keys, lie
 * 8 px: six rows of 48 px at a 56 px pitch, the parameter card's header
 * moved to the side column's page keys. */
TEST_CASE(the_steppers_keep_a_finger_between_rows)
{
    fresh();
    CHECK_EQ(ROWS_MAX, ST_PARM_ROWS);
    for (int i = 0; i + 1 < ROWS_MAX; ++i) {
        CHECK_EQ(pg.down[i + 1].y - (pg.down[i].y + pg.down[i].h), 8);
        CHECK_EQ(pg.up[i + 1].y - (pg.up[i].y + pg.up[i].h), 8);
        CHECK_EQ(pg.row[i + 1].y - (pg.row[i].y + pg.row[i].h), 8);
    }
    for (int i = 0; i < ROWS_MAX; ++i) {
        CHECK_EQ(value_rect(i).x - (pg.row[i].x + pg.row[i].w), 8);
        CHECK_EQ(pg.down[i].x - (value_rect(i).x + value_rect(i).w), 8);
        CHECK_EQ(pg.up[i].x - (pg.down[i].x + pg.down[i].w), 8);
        /* "+" ends inside the parameter card, 8 px from its edge. */
        CHECK_EQ(pg.up[i].x + pg.up[i].w, PAD + PARM_W - 8);
    }
    /* The side column right of the card, 8 px apart. */
    CHECK_EQ(SIDE_X, PAD + PARM_W + 8);
    CHECK_EQ(SIDE_X + SIDE_W, W - PAD);
    CHECK(pg.page_up.x >= SIDE_X && s.st.hand_btn.x >= SIDE_X);
}

/* The fullest page of each kind ends above what is under it: the list's
 * seven rows above its footer, whose line ends above the alert band; the
 * six parameter rows inside their card; a run's eight lines inside its
 * card and the two help lines above the foot row. */
TEST_CASE(the_fullest_pages_end_above_their_footers)
{
    fresh();
    CHECK_EQ(SP_ROWS, ST_LIST_ROWS);
    const gfx_rect_t last = s.st.rows[SP_ROWS - 1];
    CHECK(last.y + last.h + 4 <= SP_FOOT_Y);
    CHECK(SP_FOOT_Y + 20 <= FOOT);
    CHECK(pg.row[ROWS_MAX - 1].y + ROW_H <= CARD_B - 4);
    CHECK(RUN_L0 + (RUN_LINES - 1) * LINE_P + 20 <= RUN_Y + RUN_H);
    CHECK(HELP_Y + HELP_P + 20 <= BTN_Y);
    CHECK(RUN_Y + RUN_H <= HELP_Y);
    /* The side column holds MANUAL INTERVENTION REQUIRED and four lines
     * of the device and three of the help under it at the least. */
    CHECK(HAND_Y + UI_TAP_MIN + 10 + 7 * LINE_P <= SIDE_B);
    /* The search keyboard docks right of the narrowed rows. */
    CHECK(s.st.rows[0].x + UI_TAP_MIN < SP_DOCK_X - 8);
}

/* The labels drawn in a control fit it with 4 px a side in every
 * language: MANUAL INTERVENTION REQUIRED, TIMING's title beside the page
 * keys, CHANGE with the fullest count, the value column's longest word,
 * BACK, and the foot row's buttons. */
TEST_CASE(the_longest_labels_fit_their_controls_in_every_language)
{
    fresh();
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        const ui_lang_t lang = (ui_lang_t)l;
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_SP_HAND_BTN)) * SP_CELL
              <= s.st.hand_btn.w - 8);
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_SP_HAND_BTN)) * SP_CELL
              <= s.st.hand_res.w - 8);
        CHECK(SIDE_IN + gfx_text_cells(ui_tr_in(lang, TX_SP_TIMING_TITLE))
                            * SP_CELL
              <= pg.page_up.x - 8);
        /* "10-15 of 15" beside CHANGE, 8 px from either. */
        char count[40];
        snprintf(count, sizeof(count), ui_tr_in(lang, TX_PG_RANGE_OF), 10,
                 15, 15);
        CHECK(SIDE_IN + (gfx_text_cells(ui_tr_in(lang, TX_SP_CHANGE))
                         + gfx_text_cells(count)) * SP_CELL + 8
              <= pg.page_up.x - 8);
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_SP_ACTION)) * SP_CELL
              <= CTRL_W);
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_SP_NOTHING_TO_CHOOSE))
                  * SP_CELL
              <= CTRL_W);
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_LOG_BACK)) * SP_CELL
              <= pg.back.w - 8);
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_SP_DEFAULTS)) * SP_CELL
              <= pg.read_btn.w - 8);
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_SP_HOLD_TO_RUN)) * SP_CELL
              <= s.st.hold_btn.w - 8);
        CHECK(gfx_text_cells(ui_tr_in(lang, TX_SP_STEPS_BTN)) * SP_CELL
              <= s.st.steps_btn.w - 8);
    }
}

/* The places the suites and render_ui tap (stick_spots.h) lie on the
 * controls they name. */
TEST_CASE(the_spots_the_suites_tap_lie_on_their_controls)
{
    fresh();
    CHECK(gfx_rect_contains(pg.back, ST_BACK_X, ST_CRUMB_CY));
    CHECK(gfx_rect_contains(s.st.find_box, ST_FIND_X, ST_CRUMB_CY));
    CHECK(!gfx_rect_contains(s.st.find_clr, ST_FIND_X, ST_CRUMB_CY));
    CHECK(gfx_rect_contains(s.st.find_clr, ST_FIND_CLR_X, ST_CRUMB_CY));
    CHECK(gfx_rect_contains(s.st.list_up, ST_LIST_UP_X, ST_CRUMB_CY));
    CHECK(gfx_rect_contains(s.st.list_dn, ST_LIST_DN_X, ST_CRUMB_CY));
    CHECK(gfx_rect_contains(s.st.timing_btn, ST_TIMING_X, ST_CRUMB_CY));
    for (int i = 0; i < SP_ROWS; ++i) {
        CHECK(gfx_rect_contains(s.st.rows[i], 400, ST_ROW_CY(i)));
        CHECK(gfx_rect_contains(s.st.rows[i], 100, ST_ROW_CY(i)));
    }
    for (int i = 0; i < ROWS_MAX; ++i) {
        CHECK(gfx_rect_contains(pg.row[i], ST_PROW_X, ST_STEP_CY(i)));
        CHECK(gfx_rect_contains(value_rect(i), ST_VALUE_X, ST_STEP_CY(i)));
        CHECK(gfx_rect_contains(pg.down[i], ST_STEP_DN_X, ST_STEP_CY(i)));
        CHECK(gfx_rect_contains(pg.up[i], ST_STEP_UP_X, ST_STEP_CY(i)));
    }
    CHECK(gfx_rect_contains(pg.page_up, ST_PAGE_UP_X, ST_PAGE_CY));
    CHECK(gfx_rect_contains(pg.page_dn, ST_PAGE_DN_X, ST_PAGE_CY));
    CHECK(gfx_rect_contains(s.st.hand_btn, ST_HAND_X, ST_HAND_Y));
    CHECK(gfx_rect_contains(pg.read_btn, ST_READ_X, ST_BTN_CY));
    CHECK(gfx_rect_contains(pg.write_btn, ST_WRITE_X, ST_BTN_CY));
    CHECK(gfx_rect_contains(s.st.hand_res, ST_HAND_RES_X, ST_BTN_CY));
    CHECK(gfx_rect_contains(s.st.hold_btn, ST_HOLD_X, ST_PBTN_CY));
    CHECK(gfx_rect_contains(s.st.steps_btn, ST_STEPS_X, ST_PBTN_CY));
    CHECK(gfx_rect_contains(s.st.cancel_btn, ST_CANCEL_X, ST_PBTN_CY));
    CHECK_EQ(ST_DOCK_X, SP_DOCK_X);
    CHECK_EQ(ST_DOCK_Y, SP_ROW_Y0);
}

int main(void)
{
    RUN(every_control_is_a_tap_target_above_the_alert_band);
    RUN(no_two_controls_of_a_page_overlap);
    RUN(the_steppers_keep_a_finger_between_rows);
    RUN(the_fullest_pages_end_above_their_footers);
    RUN(the_longest_labels_fit_their_controls_in_every_language);
    RUN(the_spots_the_suites_tap_lie_on_their_controls);
    return test_summary("esc_stick_layout");
}
