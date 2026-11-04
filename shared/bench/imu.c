/**
 * @file
 * @brief The IMU's reading (imu.h): frames from protocols/witmotion into
 *        the newest angles, the frame rate, battery, temperature, the zero,
 *        the still time and the age.
 *
 * Only an angle frame (0x53) makes the reading valid; it goes stale
 * IMU_STALE_MS after the last one, the non-safety allowance of servo_gap.h.
 * Every time is an unsigned difference of the 32-bit ms clock, so the age,
 * the still time and the rate window hold across its wrap.
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu.h"

#include <math.h>
#include <string.h>

/** @brief The longest still time kept, ms: the field's range. */
#define IMU_STILL_MAX_MS 65535u

/**
 * @brief @p d wrapped to -180 up to just under 180.
 * @param d An angle, °.
 * @return The same direction, -180 to under 180.
 */
static float wrap180(float d)
{
    float w = fmodf(d + 180.0f, 360.0f);
    if (w < 0.0f) {
        w += 360.0f;
    }
    return w - 180.0f;
}

void imu_init(imu_t *m)
{
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    wit_parser_init(&m->p);
    m->r.battery_v = NAN;
    m->r.temp_c    = NAN;
}

/**
 * @brief Close the rate window when it has run IMU_RATE_WIN_MS: the angle
 *        frames counted in it, as 0.1Hz over its length.
 * @param m      The model.
 * @param now_ms The clock.
 */
static void rate_roll(imu_t *m, uint32_t now_ms)
{
    const uint32_t span = now_ms - m->rate_from_ms;
    if (span < IMU_RATE_WIN_MS) {
        return;
    }
    /* A window that ran two lengths or more saw the frames stop: its count
     * is not spread over the silence. */
    const uint32_t dhz = (span >= 2u * IMU_RATE_WIN_MS)
        ? 0u
        : (uint32_t)m->rate_count * 10000u / span;
    m->r.rate_dhz = (uint16_t)((dhz > 65535u) ? 65535u : dhz);
    m->rate_count = 0u;
    m->rate_from_ms = now_ms;
}

/**
 * @brief An angle frame: the reading valid at @p now_ms, the still time
 *        carried on or started again, the rate counted.
 * @param m      The model.
 * @param a      The decoded frame.
 * @param now_ms When it came.
 */
static void take_angle(imu_t *m, const wit_angle_t *a, uint32_t now_ms)
{
    const float v[3] = { a->roll, a->pitch, a->yaw };
    bool still = m->r.valid;
    for (int i = 0; still && i < 3; ++i) {
        if (fabsf(wrap180(v[i] - m->still_ref[i])) > IMU_STILL_DEG) {
            still = false;
        }
    }
    if (still) {
        const uint32_t held = now_ms - m->still_from_ms;
        m->r.still_ms = (uint16_t)((held > IMU_STILL_MAX_MS) ? IMU_STILL_MAX_MS
                                                              : held);
    } else {
        memcpy(m->still_ref, v, sizeof(m->still_ref));
        m->still_from_ms = now_ms;
        m->r.still_ms = 0u;
    }
    memcpy(m->r.angle, v, sizeof(m->r.angle));
    m->r.version  = a->version;
    m->r.taken_ms = now_ms;
    m->r.valid    = true;
    ++m->rate_count;
}

void imu_feed(imu_t *m, const uint8_t *b, size_t n, uint32_t now_ms)
{
    if (m == NULL || b == NULL) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        wit_frame_t f;
        if (!wit_parser_feed(&m->p, b[i], &f)) {
            continue;
        }
        ++m->r.frames;
        wit_acc_t acc;
        wit_gyro_t gyro;
        wit_angle_t ang;
        if (wit_decode_angle(&f, &ang)) {
            take_angle(m, &ang, now_ms);
        } else if (wit_decode_gyro(&f, &gyro)) {
            m->r.battery_v = gyro.battery_v;
        } else if (wit_decode_acc(&f, &acc)) {
            m->r.temp_c = acc.temp_c;
        }
    }
    m->r.bad = m->p.bad_sum + m->p.resyncs;
    imu_tick(m, now_ms);
}

void imu_tick(imu_t *m, uint32_t now_ms)
{
    if (m == NULL) {
        return;
    }
    if (m->r.valid && now_ms - m->r.taken_ms >= IMU_STALE_MS) {
        m->r.valid = false;
        m->r.still_ms = 0u;
    }
    rate_roll(m, now_ms);
}

bool imu_zero(imu_t *m)
{
    if (m == NULL || !m->r.valid) {
        return false;
    }
    memcpy(m->r.zero, m->r.angle, sizeof(m->r.zero));
    m->r.zeroed = true;
    return true;
}

void imu_unzero(imu_t *m)
{
    if (m == NULL) {
        return;
    }
    memset(m->r.zero, 0, sizeof(m->r.zero));
    m->r.zeroed = false;
}

bool imu_reading(const imu_t *m, imu_reading_t *out)
{
    if (m == NULL || out == NULL) {
        return false;
    }
    *out = m->r;
    return out->valid;
}

float imu_deg(const imu_reading_t *r, imu_axis_t axis, bool neg)
{
    if (r == NULL || !r->valid || (int)axis < 0 || (int)axis > 2) {
        return 0.0f;
    }
    const float d = wrap180(r->angle[axis] - (r->zeroed ? r->zero[axis]
                                                         : 0.0f));
    /* -180 negated is 180: kept inside the range as -180. */
    return (neg && d > -180.0f) ? -d : d;
}

const char *imu_axis_name(imu_axis_t axis)
{
    switch (axis) {
    case IMU_AXIS_X: return "X";
    case IMU_AXIS_Y: return "Y";
    case IMU_AXIS_Z: return "Z";
    default:         return "";
    }
}
