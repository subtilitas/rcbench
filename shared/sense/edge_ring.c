/*
 * The tap's edge words and their ring.  See edge_ring.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "edge_ring.h"

#include <string.h>

#define HALF (1u << (EDGE_COUNT_BITS - 1u))

uint32_t edge_word(uint32_t count, bool level)
{
    return ((count & EDGE_COUNT_MASK) << 1) | (level ? 1u : 0u);
}

uint32_t edge_word_count(uint32_t word)
{
    return word >> 1;
}

bool edge_word_level(uint32_t word)
{
    return (word & 1u) != 0u;
}

uint64_t edge_tick(uint32_t count, uint64_t est)
{
    /* The counter counts down from 0: after k ticks it holds -k. */
    const uint32_t k = (0u - count) & EDGE_COUNT_MASK;
    const uint32_t e = (uint32_t)(est & EDGE_COUNT_MASK);
    uint32_t d = (k - e) & EDGE_COUNT_MASK;
    if (d < HALF) {
        return est + d;
    }
    const uint32_t back = (1u << EDGE_COUNT_BITS) - d;   /* 1 .. 2^30 */
    return est >= back ? est - back : 0u;
}

void edge_ring_init(edge_ring_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof(*r));
    }
}

size_t edge_ring_take(edge_ring_t *r, const uint32_t *ring, uint32_t size,
                      uint32_t wr, uint64_t est, tone_edge_t *out, size_t max,
                      bool *overrun)
{
    if (overrun != NULL) {
        *overrun = false;
    }
    if (r == NULL || ring == NULL || out == NULL || size == 0u
        || (size & (size - 1u)) != 0u) {
        return 0u;
    }
    const uint32_t mask = size - 1u;
    wr &= mask;
    r->rd &= mask;
    /* The slot of the last word taken holds another word: the DMA has
     * written the whole ring since. */
    if (r->have_last && ring[(r->rd - 1u) & mask] != r->last) {
        ++r->overruns;
        r->rd = wr;
        r->last = ring[(wr - 1u) & mask];
        if (overrun != NULL) {
            *overrun = true;
        }
        return 0u;
    }
    size_t n = 0;
    while (r->rd != wr && n < max) {
        const uint32_t w = ring[r->rd];
        out[n].t = edge_tick(edge_word_count(w), est);
        out[n].level = edge_word_level(w);
        ++n;
        r->last = w;
        r->have_last = true;
        r->rd = (r->rd + 1u) & mask;
    }
    return n;
}
