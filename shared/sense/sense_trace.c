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
        if (p->lost < SENSE_TRACE_LOST_MAX) {
            ++p->lost;
        }
        return false;
    }
    sense_trace_rec_t *r = &tr->buf[head & tr->mask];
    r->t    = t;
    r->v    = v;
    r->meta = ((uint32_t)kind << 24) | p->lost;
    p->lost = 0u;
    atomic_store_explicit(&tr->head, head + 1u, memory_order_release);
    return true;
}

/* The set-up and the state as they stand: a record for each that moved,
 * the shunt's first, written again at the next call while the ring has
 * no room for it. */
static void publish(sense_trace_t *tr, uint32_t cfg, uint32_t shunt)
{
    sense_trace_src_t *p = &tr->src;
    if (shunt != p->shunt) {
        p->shunt      = shunt;
        p->shunt_owed = true;
    }
    if (cfg != p->cfg) {
        p->cfg      = cfg;
        p->cfg_owed = true;
    }
    if (p->shunt_owed
        && push(tr, SENSE_TRACE_SHUNT, p->t, (int32_t)p->shunt)) {
        p->shunt_owed = false;
    }
    /* After the shunt's, never before it: the other core can free a
     * place between the two. */
    if (p->cfg_owed && !p->shunt_owed
        && push(tr, SENSE_TRACE_CFG, p->t, (int32_t)p->cfg)) {
        p->cfg_owed = false;
    }
}

void sense_trace_feed(sense_trace_t *tr, const sense_sched_t *s)
{
    sense_trace_src_t *p = &tr->src;
    if (tr->buf == NULL) {
        return;
    }
    if (s == NULL) {
        /* No bus: the next schedule starts afresh. */
        p->have = false;
        p->win  = 0u;
        p->n_v  = 0u;
        p->n_hi = 0u;
        p->n_lo = 0u;
        publish(tr, 0u, 0u);
        return;
    }
    const bool online = ina3221_state(&s->i3221) == SENSE_PART_ONLINE;
    publish(tr, (uint32_t)s->i3221.config | (online ? 1u << 16 : 0u)
                    | ((uint32_t)s->i3221_resets << 24),
            s->i3221.shunt_uohm);

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

/* The record at the tail leaves the ring; a set-up record leaves what it
 * says behind. */
static void pop(sense_trace_t *tr)
{
    sense_trace_out_t *o = &tr->out;
    const uint32_t tail = tail_of(tr);
    const sense_trace_rec_t *r = &tr->buf[tail & tr->mask];
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

/* A line for the console's queue, or counted when it is full. */
static void mark(sense_trace_out_t *o, sense_trace_trig_t kind, uint32_t t,
                 uint16_t ch, uint16_t us)
{
    if (o->q_n >= SENSE_TRACE_MARKS) {
        ++o->n_mlost;
        return;
    }
    sense_trace_mark_t *m =
        &o->q[(o->q_head + o->q_n) % SENSE_TRACE_MARKS];
    m->kind = (uint8_t)kind;
    m->t    = t;
    m->ch   = ch;
    m->us   = us;
    ++o->q_n;
}

void sense_trace_trigger(sense_trace_t *tr, sense_trace_trig_t kind,
                         uint64_t at_us, uint16_t ch, uint16_t us)
{
    sense_trace_out_t *o = &tr->out;
    if (tr->buf == NULL) {
        return;
    }
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
        o->n_s = o->n_v = o->n_m = o->n_lost = o->n_mlost = 0u;
        o->q_n = o->q_head = 0u;
    } else if (open_at(o, t)) {
        extend(o, end_t);
    } else {
        /* The trace under way is over at this trigger's time and its
         * lines are not all written: the trigger is the next trace's. */
        if (o->wait_n >= SENSE_TRACE_MARKS) {
            ++o->wait_lost;
            return;
        }
        o->wait[o->wait_n].kind  = (uint8_t)kind;
        o->wait[o->wait_n].at_us = at_us;
        o->wait[o->wait_n].ch    = ch;
        o->wait[o->wait_n].us    = us;
        ++o->wait_n;
        return;
    }
    mark(o, kind, t, ch, us);
}

/* A slewed command of @p w has ended, at its last change: said in the
 * trace under way. */
static void slew_ends(sense_trace_out_t *o, sense_trace_watch_t *w,
                      uint16_t ch)
{
    if (w->slewed && open_at(o, w->changed_t)) {
        mark(o, SENSE_TRACE_MARK_DEST, w->changed_t, ch, w->pulse);
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
         * got to is its end. */
        slew_ends(o, w, ch);
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
        return true;
    }
    w->slewed = true;
    if (open_at(o, now_t)) {
        extend(o, now_t + SENSE_TRACE_EDGE_MS * SENSE_TRACE_T_PER_MS);
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
         * before it is still written. */
        const uint32_t now_t = to_t(now_us);
        if ((int32_t)(now_t - o->end_t) < 0) {
            o->end_t = now_t;
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
                     line_t *l)
{
    static const char k_why[] = { '?', 't', 's', 'k' };
    put_kv(l, "$Z n=", o->id, 65535u);
    put_kv(l, " s=", o->n_s, 99999999u);
    put_kv(l, " v=", o->n_v, 9999999u);
    put_kv(l, " l=", o->n_lost, 99999999u);
    put_kv(l, " m=", o->n_m, 9999u);
    put_kv(l, " ml=", o->n_mlost, 9999u);
    put_s(l, " e=");
    put_c(l, k_why[why]);
    put_eol(l);
}

/* What writing a line changes. */
typedef enum {
    ACT_NONE = 0,   /* nothing to write yet                          */
    ACT_STAGE,      /* a header line: the stage moves on             */
    ACT_MARK,
    ACT_LOST,       /* the $L line of the record at the tail         */
    ACT_REC,        /* the record at the tail, with or without a line */
    ACT_END,        /* the end line, on the trace's time             */
    ACT_END_SETUP,  /* the end line, at a set-up record              */
} act_t;

/* The end line of a trace that ran to its end. */
static act_t ends(const sense_trace_out_t *o, line_t *l)
{
    line_end(o, o->stopped ? SENSE_TRACE_END_KEY : SENSE_TRACE_END_TIME, l);
    return ACT_END;
}

/* The next line of the trace under way into @p l, and what it stands
 * for.  Changes nothing. */
static act_t next_line(const sense_trace_t *tr, uint32_t now_t, line_t *l)
{
    const sense_trace_out_t *o = &tr->out;
    l->n = 0u;
    if (o->stage == SENSE_TRACE_HEAD_T) {
        line_start(o, l);
        return ACT_STAGE;
    }
    if (o->stage == SENSE_TRACE_HEAD_H) {
        line_setup(o, l);
        return ACT_STAGE;
    }
    if (o->q_n > 0u) {
        line_mark(&o->q[o->q_head], l);
        return ACT_MARK;
    }
    const sense_trace_rec_t *r = peek(tr);
    if (r == NULL) {
        /* A record of before the end may still be on its way. */
        if (!at_or_past(now_t, o->end_t + SENSE_TRACE_END_WAIT_MS
                                          * SENSE_TRACE_T_PER_MS)) {
            return ACT_NONE;
        }
        return ends(o, l);
    }
    /* Records dropped before this one: said before it, and before the
     * end line when this one lies past the end. */
    if (META_LOST(r->meta) != 0u && !o->lost_shown) {
        put_kv(l, "$L n=", META_LOST(r->meta), SENSE_TRACE_LOST_MAX);
        put_eol(l);
        return ACT_LOST;
    }
    if (at_or_past(r->t, o->end_t)) {
        return ends(o, l);
    }
    switch ((sense_trace_kind_t)META_KIND(r->meta)) {
    case SENSE_TRACE_SHUNT:
    case SENSE_TRACE_CFG:
        /* Another shunt or Configuration: the samples after it are not
         * this header's. */
        if (changes_setup(o, r)) {
            line_end(o, SENSE_TRACE_END_SETUP, l);
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
 * next trace, in their order, over what the ring holds since the end. */
static void ended(sense_trace_t *tr)
{
    sense_trace_out_t *o = &tr->out;
    const uint8_t n = o->wait_n;
    const uint32_t lost = o->wait_lost;
    to_idle(tr);
    o->wait_n    = 0u;
    o->wait_lost = 0u;
    for (uint8_t k = 0u; k < n; ++k) {
        sense_trace_trigger(tr, (sense_trace_trig_t)o->wait[k].kind,
                            o->wait[k].at_us, o->wait[k].ch, o->wait[k].us);
    }
    if (n > 0u) {
        o->n_mlost += lost;
    }
}

/* The line @p act stood for is written. */
static void commit(sense_trace_t *tr, act_t act)
{
    sense_trace_out_t *o = &tr->out;
    const sense_trace_rec_t *r = peek(tr);
    switch (act) {
    case ACT_STAGE:
        o->stage = (o->stage == SENSE_TRACE_HEAD_T) ? SENSE_TRACE_HEAD_H
                                                    : SENSE_TRACE_BODY;
        break;
    case ACT_MARK:
        o->q_head = (uint8_t)((o->q_head + 1u) % SENSE_TRACE_MARKS);
        --o->q_n;
        ++o->n_m;
        break;
    case ACT_LOST:
        o->n_lost    += META_LOST(r->meta);
        o->lost_shown = true;
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
        /* The set-up's records go with the trace they ended: the next
         * header carries what they say. */
        while (r != NULL && is_setup(r)) {
            pop(tr);
            r = peek(tr);
        }
        ended(tr);
        break;
    case ACT_END:
    default:
        ended(tr);
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
    if (!connected && o->stage != SENSE_TRACE_IDLE) {
        to_idle(tr);
        o->wait_n    = 0u;
        o->wait_lost = 0u;
        ++o->abandoned;
    }
    if (o->stage == SENSE_TRACE_IDLE) {
        idle(tr);
        return 0u;
    }
    const uint32_t now_t = to_t(now_us);
    size_t n = 0u;
    for (;;) {
        line_t l;
        const act_t act = next_line(tr, now_t, &l);
        if (act == ACT_NONE || l.n > room - n) {
            break;
        }
        memcpy(&out[n], l.s, l.n);
        n += l.n;
        commit(tr, act);
        if (act == ACT_END || act == ACT_END_SETUP) {
            break;
        }
    }
    return n;
}
