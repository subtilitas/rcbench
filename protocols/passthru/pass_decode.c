/**
 * @file
 * @brief The pass-through's pulse decoder.
 *
 * See pass_decode.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pass_decode.h"

#include <string.h>

/**
 * @brief A pin with no figure, its lost time from @p now_us.
 *
 * @param p       The pin.
 * @param now_us  The microsecond timer now.
 */
static void pin_clear(pass_pin_t *p, uint32_t now_us)
{
    memset(p, 0, sizeof(*p));
    p->ref_us = now_us;
}

void pass_dec_init(pass_dec_t *d, uint32_t now_us)
{
    memset(d, 0, sizeof(*d));
    for (unsigned b = 0; b < PASS_PINS; ++b) {
        pin_clear(&d->pin[b], now_us);
    }
}

void pass_dec_skip(pass_dec_t *d, uint32_t mask, uint32_t now_us)
{
    mask &= PASS_PIN_MASK;
    const uint32_t changed = mask ^ d->skip;
    for (unsigned b = 0; b < PASS_PINS; ++b) {
        if ((changed & (1u << b)) != 0u) {
            pin_clear(&d->pin[b], now_us);
        }
    }
    d->skip = mask;
}

uint32_t pass_dec_lost_us(const pass_pin_t *p)
{
    const uint32_t n = PASS_LOST_PERIODS * p->period_us;
    return (n > PASS_LOST_MIN_US) ? n : PASS_LOST_MIN_US;
}

/**
 * @brief The pin lost: no width, no period, no gap from the last pulse.
 *
 * @param p  The pin.
 */
static void pin_lose(pass_pin_t *p)
{
    p->lost       = true;
    p->width_us   = 0u;
    p->period_us  = 0u;
    p->odd_us     = 0u;
    p->have_frame = false;
}

/**
 * @brief A pulse taken: its gap from the last one judged, then it is the
 *        last one.
 *
 * @param p         The pin.
 * @param rise_us   Its rise.
 * @param width_us  Its width.
 */
static void take(pass_pin_t *p, uint32_t rise_us, uint32_t width_us)
{
    if (p->have_frame && !p->lost) {
        const uint32_t gap = rise_us - p->frame_us;
        if (gap >= pass_dec_lost_us(p)) {
            /* Lost between two passes: the gap is a loss, not frames. */
            p->period_us = 0u;
            p->odd_us    = 0u;
        } else if (p->period_us == 0u) {
            p->period_us = (gap <= PASS_PERIOD_MAX_US) ? gap : 0u;
        } else if (2u * (uint64_t)gap > 3u * (uint64_t)p->period_us) {
            const uint32_t o = p->odd_us;
            const uint32_t diff = (gap > o) ? gap - o : o - gap;
            if (o != 0u && 8u * (uint64_t)diff <= o
                && gap <= PASS_PERIOD_MAX_US) {
                /* A second long gap like the first: a new, slower rate. */
                p->period_us = gap;
                p->odd_us    = 0u;
            } else {
                p->missed += (gap + p->period_us / 2u) / p->period_us - 1u;
                p->odd_us = gap;
            }
        } else {
            p->period_us = gap;
            p->odd_us    = 0u;
        }
    }
    p->frame_us   = rise_us;
    p->ref_us     = rise_us;
    p->have_frame = true;
    p->lost       = false;
    p->width_us   = width_us;
    ++p->frames;
}

/**
 * @brief One pin's edge.
 *
 * @param p         The pin.
 * @param high      The level after it.
 * @param stamp_us  When.
 */
static void edge(pass_pin_t *p, bool high, uint32_t stamp_us)
{
    if (high) {
        p->rise_us   = stamp_us;
        p->rise_seen = true;
        return;
    }
    if (!p->rise_seen) {
        return;
    }
    p->rise_seen = false;
    const uint32_t w = stamp_us - p->rise_us;
    if (w < PASS_PULSE_MIN_US || w > PASS_PULSE_MAX_US) {
        ++p->glitches;
        return;
    }
    take(p, p->rise_us, w);
}

void pass_dec_event(pass_dec_t *d, uint32_t state, uint32_t stamp_us)
{
    state &= PASS_PIN_MASK;
    if (!d->started) {
        d->level   = state;
        d->started = true;
        return;
    }
    const uint32_t changed = (state ^ d->level) & ~d->skip;
    d->level = state;
    for (unsigned b = 0; b < PASS_PINS; ++b) {
        if ((changed & (1u << b)) != 0u) {
            edge(&d->pin[b], (state & (1u << b)) != 0u, stamp_us);
        }
    }
}

void pass_dec_now(pass_dec_t *d, uint32_t now_us)
{
    for (unsigned b = 0; b < PASS_PINS; ++b) {
        pass_pin_t *p = &d->pin[b];
        if ((d->skip & (1u << b)) != 0u || p->lost) {
            continue;
        }
        const uint32_t since = now_us - p->ref_us;
        /* A reference later than now, a stamp taken after the clock was
         * read, is no time passed. */
        if (since < 0x80000000u && since >= pass_dec_lost_us(p)) {
            pin_lose(p);
        }
    }
}

void pass_dec_restart(pass_dec_t *d, uint32_t level, uint32_t now_us)
{
    d->level   = level & PASS_PIN_MASK;
    d->started = true;
    for (unsigned b = 0; b < PASS_PINS; ++b) {
        pass_pin_t *p = &d->pin[b];
        p->rise_seen  = false;
        p->have_frame = false;
        p->odd_us     = 0u;
        p->ref_us     = now_us;
    }
}
