/**
 * @file
 * @brief The index pulse found in a sampled channel (vibe_edge.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "vibe_edge.h"

void vibe_edge_init(vibe_edge_t *e, int16_t lo, int16_t hi, bool rising)
{
    if (e == NULL) {
        return;
    }
    int32_t a = lo;
    int32_t b = hi;
    if (b < a) {
        const int32_t t = a;
        a = b;
        b = t;
    }
    e->sign = rising ? 1 : -1;
    /* A falling mark is a rising one of the negated signal. */
    e->lo = rising ? a : -b;
    e->hi = rising ? b : -a;
    e->mark = false;
    e->have_prev = false;
    e->prev = 0;
    e->pos_q8 = 0u;
}

size_t vibe_edge_scan(vibe_edge_t *e, const int16_t *x, size_t n,
                      uint32_t *out, size_t max)
{
    if (e == NULL || x == NULL) {
        return 0u;
    }
    const int32_t mid = (e->lo + e->hi) / 2;
    size_t got = 0u;
    for (size_t i = 0; i < n; ++i) {
        const int32_t v = e->sign * (int32_t)x[i];
        if (!e->mark && v >= e->hi) {
            e->mark = true;
            uint32_t at = e->pos_q8;
            if (e->have_prev && v > e->prev) {
                int32_t frac = ((mid - e->prev) * 256) / (v - e->prev);
                if (frac < 0) {
                    frac = 0;
                } else if (frac > 256) {
                    frac = 256;
                }
                at = e->pos_q8 - 256u + (uint32_t)frac;
            }
            if (out != NULL && got < max) {
                out[got] = at;
            }
            ++got;
        } else if (e->mark && v <= e->lo) {
            e->mark = false;
        }
        e->prev = v;
        e->have_prev = true;
        e->pos_q8 += 256u;
    }
    return (got > max) ? max : got;
}

void vibe_index_init(vibe_index_t *x, int16_t lo, int16_t hi)
{
    if (x == NULL) {
        return;
    }
    vibe_edge_init(&x->rise, lo, hi, true);
    vibe_edge_init(&x->fall, lo, hi, false);
    x->mid = ((int32_t)lo + (int32_t)hi) / 2;
    x->high = 0u;
    x->total = 0u;
    x->low_mark = false;
}

size_t vibe_index_scan(vibe_index_t *x, const int16_t *s, size_t n,
                       uint32_t *out, size_t max)
{
    if (x == NULL || s == NULL) {
        return 0u;
    }
    /* Both comparators see every sample, so either one's edges hold the
     * moment the polarity changes. */
    size_t got;
    if (x->low_mark) {
        got = vibe_edge_scan(&x->fall, s, n, out, max);
        (void)vibe_edge_scan(&x->rise, s, n, NULL, 0u);
    } else {
        got = vibe_edge_scan(&x->rise, s, n, out, max);
        (void)vibe_edge_scan(&x->fall, s, n, NULL, 0u);
    }
    for (size_t i = 0; i < n; ++i) {
        if ((int32_t)s[i] >= x->mid) {
            ++x->high;
        }
        if (++x->total >= VIBE_INDEX_SPAN) {
            x->low_mark = 2u * x->high > x->total;
            x->high = 0u;
            x->total = 0u;
        }
    }
    return got;
}
