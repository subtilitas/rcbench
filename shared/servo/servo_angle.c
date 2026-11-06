/**
 * @file
 * @brief The angle source of the servo runs: the IMU's angle as AS5600
 *        counts, the centre each source's degrees are taken from, and the
 *        source's name.
 *
 * The servo test and the limit run judge moves in counts of the AS5600
 * (SERVO_TEST_ENC_TOL_COUNTS, servo_test.h).  An IMU angle is carried in the
 * same form: 360 / 4096 = 0.088° a count, centre 0, so its degrees from the
 * zero the IMU took are the counts from count 0.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_angle.h"

#include <math.h>
#include <stdio.h>

bool servo_angle_enc(const servo_angle_in_t *in, servo_test_enc_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = (servo_test_enc_t){ 0 };
    if (in == NULL) {
        return false;
    }
    out->taken_ms = in->taken_ms;
    out->gap      = in->gap;
    /* An angle that is not a number is no reading. */
    if (!in->valid || !isfinite(in->deg)) {
        return false;
    }
    /* Wrapped first, so the count of any finite angle is in range. */
    const double turn = fmod((double)in->deg, 360.0);
    long c = lround(turn * (double)SERVO_ANGLE_COUNTS / 360.0)
             % (long)SERVO_ANGLE_COUNTS;
    if (c < 0) {
        c += (long)SERVO_ANGLE_COUNTS;
    }
    out->valid    = true;
    out->raw      = (uint16_t)c;
    out->still_ms = in->still_ms;
    /* The degrees as read too, for the readouts' hundredths: raw holds
     * them to 0.088°. */
    const double w = (turn > 180.0) ? turn - 360.0
                     : (turn < -180.0) ? turn + 360.0 : turn;
    out->exact    = true;
    out->deg      = (float)w;
    return true;
}

uint16_t servo_angle_centre(servo_angle_src_t src, uint16_t enc_centre)
{
    return (src == SERVO_ANGLE_IMU) ? 0u : enc_centre;
}

const char *servo_angle_name(servo_angle_src_t src, uint8_t axis, bool neg,
                             char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return buf;
    }
    if (src != SERVO_ANGLE_IMU) {
        snprintf(buf, n, "AS5600");
    } else if (axis > 2u) {
        snprintf(buf, n, "IMU");
    } else {
        snprintf(buf, n, "IMU %c%c", "XYZ"[axis], neg ? '-' : '+');
    }
    return buf;
}
