/**
 * @file test_servo_rudder.c
 * @brief The control surface's geometry (servo_rudder.h): the trailing
 *        edge's travel L x sin(theta) and its inverse, the deflection from
 *        a zero wrapped at 180°, and the largest deflection each side.
 *
 * Cases:
 * - mm_is_l_sin_theta_at_0_30_90_and_minus_30
 * - deg_of_mm_refuses_mm_past_l_and_a_short_length
 * - zero_makes_the_deflection_zero_and_wraps_at_180
 * - max_holds_the_largest_each_side_until_reset
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "servo_rudder.h"

/* L x sin(theta) at the four angles, at the shortest length and under
 * it, and for a length that is no number. */
TEST_CASE(mm_is_l_sin_theta_at_0_30_90_and_minus_30)
{
    CHECK_NEAR(servo_rudder_mm(0.0f, 100.0f), 0.0f, 1e-4f);
    CHECK_NEAR(servo_rudder_mm(30.0f, 100.0f), 50.0f, 1e-3f);
    CHECK_NEAR(servo_rudder_mm(90.0f, 100.0f), 100.0f, 1e-3f);
    CHECK_NEAR(servo_rudder_mm(-30.0f, 100.0f), -50.0f, 1e-3f);
    CHECK_NEAR(servo_rudder_mm(30.0f, SERVO_RUDDER_LEN_MAX_MM), 250.0f, 1e-2f);
    /* 10mm is the shortest length taken; 9.99mm and 0 read 0. */
    CHECK_NEAR(servo_rudder_mm(30.0f, SERVO_RUDDER_LEN_MIN_MM), 5.0f, 1e-4f);
    CHECK_EQ(servo_rudder_mm(30.0f, 9.99f), 0.0f);
    CHECK_EQ(servo_rudder_mm(30.0f, 0.0f), 0.0f);
    CHECK_EQ(servo_rudder_mm(30.0f, -100.0f), 0.0f);
    CHECK_EQ(servo_rudder_mm(30.0f, NAN), 0.0f);
}

/* asin(mm / L): the edges of the range taken, one past them refused with
 * the output untouched, a length under 10mm and NULL refused. */
TEST_CASE(deg_of_mm_refuses_mm_past_l_and_a_short_length)
{
    float deg = 0.0f;
    CHECK(servo_rudder_deg_of_mm(50.0f, 100.0f, &deg));
    CHECK_NEAR(deg, 30.0f, 1e-3f);
    CHECK(servo_rudder_deg_of_mm(-50.0f, 100.0f, &deg));
    CHECK_NEAR(deg, -30.0f, 1e-3f);
    CHECK(servo_rudder_deg_of_mm(100.0f, 100.0f, &deg));
    CHECK_NEAR(deg, 90.0f, 1e-3f);
    CHECK(servo_rudder_deg_of_mm(-100.0f, 100.0f, &deg));
    CHECK_NEAR(deg, -90.0f, 1e-3f);
    CHECK(servo_rudder_deg_of_mm(0.0f, 100.0f, &deg));
    CHECK_NEAR(deg, 0.0f, 1e-6f);
    CHECK(servo_rudder_deg_of_mm(5.0f, SERVO_RUDDER_LEN_MIN_MM, &deg));
    CHECK_NEAR(deg, 30.0f, 1e-3f);

    deg = 7.0f;
    CHECK(!servo_rudder_deg_of_mm(100.01f, 100.0f, &deg));
    CHECK(!servo_rudder_deg_of_mm(-100.01f, 100.0f, &deg));
    CHECK(!servo_rudder_deg_of_mm(5.0f, 9.99f, &deg));
    CHECK(!servo_rudder_deg_of_mm(0.0f, 0.0f, &deg));
    CHECK(!servo_rudder_deg_of_mm(NAN, 100.0f, &deg));
    CHECK(!servo_rudder_deg_of_mm(5.0f, NAN, &deg));
    CHECK_EQ(deg, 7.0f);
    CHECK(!servo_rudder_deg_of_mm(5.0f, 100.0f, NULL));

    /* And back: the mm of the angle found is the mm asked for. */
    CHECK(servo_rudder_deg_of_mm(37.5f, 120.0f, &deg));
    CHECK_NEAR(servo_rudder_mm(deg, 120.0f), 37.5f, 1e-3f);
}

/* Without a zero the deflection is the angle itself; ZERO takes the angle
 * now, and the deflection from it runs -180 up to 180 across the wrap.
 * A new zero clears the MAX. */
TEST_CASE(zero_makes_the_deflection_zero_and_wraps_at_180)
{
    servo_rudder_t r;
    memset(&r, 0x5A, sizeof(r));
    servo_rudder_init(&r);
    CHECK(!r.zeroed);
    CHECK(!r.have[0] && !r.have[1]);
    CHECK_EQ(servo_rudder_defl(&r, 37.0f), 37.0f);
    CHECK_EQ(servo_rudder_defl(&r, 200.0f), 200.0f);
    CHECK_EQ(servo_rudder_defl(NULL, 12.5f), 12.5f);

    servo_rudder_zero(&r, 170.0f);
    CHECK(r.zeroed);
    CHECK_EQ(r.zero_deg, 170.0f);
    CHECK_NEAR(servo_rudder_defl(&r, 170.0f), 0.0f, 1e-4f);
    CHECK_NEAR(servo_rudder_defl(&r, 160.0f), -10.0f, 1e-4f);
    CHECK_NEAR(servo_rudder_defl(&r, -170.0f), 20.0f, 1e-4f);
    CHECK_NEAR(servo_rudder_defl(&r, 179.9f), 9.9f, 1e-3f);

    servo_rudder_zero(&r, -170.0f);
    CHECK_NEAR(servo_rudder_defl(&r, 170.0f), -20.0f, 1e-4f);
    CHECK_NEAR(servo_rudder_defl(&r, -160.0f), 10.0f, 1e-4f);

    /* Half a turn from the zero reads -180, a hair less reads +179.9. */
    servo_rudder_zero(&r, 0.0f);
    CHECK_NEAR(servo_rudder_defl(&r, 180.0f), -180.0f, 1e-4f);
    CHECK_NEAR(servo_rudder_defl(&r, -180.0f), -180.0f, 1e-4f);
    CHECK_NEAR(servo_rudder_defl(&r, 179.9f), 179.9f, 1e-3f);
    CHECK_NEAR(servo_rudder_defl(&r, 540.0f), -180.0f, 1e-3f);
    CHECK_NEAR(servo_rudder_defl(&r, -359.0f), 1.0f, 1e-3f);

    /* ZERO clears what MAX held. */
    servo_rudder_track(&r, 12.0f);
    servo_rudder_track(&r, -8.0f);
    CHECK(r.have[0] && r.have[1]);
    servo_rudder_zero(&r, 5.0f);
    CHECK(!r.have[0] && !r.have[1]);
    CHECK_EQ(r.max_deg[0], 0.0f);
    CHECK_EQ(r.max_deg[1], 0.0f);

    servo_rudder_zero(NULL, 1.0f);
    servo_rudder_init(NULL);
}

/* MAX keeps the largest magnitude each side, 0 and NaN on neither, until
 * RESET MAX clears both. */
TEST_CASE(max_holds_the_largest_each_side_until_reset)
{
    servo_rudder_t r;
    servo_rudder_init(&r);
    servo_rudder_track(&r, 0.0f);
    servo_rudder_track(&r, NAN);
    CHECK(!r.have[0] && !r.have[1]);

    servo_rudder_track(&r, 5.0f);
    CHECK(r.have[1] && !r.have[0]);
    CHECK_EQ(r.max_deg[1], 5.0f);
    servo_rudder_track(&r, -3.0f);
    CHECK(r.have[0]);
    CHECK_EQ(r.max_deg[0], 3.0f);
    servo_rudder_track(&r, 12.0f);
    servo_rudder_track(&r, 11.9f);
    servo_rudder_track(&r, -1.0f);
    servo_rudder_track(&r, 0.0f);
    CHECK_EQ(r.max_deg[1], 12.0f);
    CHECK_EQ(r.max_deg[0], 3.0f);
    /* The same magnitude again changes nothing; a larger one each side. */
    servo_rudder_track(&r, -3.0f);
    CHECK_EQ(r.max_deg[0], 3.0f);
    servo_rudder_track(&r, -3.01f);
    CHECK_NEAR(r.max_deg[0], 3.01f, 1e-6f);
    servo_rudder_track(&r, 180.0f);
    CHECK_EQ(r.max_deg[1], 180.0f);
    /* The zero stays through a RESET. */
    servo_rudder_zero(&r, 2.0f);
    servo_rudder_track(&r, 4.0f);
    servo_rudder_reset_max(&r);
    CHECK(!r.have[0] && !r.have[1]);
    CHECK_EQ(r.max_deg[0], 0.0f);
    CHECK_EQ(r.max_deg[1], 0.0f);
    CHECK(r.zeroed);
    CHECK_EQ(r.zero_deg, 2.0f);
    /* A side's first value is kept even when small. */
    servo_rudder_track(&r, -0.1f);
    CHECK(r.have[0]);
    CHECK_NEAR(r.max_deg[0], 0.1f, 1e-6f);

    servo_rudder_track(NULL, 1.0f);
    servo_rudder_reset_max(NULL);
}

int main(void)
{
    RUN(mm_is_l_sin_theta_at_0_30_90_and_minus_30);
    RUN(deg_of_mm_refuses_mm_past_l_and_a_short_length);
    RUN(zero_makes_the_deflection_zero_and_wraps_at_180);
    RUN(max_holds_the_largest_each_side_until_reset);
    return test_summary("servo_rudder");
}
