/**
 * @file sense_limit_link.c
 * @brief SENSE_LIMIT, the panel's half.  See sense_limit_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_limit_link.h"

#include <string.h>

void sense_limit_link_init(sense_limit_link_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

void sense_limit_link_lost(sense_limit_link_t *s)
{
    if (s == NULL) {
        return;
    }
    s->up         = false;
    s->page       = false;
    s->known      = false;
    s->refused    = false;
    s->bad_page   = false;
    s->flushed    = false;
    s->pre_read   = false;
    s->pending    = SENSE_LIMIT_LINK_OP_NONE;
    s->asked      = false;
    s->over       = 0u;
    s->lost       = false;
    s->ev_refused = false;
    memset(s->held, 0, sizeof(s->held));
    /* have_count and count stay: the link-up read compares with them. */
}

void sense_limit_link_came_up(sense_limit_link_t *s, uint16_t minor,
                              uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    sense_limit_link_lost(s);
    s->up   = true;
    s->page = minor >= LINK_MINOR_SENSE_LIMIT;
    /* A limit asked before the link rests from now, as an edit does. */
    s->want_ms = now_ms;
}

void sense_limit_link_want(sense_limit_link_t *s, const uint16_t *ma,
                           uint32_t now_ms)
{
    if (s == NULL || ma == NULL) {
        return;
    }
    uint16_t next[LINK_SL_KEPT];
    for (unsigned c = 0u; c < LINK_SL_KEPT; ++c) {
        next[c] = (ma[c] > LINK_SL_MA_MAX) ? (uint16_t)LINK_SL_MA_MAX : ma[c];
    }
    if (s->want_set && memcmp(next, s->want, sizeof(next)) == 0) {
        return;
    }
    memcpy(s->want, next, sizeof(next));
    s->want_set   = true;
    s->want_ms    = now_ms;
    s->refused    = false;     /* a new question */
    s->ev_refused = false;
    s->flushed    = false;
}

static bool any_limit(const uint16_t *ma)
{
    for (unsigned c = 0u; c < LINK_SL_KEPT; ++c) {
        if (ma[c] != 0u) {
            return true;
        }
    }
    return false;
}

static bool owed(const sense_limit_link_t *s)
{
    return s->want_set && !s->refused
           && memcmp(s->want, s->held, sizeof(s->want)) != 0;
}

bool sense_limit_link_next(sense_limit_link_t *s, uint32_t now_ms,
                           sense_limit_link_op_t *op)
{
    if (s == NULL || op == NULL) {
        return false;
    }
    memset(op, 0, sizeof(*op));
    s->pending = SENSE_LIMIT_LINK_OP_NONE;
    if (!s->up || !s->page) {
        return false;
    }
    if (!s->known) {
        op->kind = SENSE_LIMIT_LINK_OP_READ;
        op->off  = 0u;
        op->n    = (uint8_t)LINK_SL_COUNT;
        s->pending = op->kind;
        return true;
    }
    const bool write = owed(s)
        && (uint32_t)(now_ms - s->want_ms) >= SENSE_LIMIT_LINK_SETTLE_MS;
    if (write && any_limit(s->held) && !s->flushed) {
        /* The bits judged under the limits held, before they change. */
        op->kind = SENSE_LIMIT_LINK_OP_OVER;
        op->off  = (uint8_t)LINK_SL_OVER;
        op->n    = 2u;
        s->asked    = true;
        s->asked_ms = now_ms;
        s->pending  = op->kind;
        s->pre_read = true;
        return true;
    }
    if (write) {
        op->kind  = SENSE_LIMIT_LINK_OP_WRITE;
        op->write = true;
        op->off   = (uint8_t)LINK_SL_LIMIT_0;
        op->n     = (uint8_t)LINK_SL_KEPT;
        op->regs  = s->want;
        s->pending = op->kind;
        return true;
    }
    if (any_limit(s->held)
        && (!s->asked
            || (uint32_t)(now_ms - s->asked_ms) >= SENSE_LIMIT_LINK_READ_MS)) {
        op->kind = SENSE_LIMIT_LINK_OP_OVER;
        op->off  = (uint8_t)LINK_SL_OVER;
        op->n    = 2u;
        s->asked    = true;
        s->asked_ms = now_ms;
        s->pending  = op->kind;
        return true;
    }
    return false;
}

/* OVER and OVER_COUNT as a read returned them, each bit with the limit
 * the page held, the one it was judged by.  A count that moved with no
 * bit is a reply lost, its bits with it; a count of 0 is an IOmcu that
 * started again with no window over since. */
static void take_over(sense_limit_link_t *s, uint16_t over, uint16_t count)
{
    const uint16_t mask = (uint16_t)((1u << LINK_SL_KEPT) - 1u);
    over = (uint16_t)(over & mask);
    for (unsigned c = 0u; c < LINK_SL_KEPT; ++c) {
        if ((over & (1u << c)) != 0u) {
            s->ch_lim[c] = s->held[c];
        }
    }
    s->over = (uint16_t)(s->over | over);
    if (s->have_count && count != s->count && count != 0u && over == 0u) {
        s->lost = true;
    }
    s->count      = count;
    s->have_count = true;
}

void sense_limit_link_done(sense_limit_link_t *s, int result,
                           const uint16_t *regs, uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    const sense_limit_link_op_kind_t op = s->pending;
    const bool pre = s->pre_read;
    s->pending  = SENSE_LIMIT_LINK_OP_NONE;
    s->pre_read = false;
    if (result == SENSE_LIMIT_LINK_NO_ANSWER
        || op == SENSE_LIMIT_LINK_OP_NONE) {
        return;                         /* owed still: asked again */
    }
    if (result == (int)LINK_NACK_BAD_PAGE) {
        /* An IOmcu that names 4.27 or later and has not the page: nothing
         * more goes to it until the link comes up again, said as a
         * refusal. */
        s->page       = false;
        s->bad_page   = true;
        s->ev_refused = true;
        return;
    }
    if (op == SENSE_LIMIT_LINK_OP_WRITE) {
        if (result == SENSE_LIMIT_LINK_ACK) {
            memcpy(s->held, s->want, sizeof(s->held));
        } else {
            s->refused    = true;
            s->ev_refused = true;
        }
        s->flushed = false;
        return;
    }
    if (result != SENSE_LIMIT_LINK_ACK || regs == NULL) {
        return;                         /* read again on its clock */
    }
    if (op == SENSE_LIMIT_LINK_OP_READ) {
        memcpy(s->held, regs, sizeof(s->held));
        s->known = true;
        /* It read OVER too: the next OVER read is a period on. */
        s->asked    = true;
        s->asked_ms = now_ms;
        take_over(s, regs[LINK_SL_OVER], regs[LINK_SL_OVER_COUNT]);
    } else {
        take_over(s, regs[0], regs[1]);
        s->flushed = s->flushed || pre;
    }
}

sense_limit_link_ev_t sense_limit_link_event(sense_limit_link_t *s,
                                             uint32_t now_ms, unsigned *ch,
                                             uint16_t *ma)
{
    if (s == NULL) {
        return SENSE_LIMIT_LINK_EV_NONE;
    }
    /* A channel told within the repeat time is not told again. */
    for (unsigned c = 0u; c < LINK_SL_KEPT; ++c) {
        const uint16_t bit = (uint16_t)(1u << c);
        const uint32_t age = (uint32_t)(now_ms - s->ch_ms[c]);
        if ((s->told_ch & bit) != 0u && age >= SENSE_LIMIT_LINK_REPEAT_MS) {
            s->told_ch = (uint16_t)(s->told_ch & ~bit);
        }
    }
    s->over = (uint16_t)(s->over & ~s->told_ch);
    if (s->told && (uint32_t)(now_ms - s->told_ms) < SENSE_LIMIT_LINK_GAP_MS) {
        return SENSE_LIMIT_LINK_EV_NONE;
    }
    sense_limit_link_ev_t ev = SENSE_LIMIT_LINK_EV_NONE;
    if (s->ev_refused) {
        s->ev_refused = false;
        ev = SENSE_LIMIT_LINK_EV_REFUSED;
    } else if (s->over != 0u) {
        /* In turn from the channel after the one told last. */
        unsigned c = s->turn;
        while ((s->over & (1u << c)) == 0u) {
            c = (c + 1u) % LINK_SL_KEPT;
        }
        s->over    = (uint16_t)(s->over & ~(1u << c));
        s->told_ch = (uint16_t)(s->told_ch | (1u << c));
        s->ch_ms[c] = now_ms;
        s->turn    = (uint8_t)((c + 1u) % LINK_SL_KEPT);
        if (ch != NULL) {
            *ch = c;
        }
        if (ma != NULL) {
            *ma = s->ch_lim[c];
        }
        ev = SENSE_LIMIT_LINK_EV_OVER;
    } else if (s->lost) {
        s->lost = false;
        ev = SENSE_LIMIT_LINK_EV_LOST;
    }
    if (ev != SENSE_LIMIT_LINK_EV_NONE) {
        s->told    = true;
        s->told_ms = now_ms;
    }
    return ev;
}

uint16_t sense_limit_link_held_ma(const sense_limit_link_t *s, unsigned ch)
{
    return (s != NULL && s->known && ch < LINK_SL_KEPT) ? s->held[ch] : 0u;
}

sense_limit_st_t sense_limit_link_status(const sense_limit_link_t *s)
{
    if (s == NULL || !s->up) {
        return SENSE_LIMIT_ST_UNKNOWN;
    }
    if (!s->page) {
        return s->bad_page ? SENSE_LIMIT_ST_REFUSED : SENSE_LIMIT_ST_OLD;
    }
    if (!s->known) {
        return SENSE_LIMIT_ST_UNKNOWN;
    }
    if (s->refused) {
        return SENSE_LIMIT_ST_REFUSED;
    }
    return owed(s) ? SENSE_LIMIT_ST_PENDING : SENSE_LIMIT_ST_HELD;
}

uint16_t sense_limit_link_unheld(const sense_limit_link_t *s)
{
    if (s == NULL || !s->up || !s->want_set || (s->page && !s->known)) {
        return 0u;
    }
    uint16_t bits = 0u;
    for (unsigned c = 0u; c < LINK_SL_KEPT; ++c) {
        const bool mark = s->page ? s->want[c] != s->held[c]
                                  : s->want[c] != 0u;
        bits = (uint16_t)(bits | (mark ? (1u << c) : 0u));
    }
    return bits;
}
