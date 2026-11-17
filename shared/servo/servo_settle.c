/**
 * @file
 * @brief The settle rule's window; see servo_settle.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_settle.h"

#include <math.h>
#include <string.h>

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

/**
 * @brief Empty one bucket.
 * @param s The window.
 * @param k Its bucket number, from from_ms.
 */
static void empty(servo_settle_t *s, uint32_t k)
{
    const uint32_t i = k % SERVO_SETTLE_BUCKETS;
    s->lo[i] = 1.0f;
    s->hi[i] = -1.0f;
}

void servo_settle_reset(servo_settle_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

void servo_settle_feed(servo_settle_t *s, float deg, uint32_t now_ms)
{
    if (s == NULL || !isfinite(deg)) {
        return;
    }
    if (!s->have) {
        s->have    = true;
        s->ref     = deg;
        s->from_ms = now_ms;
        s->last_ms = now_ms;
        s->last_k  = 0u;
        for (uint32_t k = 0; k < SERVO_SETTLE_BUCKETS; ++k) {
            empty(s, k);
        }
    }
    uint32_t k = s->last_k;
    if ((int32_t)(now_ms - s->last_ms) > 0) {
        s->last_ms = now_ms;
        k = (now_ms - s->from_ms) / SERVO_SETTLE_BUCKET_MS;
        /* The buckets passed over hold no reading: emptied, all of them
         * at most. */
        const uint32_t gap = k - s->last_k;
        const uint32_t n = gap < SERVO_SETTLE_BUCKETS ? gap
                                                      : SERVO_SETTLE_BUCKETS;
        for (uint32_t j = 1; j <= n; ++j) {
            empty(s, k - n + j);
        }
        s->last_k = k;
    }
    const uint32_t i = k % SERVO_SETTLE_BUCKETS;
    const float v = wrap180(deg - s->ref);
    if (s->lo[i] > s->hi[i]) {
        s->lo[i] = v;
        s->hi[i] = v;
    } else {
        s->lo[i] = fminf(s->lo[i], v);
        s->hi[i] = fmaxf(s->hi[i], v);
    }
}

float servo_settle_span(const servo_settle_t *s, uint32_t win_ms)
{
    if (s == NULL || !s->have) {
        return -1.0f;
    }
    if (win_ms > SERVO_SETTLE_WIN_MAX_MS) {
        win_ms = SERVO_SETTLE_WIN_MAX_MS;
    }
    const uint32_t age = s->last_ms - s->from_ms;
    if (age < win_ms) {
        return -1.0f;
    }
    /* The bucket that holds last_ms - win_ms, through the newest. */
    const uint32_t k0 = (age - win_ms) / SERVO_SETTLE_BUCKET_MS;
    float lo = 0.0f, hi = 0.0f;
    bool any = false;
    for (uint32_t k = k0; k <= s->last_k; ++k) {
        const uint32_t i = k % SERVO_SETTLE_BUCKETS;
        if (s->lo[i] > s->hi[i]) {
            continue;
        }
        lo  = any ? fminf(lo, s->lo[i]) : s->lo[i];
        hi  = any ? fmaxf(hi, s->hi[i]) : s->hi[i];
        any = true;
    }
    return any ? hi - lo : -1.0f;
}

bool servo_settle_held(const servo_settle_t *s, uint32_t win_ms,
                       float band_deg)
{
    const float span = servo_settle_span(s, win_ms);
    return span >= 0.0f && span <= band_deg + SERVO_SETTLE_EPS_DEG;
}
