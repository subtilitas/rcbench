/**
 * @file test_servo_two_drags.c
 * @brief Two fingers on SERVO's LIVE task: one on the dial and one on SPEED,
 *        each drag its own contact's.  A second finger leaves the first
 *        one's drag running; a DOWN with a drag's own track id ends that
 *        drag, whose release went missing, and no other.
 *
 * Cases:
 * - a_second_finger_on_speed_changes_the_rate_of_a_dial_drag
 * - a_second_finger_on_the_dial_leaves_a_speed_drag_running
 * - a_second_finger_on_the_dial_takes_nothing_from_its_drag
 * - a_second_finger_on_release_ends_the_dial_drag
 * - a_second_finger_on_center_ends_the_dial_drag
 * - a_down_with_the_dials_own_id_ends_its_drag_and_not_speeds
 * - a_down_with_speeds_own_id_ends_its_drag_and_not_the_dials
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/* The feed works in panel coordinates and removes the band's height, as the
 * router does (touch_feed.h). */
#define P(y) ((y) + UI_BAND_H)

/* Beside the dial, 190px right of the shaft: the arc ends at 174px. */
#define BESIDE_X (SHAFT_X + 190)
/* On the arc, level with the shaft. */
#define ONTO_X   (SHAFT_X + 150)

/* SPEED's track: where a drag on it starts, and 60px and 120px on. */
#define SPD_X0 (SV_RC_X + 44)
#define SPD_X1 (SV_RC_X + 104)
#define SPD_X2 (SV_RC_X + 164)

/** @brief SERVO fed through the tracker, armed, nothing waiting. */
static void fed_armed(void)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
    servo_screen_set_armed(true);
    while (last_cmd().kind != SERVO_CMD_NONE) { }
}

/** @brief The last command posted after @p secs of frames. */
static servo_cmd_t after_frames(float secs)
{
    frames(secs);
    return last_cmd();
}

/* Finger 0 drags the dial; finger 1 comes down on SPEED and drags it.  The
 * running move takes the new rate, and the dial still follows finger 0. */
TEST_CASE(a_second_finger_on_speed_changes_the_rate_of_a_dial_drag)
{
    fed_armed();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(60.0f, 110, &x2, &y2);
    finger(0, x, P(y));
    const servo_cmd_t first = last_cmd();
    CHECK_EQ(first.kind, SERVO_CMD_POSITION);

    finger(1, SPD_X0, P(SV_SPEED_Y));
    glide(1, SPD_X1, P(SV_SPEED_Y), 8);
    const servo_cmd_t rate = last_cmd();
    CHECK_EQ(rate.kind, SERVO_CMD_POSITION);
    CHECK(rate.slew_per_s != first.slew_per_s);
    const uint16_t held = servo_screen_commanded();

    glide(0, x2, P(y2), 8);
    const servo_cmd_t c = after_frames(0.3f);
    CHECK(servo_screen_commanded() != held);
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.slew_per_s, rate.slew_per_s);
    lift(1);
    lift(0);
}

/* Finger 0 drags SPEED; finger 1 comes down on the dial, which commands the
 * horn.  Finger 0's travel goes on changing the rate the move runs at. */
TEST_CASE(a_second_finger_on_the_dial_leaves_a_speed_drag_running)
{
    fed_armed();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(60.0f, 110, &x2, &y2);
    finger(0, SPD_X0, P(SV_SPEED_Y));
    glide(0, SPD_X1, P(SV_SPEED_Y), 8);

    finger(1, x, P(y));
    const servo_cmd_t on_dial = last_cmd();
    CHECK_EQ(on_dial.kind, SERVO_CMD_POSITION);

    glide(0, SPD_X2, P(SV_SPEED_Y), 8);
    const servo_cmd_t rate = last_cmd();
    CHECK_EQ(rate.kind, SERVO_CMD_POSITION);
    CHECK(rate.slew_per_s != on_dial.slew_per_s);

    const uint16_t held = servo_screen_commanded();
    glide(1, x2, P(y2), 8);
    (void)after_frames(0.3f);
    CHECK(servo_screen_commanded() != held);
    lift(0);
    lift(1);
}

/* A second finger on the dial while the first drags it: the horn stays the
 * first finger's, and the second's travel commands nothing. */
TEST_CASE(a_second_finger_on_the_dial_takes_nothing_from_its_drag)
{
    fed_armed();
    int x, y, x2, y2, x3, y3;
    dial_at(40.0f, 110, &x, &y);
    dial_at(-40.0f, 110, &x2, &y2);
    dial_at(-60.0f, 110, &x3, &y3);
    finger(0, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    (void)after_frames(0.3f);
    const uint16_t held = servo_screen_commanded();

    finger(1, x2, P(y2));
    glide(1, x3, P(y3), 8);
    (void)after_frames(0.3f);
    CHECK_EQ(servo_screen_commanded(), held);
    lift(1);
    lift(0);
}

/* Finger 0 drags the dial; finger 1 taps RELEASE.  The servo stays
 * released: finger 0's travel afterwards commands no position. */
TEST_CASE(a_second_finger_on_release_ends_the_dial_drag)
{
    fed_armed();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(70.0f, 110, &x2, &y2);
    finger(0, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    (void)after_frames(0.3f);

    finger(1, SV_RELEASE_X, P(SV_BTN_Y));
    lift(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
    glide(0, x2, P(y2), 8);
    (void)after_frames(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    lift(0);
}

/* Finger 0 drags the dial; finger 1 taps CENTER.  The horn goes to the
 * center and stays there: finger 0's travel afterwards commands nothing. */
TEST_CASE(a_second_finger_on_center_ends_the_dial_drag)
{
    fed_armed();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(70.0f, 110, &x2, &y2);
    finger(0, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    (void)after_frames(0.3f);

    finger(1, SV_CENTER_X, P(SV_BTN_Y));
    lift(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_CENTER);
    const uint16_t center = servo_screen_commanded();
    glide(0, x2, P(y2), 8);
    (void)after_frames(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), center);
    lift(0);
}

/* Finger 0's release from the dial goes missing while finger 1 drags SPEED.
 * Finger 0 comes down again with the same id beside the dial and slides onto
 * it: the stale drag ended at that DOWN, so the horn does not jump to it.
 * Finger 1's SPEED drag goes on. */
TEST_CASE(a_down_with_the_dials_own_id_ends_its_drag_and_not_speeds)
{
    fed_armed();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    finger(0, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    finger(1, SPD_X0, P(SV_SPEED_Y));
    glide(1, SPD_X1, P(SV_SPEED_Y), 8);
    (void)after_frames(0.3f);
    feed_lose_next(1);
    lift(0);
    const uint16_t held = servo_screen_commanded();

    finger(0, BESIDE_X, P(SHAFT_Y));
    glide(0, ONTO_X, P(SHAFT_Y), 8);
    (void)after_frames(0.3f);
    CHECK_EQ(servo_screen_commanded(), held);

    /* SPEED is still finger 1's. */
    const servo_cmd_t before = last_cmd();
    CHECK_EQ(before.kind, SERVO_CMD_NONE);
    glide(1, SPD_X2, P(SV_SPEED_Y), 8);
    const servo_cmd_t rate = last_cmd();
    CHECK_EQ(rate.kind, SERVO_CMD_POSITION);
    CHECK_EQ(rate.value_us, held);
    lift(0);
    lift(1);
}

/* Finger 1's release from SPEED goes missing while finger 0 drags the dial.
 * Finger 1 comes down again with the same id on SPEED's label and slides
 * onto the track: the stale drag ended at that DOWN, so the rate stays.  The
 * dial is still finger 0's. */
TEST_CASE(a_down_with_speeds_own_id_ends_its_drag_and_not_the_dials)
{
    fed_armed();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(60.0f, 110, &x2, &y2);
    finger(0, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    finger(1, SPD_X0, P(SV_SPEED_Y));
    glide(1, SPD_X1, P(SV_SPEED_Y), 8);
    const servo_cmd_t rate = last_cmd();
    CHECK_EQ(rate.kind, SERVO_CMD_POSITION);
    feed_lose_next(1);
    lift(1);

    finger(1, SPD_X1, P(SV_SPEED_ROW_Y + 10));
    glide(1, SPD_X2, P(SV_SPEED_Y), 8);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* The dial is still finger 0's, at the rate SPEED last set. */
    const uint16_t held = servo_screen_commanded();
    glide(0, x2, P(y2), 8);
    const servo_cmd_t c = after_frames(0.3f);
    CHECK(servo_screen_commanded() != held);
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.slew_per_s, rate.slew_per_s);
    lift(1);
    lift(0);
}

int main(void)
{
    RUN(a_second_finger_on_speed_changes_the_rate_of_a_dial_drag);
    RUN(a_second_finger_on_the_dial_leaves_a_speed_drag_running);
    RUN(a_second_finger_on_the_dial_takes_nothing_from_its_drag);
    RUN(a_second_finger_on_release_ends_the_dial_drag);
    RUN(a_second_finger_on_center_ends_the_dial_drag);
    RUN(a_down_with_the_dials_own_id_ends_its_drag_and_not_speeds);
    RUN(a_down_with_speeds_own_id_ends_its_drag_and_not_the_dials);
    return test_summary("servo_two_drags");
}
