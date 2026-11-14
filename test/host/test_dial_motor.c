/*
 * The M5Stack Dial on MOTOR & ESC: its turn, its PRESS and its HOLD from
 * the Dial's EVENTs (dial_rig.h) through the controller (dial_ctl.h) and
 * the frame's paths (dial_panel.h) into the bench screen.
 *
 * Under test: the screen button held 10s, a HOLD among it, on a SAFE bench
 * posts no ARM and leaves the bench SAFE; turns while SAFE move nothing
 * and are not kept for the ARM; 5 detents at 1% move the throttle 5%; a
 * PRESS sets 0% on an ARMED bench and does nothing while SAFE; a HOLD
 * posts DISARM on an ARMED bench and nothing while SAFE; SCALE's full
 * scales: the run's peak speed over the plot's, SUPPLY's VOLTAGE MAX and
 * TRIP CURRENT while its output is on over the plot's, none off a bench
 * face.
 *
 * SPDX-License-Identifier: MIT
 */
#include "motor_rig.h"

#include "dial_panel.h"
#include "dial_rig.h"
#include "settings.h"

/** @brief One frame of the panel: the Dial's input as knob_apply() takes
 *         it, then the screen's tick; every command posted is counted. */
static void frame(drig_t *r, unsigned *arms, unsigned *throttles)
{
    const dial_input_t in = drig_take(r);
    motor_screen_knob_frame();
    const float span = dial_span(SCREEN_MOTOR, in.detents, 10u, 10u);
    if (span != 0.0f) {
        motor_screen_knob(span);
    }
    dial_gestures(SCREEN_MOTOR, &in, true);
    scr->tick(0.016f);
    motor_cmd_t c;
    while (motor_screen_poll_cmd(&c)) {
        if (c.kind == MOTOR_CMD_ARM && arms != NULL) {
            ++*arms;
        }
        if (c.kind == MOTOR_CMD_THROTTLE && throttles != NULL) {
            ++*throttles;
        }
    }
}

TEST_CASE(a_10s_dial_press_on_arm_leaves_the_bench_safe)
{
    fresh();
    drig_t r;
    drig_up(&r);
    unsigned arms = 0u, throttles = 0u;
    /* The button down for 10s: an EVENT every 200ms with the button held,
     * HOLD once at 1,000ms, and a finger on the glass over ARM's corner. */
    for (uint32_t t = 0; t <= 10000u; t += 16u) {
        if (t % 192u == 0u) {
            drig_event(&r, 0, DK_BTN_SCREEN | DK_BTN_TOUCH,
                       (t == 960u) ? DK_G_HOLD : DK_G_NONE);
        }
        drig_run(&r, 16u);
        frame(&r, &arms, &throttles);
    }
    CHECK_EQ(arms, 0u);
    CHECK_EQ(throttles, 0u);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(r.c.in.holds, 0u);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(turns_while_safe_move_nothing_and_are_not_kept)
{
    fresh();
    drig_t r;
    drig_up(&r);
    unsigned throttles = 0u;
    drig_event(&r, 0, 0u, DK_G_NONE);
    for (int16_t k = 1; k <= 10; ++k) {
        drig_event(&r, k, 0u, DK_G_NONE);
        frame(&r, NULL, &throttles);
    }
    CHECK_EQ(throttles, 0u);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    /* ARMED: the turns before it are gone; 5 detents at 1% are 5%. */
    motor_screen_set_armed(true);
    (void)last_cmd();
    frame(&r, NULL, &throttles);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    drig_event(&r, 15, 0u, DK_G_NONE);
    frame(&r, NULL, &throttles);
    CHECK_NEAR(motor_screen_throttle(), 5.0f, 0.001f);
    CHECK_EQ(throttles, 1u);
    /* At 5% a detent is 5%. */
    CHECK_NEAR(dial_span(SCREEN_MOTOR, 1, 50u, 10u), 0.05f, 1e-6f);
    CHECK_EQ(dial_span(SCREEN_SETUP, 5, 10u, 10u), 0.0f);
}

TEST_CASE(a_press_sets_0_percent_and_a_hold_disarms)
{
    fresh();
    motor_screen_set_armed(true);
    motor_screen_knob_frame();
    motor_screen_knob(0.3f);
    (void)last_cmd();
    CHECK_NEAR(motor_screen_throttle(), 30.0f, 0.001f);
    dial_input_t in = { .presses = 1u };
    dial_gestures(SCREEN_MOTOR, &in, true);
    motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_EQ(c.value, 0.0f);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    /* At 0% already: nothing. */
    CHECK(!motor_screen_dial_press());
    /* Not in a frame whose turn is dropped. */
    motor_screen_knob_frame();
    motor_screen_knob(0.2f);
    (void)last_cmd();
    dial_gestures(SCREEN_MOTOR, &in, false);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK_NEAR(motor_screen_throttle(), 20.0f, 0.001f);
    /* HOLD: DISARM, whatever the frame lost, over a PRESS in it. */
    const dial_input_t hold = { .presses = 1u, .holds = 1u };
    dial_gestures(SCREEN_MOTOR, &hold, false);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

TEST_CASE(press_and_hold_while_safe_do_nothing)
{
    fresh();
    const dial_input_t in = { .presses = 3u, .holds = 2u };
    dial_gestures(SCREEN_MOTOR, &in, true);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK(!motor_screen_dial_press());
    CHECK(!motor_screen_dial_hold());
    /* Another screen takes nothing either. */
    motor_screen_set_armed(true);
    (void)last_cmd();
    dial_gestures(SCREEN_SETUP, &in, true);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    dial_gestures(SCREEN_MOTOR, NULL, true);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(scale_takes_the_peak_the_supply_and_else_the_plot)
{
    fresh();
    bench_state_t b;
    memset(&b, 0, sizeof b);
    b.valid = true;
    dial_model_in_t in;
    memset(&in, 0, sizeof in);
    in.route = SCREEN_MOTOR;
    in.title = "MOTOR & ESC";
    in.bench = &b;
    in.thr_tenths = 10u;
    in.srv_tenths = 10u;
    dial_builder_t bld;
    memset(&bld, 0, sizeof bld);
    dial_model_t m;
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.face, DK_FACE_MOTOR);
    CHECK(m.has_scale);
    float volt = 0.0f, curr = 0.0f, rpm = 0.0f;
    motor_screen_plot_scales(&volt, &curr, &rpm);
    CHECK(volt > 0.0f && curr > 0.0f && rpm > 0.0f);
    /* Before a peak, and with SUPPLY off: the plot's. */
    CHECK_EQ(m.scale.rpm, (uint16_t)lroundf(rpm));
    CHECK_EQ(m.scale.mv, (uint16_t)lroundf(volt * 1000.0f));
    CHECK_EQ(m.scale.ca, (uint16_t)lroundf(curr * 100.0f));
    CHECK_EQ(m.scale.rail_mv, 0u);
    CHECK_EQ(m.scale.rail_ca, 0u);
    /* The run's peak. */
    b.rpm_max = 14321.4f;
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.scale.rpm, 14321u);
    /* cppcheck-suppress redundantAssignment
     * the build reads b through in.bench */
    b.rpm_max = 90000.0f;
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.scale.rpm, 65535u);
    /* SUPPLY on: its VOLTAGE MAX and TRIP CURRENT, for the rail too. */
    settings_set(SET_SUPPLY_V_MAX, 12.6f);
    settings_set(SET_SUPPLY_TRIP_I, 3.0f);
    in.supply_on = true;
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.scale.mv, 12600u);
    CHECK_EQ(m.scale.ca, 300u);
    CHECK_EQ(m.scale.rail_mv, 12600u);
    CHECK_EQ(m.scale.rail_ca, 300u);
    /* No trip set: the plot's current, the rail's not known. */
    settings_set(SET_SUPPLY_TRIP_I, 0.0f);
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.scale.ca, (uint16_t)lroundf(curr * 100.0f));
    CHECK_EQ(m.scale.rail_ca, 0u);
    /* Off a bench face: no SCALE. */
    in.route = SCREEN_OVERVIEW;
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.face, DK_FACE_HOME);
    CHECK(!m.has_scale);
}

TEST_CASE(live_motor_carries_each_reading_with_its_flag_only)
{
    fresh();
    motor_screen_set_armed(true);
    bench_state_t b;
    memset(&b, 0, sizeof b);
    b.valid = true;
    b.voltage = 16.8f;
    b.current = 12.34f;
    b.power = 207.3f;
    b.rpm = 9876.0f;
    b.temp_esc = 41.6f;
    b.temp_motor = 55.2f;
    dial_model_in_t in;
    memset(&in, 0, sizeof in);
    in.route = SCREEN_MOTOR;
    in.bench = &b;
    in.motor_armed = true;
    in.run_s = 42u;
    in.thr_tenths = 10u;
    in.srv_tenths = 10u;
    dial_builder_t bld;
    memset(&bld, 0, sizeof bld);
    dial_model_t m;
    /* No flag: no reading, whatever the fields hold. */
    dial_model_build(&bld, &in, &m);
    CHECK(m.has_motor);
    CHECK_EQ(m.motor.valid, 0u);
    CHECK_EQ(m.motor.mv, 0u);
    CHECK_EQ(m.motor.run_s, 42u);
    CHECK((m.motor.flags & DK_LIVE_ARMED) != 0u);
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                         | LINK_BN_RPM_OK | LINK_BN_TEMP_OK
                         | LINK_BN_TEMP_MOT_OK);
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.motor.valid, (uint8_t)(DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER
                                      | DK_MV_RPM | DK_MV_ESC_T
                                      | DK_MV_MOT_T));
    CHECK_EQ(m.motor.mv, 16800u);
    CHECK_EQ(m.motor.ca, 1234);
    CHECK_EQ(m.motor.dw, 2073u);
    CHECK_EQ(m.motor.rpm, 9876u);
    CHECK_EQ(m.motor.esc_c, 42);
    CHECK_EQ(m.motor.mot_c, 55);
    /* Voltage alone: no power. */
    /* cppcheck-suppress redundantAssignment
     * the build reads b through in.bench */
    b.flags = LINK_BN_VOLTAGE_OK;
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.motor.valid, (uint8_t)DK_MV_VOLT);
    /* Another screen: OTHER with its title; no input, no change. */
    in.route = SCREEN_SETUP;
    in.title = "SETUP";
    dial_model_build(&bld, &in, &m);
    CHECK_EQ(m.face, DK_FACE_OTHER);
    CHECK_STR_EQ(m.title, "SETUP");
    CHECK(!m.has_motor && !m.has_value);
    m.face = 99u;
    dial_model_build(&bld, NULL, &m);
    CHECK_EQ(m.face, 99u);
    dial_model_build(NULL, &in, &m);
    CHECK_EQ(m.face, 99u);
    /* No turn, no span; the servo's without a travel is none. */
    CHECK_EQ(dial_span(SCREEN_MOTOR, 0, 10u, 10u), 0.0f);
}

TEST_CASE(a_hold_while_an_arm_is_on_its_way_disarms)
{
    /* The ARM posted, not yet collected: the HOLD's DISARM replaces it. */
    fresh();
    hold_arm();
    motor_cmd_t c;
    CHECK(motor_screen_dial_hold());
    c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    /* The ARM collected, the bench not yet ARMED: a DISARM follows it. */
    fresh();
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
    CHECK(motor_screen_dial_hold());
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
    /* The bench then reports ARMED from the old ARM: SAFE stays asked. */
    motor_screen_set_armed(true);
    motor_screen_set_armed(false);
    tick_for(4);
    CHECK(last_cmd().kind != MOTOR_CMD_ARM);
    /* Nothing on its way, SAFE: nothing. */
    fresh();
    CHECK(!motor_screen_dial_hold());
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

int main(void)
{
    RUN(a_10s_dial_press_on_arm_leaves_the_bench_safe);
    RUN(turns_while_safe_move_nothing_and_are_not_kept);
    RUN(a_press_sets_0_percent_and_a_hold_disarms);
    RUN(press_and_hold_while_safe_do_nothing);
    RUN(scale_takes_the_peak_the_supply_and_else_the_plot);
    RUN(live_motor_carries_each_reading_with_its_flag_only);
    RUN(a_hold_while_an_arm_is_on_its_way_disarms);
    return test_summary("dial_motor");
}
