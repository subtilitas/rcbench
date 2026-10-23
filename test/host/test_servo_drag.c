/*
 * The SERVO screen's drag filter: the dial's target follows the finger
 * through a one-pole filter of SV_DRAG_TAU_MS (20ms), by the frame's own
 * length, and the horn and the posted pulse both follow the target.
 *
 * The filter moves nothing while the value is not live, a DISARM, a STOP
 * or SAFE ends it at once, an ARM starts from the rest with nothing of a
 * drag before it, and TRAVEL holds the filtered target.  A finger that
 * lifts leaves the target to reach its last angle.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/** @brief The time constant, ms, mirrored from servo_screen_int.h. */
#define TAU_MS 20.0

/** @brief Frames at @p hz for @p secs seconds. */
static void at_hz(float hz, float secs)
{
    const int n = (int)lroundf(secs * hz);
    for (int i = 0; i < n; ++i) {
        scr->tick(1.0f / hz);
    }
}

/** @brief A contact of @p type, id @p id, on the dial at @p deg. */
static void on_dial(float deg, touch_event_type_t type, uint8_t id)
{
    int x, y;
    dial_at(deg, 110, &x, &y);
    ev(x, y, type, id);
}

/** @brief Everything waiting taken; false if a position was among it. */
static bool no_position_posted(void)
{
    bool none = true;
    servo_cmd_t c;
    while (servo_screen_take(&c)) {
        none = none && c.kind != SERVO_CMD_POSITION
               && c.kind != SERVO_CMD_CENTRE;
    }
    return none;
}

/** @brief The pulse a tap on the dial at @p deg commands, on a fresh
 *         armed bench. */
static uint16_t tap_pulse(float deg)
{
    fresh_armed();
    on_dial(deg, TOUCH_EVENT_DOWN, 1);
    on_dial(deg, TOUCH_EVENT_UP, 1);
    return last_cmd().value_us;
}

/** @brief A drag held at @p from, settled, and moved to @p to: no frame
 *         yet since the move. */
static void held_then_moved(float from, float to)
{
    fresh_armed();
    on_dial(from, TOUCH_EVENT_DOWN, 1);
    at_hz(60.0f, 0.5f);
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
    on_dial(to, TOUCH_EVENT_MOVE, 1);
}

/* ------------------------------------------------------------- the step */

/*
 * A finger that jumps from -40 to +40deg: the target covers
 * 1 - e^(-t / 20ms) of the way, the same at 30, 60 and 120 frames a
 * second -- 81.1% after 33.3ms, 99.3% after 100ms -- and the horn is
 * drawn where the pulse is.  A move alone commands nothing until a frame.
 */
TEST_CASE(the_step_follows_the_time_constant_at_any_frame_rate)
{
    static const float k_hz[3] = { 30.0f, 60.0f, 120.0f };
    const double want1 = 1.0 - exp(-(1000.0 / 30.0) / TAU_MS);
    const double want2 = 1.0 - exp(-100.0 / TAU_MS);
    double got1[3];
    for (int i = 0; i < 3; ++i) {
        const float hz = k_hz[i];
        held_then_moved(-40.0f, 40.0f);
        const uint16_t a = servo_screen_commanded();
        CHECK(!servo_screen_take(&(servo_cmd_t){ 0 }));
        CHECK_EQ(servo_screen_commanded(), a);
        at_hz(hz, 1.0f / 30.0f);
        const uint16_t p1 = servo_screen_commanded();
        CHECK_EQ(servo_screen_drawn(), p1);
        at_hz(hz, 0.1f - 1.0f / 30.0f);
        const uint16_t p2 = servo_screen_commanded();
        CHECK_EQ(servo_screen_drawn(), p2);
        at_hz(hz, 0.5f);
        const uint16_t b = servo_screen_commanded();
        CHECK(b > a + 400);
        got1[i] = (double)(p1 - a) / (double)(b - a);
        const double f2 = (double)(p2 - a) / (double)(b - a);
        if (fabs(got1[i] - want1) > 0.01 || fabs(f2 - want2) > 0.01) {
            T_FAIL("%.0f Hz: %.4f after 33ms, %.4f after 100ms; "
                   "wanted %.4f and %.4f", (double)hz, got1[i], f2, want1,
                   want2);
        }
    }
    CHECK(fabs(got1[0] - got1[2]) < 0.01);
}

/*
 * A finger at a steady 90deg a second, read every 10.5ms, with the
 * frames at 60.4Hz: the horn steps 1.49deg a frame on average, and
 * the steps' standard deviation is under 20% of that.  Taken straight
 * from the touch reads it is 34%, the steps 0.46 to 2.13deg.
 */
TEST_CASE(a_steady_drag_steps_evenly)
{
    fresh_armed();
    const double fr = 1.0 / 60.4, poll = 0.0105, w = 90.0;
    double t_poll = 0.0, t = 0.0;
    bool first = true;
    double sum = 0.0, sum2 = 0.0;
    int n = 0;
    int was = -1;
    for (int i = 0; i < 80; ++i) {
        t += fr;
        while (t_poll <= t) {
            const float deg = (float)(-60.0 + w * t_poll);
            int x, y;
            const double k = 3.14159265358979 / 180.0;
            x = SHAFT_X + (int)lround(120.0 * cos(deg * k));
            y = SHAFT_Y - (int)lround(120.0 * sin(deg * k));
            ev(x, y, first ? TOUCH_EVENT_DOWN : TOUCH_EVENT_MOVE, 1);
            first = false;
            t_poll += poll;
        }
        scr->tick((float)fr);
        const int now = (int)servo_screen_drawn();
        if (i >= 10 && was >= 0) {
            const double step = (double)(now - was);
            sum += step;
            sum2 += step * step;
            ++n;
        }
        was = now;
    }
    const double mean = sum / n;
    const double sd = sqrt(sum2 / n - mean * mean);
    /* 1.49deg a frame at 500us over 90deg: 8.3us. */
    CHECK(mean > 7.8 && mean < 8.8);
    if (sd / mean > 0.20) {
        T_FAIL("steps of %.2f us, sd %.2f us (%.0f %%)", mean, sd,
               100.0 * sd / mean);
    }
}

/* A frame of no length moves nothing and posts nothing. */
TEST_CASE(a_frame_of_no_length_moves_nothing)
{
    held_then_moved(-40.0f, 40.0f);
    const uint16_t a = servo_screen_commanded();
    scr->tick(0.0f);
    CHECK(no_position_posted());
    CHECK_EQ(servo_screen_commanded(), a);
    at_hz(60.0f, 1.0f / 60.0f);
    CHECK(servo_screen_commanded() > a);
}

/* A position commanded from elsewhere during a drag takes the horn; the
 * finger's next move takes it back from there through the filter, with no
 * jump to the finger. */
TEST_CASE(a_drag_after_another_command_moves_on_from_it)
{
    const uint16_t at_zero = tap_pulse(0.0f);
    const uint16_t want = tap_pulse(40.0f);
    held_then_moved(-40.0f, -40.0f);
    servo_screen_set_commanded(0.0f);
    CHECK_EQ(last_cmd().value_us, at_zero);
    at_hz(60.0f, 0.2f);
    CHECK(no_position_posted());
    CHECK_EQ(servo_screen_commanded(), at_zero);
    on_dial(40.0f, TOUCH_EVENT_MOVE, 1);
    at_hz(60.0f, 1.0f / 60.0f);
    const uint16_t first = servo_screen_commanded();
    /* 56% of the 222us to +40deg in one frame of 16.7ms. */
    CHECK(first > at_zero + 100 && first < at_zero + 150);
    at_hz(60.0f, 0.5f);
    CHECK_EQ(servo_screen_commanded(), want);
}

/* ---------------------------------------------------------- the release */

/* A finger lifted mid-step: the target goes on to the finger's last angle,
 * exactly the pulse a tap there gives, and then nothing more is posted. */
TEST_CASE(a_release_settles_on_the_last_finger_angle)
{
    const uint16_t want = tap_pulse(40.0f);
    held_then_moved(-40.0f, 40.0f);
    on_dial(40.0f, TOUCH_EVENT_UP, 1);
    uint16_t was = servo_screen_commanded();
    for (int i = 0; i < 30; ++i) {
        at_hz(60.0f, 1.0f / 60.0f);
        const uint16_t now = servo_screen_commanded();
        CHECK(now >= was && now <= want);
        was = now;
    }
    CHECK_EQ(servo_screen_commanded(), want);
    CHECK_EQ(servo_screen_drawn(), want);
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
    at_hz(60.0f, 0.5f);
    CHECK(!servo_screen_take(&junk));
}

/* CENTRE tapped while the target is still on its way: CENTRE's command
 * stands, and the filter posts nothing over it. */
TEST_CASE(another_command_takes_the_horn_from_the_filter)
{
    held_then_moved(-40.0f, 40.0f);
    on_dial(40.0f, TOUCH_EVENT_UP, 1);
    at_hz(60.0f, 1.0f / 60.0f);
    tap(SV_CENTRE_X, SV_BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_CENTRE);
    at_hz(60.0f, 0.5f);
    CHECK(no_position_posted());
    CHECK_EQ(servo_screen_commanded(), 1500);
}

/* SPEED changed while the target is on its way says the position again
 * under the new rate (reissue()); the target still reaches the finger. */
TEST_CASE(a_speed_change_leaves_the_filter_running)
{
    const uint16_t want = tap_pulse(40.0f);
    held_then_moved(-40.0f, 40.0f);
    on_dial(40.0f, TOUCH_EVENT_UP, 1);
    at_hz(60.0f, 1.0f / 60.0f);
    tap(SPEED_X_HALF, SPEED_Y);
    at_hz(60.0f, 0.5f);
    CHECK_EQ(servo_screen_commanded(), want);
}

/* ------------------------------------------------- disarm, stop and SAFE */

/** @brief The three ways out of driving, mid-step. */
typedef enum { OUT_DISARM, OUT_STOP, OUT_SAFE } way_out_t;

/* DISARM, STOP or SAFE while the target is on its way: nothing more is
 * posted, the value and the horn stay where they were, and the finger
 * moving on commands nothing. */
TEST_CASE(disarm_stop_and_safe_end_the_drag_at_once)
{
    for (int way = OUT_DISARM; way <= OUT_SAFE; ++way) {
        held_then_moved(-40.0f, 40.0f);
        at_hz(60.0f, 1.0f / 60.0f);
        const uint16_t part = servo_screen_commanded();
        CHECK(part > 1500 - 230 && part < 1500 + 220);
        if (way == OUT_DISARM) {
            ev(SV_RC_X + 40, SV_ARM_Y + 16, TOUCH_EVENT_DOWN, 2);
            ev(SV_RC_X + 40, SV_ARM_Y + 16, TOUCH_EVENT_UP, 2);
            CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
        } else if (way == OUT_STOP) {
            servo_screen_cancel_arm();
        } else {
            servo_screen_set_armed(false);
        }
        const uint16_t value = servo_screen_commanded();
        const uint16_t drawn = servo_screen_drawn();
        CHECK_EQ(value, part);
        at_hz(60.0f, 0.5f);
        on_dial(60.0f, TOUCH_EVENT_MOVE, 1);
        at_hz(60.0f, 0.5f);
        CHECK(no_position_posted());
        CHECK_EQ(servo_screen_commanded(), value);
        CHECK_EQ(servo_screen_drawn(), drawn);
        on_dial(60.0f, TOUCH_EVENT_UP, 1);
    }
}

/* The touch stream broke mid-step (cancel(), as touch_stream_broke()
 * calls it on an armed bench): the drag is abandoned where its target has
 * got to, and no frame after it commands a position. */
TEST_CASE(a_cancelled_gesture_ends_the_drag_at_once)
{
    held_then_moved(-40.0f, 40.0f);
    at_hz(60.0f, 1.0f / 60.0f);
    const uint16_t part = servo_screen_commanded();
    CHECK(part > 1500 - 230 && part < 1500 + 220);
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
    scr->cancel();
    at_hz(60.0f, 0.5f);
    CHECK(no_position_posted());
    CHECK_EQ(servo_screen_commanded(), part);
}

/* Disarmed mid-step and armed again, at once or 0.2s later: the value is
 * the rest the arm sets, the horn is drawn there, and nothing of the drag
 * before is posted. */
TEST_CASE(an_arm_after_a_drag_starts_from_the_rest)
{
    static const float k_gap_s[2] = { 0.0f, 0.2f };
    for (int g = 0; g < 2; ++g) {
        held_then_moved(-40.0f, 40.0f);
        at_hz(60.0f, 1.0f / 60.0f);
        servo_screen_set_armed(false);
        at_hz(60.0f, k_gap_s[g]);
        servo_screen_set_armed(true);
        CHECK_EQ(servo_screen_commanded(), 1500);
        CHECK(no_position_posted());
        at_hz(60.0f, 0.5f);
        CHECK(no_position_posted());
        CHECK_EQ(servo_screen_commanded(), 1500);
        CHECK_EQ(servo_screen_drawn(), 1500);
        /* The finger still down from before commands nothing. */
        on_dial(10.0f, TOUCH_EVENT_MOVE, 1);
        at_hz(60.0f, 0.5f);
        CHECK(no_position_posted());
        CHECK_EQ(servo_screen_commanded(), 1500);
    }
}

/* ------------------------------------------------------------ the limits */

/* A finger past TRAVEL, 90deg each way, at the dial's 100: every
 * frame's pulse stays inside 1000 to 2000us, and the target settles on
 * the end. */
TEST_CASE(travel_holds_the_filtered_target)
{
    fresh_armed();
    on_dial(-100.0f, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(last_cmd().value_us, 1000);
    on_dial(100.0f, TOUCH_EVENT_MOVE, 1);
    for (int i = 0; i < 40; ++i) {
        at_hz(60.0f, 1.0f / 60.0f);
        servo_cmd_t c;
        while (servo_screen_take(&c)) {
            CHECK(c.value_us >= 1000 && c.value_us <= 2000);
        }
    }
    CHECK_EQ(servo_screen_commanded(), 2000);
    on_dial(-100.0f, TOUCH_EVENT_MOVE, 1);
    at_hz(60.0f, 0.5f);
    CHECK_EQ(servo_screen_commanded(), 1000);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(the_step_follows_the_time_constant_at_any_frame_rate);
    RUN(a_steady_drag_steps_evenly);
    RUN(a_frame_of_no_length_moves_nothing);
    RUN(a_drag_after_another_command_moves_on_from_it);
    RUN(a_release_settles_on_the_last_finger_angle);
    RUN(another_command_takes_the_horn_from_the_filter);
    RUN(a_speed_change_leaves_the_filter_running);
    RUN(disarm_stop_and_safe_end_the_drag_at_once);
    RUN(a_cancelled_gesture_ends_the_drag_at_once);
    RUN(an_arm_after_a_drag_starts_from_the_rest);
    RUN(travel_holds_the_filtered_target);
    return test_summary("servo_drag");
}
