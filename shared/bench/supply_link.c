/*
 * The panel's half of the SUPPLY link page.  See supply_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_link.h"

#include <stddef.h>
#include <string.h>

/* The page's own bounds on the set points (shared/outputs/supply_page.c):
 * it refuses more and stores less as the module's least, so both ends are
 * kept here and a read-back matches what was written. */
#define PAGE_MIN_MV  1000u
#define PAGE_MAX_MV 20000u
#define PAGE_MIN_MA    50u
#define PAGE_MAX_MA  3000u

void supply_link_init(supply_link_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->want.tx = -1;
        s->want.rx = -1;
        s->want.baud = 1u;
        s->mv = (uint16_t)PAGE_MIN_MV;
        s->ma = (uint16_t)PAGE_MIN_MA;
    }
}

void supply_link_lost(supply_link_t *s)
{
    if (s == NULL) {
        return;
    }
    s->wired     = false;
    s->refused   = false;
    s->commanded = false;
    s->pending   = SUPPLY_LINK_W_NONE;
    s->read_any  = false;
    s->asked     = false;
    /* The caller sees the supply stop answering and switches its own ON
     * off with the operator told; this end only stops asking for it. */
    s->on = false;
}

/* The wiring as the page is to hold it: disabled unless both pins are set
 * and differ, with unset pins written as 0. */
static supply_wiring_t as_written(const supply_wiring_t *w)
{
    supply_wiring_t out = *w;
    if (!out.en || out.tx < 0 || out.rx < 0 || out.tx == out.rx) {
        out.en = false;
    }
    if (out.tx < 0) {
        out.tx = 0;
    }
    if (out.rx < 0) {
        out.rx = 0;
    }
    return out;
}

static bool same_wiring(const supply_wiring_t *a, const supply_wiring_t *b)
{
    return a->en == b->en && a->tx == b->tx && a->rx == b->rx
           && a->baud == b->baud;
}

void supply_link_wire(supply_link_t *s, const supply_wiring_t *w)
{
    if (s == NULL || w == NULL) {
        return;
    }
    if (!same_wiring(&s->want, w)) {
        s->want    = *w;
        s->refused = false;
    }
}

void supply_link_command(supply_link_t *s, bool on, uint16_t mv, uint16_t ma)
{
    if (s == NULL) {
        return;
    }
    s->on = on;
    s->mv = (mv > PAGE_MAX_MV) ? (uint16_t)PAGE_MAX_MV
            : (mv < PAGE_MIN_MV) ? (uint16_t)PAGE_MIN_MV : mv;
    s->ma = (ma > PAGE_MAX_MA) ? (uint16_t)PAGE_MAX_MA
            : (ma < PAGE_MIN_MA) ? (uint16_t)PAGE_MIN_MA : ma;
}

supply_link_write_t supply_link_next(supply_link_t *s, uint8_t *off,
                                     uint8_t *n, uint16_t *regs)
{
    if (s == NULL || off == NULL || n == NULL || regs == NULL) {
        return SUPPLY_LINK_W_NONE;
    }
    s->pending = SUPPLY_LINK_W_NONE;

    /* An OFF first, alone, whatever else is owed -- and while what the
     * page holds is not known, whatever is asked. */
    if (!s->commanded || (!s->on && s->page_on)) {
        s->out[LINK_SP_OUTPUT] = 0u;
        *off = (uint8_t)LINK_SP_OUTPUT;
        *n   = 1u;
        regs[0] = 0u;
        s->pending = SUPPLY_LINK_W_OFF;
        return s->pending;
    }

    /*
     * The wiring, with the page's output off and nothing on it that may be
     * on, as a read since showed: the page refuses it otherwise, and a
     * refusal is taken as the pins' and not written again.
     */
    const supply_wiring_t w = as_written(&s->want);
    if (!s->refused && (!s->wired || !same_wiring(&s->page, &w))) {
        const bool page_off = s->commanded && !s->page_on && s->read_any
                              && s->regs[LINK_SP_OUTPUT] == 0u
                              && (s->regs[LINK_SP_FLAGS] & LINK_SP_LIVE) == 0u;
        if (!page_off) {
            return SUPPLY_LINK_W_NONE;   /* the OFF's read first */
        }
        s->out[LINK_SP_ENABLE] = w.en ? 1u : 0u;
        s->out[LINK_SP_TX_PIN] = (uint16_t)w.tx;
        s->out[LINK_SP_RX_PIN] = (uint16_t)w.rx;
        s->out[LINK_SP_BAUD]   = w.baud;
        *off = (uint8_t)LINK_SP_ENABLE;
        *n   = 4u;
        memcpy(regs, &s->out[LINK_SP_ENABLE], 4u * sizeof(uint16_t));
        s->pending = SUPPLY_LINK_W_WIRING;
        return s->pending;
    }

    /* An ON with nothing wired to serve it is refused here, once -- and
     * one whose wiring was refused, though the page still holds the old:
     * the settings name other pins, and that may be another supply. */
    if (s->on && (s->refused || !s->wired || !s->page.en)) {
        if (s->refused || (s->wired && !s->page.en)) {
            s->on = false;
            s->events |= SUPPLY_LINK_EV_ON_REFUSED;
        }
        return SUPPLY_LINK_W_NONE;
    }

    /* The command, ON and the set points in one frame; the set points with
     * the output off too, so the module holds them before it comes on. */
    if (!s->commanded || s->page_on != s->on || s->page_mv != s->mv
        || s->page_ma != s->ma) {
        s->out[LINK_SP_OUTPUT] = s->on ? 1u : 0u;
        s->out[LINK_SP_SET_MV] = s->mv;
        s->out[LINK_SP_SET_MA] = s->ma;
        *off = (uint8_t)LINK_SP_OUTPUT;
        *n   = 3u;
        memcpy(regs, &s->out[LINK_SP_OUTPUT], 3u * sizeof(uint16_t));
        s->pending = SUPPLY_LINK_W_COMMAND;
        return s->pending;
    }
    return SUPPLY_LINK_W_NONE;
}

void supply_link_written(supply_link_t *s, int result)
{
    if (s == NULL) {
        return;
    }
    const supply_link_write_t w = s->pending;
    s->pending = SUPPLY_LINK_W_NONE;
    if (result == SUPPLY_LINK_NO_ANSWER) {
        return;                          /* owed still: tried again */
    }
    switch (w) {
    case SUPPLY_LINK_W_OFF:
        if (result == SUPPLY_LINK_ACK) {
            if (!s->commanded) {
                /* What set points the page holds is not known yet. */
                s->page_mv = 0xFFFFu;
                s->page_ma = 0xFFFFu;
            }
            s->commanded = true;
            s->page_on   = false;
        }
        break;
    case SUPPLY_LINK_W_WIRING:
        if (result == SUPPLY_LINK_ACK) {
            s->wired = true;
            s->page.en   = s->out[LINK_SP_ENABLE] != 0u;
            s->page.tx   = (int8_t)s->out[LINK_SP_TX_PIN];
            s->page.rx   = (int8_t)s->out[LINK_SP_RX_PIN];
            s->page.baud = (uint8_t)s->out[LINK_SP_BAUD];
        } else {
            s->refused = true;
            s->events |= SUPPLY_LINK_EV_WIRING_REFUSED;
        }
        break;
    case SUPPLY_LINK_W_COMMAND:
        if (result == SUPPLY_LINK_ACK || s->out[LINK_SP_OUTPUT] == 0u) {
            /* Taken -- or set points refused with the output off, which
             * are not asked for again until they change. */
            s->commanded = true;
            s->page_on   = s->out[LINK_SP_OUTPUT] != 0u
                           && result == SUPPLY_LINK_ACK;
            s->page_mv   = s->out[LINK_SP_SET_MV];
            s->page_ma   = s->out[LINK_SP_SET_MA];
        } else {
            /* An ON refused: no heartbeat at the far end, or no supply. */
            s->on = false;
            s->events |= SUPPLY_LINK_EV_ON_REFUSED;
        }
        break;
    case SUPPLY_LINK_W_NONE:
    default:
        break;
    }
}

bool supply_link_settled(const supply_link_t *s)
{
    if (s == NULL) {
        return false;
    }
    const supply_wiring_t w = as_written(&s->want);
    return s->commanded && s->page_on == s->on && s->page_mv == s->mv
           && s->page_ma == s->ma
           && (s->refused || (s->wired && same_wiring(&s->page, &w)));
}

bool supply_link_read_due(const supply_link_t *s, uint32_t now_ms)
{
    return s != NULL
           && (!s->asked
               || (uint32_t)(now_ms - s->asked_ms) >= SUPPLY_LINK_READ_MS);
}

void supply_link_read(supply_link_t *s, const uint16_t *regs,
                      uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    s->asked    = true;
    s->asked_ms = now_ms;
    if (regs == NULL) {
        return;
    }
    const uint16_t was = s->read_any ? s->regs[LINK_SP_FLAGS] : 0u;
    memcpy(s->regs, regs, sizeof(s->regs));
    s->read_any = true;
    s->read_ms  = now_ms;

    /* The page holding other than was written: owed again. */
    if (s->wired
        && (regs[LINK_SP_ENABLE] != (s->page.en ? 1u : 0u)
            || regs[LINK_SP_TX_PIN] != (uint16_t)s->page.tx
            || regs[LINK_SP_RX_PIN] != (uint16_t)s->page.rx
            || regs[LINK_SP_BAUD] != s->page.baud)) {
        s->wired = false;
    }
    if (s->commanded && regs[LINK_SP_OUTPUT] != (s->page_on ? 1u : 0u)) {
        if (s->page_on && s->on && regs[LINK_SP_OUTPUT] == 0u) {
            /* Switched off at the far end: the heartbeat stopped, or the
             * coprocessor started again.  Not switched back on. */
            s->on = false;
            s->events |= SUPPLY_LINK_EV_ON_LOST;
        }
        s->commanded = false;
    } else if (s->commanded
               && (regs[LINK_SP_SET_MV] != s->page_mv
                   || regs[LINK_SP_SET_MA] != s->page_ma)) {
        /* The set points not as written: written again. */
        s->page_mv = 0xFFFFu;
        s->page_ma = 0xFFFFu;
    }

    const uint16_t f = regs[LINK_SP_FLAGS];
    if ((f & LINK_SP_STUCK) != 0u && (was & LINK_SP_STUCK) == 0u) {
        s->events |= SUPPLY_LINK_EV_STUCK;
    }
    if ((f & LINK_SP_SET_STUCK) != 0u && (was & LINK_SP_SET_STUCK) == 0u) {
        s->events |= SUPPLY_LINK_EV_SET_STUCK;
    }
}

void supply_link_state(const supply_link_t *s, uint32_t now_ms,
                       supply_state_t *st)
{
    if (s == NULL || st == NULL) {
        return;
    }
    const bool fresh = s->read_any
                       && (uint32_t)(now_ms - s->read_ms) < SUPPLY_LINK_STALE_MS;
    const uint16_t f = s->regs[LINK_SP_FLAGS];
    st->online = fresh && s->wired && s->page.en
                 && (f & LINK_SP_ONLINE) != 0u;
    if (!st->online) {
        st->mode = SUPPLY_MODE_OFF;
        st->ok   = 0u;
        return;
    }
    st->mode   = ((f & LINK_SP_ON) == 0u) ? SUPPLY_MODE_OFF
                 : (LINK_SP_MODE(f) == 0u) ? SUPPLY_MODE_CV
                                           : SUPPLY_MODE_CC;
    st->v      = (float)s->regs[LINK_SP_V_MV] / 1000.0f;
    st->i      = (float)s->regs[LINK_SP_I_MA] / 1000.0f;
    st->p      = st->v * st->i;
    st->set_v  = (float)s->regs[LINK_SP_SET_MV_RB] / 1000.0f;
    st->set_i  = (float)s->regs[LINK_SP_SET_MA_RB] / 1000.0f;
    st->ok     = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
}

uint8_t supply_link_events(supply_link_t *s)
{
    if (s == NULL) {
        return 0u;
    }
    const uint8_t e = s->events;
    s->events = 0u;
    return e;
}
