/*
 * The angle source of the servo runs (servo_angle.h): the IMU's angle in
 * AS5600 counts, the centre each source's degrees are taken from, and the
 * source's name.
 *
 * Under test: 0, 90, -90, 180 and -180° and the smallest step that rounds
 * to a count; the wrap of an angle past a turn and of one not a number; a
 * reading not valid keeping its time and its gap and nothing else; the
 * time across the 32-bit wrap; NULL; the centre of each source; every
 * source's and axis's name, and a buffer too short for it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "servo_angle.h"
#include "servo_test.h"

/* The count @p deg becomes, valid. */
static uint16_t counts_of(float deg)
{
    const servo_angle_in_t in = { .valid = true, .deg = deg,
                                  .still_ms = 0u, .taken_ms = 1000u };
    servo_test_enc_t e;
    CHECK(servo_angle_enc(&in, &e));
    CHECK(e.valid);
    return e.raw;
}

TEST_CASE(degrees_become_counts_with_centre_zero)
{
    CHECK_EQ(counts_of(0.0f), 0u);
    CHECK_EQ(counts_of(90.0f), 1024u);
    CHECK_EQ(counts_of(-90.0f), 3072u);
    CHECK_EQ(counts_of(180.0f), 2048u);
    CHECK_EQ(counts_of(-180.0f), 2048u);
    /* 0.05° is 0.57 of a count: one; 0.04° is 0.46: none. */
    CHECK_EQ(counts_of(0.05f), 1u);
    CHECK_EQ(counts_of(0.04f), 0u);
    CHECK_EQ(counts_of(-0.05f), 4095u);
    /* Half a count, 0.0439°, rounds away from zero. */
    CHECK_EQ(counts_of(360.0f / 8192.0f), 1u);
    /* Past a turn: the same place on the circle. */
    CHECK_EQ(counts_of(450.0f), 1024u);
    CHECK_EQ(counts_of(-450.0f), 3072u);
    CHECK_EQ(counts_of(360.0f), 0u);
    /* Just under 180 rounds up to the half turn, never to 4096. */
    CHECK_EQ(counts_of(359.99f), 0u);
    CHECK_EQ(counts_of(179.99f), 2048u);
    /* Back to degrees through the servo test's own rule, centre 0. */
    for (int d = -179; d <= 179; d += 7) {
        const uint16_t raw = counts_of((float)d);
        CHECK(raw < SERVO_ANGLE_COUNTS);
        CHECK_NEAR(servo_test_enc_deg(raw, 0u), (float)d,
                   180.0f / (float)SERVO_ANGLE_COUNTS + 1e-4f);
    }
    /* still_ms and the time come through. */
    const servo_angle_in_t in = { .valid = true, .deg = 10.0f,
                                  .still_ms = 120u, .taken_ms = 77u };
    servo_test_enc_t e;
    CHECK(servo_angle_enc(&in, &e));
    CHECK_EQ(e.still_ms, 120u);
    CHECK_EQ(e.taken_ms, 77u);
    CHECK(!e.gap && !e.no_magnet && !e.weak && !e.strong);
}

TEST_CASE(a_reading_not_valid_keeps_its_time_and_gap)
{
    servo_test_enc_t e;
    memset(&e, 0xA5, sizeof(e));
    const servo_angle_in_t lost = { .valid = false, .deg = 45.0f,
                                    .still_ms = 300u,
                                    .taken_ms = UINT32_MAX, .gap = true };
    CHECK(!servo_angle_enc(&lost, &e));
    CHECK(!e.valid);
    CHECK_EQ(e.taken_ms, UINT32_MAX);   /* the 32-bit wrap, kept as it is */
    CHECK(e.gap);
    CHECK_EQ(e.raw, 0u);
    CHECK_EQ(e.still_ms, 0u);
    CHECK(!e.no_magnet && !e.weak && !e.strong);
    /* A valid reading after the wrap, with the gap before it. */
    const servo_angle_in_t back = { .valid = true, .deg = -90.0f,
                                    .taken_ms = 3u, .gap = true };
    CHECK(servo_angle_enc(&back, &e));
    CHECK_EQ(e.taken_ms, 3u);
    CHECK(e.gap);
    CHECK_EQ(e.raw, 3072u);
    /* An angle that is not a number is no reading. */
    const servo_angle_in_t nan_in = { .valid = true, .deg = NAN,
                                      .taken_ms = 10u };
    CHECK(!servo_angle_enc(&nan_in, &e));
    CHECK(!e.valid);
    CHECK_EQ(e.taken_ms, 10u);
    const servo_angle_in_t inf_in = { .valid = true, .deg = INFINITY,
                                      .taken_ms = 11u };
    CHECK(!servo_angle_enc(&inf_in, &e));
    CHECK(!e.valid);
    /* NULL: false, and an output cleared when there is one. */
    CHECK(!servo_angle_enc(&back, NULL));
    memset(&e, 0xA5, sizeof(e));
    CHECK(!servo_angle_enc(NULL, &e));
    CHECK(!e.valid);
    CHECK_EQ(e.taken_ms, 0u);
    CHECK(!e.gap);
}

TEST_CASE(centre_is_the_setting_for_the_as5600_and_zero_for_the_imu)
{
    CHECK_EQ(servo_angle_centre(SERVO_ANGLE_AS5600, 3000u), 3000u);
    CHECK_EQ(servo_angle_centre(SERVO_ANGLE_AS5600, 0u), 0u);
    CHECK_EQ(servo_angle_centre(SERVO_ANGLE_AS5600, 4095u), 4095u);
    CHECK_EQ(servo_angle_centre(SERVO_ANGLE_IMU, 3000u), 0u);
    CHECK_EQ(servo_angle_centre(SERVO_ANGLE_IMU, 4095u), 0u);
}

TEST_CASE(the_name_of_every_source_and_axis)
{
    char buf[12];
    CHECK_STR_EQ(servo_angle_name(SERVO_ANGLE_AS5600, 2u, true, buf,
                                  sizeof(buf)), "AS5600");
    static const char *const k_want[3][2] = {
        { "IMU X+", "IMU X-" }, { "IMU Y+", "IMU Y-" }, { "IMU Z+", "IMU Z-" },
    };
    for (uint8_t a = 0u; a < 3u; ++a) {
        for (int neg = 0; neg < 2; ++neg) {
            CHECK_STR_EQ(servo_angle_name(SERVO_ANGLE_IMU, a, neg != 0, buf,
                                          sizeof(buf)), k_want[a][neg]);
        }
    }
    /* An axis past Z has no letter. */
    CHECK_STR_EQ(servo_angle_name(SERVO_ANGLE_IMU, 3u, false, buf,
                                  sizeof(buf)), "IMU");
    /* The buffer is returned; a short one holds what fits, terminated. */
    CHECK(servo_angle_name(SERVO_ANGLE_IMU, 0u, false, buf, sizeof(buf))
          == buf);
    char tiny[4];
    CHECK_STR_EQ(servo_angle_name(SERVO_ANGLE_IMU, 0u, false, tiny,
                                  sizeof(tiny)), "IMU");
    CHECK(servo_angle_name(SERVO_ANGLE_IMU, 0u, false, NULL, 8u) == NULL);
    tiny[0] = 'q';
    CHECK(servo_angle_name(SERVO_ANGLE_IMU, 0u, false, tiny, 0u) == tiny);
    CHECK_EQ(tiny[0], 'q');
    /* Every name fits the runs' fields. */
    CHECK(sizeof(((servo_test_cfg_t *)0)->enc_source) >= sizeof("IMU X+"));
}

int main(void)
{
    RUN(degrees_become_counts_with_centre_zero);
    RUN(a_reading_not_valid_keeps_its_time_and_gap);
    RUN(centre_is_the_setting_for_the_as5600_and_zero_for_the_imu);
    RUN(the_name_of_every_source_and_axis);
    return test_summary("servo_angle");
}
