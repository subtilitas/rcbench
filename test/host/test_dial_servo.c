/*
 * The M5Stack Dial on SERVO LIVE: its turn, its PRESS and its HOLD from the
 * Dial's EVENTs (dial_rig.h) through the controller (dial_ctl.h) and the
 * frame's paths (dial_panel.h) into the bench screen.
 *
 * Under test: the screen button held 10s, a HOLD among it, on a SAFE bench
 * posts no ARM; turns while SAFE move nothing and are not kept; 5 detents
 * at 1° move the horn 5°; a PRESS is CENTER on an ARMED bench and nothing
 * while SAFE; a HOLD disarms an ARMED bench and does nothing while SAFE;
 * SCALE carries the profile's travel and pulse ends.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "dial_panel.h"
#include "dial_rig.h"

/** @brief SERVO LIVE's route. */
#define LIVE_ROUTE servo_fn_screen_id(SERVO_FN_LIVE)

/** @brief One frame of the panel: the Dial's input as knob_apply() takes
 *         it, then the screen's tick; ARMs and positions counted. */
static void frame(drig_t *r, unsigned *arms, unsigned *moves)
{
    const dial_input_t in = drig_take(r);
    servo_screen_knob_frame();
    const float span = dial_span(LIVE_ROUTE, in.detents, 10u, 10u);
    if (span != 0.0f) {
        servo_screen_knob(span);
    }
    dial_gestures(LIVE_ROUTE, &in, true);
    scr->tick(0.016f);
    servo_cmd_t c;
    while (servo_screen_take(&c)) {
        if (c.kind == SERVO_CMD_ARM && arms != NULL) {
            ++*arms;
        }
        if ((c.kind == SERVO_CMD_POSITION || c.kind == SERVO_CMD_CENTER)
            && moves != NULL) {
            ++*moves;
        }
    }
}

TEST_CASE(a_10s_dial_press_on_arm_leaves_the_bench_safe)
{
    fresh();
    drig_t r;
    drig_up(&r);
    unsigned arms = 0u, moves = 0u;
    for (uint32_t t = 0; t <= 10000u; t += 16u) {
        if (t % 192u == 0u) {
            drig_event(&r, 0, DK_BTN_SCREEN | DK_BTN_TOUCH,
                       (t == 960u) ? DK_G_HOLD : DK_G_NONE);
        }
        drig_run(&r, 16u);
        frame(&r, &arms, &moves);
    }
    CHECK_EQ(arms, 0u);
    CHECK_EQ(moves, 0u);
    CHECK(!servo_screen_arm_held());
    CHECK(!servo_screen_arm_asked());
}

TEST_CASE(turns_while_safe_move_nothing_and_are_not_kept)
{
    fresh();
    drig_t r;
    drig_up(&r);
    unsigned moves = 0u;
    const uint16_t at = servo_screen_commanded();
    drig_event(&r, 0, 0u, DK_G_NONE);
    for (int16_t k = 1; k <= 10; ++k) {
        drig_event(&r, k, 0u, DK_G_NONE);
        frame(&r, NULL, &moves);
    }
    CHECK_EQ(moves, 0u);
    CHECK_EQ(servo_screen_commanded(), at);
    servo_screen_set_armed(true);
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
    frame(&r, NULL, &moves);
    CHECK_EQ(moves, 0u);
    drig_event(&r, 15, 0u, DK_G_NONE);
    frame(&r, NULL, &moves);
    CHECK_EQ(moves, 1u);
    CHECK_NEAR(servo_screen_commanded_deg(), 5.0f, 0.01f);
}

TEST_CASE(a_press_is_center_and_a_hold_disarms)
{
    fresh_armed();
    servo_screen_knob_frame();
    servo_screen_knob(0.25f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    CHECK(servo_screen_commanded_deg() > 1.0f);
    const dial_input_t press = { .presses = 1u };
    dial_gestures(LIVE_ROUTE, &press, true);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_CENTER);
    CHECK_NEAR(servo_screen_commanded_deg(), 0.0f, 0.001f);
    /* HOLD: DISARM, whatever the frame lost. */
    const dial_input_t hold = { .holds = 1u };
    dial_gestures(LIVE_ROUTE, &hold, false);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
}

TEST_CASE(press_and_hold_while_safe_do_nothing)
{
    fresh();
    const dial_input_t in = { .presses = 2u, .holds = 2u };
    dial_gestures(LIVE_ROUTE, &in, true);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_dial_press());
    CHECK(!servo_screen_dial_hold());
}

TEST_CASE(scale_carries_the_travel_and_the_pulse_ends)
{
    fresh();
    dial_model_in_t in;
    memset(&in, 0, sizeof in);
    in.route = LIVE_ROUTE;
    in.title = "SERVO";
    in.thr_tenths = 10u;
    in.srv_tenths = 10u;
    dial_builder_t bld;
    memset(&bld, 0, sizeof bld);
    dial_model_t m;
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.face, DK_FACE_SERVO);
    CHECK(m.has_scale);
    uint16_t lo = 0u, hi = 0u;
    servo_screen_pulse_ends(&lo, &hi);
    CHECK(lo >= DK_US_MIN && hi <= DK_US_MAX && lo < hi);
    CHECK_EQ(m.scale.us_min, lo);
    CHECK_EQ(m.scale.us_max, hi);
    CHECK_EQ(m.scale.travel,
             (uint16_t)lroundf(servo_screen_travel_deg() * 10.0f));
    CHECK(m.scale.travel > 0u);
    /* The packet the stream writes from it decodes to the same. */
    dial_stream_t st;
    dial_stream_init(&st);
    uint8_t pkt[DK_PKT_MAX];
    size_t n;
    while ((n = dial_stream_next(&st, &m, 0, false, pkt)) != 0u
           && pkt[0] != DK_K_SCALE) { }
    dk_scale_t c;
    CHECK(n == DK_LEN_SCALE && dk_scale_decode(pkt, n, &c));
    CHECK_EQ(c.us_min, lo);
    CHECK_EQ(c.travel, m.scale.travel);
}

TEST_CASE(live_servo_carries_the_rail_from_the_servo_channel)
{
    fresh_armed();
    bench_state_t b;
    memset(&b, 0, sizeof b);
    b.valid = true;
    b.servo_min_mv[0] = 6012u;
    b.servo_mean_ma[0] = 1234;
    dial_model_in_t in;
    memset(&in, 0, sizeof in);
    in.route = LIVE_ROUTE;
    in.bench = &b;
    in.servo_armed = true;
    in.thr_tenths = 10u;
    in.srv_tenths = 10u;
    dial_builder_t bld;
    memset(&bld, 0, sizeof bld);
    dial_model_t m;
    /* CH1's window not ready: no rail. */
    dial_model_build(&bld, &in, &m);
    CHECK(m.has_servo);
    CHECK((m.servo.flags & DK_LIVE_RAIL) == 0u);
    CHECK((m.servo.flags & DK_LIVE_ARMED) != 0u);
    b.servo_ok = 1u;
    dial_model_build(&bld, &in, &m);
    CHECK((m.servo.flags & DK_LIVE_RAIL) != 0u);
    CHECK_EQ(m.servo.rail_mv, 6012u);
    CHECK_EQ(m.servo.rail_ca, 123);
    CHECK_EQ(m.servo.rail_dw, 74u);   /* 6.012V x 1.234A = 7.4W */
    /* Without its current sample: none. */
    b.servo_no_current = 1u;
    dial_model_build(&bld, &in, &m);
    CHECK((m.servo.flags & DK_LIVE_RAIL) == 0u);
}

int main(void)
{
    RUN(a_10s_dial_press_on_arm_leaves_the_bench_safe);
    RUN(turns_while_safe_move_nothing_and_are_not_kept);
    RUN(a_press_is_center_and_a_hold_disarms);
    RUN(press_and_hold_while_safe_do_nothing);
    RUN(scale_carries_the_travel_and_the_pulse_ends);
    RUN(live_servo_carries_the_rail_from_the_servo_channel);
    return test_summary("dial_servo");
}
