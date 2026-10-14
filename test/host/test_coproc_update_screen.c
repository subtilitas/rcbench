/*
 * The coprocessor update's screen: the START hold, CANCEL, the two latches
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

/* Mirrored from coproc_update_screen.c: the buttons of the question. */
#define CANCEL_X 264
#define START_X  536
#define BTN_Y    282

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
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(2);
    ev(TOUCH_EVENT_UP, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_QUESTION);
}

TEST_CASE(half_the_hold_starts_nothing)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    ev(TOUCH_EVENT_UP, 1, START_X, BTN_Y, 0);
    CHECK(!coproc_update_screen_take_start());
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_QUESTION);
}

TEST_CASE(the_hold_starts_once_and_shows_the_progress)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK_EQ(coproc_update_screen_view(), CU_VIEW_PROGRESS);
    CHECK(coproc_update_screen_take_start());
    CHECK(!coproc_update_screen_take_start());
    ev(TOUCH_EVENT_UP, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
    CHECK(!coproc_update_screen_take_cancel());
}

TEST_CASE(an_older_image_takes_the_same_hold)
{
    fresh(k_new, k_old);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(coproc_update_screen_take_start());
}

TEST_CASE(one_late_frame_cannot_start)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    scr()->tick(5.0f);
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_press_that_slides_off_start_abandons_the_hold)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    ev(TOUCH_EVENT_MOVE, 1, START_X, BTN_Y - 100, 0);
    ev(TOUCH_EVENT_MOVE, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_cancelled_gesture_starts_nothing)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    scr()->cancel();
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_start());
    /* And a fresh press after it is taken. */
    ev(TOUCH_EVENT_DOWN, 2, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(coproc_update_screen_take_start());
}

TEST_CASE(cancel_is_a_tap_and_is_handed_over_once)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CANCEL_X, BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CANCEL_X, BTN_Y, 0);
    CHECK(coproc_update_screen_take_cancel());
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_release_off_cancel_or_flagged_is_no_cancel)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, CANCEL_X, BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CANCEL_X, BTN_Y - 120, 0);
    CHECK(!coproc_update_screen_take_cancel());
    ev(TOUCH_EVENT_DOWN, 1, CANCEL_X, BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CANCEL_X, BTN_Y, TOUCH_FLAG_NO_TAP);
    CHECK(!coproc_update_screen_take_cancel());
}

TEST_CASE(a_second_finger_does_not_end_the_first_ones_hold)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS / 2);
    ev(TOUCH_EVENT_DOWN, 2, CANCEL_X, BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 2, CANCEL_X, BTN_Y, 0);
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
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    scr()->leave();
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(coproc_update_screen_take_start());
}

TEST_CASE(the_progress_view_takes_no_touch)
{
    fresh(k_old, k_new);
    coproc_update_screen_progress(CU_STEP_SEND, 10u, 2994u);
    ev(TOUCH_EVENT_DOWN, 1, CANCEL_X, BTN_Y, 0);
    ev(TOUCH_EVENT_UP, 1, CANCEL_X, BTN_Y, 0);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
    ticks(HOLD_TICKS);
    CHECK(!coproc_update_screen_take_cancel());
    CHECK(!coproc_update_screen_take_start());
}

TEST_CASE(a_new_offer_returns_to_the_question_with_clear_latches)
{
    fresh(k_old, k_new);
    ev(TOUCH_EVENT_DOWN, 1, START_X, BTN_Y, 0);
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

/* Every step, with the count at 0, part way, at the end and past it, in
 * both views and both languages, inside the canvas. */
TEST_CASE(every_view_renders_inside_its_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 480)];
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
            guard_canvas_init(&g, &c, px, 800, 432);
            coproc_update_screen_invalidate();
            scr()->render(&c, 0);
            CHECK_EQ(guard_canvas_touched(&g), 0u);
            CHECK_EQ(guard_canvas_unpainted(&g), 0u);
        }
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
            for (size_t k = 0; k < sizeof(counts) / sizeof(counts[0]); ++k) {
                coproc_update_screen_progress(steps[i], counts[k][0],
                                              counts[k][1]);
                guard_canvas_init(&g, &c, px, 800, 480);
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
    return test_summary("coproc_update_screen");
}
