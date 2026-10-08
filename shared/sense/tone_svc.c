/*
 * The tone service.  See tone_svc.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tone_svc.h"

#include <string.h>

#define U16_MAX 65535u

static uint16_t clip16(uint64_t v)
{
    return (uint16_t)(v > U16_MAX ? U16_MAX : v);
}

/* A non-negative float rounded to the nearest integer, clipped to 16 bits. */
static uint16_t round16(float v)
{
    if (!(v > 0.0f)) {
        return 0u;
    }
    if (v >= 65535.0f) {
        return U16_MAX;
    }
    return (uint16_t)(v + 0.5f);
}

void tone_svc_init(tone_svc_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

uint64_t tone_svc_ticks(uint64_t us)
{
    /* 37.5 ticks per microsecond. */
    return us * (TONE_SVC_TICK_HZ / 500000u) / 2u;
}

void tone_svc_cfg(const tone_cmd_t *cmd, tone_cfg_t *c)
{
    tone_cfg_defaults(c, TONE_SVC_TICK_HZ);
    c->f_min_hz    = cmd->f_min_hz;
    c->f_max_hz    = cmd->f_max_hz;
    c->split_pct   = cmd->split_pct;
    c->gap_us      = cmd->gap_ms * 1000u;
    c->min_periods = cmd->min_periods;
    c->hold_ns     = TONE_SVC_HOLD_NS;
}

void tone_svc_rec(const tone_beep_t *b, tone_rec_t *r)
{
    memset(r, 0, sizeof(*r));
    const uint64_t hz = TONE_SVC_TICK_HZ;
    r->start_ms    = (uint32_t)(b->start * 1000u / hz);
    r->len_dms     = clip16((b->end - b->start) * 10000u / hz);
    r->freq_dhz    = round16(b->freq_hz * 10.0f);
    r->bursts      = clip16(b->bursts);
    r->carrier_hhz = round16(b->carrier_hz / 100.0f);
    r->flags       = (uint8_t)(b->flags & 3u);
}

/* Restart under a new order: the detector, the hold-off horizon, the ring
 * reader and the flags.  False when the order's settings are refused. */
static bool restart(tone_svc_t *s, const tone_cmd_t *cmd)
{
    tone_cfg_t c;
    tone_svc_cfg(cmd, &c);
    const bool fresh = !s->active || s->cap_gen != cmd->cap_gen;
    s->gen = cmd->gen;
    s->cap_gen = cmd->cap_gen;
    s->active = false;
    if (!tone_init(&s->det, &c)) {
        return false;
    }
    tone_holdoff_init(&s->hold, TONE_SVC_TICK_HZ, TONE_SVC_HOLD_NS);
    if (fresh) {
        /* The counter and the ring begin again. */
        edge_ring_init(&s->ring);
        s->overrun = false;
    }
    s->active = true;
    return true;
}

static void report(const tone_svc_t *s, tone_status_t *st)
{
    memset(st, 0, sizeof(*st));
    st->gen = s->gen;
    st->cap_gen = s->cap_gen;
    if (!s->active) {
        return;
    }
    st->running = true;
    st->overrun = s->overrun;
    st->beep = tone_busy(&s->det);
    tone_window_t w;
    if (tone_window(&s->det, &w)) {
        st->tone = w.present;
        st->window = (uint16_t)(w.index & U16_MAX);
        if (w.present) {
            st->win_freq_dhz = round16(w.freq_hz * 10.0f);
            st->win_periods = clip16(w.periods);
        }
    }
    const tone_stats_t *ts = tone_stats(&s->det);
    st->lost = ts->lost;
    st->glitches = ts->glitches;
}

size_t tone_svc_step(tone_svc_t *s, const tone_cmd_t *cmd,
                     const uint32_t *ring, uint32_t wr, uint64_t now,
                     bool fifo_overrun, tone_rec_t *rec, tone_status_t *st)
{
    if (s == NULL || cmd == NULL || st == NULL) {
        return 0u;
    }
    if (!cmd->run) {
        s->active = false;
        s->gen = cmd->gen;
        s->cap_gen = cmd->cap_gen;
        report(s, st);
        return 0u;
    }
    if ((!s->active || s->gen != cmd->gen || s->cap_gen != cmd->cap_gen)
        && !restart(s, cmd)) {
        report(s, st);
        return 0u;
    }
    if (fifo_overrun) {
        /* The state machine dropped a word: the edges have a hole.  The
         * beep under way ends before the pass's words go in, so none
         * completes across it. */
        tone_flush(&s->det);
        s->overrun = true;
    }
    if (ring != NULL) {
        tone_edge_t *buf = s->batch;
        size_t n;
        do {
            bool lap = false;
            n = edge_ring_take(&s->ring, ring, TONE_RING_WORDS, wr, now, buf,
                               TONE_SVC_BATCH, &lap);
            if (lap) {
                tone_flush(&s->det);
                s->overrun = true;
            }
            if (n != 0u) {
                tone_feed(&s->det, buf, n);
                if (buf[n - 1u].t > s->hold.out_t) {
                    s->hold.out_t = buf[n - 1u].t;
                }
            }
        } while (n == TONE_SVC_BATCH);
    }
    const uint64_t margin = tone_svc_ticks(TONE_SVC_MARGIN_US);
    tone_advance(&s->det,
                 tone_holdoff_horizon(&s->hold, now > margin ? now - margin
                                                             : 0u));
    size_t nb = 0;
    tone_beep_t b;
    while (rec != NULL && nb < TONE_SVC_BEEPS && tone_next_beep(&s->det, &b)) {
        tone_svc_rec(&b, &rec[nb]);
        ++nb;
    }
    report(s, st);
    return nb;
}
