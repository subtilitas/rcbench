/**
 * @file
 * @brief The capture's ring.
 *
 * See pass_ring.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pass_ring.h"

/** @brief An index into the ring. */
#define WRAP(i) ((uint32_t)(i) & (PASS_RING_EVENTS - 1u))

_Static_assert((PASS_RING_EVENTS & (PASS_RING_EVENTS - 1u)) == 0u,
               "the ring is a power of two");

void pass_ring_init(pass_ring_t *r)
{
    r->rd = 0u;
    r->last_state = 0u;
    r->last_stamp = 0u;
}

/**
 * @brief Whether the slot before the reader still holds the event read
 *        there.
 *
 * @param r       The reader.
 * @param states  The states.
 * @param stamps  The stamps.
 * @return        true when it does.
 */
static bool intact(const pass_ring_t *r, const volatile uint32_t *states,
                   const volatile uint32_t *stamps)
{
    const uint32_t p = WRAP(r->rd - 1u);
    return states[p] == r->last_state && stamps[p] == r->last_stamp;
}

pass_drain_t pass_ring_drain(pass_ring_t *r,
                             const volatile uint32_t *states,
                             const volatile uint32_t *stamps,
                             uint32_t head, uint32_t now_us, pass_dec_t *d)
{
    pass_drain_t out = { 0u, false };
    const uint32_t h = WRAP(head);
    bool ok = intact(r, states, stamps);
    if (ok) {
        const uint32_t n = WRAP(h - r->rd);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t s = WRAP(r->rd + i);
            pass_dec_event(d, states[s], stamps[s]);
        }
        out.events = n;
        /* The DMA reaches the slot before the reader's first: unchanged,
         * nothing just read was overwritten while it was read. */
        ok = intact(r, states, stamps);
    }
    const uint32_t newest = WRAP(h - 1u);
    r->rd = h;
    r->last_state = states[newest];
    r->last_stamp = stamps[newest];
    if (!ok) {
        out.lapped = true;
        pass_dec_restart(d, r->last_state, now_us);
    }
    pass_dec_now(d, now_us);
    return out;
}
