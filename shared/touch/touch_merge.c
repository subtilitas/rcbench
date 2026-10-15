/*
 * SPDX-License-Identifier: MIT
 */

#include "touch_merge.h"

#include <string.h>

static int clamp_count(int n)
{
    if (n < 0) {
        return 0;
    }
    return n > TOUCH_MAX_POINTS ? TOUCH_MAX_POINTS : n;
}

static bool same_point(const touch_point_t *a, const touch_point_t *b)
{
    return a->id == b->id && a->x == b->x && a->y == b->y
           && a->strength == b->strength;
}

void touch_merge_reset(touch_merge_t *m)
{
    if (m != NULL) {
        memset(m, 0, sizeof(*m));
    }
}

void touch_merge_glass(touch_merge_t *m, const touch_point_t *pts, int n)
{
    if (m == NULL) {
        return;
    }
    n = (pts != NULL) ? clamp_count(n) : 0;
    for (int i = 0; i < n; ++i) {
        m->glass[i] = pts[i];
    }
    m->glass_n = (uint8_t)n;
}

bool touch_merge_inject_due(const touch_merge_t *m, uint32_t now_ms)
{
    if (m == NULL) {
        return false;
    }
    if (m->fails < TOUCH_MERGE_FAIL_DROP) {
        return true;
    }
    return (uint32_t)(now_ms - m->last_try_ms) >= TOUCH_MERGE_RETRY_MS;
}

bool touch_merge_inject(touch_merge_t *m, const touch_point_t *pts, int n,
                        bool fresh, uint32_t now_ms)
{
    if (m == NULL) {
        return false;
    }
    m->fails = 0;
    m->last_try_ms = now_ms;
    if (!fresh) {
        return false;
    }
    n = (pts != NULL) ? clamp_count(n) : 0;
    bool changed = (n != m->inject_n);
    for (int i = 0; i < n; ++i) {
        touch_point_t p = pts[i];
        p.id = (uint8_t)(p.id | TOUCH_MERGE_INJECT_ID);
        if (!changed && !same_point(&p, &m->inject[i])) {
            changed = true;
        }
        m->inject[i] = p;
    }
    m->inject_n = (uint8_t)n;
    return changed;
}

bool touch_merge_inject_failed(touch_merge_t *m, uint32_t now_ms)
{
    if (m == NULL) {
        return false;
    }
    m->last_try_ms = now_ms;
    if (m->fails < TOUCH_MERGE_FAIL_DROP) {
        ++m->fails;
    }
    if (m->fails < TOUCH_MERGE_FAIL_DROP || m->inject_n == 0u) {
        return false;
    }
    m->inject_n = 0;
    return true;
}

int touch_merge_frame(const touch_merge_t *m, touch_point_t *out, int max)
{
    if (m == NULL || out == NULL || max <= 0) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < m->glass_n && n < max; ++i) {
        out[n++] = m->glass[i];
    }
    for (int i = 0; i < m->inject_n && n < max; ++i) {
        out[n++] = m->inject[i];
    }
    return n;
}
