/**
 * @file
 * @brief The glass's contacts and a touch injector's, merged (touch_merge.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "touch_merge.h"

#include <string.h>

/** @brief @p n held to 0..TOUCH_MAX_POINTS. */
static int clamp_count(int n)
{
    if (n < 0) {
        return 0;
    }
    return n > TOUCH_MAX_POINTS ? TOUCH_MAX_POINTS : n;
}

/** @brief The same contact at the same place and strength. */
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

void touch_merge_attach(touch_merge_t *m, uint32_t now_ms)
{
    if (m == NULL) {
        return;
    }
    touch_merge_reset(m);
    m->ok_ms = now_ms;
    m->last_try_ms = now_ms;
}

void touch_merge_start(touch_merge_t *m, bool answered, uint32_t now_ms)
{
    if (m == NULL) {
        return;
    }
    if (answered) {
        touch_merge_attach(m, now_ms);
        return;
    }
    /* Detached, so every read and age path refuses it; absent, so the probe
     * path does too. */
    touch_merge_reset(m);
    m->detached = true;
    m->absent = true;
}

bool touch_merge_attached(const touch_merge_t *m)
{
    return m != NULL && !m->detached;
}

bool touch_merge_expire(touch_merge_t *m, uint32_t now_ms)
{
    if (m == NULL || m->detached
        || (uint32_t)(now_ms - m->ok_ms) < TOUCH_MERGE_DETACH_MS) {
        return false;
    }
    /* The tracker sends an UP for each contact dropped here, and nothing
     * the injector reported before the detach comes back after it. */
    m->detached = true;
    m->inject_n = 0;
    m->fails = 0;
    m->last_try_ms = now_ms;
    return true;
}

bool touch_merge_probe_due(const touch_merge_t *m, uint32_t now_ms)
{
    return m != NULL && m->detached && !m->absent
           && (uint32_t)(now_ms - m->last_try_ms) >= TOUCH_MERGE_PROBE_MS;
}

bool touch_merge_probe(touch_merge_t *m, bool answered, uint32_t now_ms)
{
    if (m == NULL || !m->detached || m->absent) {
        return false;
    }
    m->last_try_ms = now_ms;
    if (!answered) {
        return false;
    }
    /* Back with no contacts: the first report read after this is the
     * injector's whole set. */
    m->detached = false;
    m->inject_n = 0;
    m->fails = 0;
    m->ok_ms = now_ms;
    return true;
}

/**
 * @brief Decide which injected contacts the frame has room for.
 *
 * The room is what the glass leaves.  A contact shown before keeps its
 * place before a new one gets any, in report order; a cut one stays cut.
 *
 * @param m The merge.
 */
static void recut(touch_merge_t *m)
{
    int room = TOUCH_MAX_POINTS - (int)m->glass_n;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < m->inject_n; ++i) {
            if (m->cut[i] || (pass == 0) != m->shown[i]) {
                continue;
            }
            if (room > 0) {
                --room;
                m->shown[i] = true;
            } else {
                m->shown[i] = false;
                m->cut[i] = true;
            }
        }
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
    recut(m);
}

bool touch_merge_inject_due(const touch_merge_t *m, uint32_t now_ms)
{
    if (m == NULL || m->detached) {
        return false;
    }
    if (m->fails < TOUCH_MERGE_FAIL_DROP) {
        return true;
    }
    return (uint32_t)(now_ms - m->last_try_ms) >= TOUCH_MERGE_RETRY_MS;
}

/** @brief Whether two merged sets differ. */
static bool frames_differ(const touch_point_t *a, int na,
                          const touch_point_t *b, int nb)
{
    if (na != nb) {
        return true;
    }
    for (int i = 0; i < na; ++i) {
        if (!same_point(&a[i], &b[i])) {
            return true;
        }
    }
    return false;
}

bool touch_merge_inject(touch_merge_t *m, const touch_point_t *pts, int n,
                        bool fresh, uint32_t now_ms)
{
    if (m == NULL || m->detached) {
        return false;
    }
    m->fails = 0;
    m->last_try_ms = now_ms;
    m->ok_ms = now_ms;
    if (!fresh) {
        return false;
    }
    touch_point_t before[TOUCH_MAX_POINTS];
    const int nb = touch_merge_frame(m, before, TOUCH_MAX_POINTS);

    n = (pts != NULL) ? clamp_count(n) : 0;
    bool cut[TOUCH_MAX_POINTS] = { false };
    bool shown[TOUCH_MAX_POINTS] = { false };
    for (int i = 0; i < n; ++i) {
        const uint8_t id = (uint8_t)(pts[i].id | TOUCH_MERGE_INJECT_ID);
        /* The same track id in the last report: the same contact. */
        for (int k = 0; k < m->inject_n; ++k) {
            if (m->inject[k].id == id) {
                cut[i] = m->cut[k];
                shown[i] = m->shown[k];
                break;
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        m->inject[i] = pts[i];
        m->inject[i].id = (uint8_t)(pts[i].id | TOUCH_MERGE_INJECT_ID);
        m->cut[i] = cut[i];
        m->shown[i] = shown[i];
    }
    m->inject_n = (uint8_t)n;
    recut(m);

    touch_point_t after[TOUCH_MAX_POINTS];
    const int na = touch_merge_frame(m, after, TOUCH_MAX_POINTS);
    return frames_differ(before, nb, after, na);
}

bool touch_merge_inject_failed(touch_merge_t *m, uint32_t now_ms)
{
    if (m == NULL || m->detached) {
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
        if (m->shown[i]) {
            out[n++] = m->inject[i];
        }
    }
    return n;
}

int touch_merge_mark_cut(const touch_merge_t *m, touch_event_t *evts, int n)
{
    if (m == NULL || evts == NULL) {
        return 0;
    }
    int marked = 0;
    for (int e = 0; e < n; ++e) {
        if (evts[e].type != TOUCH_EVENT_UP
            || (evts[e].point.id & TOUCH_MERGE_INJECT_ID) == 0u) {
            continue;
        }
        for (int i = 0; i < m->inject_n; ++i) {
            if (m->cut[i] && m->inject[i].id == evts[e].point.id) {
                evts[e].flags = (uint8_t)(evts[e].flags | TOUCH_FLAG_NO_TAP);
                ++marked;
                break;
            }
        }
    }
    return marked;
}
