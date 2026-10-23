/*
 * The IOmcu update's screen: the START hold, CANCEL, the two latches
 * the application reads, the version comparison behind the word "older",
 * and the band the router draws over each view.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "coproc_update_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* The buttons of the question, in the body's coordinates. */
#include "upd_spots.h"

#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static const ui_screen_t *scr(void) { return coproc_update_screen(); }

static const cu_version_t k_old = { 0, 17, 0 };
static const cu_version_t k_new = { 0, 18, 0 };

static void fresh(cu_version_t running, cu_version_t carried)
{
    ui_theme_set(UI_THEME_DARK);
    scr()->reset();
    coproc_update_screen_offer(running, carried);
}

static void ev(touch_event_type_t type, uint8_t id, int x, int y,
               uint8_t flags)
{
    touch_event_t e = { .type = type, .flags = flags,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr()->event(&e);
}

static void ticks(int n)
{
    for (int i = 0; i < n; ++i) {
        scr()->tick(TICK_S);
    }
}

TEST_CASE(older_compares_major_then_minor_then_patch)
{
    CHECK(coproc_update_older((cu_version_t){ 0, 18, 0 },
                              (cu_version_t){ 0, 17, 9 }));
    CHECK(coproc_update_older((cu_version_t){ 1, 0, 0 },
                              (cu_version_t){ 0, 99, 99 }));
    CHECK(coproc_update_older((cu_version_t){ 0, 18, 2 },
                              (cu_version_t){ 0, 18, 1 }));
    CHECK(!coproc_update_older((cu_version_t){ 0, 18, 0 },
                               (cu_version_t){ 0, 18, 0 }));
    CHECK(!coproc_update_older((cu_version_t){ 0, 17, 9 },
                               (cu_version_t){ 0, 18, 0 }));
    CHECK(!coproc_update_older((cu_version_t){ 0, 99, 99 },
                               (cu_version_t){ 1, 0, 0 }));
}

TEST_CASE(a_tap_on_start_starts_nothing)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(2);
    ev(TOUCH_EVENT_UP, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_QUESTION);
}

TEST_CASE(half_the_hold_starts_nothing)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    ev(TOUCH_EVENT_UP, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    CHECK(!coproc_update_screen_take_start());
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_QUESTION);
}

TEST_CASE(the_hold_starts_once_and_shows_the_progress)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_PROGRESS);
    CHECK(coproc_update_screen_take_start());
    CHECK(!coproc_update_screen_take_start());
    ev(TOUCH_EVENT_UP, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
    CHECK(!coproc_update_screen_take_cancel());
}

TEST_CASE(an_older_image_takes_the_same_hold)
{
    fresh(k_new, k_old);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(coproc_update_screen_take_start());
}

TEST_CASE(one_late_frame_cannot_start)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    scr()->tick(5.0f);
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_press_that_slides_off_start_abandons_the_hold)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    ev(TOUCH_EVENT_MOVE, 1, CU_Q_START_X, CU_Q_BTN_Y - 100, 0);
    ev(TOUCH_EVENT_MOVE, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_cancelled_gesture_starts_nothing)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    scr()->cancel();
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
    /* And a fresh press after it is taken. */
    ev(TOUCH_EVENT_DOWN, 2, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(coproc_update_screen_take_start());
}

TEST_CASE(cancel_is_a_tap_and_is_handed_over_once)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    CHECK(coproc_update_screen_take_cancel());
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_release_off_cancel_or_flagged_is_no_cancel)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y - 120, 0);
    CHECK(!coproc_update_screen_take_cancel());
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, TOUCH_FLAG_NO_TAP);
    CHECK(!coproc_update_screen_take_cancel());
}

TEST_CASE(a_second_finger_does_not_end_the_first_ones_hold)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    ev(TOUCH_EVENT_DOWN, 2, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 2, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(coproc_update_screen_take_start());
}

TEST_CASE(leaving_the_question_declines_and_leaving_the_progress_does_not)
{
    fresh(k_old, k_new);
    scr()->leave();
    CHECK(coproc_update_screen_take_cancel());

    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    scr()->leave();
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(coproc_update_screen_take_start());
}

TEST_CASE(the_progress_view_takes_no_touch)
{
    fresh(k_old, k_new);
    coproc_update_screen_progress(CU_STEP_SEND, 10u, 2994u);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_new_offer_returns_to_the_question_with_clear_latches)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    coproc_update_screen_offer(k_old, k_new);
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_QUESTION);
    CHECK(!coproc_update_screen_take_start());
    CHECK(!coproc_update_screen_take_cancel());
}

TEST_CASE(the_router_draws_the_band_over_the_question_only)
{
    ui_router_init();
    fresh(k_old, k_new);
    ui_router_goto(SCREEN_COPROC_UPDATE);
    CHECK(ui_router_stop_live());
    coproc_update_screen_progress(CU_STEP_SEND, 0u, 2994u);
    CHECK(!ui_router_stop_live());
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(ui_router_stop_live());
}

/* ------------------------------------------------------------ the driver */

static cu_driver_t g_drv;
static cu_feed_t   g_feed;

/* The menu, nothing offered, the driver fresh. */
static void drive_fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    ui_router_goto(SCREEN_OVERVIEW);
    scr()->reset();
    memset(&g_drv, 0, sizeof(g_drv));
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.running = k_old;
    g_feed.carried = k_new;
}

static cu_hand_t frame(void)
{
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, false, &h);
    return h;
}

/* An offer, and the frame that opens the question for it. */
static void offer(void)
{
    g_feed.offered = true;
    ++g_feed.offer_gen;
    (void)frame();
}

static void router_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

TEST_CASE(an_offer_opens_the_question_once_on_a_safe_menu)
{
    drive_fresh();
    g_feed.offered = true;
    g_feed.offer_gen = 1u;
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, true, &h);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);   /* armed: not yet */
    ui_router_goto(SCREEN_MOTOR);
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);      /* not on the menu */
    ui_router_goto(SCREEN_OVERVIEW);
    h = frame();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_QUESTION);
    CHECK(!h.decline);
    CHECK_EQ(h.starts, 0u);
}

TEST_CASE(cancel_declines_once_and_the_question_stays_shut)
{
    drive_fresh();
    offer();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CU_Q_CANCEL_X, CU_Q_BTN_Y, 0);
    cu_hand_t h = frame();
    CHECK(h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* The way out latched CANCEL again: drained, not handed over twice. */
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/*
 * The HOME tag, an alert's jump, STOP's way to the menu, any screen the
 * router goes to: CANCEL latches on the way out, after the router has left
 * the question, and the next frame hands it over as a decline.
 */
TEST_CASE(every_way_off_the_question_is_a_decline_handed_over_once)
{
    drive_fresh();
    offer();
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    const gfx_rect_t home =
        ui_home_tag_rect(ui_router_title(SCREEN_COPROC_UPDATE));
    router_tap(home.x + home.w / 2, home.y + home.h / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    cu_hand_t h = frame();
    CHECK(h.decline);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);   /* not reopened */

    static const ui_screen_id_t k_to[] = {
        SCREEN_OVERVIEW, SCREEN_MOTOR, SCREEN_LOGS, SCREEN_SETUP,
        SCREEN_OUTPUTS, SCREEN_SUPPLY,
    };
    for (size_t i = 0; i < sizeof(k_to) / sizeof(k_to[0]); ++i) {
        drive_fresh();
        offer();
        CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
        ui_router_goto(k_to[i]);
        h = frame();
        CHECK(h.decline);
        CHECK_EQ(ui_router_current(), k_to[i]);
        h = frame();
        CHECK(!h.decline);
    }
}

TEST_CASE(an_offer_that_goes_closes_the_question_and_declines_nothing)
{
    drive_fresh();
    offer();
    g_feed.offered = false;
    cu_hand_t h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    h = frame();
    CHECK(!h.decline);
    /* And a question left after its offer went declines nothing. */
    drive_fresh();
    offer();
    g_feed.offered = false;
    ui_router_goto(SCREEN_MOTOR);
    h = frame();
    CHECK(!h.decline);
}

TEST_CASE(start_runs_the_progress_and_the_end_returns_to_the_menu)
{
    drive_fresh();
    offer();
    ev(TOUCH_EVENT_DOWN, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    ticks(HOLD_TICKS);
    ev(TOUCH_EVENT_UP, 1, CU_Q_START_X, CU_Q_BTN_Y, 0);
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_PROGRESS);

    /* Taken: the offer goes, the update runs. */
    g_feed.offered = false;
    g_feed.active = true;
    g_feed.starts_taken = 1u;
    g_feed.step = CU_STEP_SEND;
    g_feed.blocks = 2994u;
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    /* An alert's jump during the update is undone, and declines nothing. */
    ui_router_goto(SCREEN_LOGS);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);

    g_feed.active = false;
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(the_driver_takes_no_null)
{
    cu_hand_t h = { 5u, true };
    coproc_update_drive(NULL, &g_feed, false, &h);
    CHECK_EQ(h.starts, 0u);
    CHECK(!h.decline);
    coproc_update_drive(&g_drv, NULL, false, &h);
    coproc_update_drive(&g_drv, &g_feed, false, NULL);
}

/* Every step, with the count at 0, part way, at the end and past it, in
 * both views and both languages, inside the canvas. */
TEST_CASE(every_view_renders_inside_its_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_PANEL_H)];
    gfx_canvas_t c;
    guard_canvas_t g;
    static const cu_step_t steps[] = { CU_STEP_PREPARE, CU_STEP_SEND,
                                       CU_STEP_VERIFY, CU_STEP_RESTART,
                                       CU_STEP_BUY };
    static const uint16_t counts[][2] = { { 0, 0 }, { 0, 2994 },
                                          { 1497, 2994 }, { 2994, 2994 },
                                          { 3000, 2994 } };
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        static const cu_version_t pairs[][2] = {
            { { 0, 17, 0 }, { 0, 18, 0 } },
            { { 0, 18, 0 }, { 0, 17, 0 } },
            { { 0, 18, 0 }, { 0, 18, 0 } },
            { { 65535, 65535, 65535 }, { 65535, 65535, 65535 } },
        };
        for (size_t p = 0; p < sizeof(pairs) / sizeof(pairs[0]); ++p) {
            fresh(pairs[p][0], pairs[p][1]);
            guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
            coproc_update_screen_invalidate();
            scr()->render(&c, 0);
            CHECK_EQ(guard_canvas_touched(&g), 0u);
            CHECK_EQ(guard_canvas_unpainted(&g), 0u);
        }
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
            for (size_t k = 0; k < sizeof(counts) / sizeof(counts[0]); ++k) {
                coproc_update_screen_progress(steps[i], counts[k][0],
                                              counts[k][1]);
                guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_PANEL_H);
                coproc_update_screen_invalidate();
                scr()->render(&c, 1);
                CHECK_EQ(guard_canvas_touched(&g), 0u);
                CHECK_EQ(guard_canvas_unpainted(&g), 0u);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(older_compares_major_then_minor_then_patch);
    RUN(a_tap_on_start_starts_nothing);
    RUN(half_the_hold_starts_nothing);
    RUN(the_hold_starts_once_and_shows_the_progress);
    RUN(an_older_image_takes_the_same_hold);
    RUN(one_late_frame_cannot_start);
    RUN(a_press_that_slides_off_start_abandons_the_hold);
    RUN(a_cancelled_gesture_starts_nothing);
    RUN(cancel_is_a_tap_and_is_handed_over_once);
    RUN(a_release_off_cancel_or_flagged_is_no_cancel);
    RUN(a_second_finger_does_not_end_the_first_ones_hold);
    RUN(leaving_the_question_declines_and_leaving_the_progress_does_not);
    RUN(the_progress_view_takes_no_touch);
    RUN(a_new_offer_returns_to_the_question_with_clear_latches);
    RUN(the_router_draws_the_band_over_the_question_only);
    RUN(every_view_renders_inside_its_canvas);
    RUN(an_offer_opens_the_question_once_on_a_safe_menu);
    RUN(cancel_declines_once_and_the_question_stays_shut);
    RUN(every_way_off_the_question_is_a_decline_handed_over_once);
    RUN(an_offer_that_goes_closes_the_question_and_declines_nothing);
    RUN(start_runs_the_progress_and_the_end_returns_to_the_menu);
    RUN(the_driver_takes_no_null);
    return test_summary("coproc_update_screen");
}
