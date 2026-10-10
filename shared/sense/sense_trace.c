/*
 * CH1's 1 ms samples as text lines.  See sense_trace.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace.h"

#include <string.h>

#define META_KIND(m)  ((uint8_t)((m) >> 24))
#define META_LOST(m)  ((m) & SENSE_TRACE_LOST_MAX)

bool sense_trace_init(sense_trace_t *tr, sense_trace_rec_t *buf,
                      uint32_t size)
{
    memset(tr, 0, sizeof(*tr));
    atomic_init(&tr->head, 0u);
    atomic_init(&tr->tail, 0u);
    atomic_init(&tr->fed, 0u);
    atomic_init(&tr->drops, 0u);
    if (buf == NULL || size < 2u || size > (1u << 24)
        || (size & (size - 1u)) != 0u) {
        return false;
    }
    tr->buf  = buf;
    tr->mask = size - 1u;
    return true;
}

/* ------------------------------------------------------------- core 1 */

/* One record into the ring; false, and counted as dropped, when it is
 * full. */
static bool push(sense_trace_t *tr, sense_trace_kind_t kind, uint32_t t,
                 int32_t v)
{
    sense_trace_src_t *p = &tr->src;
    const uint32_t head =
        (uint32_t)atomic_load_explicit(&tr->head, memory_order_relaxed);
    const uint32_t tail =
        (uint32_t)atomic_load_explicit(&tr->tail, memory_order_acquire);
    if (head - tail > tr->mask) {
        /* The count core 0 reads moves with the one the next record
         * carries, and stops with it. */
        if (p->lost < SENSE_TRACE_LOST_MAX) {
            ++p->lost;
            ++p->dropped;
            atomic_store_explicit(&tr->drops, p->dropped,
                                  memory_order_release);
        }
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

/* The set-up and the state as they stand: a record for each that moved,
 * the shunt's first, written again at the next call while the ring has
 * no room for it.  A record's time is the start of the tick the change
 * was seen in, @p tick_t then: a tick with no sample has a time all the
 * same, and a trigger between the last sample and the change lies before
 * the change.  A record kept since, while this one found no room, moves
 * it to that record's time: the ring's times never run back. */
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
}

/* The tick's records, when it has any.  @p tick_t is its start. */
static void feed(sense_trace_t *tr, const sense_sched_t *s, uint32_t tick_t)
{
    sense_trace_src_t *p = &tr->src;
    if (s == NULL) {
        /* No bus: the next schedule starts afresh. */
        p->have = false;
        p->win  = 0u;
        p->n_v  = 0u;
        p->n_hi = 0u;
        p->n_lo = 0u;
        publish(tr, 0u, 0u, tick_t);
        return;
    }
    const bool online = ina3221_state(&s->i3221) == SENSE_PART_ONLINE;
    publish(tr, (uint32_t)s->i3221.config | (online ? 1u << 16 : 0u)
                    | ((uint32_t)s->i3221_resets << 24),
            s->i3221.shunt_uohm, tick_t);

    /* CH1's sample of this tick, when there is one: the schedule keeps it
     * newest in its history.  A set-up empties the history, so the head
     * alone does not tell a new sample from the last one. */
    /* What the window being filled gained in this tick.  A window that
     * closed, or was emptied, counts from nothing. */
    const sense_acc_t *a = &s->acc[SENSE_SRC_CH1];
    const bool same = s->win == p->win && a->n_v >= p->n_v
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
    if (s->ch1_n == 0u) {
        p->have = false;
    } else {
        const unsigned newest = (s->ch1_head + SENSE_CH1_HISTORY - 1u)
                                % SENSE_CH1_HISTORY;
        const sense_ch1_t *c = &s->ch1[newest];
        if (!p->have || s->ch1_head != p->head || c->t != p->t) {
            p->have = true;
            p->head = s->ch1_head;
            p->t    = c->t;
            if (closed && kind == SENSE_TRACE_CURRENT) {
                kind = (c->ua > 0) ? SENSE_TRACE_CLIP_HI
                                   : SENSE_TRACE_CLIP_LO;
            }
            kept    = push(tr, kind, c->t, c->ua);
        }
    }

    /* CH1's bus voltage of this tick.  Kept only behind the current
     * sample of its tick: a voltage line belongs to the sample line
     * before it. */
    if (kept && a->n_v == base_n + 1u) {
        /* One voltage, µV: it fits 32 bits, and the division is the
         * processor's. */
        (void)push(tr, SENSE_TRACE_BUS, p->t,
                   (int32_t)(a->v_sum - base_sum) / 1000);
    }
    p->win   = s->win;
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

/* ------------------------------------------------------------- core 0 */

static uint32_t to_t(uint64_t us)
{
    return (uint32_t)(us / 100u);
}

/* Whether @p a lies at or past @p b, modulo 2^32. */
static bool at_or_past(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static uint32_t tail_of(const sense_trace_t *tr)
{
    return (uint32_t)atomic_load_explicit(&tr->tail, memory_order_relaxed);
}

static uint32_t head_of(const sense_trace_t *tr)
{
    return (uint32_t)atomic_load_explicit(&tr->head, memory_order_acquire);
}

uint32_t sense_trace_held(const sense_trace_t *tr)
{
    return head_of(tr) - tail_of(tr);
}

bool sense_trace_active(const sense_trace_t *tr)
{
    return tr->out.stage != SENSE_TRACE_IDLE;
}

int32_t sense_trace_code(int32_t ua, uint32_t shunt_uohm)
{
    if (shunt_uohm == 0u) {
        return 0;
    }
    /* ina3221_current_ua() cuts code * 40e6 / R towards 0; rounding the
     * product back gives the code for every shunt up to 20 Ω. */
    const int64_t num = (int64_t)ua * (int64_t)shunt_uohm;
    const int64_t half = (num >= 0) ? 20000000 : -20000000;
    return (int32_t)((num + half) / 40000000);
}

uint16_t sense_trace_rendered(uint16_t pulse_us, uint32_t top)
{
    return (pulse_us > top) ? (uint16_t)top : pulse_us;
}

/* The record at the tail, or NULL for an empty ring. */
static const sense_trace_rec_t *peek(const sense_trace_t *tr)
{
    const uint32_t tail = tail_of(tr);
    return (head_of(tr) == tail) ? NULL : &tr->buf[tail & tr->mask];
}

static bool is_setup(const sense_trace_rec_t *r)
{
    return META_KIND(r->meta) >= SENSE_TRACE_CFG;
}

/* The records dropped before @p r that no $L line has said yet. */
static uint32_t lost_before(const sense_trace_out_t *o,
                            const sense_trace_rec_t *r)
{
    const uint32_t lost = META_LOST(r->meta);
    return (lost > o->ahead) ? lost - o->ahead : 0u;
}

/* The record at the tail leaves the ring; a set-up record leaves what it
 * says behind.  Every record leaves here, in a trace and out of one: the
 * count of dropped records it carried is read. */
static void pop(sense_trace_t *tr)
{
    sense_trace_out_t *o = &tr->out;
    const uint32_t tail = tail_of(tr);
    const sense_trace_rec_t *r = &tr->buf[tail & tr->mask];
    o->drops_seen += META_LOST(r->meta);
    o->ahead       = 0u;
    if (META_KIND(r->meta) == SENSE_TRACE_CFG) {
        o->cfg = (uint32_t)r->v;
    } else if (META_KIND(r->meta) == SENSE_TRACE_SHUNT) {
        o->shunt = (uint32_t)r->v;
    }
    o->lost_shown = false;
    atomic_store_explicit(&tr->tail, tail + 1u, memory_order_release);
}

/* Whether @p r changes the shunt or the Configuration, and not the state
 * alone. */
static bool changes_setup(const sense_trace_out_t *o,
                          const sense_trace_rec_t *r)
{
    if (META_KIND(r->meta) == SENSE_TRACE_SHUNT) {
        return (uint32_t)r->v != o->shunt;
    }
    return ((uint32_t)r->v & 0xFFFFu) != (o->cfg & 0xFFFFu);
}

/* Whether the trace under way is still open at @p t: not stopped, and
 * @p t before its end.  The console may owe lines of a trace long over. */
static bool open_at(const sense_trace_out_t *o, uint32_t t)
{
    return o->stage != SENSE_TRACE_IDLE && !o->stopped
           && !at_or_past(t, o->end_t);
}

static void extend(sense_trace_out_t *o, uint32_t end_t)
{
    if ((int32_t)(end_t - o->end_t) > 0) {
        o->end_t = end_t;
    }
}

/* @p add more triggers that found a queue full, the first of them at
 * @p first and the last at @p last, onto a count and its two times. */
static void lost_add(uint32_t *n, uint32_t span[2], uint32_t add,
                     uint32_t first, uint32_t last)
{
    if (add == 0u) {
        return;
    }
    if (*n == 0u || (int32_t)(first - span[0]) < 0) {
        span[0] = first;
    }
    if (*n == 0u || (int32_t)(last - span[1]) > 0) {
        span[1] = last;
    }
    *n = (add > UINT32_MAX - *n) ? UINT32_MAX : *n + add;
}

/* A line for the console's queue, or counted when it is full.  Whether
 * it found a place. */
static bool mark(sense_trace_out_t *o, sense_trace_trig_t kind, uint32_t t,
                 uint64_t at_us, uint16_t ch, uint16_t us)
{
    if (o->q_n >= SENSE_TRACE_MARKS) {
        lost_add(&o->n_mlost, o->mlost_t, 1u, t, t);
        return false;
    }
    sense_trace_mark_t *m =
        &o->q[(o->q_head + o->q_n) % SENSE_TRACE_MARKS];
    m->kind  = (uint8_t)kind;
    m->t     = t;
    m->at_us = at_us;
    m->ch    = ch;
    m->us    = us;
    ++o->q_n;
    /* In the order of their times: two slots' frames need not start in
     * the order the slots are looked at. */
    for (unsigned k = o->q_n - 1u; k > 0u; --k) {
        sense_trace_mark_t *later =
            &o->q[(o->q_head + k) % SENSE_TRACE_MARKS];
        sense_trace_mark_t *earlier =
            &o->q[(o->q_head + k - 1u) % SENSE_TRACE_MARKS];
        if ((int32_t)(later->t - earlier->t) >= 0) {
            break;
        }
        const sense_trace_mark_t swap = *later;
        *later   = *earlier;
        *earlier = swap;
    }
    return true;
}

/* Where a trigger went. */
typedef enum {
    TRIG_LOST = 0,  /* a queue was full: counted                     */
    TRIG_PLACED,    /* its line is in the trace under way            */
    TRIG_WAITS,     /* it waits for the next trace                   */
} place_t;

static place_t trigger(sense_trace_t *tr, sense_trace_trig_t kind,
                       uint64_t at_us, uint16_t ch, uint16_t us)
{
    sense_trace_out_t *o = &tr->out;
    const uint32_t t = to_t(at_us);
    const uint32_t len_ms = (kind == SENSE_TRACE_TRIG_KEY)
                                ? SENSE_TRACE_KEY_MS : SENSE_TRACE_EDGE_MS;
    const uint32_t end_t = t + len_ms * SENSE_TRACE_T_PER_MS;
    if (o->stage == SENSE_TRACE_IDLE) {
        ++o->id;
        o->stage   = SENSE_TRACE_HEAD_T;
        o->trig    = (uint8_t)kind;
        o->t0      = t;
        o->ms0     = (uint32_t)(at_us / 1000u);
        o->len_ms  = len_ms;
        o->end_t   = end_t;
        o->last_t  = t;
        o->stopped = false;
        o->stalled = false;
        o->n_s = o->n_v = o->n_m = o->n_lost = o->n_mlost = 0u;
        o->q_n = o->q_head = 0u;
    } else if (open_at(o, t)) {
        extend(o, end_t);
    } else {
        /* The trace under way is over at this trigger's time and its
         * lines are not all written: the trigger is the next trace's. */
        if (o->wait_n >= SENSE_TRACE_MARKS) {
            lost_add(&o->wait_lost, o->wait_lost_t, 1u, t, t);
            return TRIG_LOST;
        }
        o->wait[o->wait_n].kind  = (uint8_t)kind;
        o->wait[o->wait_n].t     = t;
        o->wait[o->wait_n].at_us = at_us;
        o->wait[o->wait_n].ch    = ch;
        o->wait[o->wait_n].us    = us;
        ++o->wait_n;
        return TRIG_WAITS;
    }
    return mark(o, kind, t, at_us, ch, us) ? TRIG_PLACED : TRIG_LOST;
}

void sense_trace_trigger(sense_trace_t *tr, sense_trace_trig_t kind,
                         uint64_t at_us, uint16_t ch, uint16_t us)
{
    sense_trace_out_t *o = &tr->out;
    if (tr->buf == NULL) {
        return;
    }
    if (trigger(tr, kind, at_us, ch, us) == TRIG_LOST
        && kind == SENSE_TRACE_TRIG_CMD) {
        /* No line of this command: what its slot does until the next one
         * would be read as an earlier command's. */
        for (unsigned k = 0u; k < SENSE_TRACE_SLOTS; ++k) {
            if (o->watch[k].have && o->watch[k].ch == ch) {
                o->watch[k].orphan = true;
            }
        }
    }
}

/* A slewed command of channel @p ch changed last at @p t, or with
 * SENSE_TRACE_MARK_DEST ended there, past the end of the trace under way:
 * kept behind the command where that waits for the next trace, one entry
 * for the command.  A command that does not wait has its line in a trace
 * that is over, or none. */
static void wait_slew(sense_trace_out_t *o, sense_trace_trig_t kind,
                      uint16_t ch, uint32_t t, uint16_t us)
{
    if (o->stage == SENSE_TRACE_IDLE) {
        return;
    }
    uint8_t cmd = o->wait_n;
    for (uint8_t k = 0u; k < o->wait_n; ++k) {
        if (o->wait[k].kind == SENSE_TRACE_TRIG_CMD && o->wait[k].ch == ch) {
            cmd = k;
        }
    }
    if (cmd == o->wait_n) {
        return;
    }
    sense_trace_mark_t *m = NULL;
    for (uint8_t k = (uint8_t)(cmd + 1u); k < o->wait_n; ++k) {
        if (o->wait[k].kind == SENSE_TRACE_MARK_STEP && o->wait[k].ch == ch) {
            m = &o->wait[k];
        }
    }
    if (m == NULL) {
        if (o->wait_n >= SENSE_TRACE_MARKS) {
            if (kind == SENSE_TRACE_MARK_DEST) {
                lost_add(&o->wait_lost, o->wait_lost_t, 1u, t, t);
            }
            return;
        }
        m = &o->wait[o->wait_n++];
    }
    m->kind  = (uint8_t)kind;
    m->t     = t;
    m->at_us = 0u;
    m->ch    = ch;
    m->us    = us;
}

/* A slewed command of @p w has ended, at its last change: said in the
 * trace that change is in. */
static void slew_ends(sense_trace_out_t *o, sense_trace_watch_t *w,
                      uint16_t ch)
{
    if (w->slewed && !w->orphan) {
        if (open_at(o, w->changed_t)) {
            mark(o, SENSE_TRACE_MARK_DEST, w->changed_t, 0u, ch, w->pulse);
        } else {
            wait_slew(o, SENSE_TRACE_MARK_DEST, ch, w->changed_t, w->pulse);
        }
    }
    w->slewed = false;
}

bool sense_trace_pulse(sense_trace_t *tr, unsigned slot, uint16_t ch,
                       uint16_t pulse_us, uint64_t now_us)
{
    sense_trace_out_t *o = &tr->out;
    if (tr->buf == NULL || slot >= SENSE_TRACE_SLOTS) {
        return false;
    }
    sense_trace_watch_t *w = &o->watch[slot];
    if (w->have && w->ch != ch) {
        /* Another output on this slot: nothing of the last one's holds. */
        memset(w, 0, sizeof(*w));
    }
    if (!w->have) {
        /* The first pulse seen is where the watch starts, not a change. */
        w->have  = true;
        w->ch    = ch;
        w->pulse = pulse_us;
        return false;
    }
    const uint32_t now_t = to_t(now_us);
    const bool held = !w->changed
                      || at_or_past(now_t, w->changed_t
                                    + SENSE_TRACE_HOLD_MS
                                      * SENSE_TRACE_T_PER_MS);
    if (held || pulse_us == 0u) {
        /* Still for long enough, or let go: where a slewed command had
         * got to is its end.  Seen past the hold, the time is not looked
         * at again, however long the slot then stays as it is; let go,
         * the next pulse is a command however soon it comes. */
        slew_ends(o, w, ch);
        w->changed = false;
    }
    if (pulse_us == w->pulse) {
        return false;
    }
    w->pulse = pulse_us;
    if (pulse_us == 0u) {
        return false;
    }
    w->changed   = true;
    w->changed_t = now_t;
    if (held) {
        w->orphan = false;
        return true;
    }
    w->slewed = true;
    if (open_at(o, now_t)) {
        extend(o, now_t + SENSE_TRACE_EDGE_MS * SENSE_TRACE_T_PER_MS);
    } else if (!w->orphan) {
        wait_slew(o, SENSE_TRACE_MARK_STEP, ch, now_t, pulse_us);
    }
    return false;
}

void sense_trace_unwatch(sense_trace_t *tr, unsigned slot)
{
    if (slot < SENSE_TRACE_SLOTS) {
        memset(&tr->out.watch[slot], 0, sizeof(tr->out.watch[slot]));
    }
}

void sense_trace_key(sense_trace_t *tr, int c, uint64_t now_us)
{
    sense_trace_out_t *o = &tr->out;
    if (c == 't' || c == 'T') {
        sense_trace_trigger(tr, SENSE_TRACE_TRIG_KEY, now_us, 0u, 0u);
    } else if ((c == 'x' || c == 'X') && o->stage != SENSE_TRACE_IDLE
               && !o->stopped) {
        /* The end moves to now, and stays: what the ring holds from
         * before it is still written.  Not to before the trace's own
         * trigger, which can lie ahead: its line is the trace's. */
        uint32_t end_t = to_t(now_us);
        if (!at_or_past(end_t, o->t0 + 1u)) {
            end_t = o->t0 + 1u;
        }
        if ((int32_t)(end_t - o->end_t) < 0) {
            o->end_t = end_t;
        }
        o->stopped = true;
    }
}

/* --------------------------------------------------------------- lines */

typedef struct {
    char   s[SENSE_TRACE_LINE_MAX];
    size_t n;
} line_t;

static void put_c(line_t *l, char c)
{
    if (l->n < sizeof(l->s)) {
        l->s[l->n++] = c;
    }
}

static void put_s(line_t *l, const char *s)
{
    while (*s != '\0') {
        put_c(l, *s++);
    }
}

static void put_u(line_t *l, uint32_t v)
{
    char d[10];
    unsigned n = 0u;
    do {
        d[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u);
    while (n > 0u) {
        put_c(l, d[--n]);
    }
}

static void put_i(line_t *l, int32_t v)
{
    if (v < 0) {
        put_c(l, '-');
        put_u(l, 0u - (uint32_t)v);
    } else {
        put_u(l, (uint32_t)v);
    }
}

static void put_x4(line_t *l, uint32_t v)
{
    static const char k_hex[] = "0123456789ABCDEF";
    for (int shift = 12; shift >= 0; shift -= 4) {
        put_c(l, k_hex[(v >> shift) & 0xFu]);
    }
}

/* A key and its value, the value held to @p max. */
static void put_kv(line_t *l, const char *key, uint32_t v, uint32_t max)
{
    put_s(l, key);
    put_u(l, (v > max) ? max : v);
}

static void put_eol(line_t *l)
{
    put_s(l, "\r\n");
}

static void put_state(line_t *l, uint32_t cfg)
{
    put_kv(l, " on=", (cfg >> 16) & 1u, 1u);
    put_kv(l, " rst=", cfg >> 24, 255u);
}

static void line_start(const sense_trace_out_t *o, line_t *l)
{
    static const char *const k_trig[] = { "cmd", "edge", "key" };
    put_kv(l, "$T v=", SENSE_TRACE_FORMAT, 9u);
    put_kv(l, " n=", o->id, 65535u);
    put_s(l, " trig=");
    put_s(l, k_trig[o->trig]);
    put_kv(l, " t=", o->t0, UINT32_MAX);
    put_kv(l, " ms=", o->ms0, UINT32_MAX);
    put_kv(l, " len=", o->len_ms, 99999u);
    put_eol(l);
}

static void line_setup(const sense_trace_out_t *o, line_t *l)
{
    put_kv(l, "$H dt_us=", SENSE_TRACE_PERIOD_US, 9999u);
    put_kv(l, " shunt_uohm=", o->shunt, UINT32_MAX);
    put_s(l, " cfg=0x");
    put_x4(l, o->cfg & 0xFFFFu);
    put_state(l, o->cfg);
    put_eol(l);
}

static void line_mark(const sense_trace_mark_t *m, line_t *l)
{
    static const char k_tag[] = { 'C', 'E', 'K', 'D' };
    put_c(l, '$');
    put_c(l, k_tag[m->kind]);
    put_kv(l, " t=", m->t, UINT32_MAX);
    if (m->kind == SENSE_TRACE_TRIG_CMD
        || m->kind == SENSE_TRACE_MARK_DEST) {
        put_kv(l, " ch=", m->ch, 65535u);
        put_kv(l, " us=", m->us, 65535u);
    }
    put_eol(l);
}

static void line_end(const sense_trace_out_t *o, sense_trace_end_t why,
                     bool stalled, uint32_t n_mlost, line_t *l)
{
    static const char k_sure[]    = { '?', 't', 's', 'k' };
    static const char k_stalled[] = { '?', 'T', 'S', 'K' };
    const char *const k_why = (stalled || o->stalled) ? k_stalled : k_sure;
    put_kv(l, "$Z n=", o->id, 65535u);
    put_kv(l, " s=", o->n_s, 99999999u);
    put_kv(l, " v=", o->n_v, 9999999u);
    put_kv(l, " l=", o->n_lost, 99999999u);
    put_kv(l, " m=", o->n_m, 9999u);
    put_kv(l, " ml=", n_mlost, 9999u);
    put_s(l, " e=");
    put_c(l, k_why[why]);
    put_eol(l);
}

/* What writing a line changes. */
typedef enum {
    ACT_NONE = 0,   /* nothing to write yet                          */
    ACT_STAGE,      /* a header line: the stage moves on             */
    ACT_MARK,
    ACT_MARK_STALLED, /* a trigger line, core 1 not seen past its time */
    ACT_LOST,       /* the $L line of the record at the tail         */
    ACT_LOST_AHEAD, /* a $L line with no record kept since the drop  */
    ACT_REC,        /* the record at the tail, with or without a line */
    ACT_END,        /* the end line, on the trace's time             */
    ACT_END_SETUP,  /* the end line, at a set-up record              */
} act_t;

/* The end line of a trace that ran to its end. */
static act_t ends(const sense_trace_out_t *o, bool stalled, line_t *l)
{
    line_end(o, o->stopped ? SENSE_TRACE_END_KEY : SENSE_TRACE_END_TIME,
             stalled, o->n_mlost, l);
    return ACT_END;
}

/* Whether a trigger is left for a trace of its own when this one ends:
 * one that waited, or -- unless the console stopped this trace -- one
 * whose line is not written. */
static bool next_starts(const sense_trace_out_t *o)
{
    if (o->wait_n > 0u) {
        return true;
    }
    for (uint8_t k = 0u; k < o->q_n && !o->stopped; ++k) {
        if (o->q[(o->q_head + k) % SENSE_TRACE_MARKS].kind
            != SENSE_TRACE_MARK_DEST) {
            return true;
        }
    }
    return false;
}

/* A trace that ends at a set-up record of time @p cut: the trigger lines
 * not written are all of after @p cut, and so the next trace's.  Whether
 * a trigger that found the queue full is the next trace's too, and
 * whether all of them are.  With no trigger left no trace follows, and
 * the count stays here. */
static bool lost_past(const sense_trace_out_t *o, uint32_t cut)
{
    return o->n_mlost > 0u && (int32_t)(o->mlost_t[1] - cut) > 0
           && next_starts(o);
}

static bool lost_all_past(const sense_trace_out_t *o, uint32_t cut)
{
    return lost_past(o, cut) && (int32_t)(o->mlost_t[0] - cut) > 0;
}

/* With the ring empty, whether a record of before @p t can still come:
 * NOT_YET while core 1 has not finished a tick that started at or past
 * @p t, STALLED when it has not SENSE_TRACE_STALL_MS after @p t. */
typedef enum { NOT_YET = 0, PASSED, STALLED } past_t;

static past_t past(uint32_t fed_t, uint32_t now_t, uint32_t t)
{
    if (at_or_past(fed_t, t)) {
        return PASSED;
    }
    return at_or_past(now_t, t + SENSE_TRACE_STALL_MS * SENSE_TRACE_T_PER_MS)
               ? STALLED : NOT_YET;
}

static void line_lost(uint32_t n, line_t *l)
{
    put_kv(l, "$L n=", n, SENSE_TRACE_LOST_MAX);
    put_eol(l);
}

/* The next line of the trace under way into @p l, and what it stands
 * for; for ACT_LOST_AHEAD the records it counts into @p ahead.  Changes
 * nothing else. */
static act_t next_line(const sense_trace_t *tr, uint32_t now_t, line_t *l,
                       uint32_t *ahead)
{
    const sense_trace_out_t *o = &tr->out;
    l->n = 0u;
    if (o->stage == SENSE_TRACE_HEAD_T) {
        line_start(o, l);
        return ACT_STAGE;
    }
    /* Core 1's progress, then its drops, then the ring, in this order.
     * A ring found empty last was empty all the while: it holds no record
     * of a tick the progress counts, core 1 dropped none in between, and
     * the drops read are those up to the last record read out of it and
     * since.  A drop core 1 adds after the read is of a tick the progress
     * read does not count. */
    const uint32_t fed_t =
        (uint32_t)atomic_load_explicit(&tr->fed, memory_order_acquire);
    const uint32_t drops =
        (uint32_t)atomic_load_explicit(&tr->drops, memory_order_acquire);
    const sense_trace_rec_t *r = peek(tr);
    if (o->stage == SENSE_TRACE_HEAD_H) {
        /* The set-up as it stands at the trace's first record: a set-up
         * record at the tail goes into the header, not into the trace. */
        if (r == NULL || !is_setup(r)) {
            line_setup(o, l);
            return ACT_STAGE;
        }
        if (lost_before(o, r) != 0u && !o->lost_shown) {
            line_lost(lost_before(o, r), l);
            return ACT_LOST;
        }
        return ACT_REC;
    }
    /* Records dropped with none kept since: no record carries their
     * count yet, and none may come before the trace's end.  Said here,
     * behind the last record written and before any line that waits for
     * core 1, the end line among them. */
    if (r == NULL && drops - o->drops_seen - o->ahead != 0u) {
        *ahead = drops - o->drops_seen - o->ahead;
        line_lost(*ahead, l);
        return ACT_LOST_AHEAD;
    }
    /* A trigger line in the order of its time: behind the records of
     * before it, so a record that ends the trace before the trigger
     * ends it before the trigger's line.  With the ring empty it waits
     * for core 1 to pass its time: a frame's start lies ahead when its
     * command is seen, and a record of before it may be on its way. */
    /* A trigger line at or past the end -- the console's stop moved the
     * end before it -- is not this trace's. */
    if (o->q_n > 0u && !at_or_past(o->q[o->q_head].t, o->end_t)) {
        const uint32_t mark_t = o->q[o->q_head].t;
        const past_t due = (r != NULL)
            ? (at_or_past(r->t, mark_t) ? PASSED : NOT_YET)
            : past(fed_t, now_t, mark_t);
        if (due != NOT_YET) {
            line_mark(&o->q[o->q_head], l);
            return (due == STALLED) ? ACT_MARK_STALLED : ACT_MARK;
        }
    }
    if (r == NULL) {
        /* A record of before the end may still be on its way.  None that
         * was dropped is unsaid here. */
        const past_t due = past(fed_t, now_t, o->end_t);
        if (due == NOT_YET) {
            return ACT_NONE;
        }
        return ends(o, due == STALLED, l);
    }
    /* Records dropped before this one: said before it, and before the
     * end line when this one lies past the end. */
    if (lost_before(o, r) != 0u && !o->lost_shown) {
        line_lost(lost_before(o, r), l);
        return ACT_LOST;
    }
    if (at_or_past(r->t, o->end_t)) {
        return ends(o, false, l);
    }
    switch ((sense_trace_kind_t)META_KIND(r->meta)) {
    case SENSE_TRACE_SHUNT:
    case SENSE_TRACE_CFG:
        /* Another shunt or Configuration: the samples after it are not
         * this header's. */
        if (changes_setup(o, r)) {
            line_end(o, SENSE_TRACE_END_SETUP, false,
                     lost_all_past(o, r->t) ? 0u : o->n_mlost, l);
            return ACT_END_SETUP;
        }
        if (META_KIND(r->meta) == SENSE_TRACE_CFG
            && (uint32_t)r->v != o->cfg) {
            put_s(l, "$S");
            put_state(l, (uint32_t)r->v);
            put_eol(l);
        }
        break;
    case SENSE_TRACE_BUS:
        put_c(l, 'v');
        put_i(l, r->v);
        put_eol(l);
        break;
    case SENSE_TRACE_CLIP_HI:
    case SENSE_TRACE_CLIP_LO:
        /* The end codes of the 13-bit range: no value reads them. */
        put_i(l, (int32_t)(r->t - o->last_t));
        put_s(l, (META_KIND(r->meta) == SENSE_TRACE_CLIP_HI) ? ",4095"
                                                              : ",-4096");
        put_eol(l);
        break;
    case SENSE_TRACE_CURRENT:
    default:
        put_i(l, (int32_t)(r->t - o->last_t));
        put_c(l, ',');
        put_i(l, sense_trace_code(r->v, o->shunt));
        put_eol(l);
        break;
    }
    return ACT_REC;
}

/* Idle from here: the scan for set-up records starts at the tail. */
static void to_idle(sense_trace_t *tr)
{
    tr->out.stage   = SENSE_TRACE_IDLE;
    tr->out.q_n     = 0u;
    tr->out.scanned = tail_of(tr);
}

/* The end line is written: the triggers that waited for it start the
 * next trace, in their order, over what the ring holds since the end.
 * @p cut is the time of the set-up record the trace ended at, NULL for a
 * trace that ended on its time. */
static void ended(sense_trace_t *tr, const uint32_t *cut)
{
    sense_trace_out_t *o = &tr->out;
    /* The trigger lines the trace ended before are triggers still: the
     * first of them, then the ones that waited.  Not after the console's
     * stop: it ends what was asked for before it.  Both are taken out of
     * the queues first: a trigger past the next trace's end waits again. */
    sense_trace_mark_t left[SENSE_TRACE_MARKS];
    sense_trace_mark_t waited[SENSE_TRACE_MARKS];
    uint8_t n_left = 0u;
    for (uint8_t k = 0u; k < o->q_n && !o->stopped; ++k) {
        left[n_left++] = o->q[(o->q_head + k) % SENSE_TRACE_MARKS];
    }
    const uint8_t n = o->wait_n;
    memcpy(waited, o->wait, n * sizeof(waited[0]));
    /* The triggers that found a queue full and are the next trace's. */
    uint32_t lost = 0u;
    uint32_t lost_t[2] = { 0u, 0u };
    if (cut != NULL && lost_past(o, *cut)) {
        lost_add(&lost, lost_t, o->n_mlost, o->mlost_t[0], o->mlost_t[1]);
    }
    lost_add(&lost, lost_t, o->wait_lost, o->wait_lost_t[0],
             o->wait_lost_t[1]);
    to_idle(tr);
    o->wait_n    = 0u;
    o->wait_lost = 0u;
    for (uint8_t k = 0u; k < n_left; ++k) {
        if (left[k].kind != SENSE_TRACE_MARK_DEST) {
            (void)trigger(tr, (sense_trace_trig_t)left[k].kind,
                          left[k].at_us, left[k].ch, left[k].us);
        }
    }
    place_t placed[SENSE_TRACE_MARKS];
    for (uint8_t k = 0u; k < n; ++k) {
        const sense_trace_mark_t *e = &waited[k];
        if (e->kind != SENSE_TRACE_MARK_DEST
            && e->kind != SENSE_TRACE_MARK_STEP) {
            placed[k] = trigger(tr, (sense_trace_trig_t)e->kind, e->at_us,
                                e->ch, e->us);
            continue;
        }
        /* A slewed command's last change goes where its command went:
         * that trace is open SENSE_TRACE_EDGE_MS past it, as it is past
         * every change it sees itself. */
        placed[k] = TRIG_LOST;
        for (uint8_t j = k; j-- > 0u;) {
            if (waited[j].kind == SENSE_TRACE_TRIG_CMD
                && waited[j].ch == e->ch) {
                placed[k] = placed[j];
                break;
            }
        }
        if (placed[k] == TRIG_PLACED) {
            extend(o, e->t + SENSE_TRACE_EDGE_MS * SENSE_TRACE_T_PER_MS);
            if (e->kind == SENSE_TRACE_MARK_DEST) {
                (void)mark(o, SENSE_TRACE_MARK_DEST, e->t, 0u, e->ch,
                           e->us);
            }
        } else if (placed[k] == TRIG_WAITS) {
            wait_slew(o, (sense_trace_trig_t)e->kind, e->ch, e->t, e->us);
        }
    }
    if (o->stage == SENSE_TRACE_IDLE) {
        return;
    }
    /* Where a slewed command ended is said in the trace its time is in. */
    for (uint8_t k = 0u; k < n_left; ++k) {
        if (left[k].kind == SENSE_TRACE_MARK_DEST
            && open_at(o, left[k].t)) {
            mark(o, SENSE_TRACE_MARK_DEST, left[k].t, 0u, left[k].ch,
                 left[k].us);
        }
    }
    lost_add(&o->n_mlost, o->mlost_t, lost, lost_t[0], lost_t[1]);
}

/* The line @p act stood for is written; @p ahead is ACT_LOST_AHEAD's
 * count. */
static void commit(sense_trace_t *tr, act_t act, uint32_t ahead)
{
    sense_trace_out_t *o = &tr->out;
    const sense_trace_rec_t *r = peek(tr);
    switch (act) {
    case ACT_STAGE:
        o->stage = (o->stage == SENSE_TRACE_HEAD_T) ? SENSE_TRACE_HEAD_H
                                                    : SENSE_TRACE_BODY;
        break;
    case ACT_MARK_STALLED:
        o->stalled = true;
        /* fall through */
    case ACT_MARK:
        o->q_head = (uint8_t)((o->q_head + 1u) % SENSE_TRACE_MARKS);
        --o->q_n;
        ++o->n_m;
        break;
    case ACT_LOST:
        o->n_lost    += lost_before(o, r);
        o->lost_shown = true;
        break;
    case ACT_LOST_AHEAD:
        /* The next record kept carries these and no others: the ring was
         * empty, so it finds room. */
        o->n_lost += ahead;
        o->ahead  += ahead;
        break;
    case ACT_REC:
        if (META_KIND(r->meta) == SENSE_TRACE_BUS) {
            ++o->n_v;
        } else if (!is_setup(r)) {
            ++o->n_s;
            o->last_t = r->t;
        }
        pop(tr);
        break;
    case ACT_END_SETUP:
        /* The record goes with the trace it ended.  One that follows it
         * is the next header's, with the records it says were dropped. */
        {
            const uint32_t cut = r->t;
            pop(tr);
            ended(tr, &cut);
        }
        break;
    case ACT_END:
    default:
        ended(tr, NULL);
        break;
    }
}

/* No trace: the newest records stay for the one a trigger starts, none
 * of them older than the last set-up or state record. */
static void idle(sense_trace_t *tr)
{
    sense_trace_out_t *o = &tr->out;
    const uint32_t head = head_of(tr);
    for (; o->scanned != head; ++o->scanned) {
        if (is_setup(&tr->buf[o->scanned & tr->mask])) {
            while (tail_of(tr) != o->scanned + 1u) {
                pop(tr);
            }
        }
    }
    const uint32_t keep = (tr->mask + 1u < 2u * SENSE_TRACE_PRE)
                              ? (tr->mask + 1u) / 2u : SENSE_TRACE_PRE;
    while (head - tail_of(tr) > keep) {
        pop(tr);
    }
}

size_t sense_trace_pump(sense_trace_t *tr, uint64_t now_us, bool connected,
                        char *out, size_t room)
{
    sense_trace_out_t *o = &tr->out;
    if (tr->buf == NULL) {
        return 0u;
    }
    const uint32_t now_t = to_t(now_us);
    /* No console, or one that has taken nothing for so long that the
     * trace's times are about to lose their order. */
    if (o->stage != SENSE_TRACE_IDLE
        && (!connected
            || (int32_t)(now_t - o->end_t)
                   >= (int32_t)SENSE_TRACE_OWED_MAX_T)) {
        to_idle(tr);
        o->wait_n    = 0u;
        o->wait_lost = 0u;
        /* A $L line of this trace is in no trace that has an end line:
         * the records it counted are said again where their record
         * stands. */
        o->lost_shown = false;
        o->ahead      = 0u;
        ++o->abandoned;
    }
    if (o->stage == SENSE_TRACE_IDLE) {
        idle(tr);
        return 0u;
    }
    size_t n = 0u;
    for (;;) {
        line_t l;
        uint32_t ahead = 0u;
        const act_t act = next_line(tr, now_t, &l, &ahead);
        if (act == ACT_NONE || l.n > room - n) {
            break;
        }
        memcpy(&out[n], l.s, l.n);
        n += l.n;
        commit(tr, act, ahead);
        if (act == ACT_END || act == ACT_END_SETUP) {
            break;
        }
    }
    return n;
}
