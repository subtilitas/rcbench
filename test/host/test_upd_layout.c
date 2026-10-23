/**
 * @file test_upd_layout.c
 * @brief UPDATES, the start-up reminder, the IOmcu's update question and
 *        progress, PANEL UPDATE, the bus-fault screen and the splash at the
 *        1024 x 600 sizes: every control a finger tall and acting where it
 *        is drawn, nothing under the alert band where the band shows, the
 *        longest German texts and the fullest lists inside their places.
 *
 * Cases:
 * - updates_buttons_act_where_they_are_drawn
 * - the_iomcu_hold_acts_at_its_foot_above_the_alert_band
 * - the_reminder_buttons_act_at_their_edges
 * - the_question_buttons_act_at_their_edges_and_old_taps_miss
 * - no_ink_under_the_alert_band
 * - the_acknowledgement_spans_the_foot_and_acts_at_its_corners
 * - the_fullest_bus_fault_lists_end_above_the_notes_and_the_rule
 * - the_progress_bar_is_36_px_and_the_notes_wrap_in_german
 * - the_splash_rows_are_40_px_and_a_long_detail_stays_on_the_panel
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "busfault_screen.h"
#include "coproc_update_screen.h"
#include "settings.h"
#include "splash_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_screen.h"
#include "wifi_screen.h"

#include "busfault_spots.h"
#include "upd_spots.h"

/** @brief A frame at the panel's rate. */
#define TICK_S     (1.0f / 39.0f)
/** @brief Frames enough to pass a hold. */
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

/** @brief The whole panel, for the screens without the band. */
static gfx_color_t s_px[UI_PANEL_W * UI_PANEL_H];
static gfx_canvas_t s_panel;    /**< the whole panel */
static gfx_canvas_t s_body;     /**< the body under the band */

/* ---------------------------------------------------------------- store */

static float g_kept[SETTING_COUNT];
static bool  g_have_kept;

/** @brief The settings' store: nothing kept. @return false */
static bool mem_load(float *values, int count)
{
    if (!g_have_kept) {
        return false;
    }
    memcpy(values, g_kept, (size_t)count * sizeof(float));
    return true;
}

/** @brief The settings' store: kept in memory. @return true */
static bool mem_save(const float *values, int count)
{
    memcpy(g_kept, values, (size_t)count * sizeof(float));
    g_have_kept = true;
    return true;
}

static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

/* -------------------------------------------------------------- helpers */

/** @brief Canvases, theme, language and settings as at a start. */
static void fresh(ui_lang_t lang)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(lang);
    g_have_kept = false;
    settings_set_store(&k_store);
    settings_init();
    ui_router_init();
    gfx_canvas_init(&s_panel, s_px, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    gfx_canvas_init(&s_body, s_px, UI_PANEL_W, UI_BODY_H, UI_PANEL_W);
    upd_screen()->reset();
    upd_remind_screen()->reset();
    coproc_update_screen()->reset();
    busfault_screen()->reset();
    splash_screen()->reset();
}

/** @brief One touch event at (@p x, @p y) to @p scr. */
static void ev(const ui_screen_t *scr, touch_event_type_t type, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}

/** @brief A tap at (@p x, @p y) on @p scr. */
static void tap(const ui_screen_t *scr, int x, int y)
{
    ev(scr, TOUCH_EVENT_DOWN, x, y);
    ev(scr, TOUCH_EVENT_UP, x, y);
}

/** @brief A press at (@p x, @p y) on @p scr held for a whole hold. */
static void hold(const ui_screen_t *scr, int x, int y)
{
    ev(scr, TOUCH_EVENT_DOWN, x, y);
    for (int i = 0; i < HOLD_TICKS; ++i) {
        scr->tick(TICK_S);
    }
    ev(scr, TOUCH_EVENT_UP, x, y);
}

/** @brief The pixels of rows @p y0 to @p y1 (exclusive), columns @p x0 to
 *         @p x1, that are not the background.
 *  @return the count */
static int ink(int x0, int y0, int x1, int y1)
{
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            n += s_px[(size_t)y * UI_PANEL_W + x] != bg;
        }
    }
    return n;
}

/** @brief UPDATES' view: a check that found @p got, the IOmcu ready. */
static void upd_pose(upd_got_t got, bool capped, upd_why_t block)
{
    upd_view_t v;
    memset(&v, 0, sizeof(v));
    v.have_net = true;
    snprintf(v.net, sizeof(v.net), "%s", "Workshop 2.4");
    snprintf(v.running, sizeof(v.running), "%s", "0.22.0");
    snprintf(v.found, sizeof(v.found), "%s", "0.23.0");
    v.job = UPD_JOB_CHECK;
    v.phase = UPD_PH_ENDED;
    v.got = got;
    v.line_capped = capped;
    v.block = block;
    v.dl_block = block;
    upd_screen_set(&v);
    const upd_cu_view_t cu = {
        .sec = CU_SEC_READY, .known = true, .have_image = true,
        .running = { 0, 21, 2 }, .carried = { 0, 22, 0 },
    };
    upd_screen_set_coproc(&cu);
}

/* ------------------------------------------------------------- UPDATES */

/*
 * The buttons are 460 x 56 px at a 64 px pitch: a press on the edge rows
 * and columns of REMINDER AT START and RELEASES acts; the 8 px between
 * two rows and the column beside them do not.
 */
TEST_CASE(updates_buttons_act_where_they_are_drawn)
{
    fresh(UI_LANG_EN);
    upd_pose(UPD_GOT_CURRENT, false, UPD_WHY_NONE);
    CHECK(UP_BTN_H >= UI_TAP_MIN);
    const ui_screen_t *u = upd_screen();
    const int top = UP_REMIND_Y - UP_BTN_H / 2;
    const bool was = settings_get_bool(SET_UPD_REMIND);
    tap(u, UP_BTN_X, top);
    CHECK(settings_get_bool(SET_UPD_REMIND) != was);
    tap(u, UP_BTN_X, top + UP_BTN_H - 1);
    CHECK(settings_get_bool(SET_UPD_REMIND) == was);
    tap(u, UP_BTN_X - UP_BTN_W / 2, UP_REMIND_Y);           /* left column */
    CHECK(settings_get_bool(SET_UPD_REMIND) != was);
    tap(u, UP_BTN_X + UP_BTN_W / 2 - 1, UP_REMIND_Y);      /* right column */
    CHECK(settings_get_bool(SET_UPD_REMIND) == was);
    /* The gap under it, and the column beside it: nothing. */
    const bool chan = settings_get_bool(SET_UPD_CHANNEL);
    for (int y = top + UP_BTN_H; y < UP_CHAN_Y - UP_BTN_H / 2; ++y) {
        tap(u, UP_BTN_X, y);
    }
    tap(u, UP_BTN_X + UP_BTN_W / 2, UP_REMIND_Y);
    tap(u, UP_BTN_X - UP_BTN_W / 2 - 1, UP_REMIND_Y);
    CHECK(settings_get_bool(SET_UPD_REMIND) == was);
    CHECK(settings_get_bool(SET_UPD_CHANNEL) == chan);
    tap(u, UP_BTN_X, UP_CHAN_Y - UP_BTN_H / 2);
    CHECK(settings_get_bool(SET_UPD_CHANNEL) != chan);
    /* CHECK, and CANCEL in its place while a check runs, is the top row:
     * far above the alert band. */
    CHECK(UP_CHECK_Y + UP_BTN_H / 2 <= UP_FLOOR);
}

/*
 * HOLD 2 s: UPDATE IOMCU is 460 x 56 px with its foot at body y 484: a
 * hold on its bottom row completes, a hold one row under it starts
 * nothing, and the row lies 8 px over the floor above the alert band.
 */
TEST_CASE(the_iomcu_hold_acts_at_its_foot_above_the_alert_band)
{
    fresh(UI_LANG_EN);
    upd_pose(UPD_GOT_CURRENT, false, UPD_WHY_NONE);
    const ui_screen_t *u = upd_screen();
    CHECK(UP_CU_T + UP_BTN_H <= UP_FLOOR);
    CHECK(UP_CARD_B <= UP_FLOOR);
    hold(u, UP_BTN_X, UP_CU_T + UP_BTN_H);
    CHECK(!upd_screen_take_coproc());
    hold(u, UP_BTN_X + UP_BTN_W / 2 - 1, UP_CU_T + UP_BTN_H - 1);
    CHECK(upd_screen_take_coproc());
    CHECK(!upd_screen_take_coproc());
}

/*
 * The reminder's NOT NOW and NEVER REMIND ME are 398 x 56 px side by side,
 * 16 px apart: NEVER at its left and bottom edge turns the reminder off;
 * the gap between them and the row under them do nothing.
 */
TEST_CASE(the_reminder_buttons_act_at_their_edges)
{
    fresh(UI_LANG_EN);
    upd_pose(UPD_GOT_NEWER, false, UPD_WHY_NONE);
    const ui_screen_t *r = upd_remind_screen();
    r->enter();
    settings_set(SET_UPD_REMIND, 1.0f);
    const int never_l = UP_R_NEVER_X - 199;
    for (int x = never_l - 16; x < never_l; ++x) {      /* the gap */
        tap(r, x, UP_R_ROW_Y);
    }
    tap(r, UP_R_NEVER_X, UP_R_ROW_T + 56);              /* under it */
    CHECK(settings_get_bool(SET_UPD_REMIND));
    tap(r, never_l, UP_R_ROW_T + 55);
    CHECK(!settings_get_bool(SET_UPD_REMIND));
    CHECK(UP_R_HOLD_T + 64 <= UP_R_BOX_B);
    CHECK(UP_R_BOX_B <= UP_FLOOR);
}

/* ------------------------------------------------------- the question */

/*
 * CANCEL and START are 398 x 56 px at body y 288: CANCEL acts at its
 * corners, START's hold completes on its bottom-right pixel.  A tap where
 * they were at 800 x 480 (body 264, 282 and 536, 282) lands on nothing.
 */
TEST_CASE(the_question_buttons_act_at_their_edges_and_old_taps_miss)
{
    fresh(UI_LANG_EN);
    const cu_version_t a = { 0, 21, 2 }, b = { 0, 22, 0 };
    const ui_screen_t *q = coproc_update_screen();
    coproc_update_screen_offer(a, b);
    CHECK(CU_Q_BTN_H >= UI_TAP_MIN);
    tap(q, 264, 282);
    hold(q, 536, 282);
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(!coproc_update_screen_take_start());

    tap(q, CU_Q_CANCEL_X - CU_Q_BTN_W / 2, CU_Q_BTN_T);
    CHECK(coproc_update_screen_take_cancel());
    tap(q, CU_Q_CANCEL_X + CU_Q_BTN_W / 2 - 1, CU_Q_BTN_T + CU_Q_BTN_H - 1);
    CHECK(coproc_update_screen_take_cancel());
    tap(q, CU_Q_CANCEL_X, CU_Q_BTN_T + CU_Q_BTN_H);
    CHECK(!coproc_update_screen_take_cancel());

    hold(q, CU_Q_START_X + CU_Q_BTN_W / 2 - 1, CU_Q_BTN_T + CU_Q_BTN_H - 1);
    CHECK(coproc_update_screen_take_start());
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_PROGRESS);
    CHECK(CU_Q_BTN_T + CU_Q_BTN_H <= UP_FLOOR);
}

/*
 * In English and German nothing of UPDATES, the reminder or the question
 * is drawn on the rows from 4 px over the alert band to the body's foot:
 * UPDATES at its fullest (the release line capped, the hold of the IOmcu)
 * and shut by the gate, the reminder with the hold shut, the question for
 * an older image.
 */
TEST_CASE(no_ink_under_the_alert_band)
{
    for (int l = 0; l < 2; ++l) {
        fresh((ui_lang_t)l);
        upd_pose(UPD_GOT_PAST_LINE, true, UPD_WHY_NONE);
        upd_screen()->render(&s_body, 0);
        CHECK_EQ(ink(0, UP_FLOOR, UI_PANEL_W, UI_BODY_H), 0);

        upd_screen_invalidate();
        upd_pose(UPD_GOT_BLOCKED, false, UPD_WHY_ARMED);
        upd_screen()->render(&s_body, 0);
        CHECK_EQ(ink(0, UP_FLOOR, UI_PANEL_W, UI_BODY_H), 0);
        upd_pose(UPD_GOT_NEWER, false, UPD_WHY_DRAWING);
        upd_remind_screen()->enter();
        upd_remind_screen()->render(&s_body, 0);
        CHECK_EQ(ink(0, UP_FLOOR, UI_PANEL_W, UI_BODY_H), 0);

        const cu_version_t a = { 0, 22, 1 }, b = { 0, 22, 0 };
        coproc_update_screen_offer(a, b);
        coproc_update_screen()->render(&s_body, 0);
        CHECK_EQ(ink(0, UP_FLOOR, UI_PANEL_W, UI_BODY_H), 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* ------------------------------------------------------- the bus fault */

/** @brief The bus fault for @p v, the IOmcu answering with every line. */
static void bf_pose(can_selftest_verdict_t v)
{
    const busfault_report_t r = {
        .verdict = v, .sent = 1284, .echoed = 1197, .corrupt = 71,
        .lost = 16, .tx_errors = 255, .rx_errors = 255, .bus_errors = 87,
        .bus_off = true, .have_remote = true, .remote_up = true,
        .remote_tx_errors = 255, .remote_rx_errors = 255,
        .remote_flags = 0x40, .remote_overflows = 255,
    };
    busfault_screen_set(&r);
}

/*
 * The acknowledgement runs 976 x 56 px across the foot, 16 px above the
 * panel's edge: a hold on its top-left and its bottom-right pixel
 * acknowledges; a hold on the row over it does not.
 */
TEST_CASE(the_acknowledgement_spans_the_foot_and_acts_at_its_corners)
{
    fresh(UI_LANG_EN);
    bf_pose(CAN_SELFTEST_SILENT);
    const ui_screen_t *b = busfault_screen();
    CHECK(BF_ACK_H >= UI_TAP_MIN);
    CHECK_EQ(BF_ACK_Y + BF_ACK_H + 16, UI_PANEL_H);
    hold(b, BF_ACK_X + 100, BF_ACK_Y - 1);
    CHECK(!busfault_screen_take_ack());
    hold(b, BF_ACK_X, BF_ACK_Y);
    CHECK(busfault_screen_take_ack());
    busfault_screen_set(busfault_screen_report());
    hold(b, BF_ACK_X + BF_ACK_W - 1, BF_ACK_Y + BF_ACK_H - 1);
    CHECK(busfault_screen_take_ack());
}

/*
 * In English and German, the fullest lists -- SILENT's five checks, two of
 * them wrapped in German, LOSSY's four, three wrapped -- end above the
 * notes, the notes and the fullest number column end above the rule, and
 * nothing is drawn between the rule and the acknowledgement.
 */
TEST_CASE(the_fullest_bus_fault_lists_end_above_the_notes_and_the_rule)
{
    static const can_selftest_verdict_t k_v[] = {
        CAN_SELFTEST_SILENT, CAN_SELFTEST_LOSSY, CAN_SELFTEST_CORRUPT,
        CAN_SELFTEST_DROPPED,
    };
    for (int l = 0; l < 2; ++l) {
        fresh((ui_lang_t)l);
        for (size_t i = 0; i < sizeof(k_v) / sizeof(k_v[0]); ++i) {
            bf_pose(k_v[i]);
            busfault_screen()->render(&s_panel, 0);
            /* The 6 rows over the first note: clear of the list. */
            CHECK_EQ(ink(0, BF_NOTE_Y - 6, BF_NUM_X, BF_NOTE_Y), 0);
            /* The rows over the rule, under it and above the button. */
            CHECK_EQ(ink(0, BF_RULE_Y - 6, UI_PANEL_W, BF_RULE_Y), 0);
            CHECK_EQ(ink(0, BF_RULE_Y + 1, UI_PANEL_W, BF_ACK_Y), 0);
            /* The notes are there: ink on each of their three lines. */
            for (int k = 0; k < 3; ++k) {
                CHECK(ink(BF_ACK_X, BF_NOTE_Y + 28 * k, BF_NUM_X,
                          BF_NOTE_Y + 28 * k + 24) > 50);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* ----------------------------------------- the progress and the splash */

/*
 * The progress bar is 36 px tall from y 180 across the panel less 24 px
 * each side; the German note on the kept version wraps to a second line,
 * which ends above y 400.
 */
TEST_CASE(the_progress_bar_is_36_px_and_the_notes_wrap_in_german)
{
    fresh(UI_LANG_DE);
    const cu_version_t a = { 0, 21, 2 }, b = { 0, 22, 0 };
    coproc_update_screen_offer(a, b);
    coproc_update_screen_progress(CU_STEP_SEND, 2994u, 2994u);
    coproc_update_screen()->render(&s_panel, 0);
    const gfx_color_t acc = ui_theme_color(UI_C_ACCENT);
    int top = -1, bottom = -1, left = -1, right = -1;
    for (int y = 64; y < 300; ++y) {
        if (s_px[(size_t)y * UI_PANEL_W + 500] == acc) {
            top = (top < 0) ? y : top;
            bottom = y;
        }
    }
    for (int x = 0; x < UI_PANEL_W; ++x) {
        if (s_px[(size_t)198 * UI_PANEL_W + x] == acc) {
            left = (left < 0) ? x : left;
            right = x;
        }
    }
    CHECK_EQ(top, 180);
    CHECK_EQ(bottom, 215);
    CHECK_EQ(left, 24);
    CHECK_EQ(right, UI_PANEL_W - 25);
    /* The fourth note line holds the wrapped end; nothing under it. */
    CHECK(ink(24, 372, UI_PANEL_W, 396) > 50);
    CHECK_EQ(ink(0, 400, UI_PANEL_W, UI_PANEL_H), 0);
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The steps are 40 px rows from y 200, the last ending above the credit;
 * a detail of SPLASH_DETAIL_MAX - 1 characters is cut 24 px short of the
 * panel's right edge.
 */
TEST_CASE(the_splash_rows_are_40_px_and_a_long_detail_stays_on_the_panel)
{
    fresh(UI_LANG_DE);
    char longest[SPLASH_DETAIL_MAX];
    memset(longest, 'W', sizeof(longest) - 1u);
    longest[sizeof(longest) - 1u] = '\0';
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, longest);
    }
    splash_screen()->render(&s_panel, 0);
    CHECK_EQ(ink(UI_PANEL_W - 24, 200, UI_PANEL_W, 200 + 7 * 40), 0);
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        const int y = 200 + i * 40;
        CHECK(ink(192, y, UI_PANEL_W, y + 36) > 100);   /* the step */
        CHECK_EQ(ink(192, y + 36, UI_PANEL_W, y + 40), 0);
    }
    /* The credit, under the last row, and the mark over the first. */
    CHECK(ink(0, UI_PANEL_H - 36, UI_PANEL_W, UI_PANEL_H - 16) > 50);
    CHECK_EQ(ink(0, 200 + 7 * 40, UI_PANEL_W, UI_PANEL_H - 40), 0);
    CHECK(ink(0, 40, UI_PANEL_W, 128) > 5000);
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(updates_buttons_act_where_they_are_drawn);
    RUN(the_iomcu_hold_acts_at_its_foot_above_the_alert_band);
    RUN(the_reminder_buttons_act_at_their_edges);
    RUN(the_question_buttons_act_at_their_edges_and_old_taps_miss);
    RUN(no_ink_under_the_alert_band);
    RUN(the_acknowledgement_spans_the_foot_and_acts_at_its_corners);
    RUN(the_fullest_bus_fault_lists_end_above_the_notes_and_the_rule);
    RUN(the_progress_bar_is_36_px_and_the_notes_wrap_in_german);
    RUN(the_splash_rows_are_40_px_and_a_long_detail_stays_on_the_panel);
    return test_summary("upd_layout");
}
