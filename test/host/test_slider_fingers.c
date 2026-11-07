/**
 * @file test_slider_fingers.c
 * @brief The slider's drag and its contacts (ui_slider.c): a drag is the
 *        contact's that began it, a second finger leaves it running, and a
 *        value set from outside during a drag re-anchors it at the finger.
 *
 * Cases:
 * - a_value_set_after_travel_measures_the_drag_from_the_finger
 * - a_second_finger_leaves_a_drag_running
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "ui_slider.h"

static ui_slider_t sl;

/** @brief A slider 400px wide from x 100, y 100 to 140, over 0 to 100. */
static void fresh_slider(void)
{
    ui_slider_init(&sl, (gfx_rect_t){ 100, 100, 400, 40 }, 0.0f, 100.0f,
                   0x1234);
}

/** @brief Hands the slider one touch of contact @p id at (@p x, @p y).
 *  @return whether the value changed */
static bool ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    return ui_slider_event(&sl, &e);
}

/*
 * A value set from outside after the finger has travelled: the drag goes on
 * from the new value by the travel made after it, not by the travel since
 * the press, which would count the earlier travel twice.
 */
TEST_CASE(a_value_set_after_travel_measures_the_drag_from_the_finger)
{
    fresh_slider();
    ui_slider_set(&sl, 10.0f);
    (void)ev(200, 120, TOUCH_EVENT_DOWN, 1);
    (void)ev(240, 120, TOUCH_EVENT_MOVE, 1); /* 40px on, ~10 points */
    ui_slider_set(&sl, 40.0f);              /* a nudge, mid-drag */
    (void)ev(260, 120, TOUCH_EVENT_MOVE, 1); /* 20px more, ~5 points */
    CHECK(sl.value > 42.0f && sl.value < 48.0f);
    (void)ev(260, 120, TOUCH_EVENT_UP, 1);
}

/*
 * A drag is its contact's alone.  A second finger coming down off the
 * track, or on it, leaves the first finger's drag running and takes nothing
 * itself; the first finger's travel goes on moving the value.
 */
TEST_CASE(a_second_finger_leaves_a_drag_running)
{
    fresh_slider();
    ui_slider_set(&sl, 10.0f);
    (void)ev(200, 120, TOUCH_EVENT_DOWN, 1);
    (void)ev(240, 120, TOUCH_EVENT_MOVE, 1);
    const float mid = sl.value;
    CHECK(!ev(700, 400, TOUCH_EVENT_DOWN, 2));
    CHECK(sl.dragging);
    CHECK(!ev(400, 120, TOUCH_EVENT_DOWN, 3));
    CHECK(!ev(460, 120, TOUCH_EVENT_MOVE, 3));
    CHECK(!ev(460, 120, TOUCH_EVENT_UP, 3));
    CHECK_EQ(sl.value, mid);
    CHECK(sl.dragging);
    CHECK(ev(280, 120, TOUCH_EVENT_MOVE, 1));
    CHECK(sl.value > mid + 9.0f && sl.value < mid + 11.0f);
    (void)ev(700, 400, TOUCH_EVENT_UP, 2);
    (void)ev(280, 120, TOUCH_EVENT_UP, 1);
    CHECK(!sl.dragging);
}

int main(void)
{
    RUN(a_value_set_after_travel_measures_the_drag_from_the_finger);
    RUN(a_second_finger_leaves_a_drag_running);
    return test_summary("slider_fingers");
}
