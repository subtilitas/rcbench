/**
 * @file test_servo_two_drags_sweep.c
 * @brief A dial drag and a second finger on SWEEP's task: SWEEP pressed by
 *        another finger takes the horn from the drag, so the dragging
 *        finger's travel after it commands no position and leaves the sweep
 *        running, and a change of SPEED restates the sweep.
 *
 * Cases:
 * - a_second_finger_on_sweep_ends_the_dial_drag
 *
 * SPDX-License-Identifier: MIT
 */

/* SWEEP's screen: its button is the one the case presses. */
#define SERVO_RIG_FN SERVO_FN_SWEEP
#include "servo_screen_rig.h"

/* The feed works in panel coordinates and removes the band's height, as the
 * router does (touch_feed.h). */
#define P(y) ((y) + UI_BAND_H)

/* SPEED's track: where a drag on it starts, and 60px on. */
#define SPD_X0 (SV_RC_X + 44)
#define SPD_X1 (SV_RC_X + 104)

/* Finger 0 drags the dial on an armed bench whose IOmcu sweeps; finger 1
 * taps SWEEP.  Finger 0's travel afterwards commands no position and the
 * sweep runs on; a change of SPEED restates the sweep, not a position. */
TEST_CASE(a_second_finger_on_sweep_ends_the_dial_drag)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    while (last_cmd().kind != SERVO_CMD_NONE) { }
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(70.0f, 110, &x2, &y2);
    finger(0, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    frames(0.3f);
    (void)last_cmd();

    finger(1, SWEEP_X, P(BTN_Y));
    lift(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    CHECK(servo_screen_sweeping());

    glide(0, x2, P(y2), 8);
    frames(0.3f);
    CHECK(last_cmd().kind != SERVO_CMD_POSITION);
    CHECK(servo_screen_sweeping());
    lift(0);

    finger(2, SPD_X0, P(SV_SPEED_Y));
    glide(2, SPD_X1, P(SV_SPEED_Y), 8);
    lift(2);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    CHECK(servo_screen_sweeping());
}

int main(void)
{
    RUN(a_second_finger_on_sweep_ends_the_dial_drag);
    return test_summary("servo_two_drags_sweep");
}
