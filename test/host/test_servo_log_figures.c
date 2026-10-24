/**
 * @file test_servo_log_figures.c
 * @brief What the SERVO screen hands a SERVO run's log for each row: the
 *        commanded pulse, as COMMANDED shows it, and the encoder's angle,
 *        as MEASURED shows it (servo_screen_log_pulse(),
 *        servo_screen_log_angle()).
 *
 * Cases:
 * - the_log_takes_the_pulse_the_screen_commands
 * - the_log_takes_the_encoders_angle_only_while_measured_shows_it
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

TEST_CASE(the_log_takes_the_pulse_the_screen_commands)
{
    /* ARMED: a position input is taken only there. */
    fresh_armed();
    uint16_t us = 0u;
    CHECK(servo_screen_log_pulse(&us));
    CHECK_EQ(us, servo_screen_commanded());
    const uint16_t centre = us;
    servo_screen_set_commanded(30.0f);
    CHECK(servo_screen_log_pulse(&us));
    CHECK_EQ(us, servo_screen_commanded());
    CHECK(us != centre);
    CHECK(!servo_screen_log_pulse(NULL));
}

TEST_CASE(the_log_takes_the_encoders_angle_only_while_measured_shows_it)
{
    fresh();
    float deg = 0.0f;
    const servo_test_enc_t e = { true, 3100u, 0u, 1000u, false, false, false,
                                 false };
    /* AS5600 off in SETUP: no angle, whatever arrives. */
    servo_screen_encoder(&e);
    CHECK(!servo_screen_log_angle(&deg));

    /* On, with a reading: the angle from the centre count, 100 counts of
     * 360/4096 deg. */
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_encoder(&e);
    CHECK(servo_screen_log_angle(&deg));
    CHECK_NEAR(deg, 100.0f * 360.0f / 4096.0f, 0.01f);

    /* A reading that is not valid, and a link that went down: none. */
    const servo_test_enc_t gone = { false, 0u, 0u, 1040u, false, false, false,
                                    false };
    servo_screen_encoder(&gone);
    CHECK(!servo_screen_log_angle(&deg));
    servo_screen_encoder(&e);
    CHECK(servo_screen_log_angle(&deg));
    servo_screen_set_link(false);
    CHECK(!servo_screen_log_angle(&deg));
    CHECK(!servo_screen_log_angle(NULL));
    servo_screen_set_link(true);
    servo_screen_encoder(NULL);
}

int main(void)
{
    RUN(the_log_takes_the_pulse_the_screen_commands);
    RUN(the_log_takes_the_encoders_angle_only_while_measured_shows_it);
    return test_summary("servo_log_figures");
}
