/**
 * @file
 * @brief The IMU's reading (imu.h): frames from protocols/witmotion into
 *        the newest angles through the low-pass, the frame rate, battery,
 *        temperature, the zero, the still time and the age.
 *
 * Only an angle frame (0x53) makes the reading valid; it goes stale
 * IMU_STALE_MS after the last one, the non-safety allowance of servo_gap.h.
 * Every time is an unsigned difference of the 32-bit ms clock, so the age,
 * the still time and the rate window hold across its wrap.
 *
 * The still time is the sensor's and is kept per axis: an axis' angle
 * within IMU_STILL_DEG of the first frame of that axis' stretch.  A move
 * on one axis starts only that axis' stretch again; pitch creeping 1°/s
 * leaves a still yaw's still time running.  An axis' still time reads 0
 * until its filtered angle has settled too.  The filter's distance from
 * the sensor's angle of that axis at the stretch's first frame shrinks by
 * (1 - 1/k) each frame after it, the sensor's angle held; once that is
 * IMU_SETTLE_DEG or less the filtered angle has settled, and the still
 * time counts from the stretch's start again.  A run that waits for the
 * still time of the axis it reads (imu_still_ms()) then takes a settled
 * angle, and the servo test times a move to where the sensor stopped.
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief The longest still time kept, ms: the field's range. */
#define IMU_STILL_MAX_MS 65535u
/** @brief The largest distance the filter can have from the sensor, °:
 *         the wrapped difference. */
#define IMU_FILTER_SPAN_DEG 180.0f

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
    m->filter      = IMU_FILTER_DEFAULT;
    m->seed        = true;
}

void imu_set_filter(imu_t *m, int32_t filter)
{
    if (m != NULL) {
        m->filter = imu_filter_clamp(filter);
    }
}

void imu_reseed(imu_t *m)
{
    if (m != NULL) {
        m->seed = true;
    }
}

void imu_content_restart(imu_t *m)
{
    if (m != NULL) {
        m->content = 0u;
        m->content_n = 0u;
    }
}

uint8_t imu_filter_clamp(int32_t v)
{
    if (v < (int32_t)IMU_FILTER_MIN) {
        return (uint8_t)IMU_FILTER_MIN;
    }
    return (uint8_t)((v > (int32_t)IMU_FILTER_MAX) ? IMU_FILTER_MAX
                                                    : (uint32_t)v);
}

float imu_filter_step(float deg, float now, int32_t filter)
{
    const uint8_t k = imu_filter_clamp(filter);
    if (k == 1u) {
        return now;
    }
    return wrap180(deg + wrap180(now - deg) / (float)k);
}

uint32_t imu_filter_settle_ms(int32_t filter, uint16_t rate_dhz)
{
    const uint8_t k = imu_filter_clamp(filter);
    if (k == 1u) {
        return 0u;
    }
    /* (1 - 1/k)^n <= IMU_SETTLE_DEG / IMU_FILTER_SPAN_DEG. */
    const double n = ceil(log((double)IMU_SETTLE_DEG
                              / (double)IMU_FILTER_SPAN_DEG)
                          / log(1.0 - 1.0 / (double)k));
    /* 1.5 periods of the rate measured, 15000 / dHz ms, rounded up. */
    uint32_t per = IMU_STALE_MS;
    if (rate_dhz > 0u) {
        const uint32_t p = (15000u + rate_dhz - 1u) / rate_dhz;
        per = (p < IMU_STALE_MS) ? p : IMU_STALE_MS;
    }
    return (uint32_t)n * per;
}

const char *imu_deg_text(char *buf, size_t n, float deg, bool plus)
{
    if (buf == NULL || n == 0u) {
        return buf;
    }
    if (!isfinite(deg) || fabsf(deg) > 100000.0f) {
        snprintf(buf, n, "---");
        return buf;
    }
    /* In float, as the angle is: 0.005f is 0.5 hundredths and rounds up. */
    const long q = lroundf(deg * 100.0f);
    const unsigned long a = (unsigned long)((q < 0) ? -q : q);
    snprintf(buf, n, "%s%lu.%02lu", (q < 0) ? "-" : (plus ? "+" : ""),
             a / 100ul, a % 100ul);
    return buf;
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
 * @brief Expire the reading once IMU_STALE_MS passed since its last angle
 *        frame: valid and the still time go.
 * @param m      The model.
 * @param now_ms The time now.
 */
static void expire(imu_t *m, uint32_t now_ms)
{
    if (m->r.valid && now_ms - m->r.taken_ms >= IMU_STALE_MS) {
        m->r.valid = false;
        memset(m->r.still_ms, 0, sizeof(m->r.still_ms));
    }
}

/**
 * @brief The filter's step on every axis, or its seed: the first frame
 *        after a reseed or a stale reading.
 * @param m   The model.
 * @param v   The frame's angles, °.
 * @param far Takes the distance left between each axis' filtered angle
 *            and @p v, °.
 */
static void filter_take(imu_t *m, const float v[3], float far[3])
{
    const bool seed = m->seed || !m->r.valid;
    for (int i = 0; i < 3; ++i) {
        m->r.angle[i] = seed ? v[i]
                             : imu_filter_step(m->r.angle[i], v[i], m->filter);
        far[i] = fabsf(wrap180(v[i] - m->r.angle[i]));
    }
    m->seed = false;
}

/**
 * @brief One axis' still time after an angle frame: carried on while the
 *        sensor's angle of @p i stays within IMU_STILL_DEG of its
 *        stretch's first frame, started again otherwise.
 * @param m      The model.
 * @param i      The axis, 0 roll, 1 pitch, 2 yaw.
 * @param v      The frame's angle of that axis, °.
 * @param far    That axis' filtered angle's distance from @p v, °.
 * @param live   The reading was valid before this frame.
 * @param now_ms When the frame came.
 */
static void still_take(imu_t *m, int i, float v, float far, bool live,
                       uint32_t now_ms)
{
    if (live && fabsf(wrap180(v - m->still_ref[i])) <= IMU_STILL_DEG) {
        if (!m->settled[i]) {
            m->resid[i] *= 1.0f - 1.0f / (float)imu_filter_clamp(m->filter);
            m->settled[i] = m->resid[i] <= IMU_SETTLE_DEG;
        }
        /* The lag now, too: a drift inside IMU_STILL_DEG keeps the still
         * stretch but can open the distance again after it settled. */
        const bool near = far <= IMU_SETTLE_DEG;
        const uint32_t held = now_ms - m->still_from_ms[i];
        m->r.still_ms[i] = (!m->settled[i] || !near) ? 0u
                           : (uint16_t)((held > IMU_STILL_MAX_MS)
                                            ? IMU_STILL_MAX_MS : held);
    } else {
        m->still_ref[i]     = v;
        m->still_from_ms[i] = now_ms;
        m->r.still_ms[i]    = 0u;
        m->resid[i]   = far;
        m->settled[i] = false;  /* judged from the next still frame on */
    }
}

/**
 * @brief An angle frame: the reading valid at @p now_ms, the filter
 *        stepped, each axis' still time carried on or started again, the
 *        rate counted.
 * @param m      The model.
 * @param a      The decoded frame.
 * @param now_ms When it came.
 */
static void take_angle(imu_t *m, const wit_angle_t *a, uint32_t now_ms)
{
    const float v[3] = { a->roll, a->pitch, a->yaw };
    /* A gap that no tick saw yet ends the old reading before this frame:
     * the frame seeds the filter and starts the still time again. */
    expire(m, now_ms);
    const bool live = m->r.valid;
    float far[3];
    filter_take(m, v, far);
    for (int i = 0; i < 3; ++i) {
        still_take(m, i, v[i], far[i], live, now_ms);
    }
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
        m->content |= wit_rsw_bit(f.type);
        if (m->content_n < UINT8_MAX) {
            ++m->content_n;
        }
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
    expire(m, now_ms);
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

uint16_t imu_still_ms(const imu_reading_t *r, imu_axis_t axis)
{
    if (r == NULL || !r->valid || (int)axis < 0 || (int)axis > 2) {
        return 0u;
    }
    return r->still_ms[axis];
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
