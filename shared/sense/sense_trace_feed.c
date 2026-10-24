/**
 * @file
 * @brief Core 1's side of the trace of the FAST channel's 1ms samples: each
 * tick's records into the ring, and the count of those it dropped.
 *
 * See sense_trace.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace.h"

/**
 * @brief A record that is not kept, counted as dropped.
 *
 * The count core 0 reads moves with the one the next record carries, and stops
 * with it.
 *
 * @param tr The trace.
 */
static void drop(sense_trace_t *tr)
{
    sense_trace_src_t *p = &tr->src;
    if (p->lost < SENSE_TRACE_LOST_MAX) {
        ++p->lost;
        ++p->dropped;
        atomic_store_explicit(&tr->drops, p->dropped, memory_order_release);
    }
}

/**
 * @brief One record into the ring; false, and counted as dropped, when it is
 * full.
 *
 * @param tr The trace.
 * @param kind The record kind.
 * @param t Time of the record, in 0.1ms.
 * @param v The record value.
 * @return true when the record was put in the ring.
 */
static bool push(sense_trace_t *tr, sense_trace_kind_t kind, uint32_t t,
                 int32_t v)
{
    sense_trace_src_t *p = &tr->src;
    const uint32_t head =
        (uint32_t)atomic_load_explicit(&tr->head, memory_order_relaxed);
    const uint32_t tail =
        (uint32_t)atomic_load_explicit(&tr->tail, memory_order_acquire);
    if (head - tail > tr->mask) {
        drop(tr);
        return false;
    }
    sense_trace_rec_t *r = &tr->buf[head & tr->mask];
    r->t    = t;
    r->v    = v;
    r->meta = ((uint32_t)kind << 24) | p->lost;
    p->lost = 0u;
    atomic_store_explicit(&tr->head, head + 1u, memory_order_release);
    p->kept_t      = t;
    p->shunt_newer = true;
    p->cfg_newer   = true;
    return true;
}

/**
 * @brief The set-up and the state as they stand: a record for each that moved,
 * the shunt's first, written again at the next call while the ring has no room
 * for it.
 *
 * A record's time is the start of the tick the change was seen in, @p tick_t
 * then: a tick with no sample has a time all the same, and a trigger between
 * the last sample and the change lies before the change.  A record kept since,
 * while this one found no room, moves it to that record's time: the ring's
 * times never run back.
 *
 * @param tr The trace.
 * @param cfg The Configuration value of FAST's part.
 * @param shunt FAST's shunt, in µΩ.
 * @param tick_t Start of the tick, in 0.1ms.
 */
static void publish(sense_trace_t *tr, uint32_t cfg, uint32_t shunt,
                    uint32_t tick_t)
{
    sense_trace_src_t *p = &tr->src;
    if (shunt != p->shunt) {
        p->shunt = shunt;
        if (!p->shunt_owed) {
            p->shunt_owed  = true;
            p->shunt_t     = tick_t;
            p->shunt_newer = false;
        }
    }
    if (cfg != p->cfg) {
        p->cfg = cfg;
        if (!p->cfg_owed) {
            p->cfg_owed  = true;
            p->cfg_t     = tick_t;
            p->cfg_newer = false;
        }
    }
    if (p->shunt_owed
        && push(tr, SENSE_TRACE_SHUNT,
                p->shunt_newer ? p->kept_t : p->shunt_t,
                (int32_t)p->shunt)) {
        p->shunt_owed = false;
    }
    /* After the shunt's, never before it: the other core can free a
     * place between the two. */
    if (p->cfg_owed && !p->shunt_owed
        && push(tr, SENSE_TRACE_CFG, p->cfg_newer ? p->kept_t : p->cfg_t,
                (int32_t)p->cfg)) {
        p->cfg_owed = false;
    }
    /* The samples dropped while a record was owed were taken under the
     * new set-up: counted from here, behind its records, the count the
     * next record carries.  The last owed record went in in this call
     * and took lost with it, so lost is 0 here. */
    if (!p->shunt_owed && !p->cfg_owed && p->lost_after != 0u) {
        p->lost       = p->lost_after;
        p->dropped   += p->lost_after;
        p->lost_after = 0u;
        atomic_store_explicit(&tr->drops, p->dropped, memory_order_release);
    }
}

/**
 * @brief The tick's records, when it has any.  @p tick_t is its start.
 *
 * @param tr The trace.
 * @param s The schedule that has ticked; NULL when there is no bus.
 * @param tick_t Start of the tick, in 0.1ms.
 */
static void feed(sense_trace_t *tr, const sense_sched_t *s, uint32_t tick_t)
{
    sense_trace_src_t *p = &tr->src;
    if (s == NULL) {
        /* No bus: the next schedule starts afresh. */
        p->have = false;
        p->win  = 0u;
        p->gen  = 0u;
        p->n_v  = 0u;
        p->n_hi = 0u;
        p->n_lo = 0u;
        publish(tr, 0u, 0u, tick_t);
        return;
    }
    /* FAST's part: its Configuration, whether it is online and its resets,
     * and FAST's shunt; all 0 with no FAST channel. */
    const ina3221_t *d = sense_sched_fast_part(s);
    const uint8_t f = s->cfg.i3221.fast;
    uint32_t cfg = 0u;
    uint32_t shunt = 0u;
    if (d != NULL) {
        const unsigned a = f / INA3221_CHANNELS;
        const bool online = ina3221_state(d) == SENSE_PART_ONLINE;
        cfg = (uint32_t)d->config | (online ? 1u << 16 : 0u)
              | ((uint32_t)s->i3221_resets[a] << 24);
        shunt = d->shunt_uohm[f % INA3221_CHANNELS];
    }
    publish(tr, cfg, shunt, tick_t);

    /* FAST's sample of this tick, when there is one: the schedule keeps it
     * newest in its history.  A set-up empties the history, so the head
     * alone does not tell a new sample from the last one. */
    /* What the window being filled gained in this tick.  A window that
     * closed, or was emptied, counts from nothing, and so does one of a
     * new schedule: its window 0 is another than the last one's.  The
     * set-up number tells the schedules apart, whatever their tick counts
     * and however long one has run. */
    static const sense_acc_t k_none;
    const sense_acc_t *a = (d != NULL) ? &s->acc[f] : &k_none;
    const bool same = s->gen == p->gen && s->win == p->win
                      && a->n_v >= p->n_v
                      && a->n_hi >= p->n_hi && a->n_lo >= p->n_lo;
    const uint16_t base_n   = same ? p->n_v : 0u;
    const int64_t  base_sum = same ? p->v_sum : 0;
    /* A clipped sample is counted there and nowhere else: the history
     * holds the end of the range for it, as it does for a value there. */
    sense_trace_kind_t kind = SENSE_TRACE_CURRENT;
    if (a->n_hi != (same ? p->n_hi : 0u)) {
        kind = SENSE_TRACE_CLIP_HI;
    } else if (a->n_lo != (same ? p->n_lo : 0u)) {
        kind = SENSE_TRACE_CLIP_LO;
    }
    /* The sample was read, and a later read of the same tick closed the
     * window it was counted in: that window's count is the ring's. */
    const bool closed = s->win != p->win && s->have_last
                        && s->ring_at == p->win
                        && s->ring[0].n_clip > p->n_hi + p->n_lo;
    bool kept = false;
    if (s->hist_n == 0u) {
        p->have = false;
    } else {
        const unsigned newest = (s->hist_head + SENSE_FAST_HISTORY - 1u)
                                % SENSE_FAST_HISTORY;
        const sense_fast_t *c = &s->hist[newest];
        if (!p->have || s->hist_head != p->head || c->t != p->t) {
            p->have = true;
            p->head = s->hist_head;
            p->t    = c->t;
            if (closed && kind == SENSE_TRACE_CURRENT) {
                kind = (c->ua > 0) ? SENSE_TRACE_CLIP_HI
                                   : SENSE_TRACE_CLIP_LO;
            }
            if (p->shunt_owed || p->cfg_owed) {
                /* The set-up record this sample is coded against is not
                 * in the ring yet: core 0 can free a place between the
                 * two pushes, and the sample would stand before it.  Its
                 * loss is counted after that record (publish()), not
                 * with the count the record carries. */
                if (p->lost_after < SENSE_TRACE_LOST_MAX) {
                    ++p->lost_after;
                }
            } else {
                kept = push(tr, kind, c->t, c->ua);
            }
        }
    }

    /* FAST's bus voltage of this tick.  Kept only behind the current
     * sample of its tick: a voltage line belongs to the sample line
     * before it. */
    if (kept && a->n_v == base_n + 1u) {
        /* One voltage, µV: it fits 32 bits, and the division is the
         * processor's. */
        (void)push(tr, SENSE_TRACE_BUS, p->t,
                   (int32_t)(a->v_sum - base_sum) / 1000);
    }
    p->win   = s->win;
    p->gen   = s->gen;
    p->n_v   = a->n_v;
    p->n_hi  = a->n_hi;
    p->n_lo  = a->n_lo;
    p->v_sum = a->v_sum;
}

void sense_trace_feed(sense_trace_t *tr, const sense_sched_t *s,
                      uint64_t tick_us)
{
    if (tr->buf == NULL) {
        return;
    }
    const uint32_t tick_t = (uint32_t)(tick_us / 100u);
    feed(tr, s, tick_t);
    /* After the tick's records and the count of those it dropped, never
     * before them: every record still to come, kept or dropped, is of a
     * later tick, so of this time or later. */
    atomic_store_explicit(&tr->fed, tick_t, memory_order_release);
}
