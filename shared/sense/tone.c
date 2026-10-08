/*
 * ESC tones from the edges of one motor phase.  The rules are in tone.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tone.h"

#include <string.h>

/* ---------------------------------------------------------------- set-up */

void tone_cfg_defaults(tone_cfg_t *c, uint32_t tick_hz)
{
    if (c == NULL) {
        return;
    }
    c->tick_hz            = tick_hz;
    c->window_us          = 8000u;
    c->f_min_hz           = 400u;
    c->f_max_hz           = 6500u;
    c->carrier_min_hz     = 7000u;
    c->glitch_ns          = 500u;
    c->hold_ns            = 8000u;
    c->gap_us             = 3000u;
    c->split_pct          = 8u;
    c->min_periods        = 3u;
    c->window_min_periods = 2u;
}

/* @p a / @p b rounded up. */
static uint64_t div_up(uint64_t a, uint64_t b)
{
    return (a + b - 1u) / b;
}

/* A duration of @p ns as ticks of @p tick_hz, rounded up: the one rule
 * for every time setting (window, glitch, hold-off, gap), so a low or a
 * silence counts as that long only once it has lasted that long.  A
 * setting derived from another, as start_low from the hold-off, is
 * derived from the rounded ticks, never rounded again. */
static uint64_t ns_ticks(uint32_t tick_hz, uint64_t ns)
{
    return div_up((uint64_t)tick_hz * ns, 1000000000u);
}

bool tone_init(tone_t *d, const tone_cfg_t *c)
{
    if (d == NULL || c == NULL) {
        return false;
    }
    memset(d, 0, sizeof *d);
    if (c->f_min_hz < 50u || c->f_min_hz >= c->f_max_hz
        || c->carrier_min_hz <= c->f_max_hz
        || c->f_max_hz > c->tick_hz || c->carrier_min_hz > c->tick_hz
        || c->glitch_ns == 0u || c->glitch_ns > 10000u
        || c->hold_ns > 100000u
        || (uint64_t)c->tick_hz * c->glitch_ns < 1000000000u
        || c->window_us < 1000u || c->window_us > 100000u
        || (uint64_t)c->gap_us * c->f_min_hz < 1000000u
        || c->min_periods == 0u || c->window_min_periods == 0u
        || c->split_pct > 100u) {
        return false;
    }
    d->c         = *c;
    d->win_ticks = ns_ticks(c->tick_hz, (uint64_t)c->window_us * 1000u);
    /* Each bound in whole ticks, rounded so that a period exactly at it,
     * which the tick clock sees as the tick below or the tick above, is
     * inside: the shortest tone period down, the longest up, the carrier's
     * up.  Durations by ns_ticks(); start_low is twice the hold-off as
     * the capture's tone_holdoff_init() rounds it. */
    d->per_min   = c->tick_hz / c->f_max_hz;
    d->per_max   = div_up(c->tick_hz, c->f_min_hz);
    d->car_max   = div_up(c->tick_hz, c->carrier_min_hz);
    d->glitch    = ns_ticks(c->tick_hz, c->glitch_ns);
    d->gap       = ns_ticks(c->tick_hz, (uint64_t)c->gap_us * 1000u);
    d->start_low = 2u * ns_ticks(c->tick_hz, c->hold_ns);
    /*
     * The shortest time between two burst starts that makes a period the
     * detector counts: at least half per_min, the shortest period in
     * range; and the low before the second start at least glitch, or it is
     * no low, and at least start_low, or it starts no burst.  The on-time
     * before that low adds nothing: a rise and a fall on one tick are
     * taken.
     */
    uint64_t spacing = d->per_min / 2u;
    if (d->glitch > spacing) {
        spacing = d->glitch;
    }
    if (d->start_low > spacing) {
        spacing = d->start_low;
    }
    d->spacing = spacing;
    /*
     * Settings under which no signal can make a tone, refused rather than
     * run deaf.  The tone and carrier ranges, as whole ticks, must not
     * meet: a rise interval of per_min, the fastest tone unchopped, would
     * read as carrier-rate at car_max or under.  Two bursts need that spacing to be under the longest
     * period in range (per_max) and under the silence that ends the run
     * (gap; at least per_max by the gap_us check above, so the weaker of
     * the two).  A window needs window_min_periods periods to end in its
     * win_ticks, each burst start at least the spacing after the one
     * before: (window_min_periods - 1) spacings under win_ticks.
     */
    if (d->per_min <= d->car_max
        || d->gap <= spacing || d->per_max <= spacing
        || (uint64_t)(c->window_min_periods - 1u) * spacing
               >= d->win_ticks) {
        memset(d, 0, sizeof *d);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------ arithmetic */

/* @p n periods over @p ticks, in Hz; 0 for none.  Only at a window's or a
 * beep's end, never per edge. */
static float hz_of(const tone_t *d, uint32_t n, uint64_t ticks)
{
    if (n == 0u || ticks == 0u) {
        return 0.0f;
    }
    return (float)d->c.tick_hz * (float)n / (float)ticks;
}

/*
 * Means are kept as a sum of ticks over a count and compared as fractions,
 * never divided down to whole ticks: at a few ticks a period, a mean cut
 * to a whole tick is off by tens of percent.  Sizes: a sum is at most a
 * beep's length in ticks, a count at most its periods, split_pct at most
 * 100; the products below stay under 2^63 for beeps of hours at 37.5 MHz.
 */
typedef struct {
    uint64_t s;              /**< ticks                                   */
    uint64_t n;              /**< periods; 0 for no mean                  */
} ratio_t;

static ratio_t ratio(uint64_t s, uint64_t n)
{
    ratio_t r = { s, n };
    return r;
}

/* x - y, scaled by both counts: x.s * y.n - y.s * x.n, as its size and
 * whether x is the larger. */
static uint64_t ratio_diff(ratio_t x, ratio_t y, bool *x_more)
{
    const uint64_t a = x.s * y.n;
    const uint64_t b = y.s * x.n;
    *x_more = a > b;
    return a > b ? a - b : b - a;
}

/* @p x lies more than pct / den percent from @p y, relative to @p y:
 * |x - y| * 100 * den > pct * y, with both sides times x.n * y.n. */
static bool off_by_more(ratio_t x, ratio_t y, uint32_t pct, uint32_t den)
{
    bool more = false;
    const uint64_t diff = ratio_diff(x, y, &more);
    return diff * 100u * den > (uint64_t)pct * y.s * x.n;
}

static ratio_t beep_mean(const tone_t *d)
{
    return ratio(d->b_sum, d->b_n);
}

/* The carrier period, ticks; 0 with no carrier seen.  The shortest rise
 * interval inside a burst, not counting a burst's first, once there is
 * one; until then the shortest carrier-rate low and the longest
 * carrier-rate high.  The rise interval is the better: a high that spans
 * two carrier periods, the low between them too short to reach the
 * input, lengthens the second and is a multiple of the first. */
static uint64_t carrier(const tone_t *d)
{
    if (d->car_rise != 0u) {
        return d->car_rise;
    }
    return d->car_low != 0u ? d->car_low + d->car_high : 0u;
}

/* A mean period inside the tone range: per_min <= s / n <= per_max. */
static bool in_band(const tone_t *d, ratio_t m)
{
    return m.n != 0u && m.s >= d->per_min * m.n && m.s <= d->per_max * m.n;
}

/* ---------------------------------------------------------------- windows */

static void window_finish(tone_t *d)
{
    tone_window_t *w = &d->window;
    w->index   = d->w_index;
    w->periods = d->w_n;
    w->bursts  = d->w_bursts;
    w->present = d->w_n >= d->c.window_min_periods
                 && in_band(d, ratio(d->w_sum, d->w_n));
    w->freq_hz = w->present ? hz_of(d, d->w_n, d->w_sum) : 0.0f;
    d->have_window = true;
    d->w_n = 0;
    d->w_sum = 0;
    d->w_bursts = 0;
}

static void window_open(tone_t *d, uint64_t index)
{
    d->w_index = index;
    d->w_end = (index + 1u) * d->win_ticks;
}

/* Close every window that ends at or before @p t.  A stretch of empty
 * windows reports the last of them only. */
static void windows_to(tone_t *d, uint64_t t)
{
    if (t < d->w_end) {
        return;
    }
    window_finish(d);
    const uint64_t index = t / d->win_ticks;
    if (index > d->w_index + 1u) {
        window_open(d, index - 1u);
        window_finish(d);
    }
    window_open(d, index);
}

/* ---------------------------------------------------------------- beeps */

static void emit(tone_t *d, uint64_t end)
{
    if (d->b_n < d->c.min_periods || !in_band(d, beep_mean(d))) {
        d->st.rejected++;
        return;
    }
    if (d->q_len == TONE_BEEP_QUEUE) {
        d->st.lost++;
        return;
    }
    tone_beep_t *b = &d->q[(d->q_head + d->q_len) % TONE_BEEP_QUEUE];
    d->q_len++;
    b->start      = d->b_start;
    b->end        = end;
    b->freq_hz    = hz_of(d, d->b_n, d->b_sum);
    b->carrier_hz = hz_of(d, 1u, carrier(d));
    b->bursts     = d->b_bursts;
    b->periods    = d->b_n;
    b->flags      = d->b_flags;
    d->st.beeps++;
}

static void beep_open(tone_t *d, uint64_t start, uint8_t flags)
{
    d->b_start  = start;
    d->b_sum    = 0;
    d->b_n      = 0;
    d->b_bursts = 1;
    d->b_flags  = flags;
}

/* The oldest @p n periods waiting join the beep under way: each is a
 * burst, and an in-range one near the beep's mean (or, before the beep
 * has one, near @p ref) is part of the mean. */
static void commit(tone_t *d, uint32_t n, ratio_t ref)
{
    for (uint32_t i = 0; i < n; ++i) {
        const tone_pend_t *e = &d->pend[i];
        d->b_bursts++;
        const ratio_t m = d->b_n != 0u ? beep_mean(d) : ref;
        if (e->good && m.n != 0u
            && !off_by_more(ratio(e->p, 1u), m, TONE_OUTLIER_PCT, 1u)) {
            d->b_sum += e->p;
            d->b_n++;
        }
    }
    d->n_pend -= n;
    memmove(&d->pend[0], &d->pend[n], d->n_pend * sizeof d->pend[0]);
}

/* The block's pitch: the mean of its in-range periods within
 * TONE_OUTLIER_PCT of their median, as a sum over a count.  The median
 * keeps one stray period -- a burst missed, a stuck line's rise -- out of
 * the mean; the mean of neighbours keeps a burst start that moves, as one
 * gated from a carrier running free does, from moving the pitch. */
static ratio_t block_pitch(const tone_t *d)
{
    uint64_t v[TONE_BLOCK];
    uint32_t k = 0;
    for (uint32_t i = 0; i < d->n_pend; ++i) {
        if (d->pend[i].good) {
            uint32_t j = k++;
            for (; j > 0u && v[j - 1u] > d->pend[i].p; --j) {
                v[j] = v[j - 1u];
            }
            v[j] = d->pend[i].p;
        }
    }
    if (k == 0u) {
        return ratio(0u, 0u);
    }
    /* The median as twice itself over 2, kept exact. */
    const ratio_t med = ratio(v[(k - 1u) / 2u] + v[k / 2u], 2u);
    ratio_t r = ratio(0u, 0u);
    for (uint32_t i = 0; i < k; ++i) {
        if (!off_by_more(ratio(v[i], 1u), med, TONE_OUTLIER_PCT, 1u)) {
            r.s += v[i];
            r.n++;
        }
    }
    /* Two periods far apart have no majority: their median stands. */
    return r.n != 0u ? r : med;
}

/* The block's periods that lie more than half of @p pct from @p m on the
 * side of @p bm. */
static uint32_t on_side(const tone_t *d, ratio_t bm, ratio_t m, uint32_t pct)
{
    bool block_more = false;
    (void)ratio_diff(bm, m, &block_more);
    uint32_t n = 0;
    for (uint32_t i = 0; i < d->n_pend; ++i) {
        const tone_pend_t *e = &d->pend[i];
        bool more = false;
        (void)ratio_diff(ratio(e->p, 1u), m, &more);
        if (e->good && more == block_more
            && off_by_more(ratio(e->p, 1u), m, pct, 2u)) {
            n++;
        }
    }
    return n;
}

/* The block's pitch @p bm lies more than @p pct from the beep's mean @p m,
 * and all but one of the block's periods lie more than half that on the
 * same side.  One stray period moves the block's pitch and is alone on
 * its side.  A carrier running free of the tone moves burst starts, and
 * with them single periods, by up to one carrier period; the block's
 * pitch, a mean of neighbours, by up to a quarter of that.  A block whose
 * pitch rests on two periods far apart, their median, has not moved. */
static bool moved(const tone_t *d, ratio_t bm, ratio_t m, uint32_t pct)
{
    return bm.n + 1u >= TONE_BLOCK && off_by_more(bm, m, pct, 1u)
           && on_side(d, bm, m, pct) + 1u >= TONE_BLOCK;
}

/* |p - x|, scaled by x.n. */
static uint64_t dist(uint64_t p, ratio_t x)
{
    bool more = false;
    return ratio_diff(ratio(p, 1u), x, &more);
}

/* A full block: the oldest period joins the beep, or the pitch has moved.
 *
 * A beep with a mean of TONE_BLOCK periods or more splits where the new
 * pitch begins, when the block moved by split_pct.  One with fewer keeps
 * its start and starts its mean again, when the block moved by
 * TONE_OUTLIER_PCT: its first periods came before the carrier was known,
 * and every period after them would be an outlier to them. */
static void block_check(tone_t *d)
{
    const ratio_t bm = block_pitch(d);
    const ratio_t m = beep_mean(d);
    const bool young = d->b_n < TONE_BLOCK;
    if (d->b_n == 0u
        || (young && !moved(d, bm, m, TONE_OUTLIER_PCT))
        || (!young && (d->c.split_pct == 0u
                       || !moved(d, bm, m, d->c.split_pct)))) {
        commit(d, 1u, bm);
        return;
    }
    /* The first period nearer the new pitch than the old:
     * |p - bm| < |p - m|, both sides times bm.n * m.n.  One at or past the
     * block's pitch is, so the search ends inside the block. */
    uint32_t at = 0;
    for (;; ++at) {
        const tone_pend_t *e = &d->pend[at];
        const uint64_t to_new = dist(e->p, bm) * m.n;
        const uint64_t to_old = dist(e->p, m) * bm.n;
        if ((e->good && to_new < to_old) || at + 1u == d->n_pend) {
            break;
        }
    }
    if (young) {
        d->b_sum = 0;
        d->b_n = 0;
        commit(d, at, bm);
        return;
    }
    const uint64_t start = d->pend[at].s_open;
    const uint64_t end = d->pend[at].e_before;
    commit(d, at, m);
    /* The burst at the split was counted as the old beep's last period's
     * end; it opens the new beep. */
    d->b_bursts--;
    d->b_flags |= (uint8_t)TONE_BEEP_BEFORE_CHANGE;
    emit(d, end);
    beep_open(d, start, (uint8_t)TONE_BEEP_AFTER_CHANGE);
}

static void period(tone_t *d, uint64_t p, uint64_t s_open, uint64_t e_before)
{
    const bool good = p >= d->per_min / 2u && p <= d->per_max;
    if (good) {
        d->w_n++;
        d->w_sum += p;
    }
    tone_pend_t *e = &d->pend[d->n_pend++];
    e->p = p;
    e->s_open = s_open;
    e->e_before = e_before;
    e->good = good;
    /* A split, or a mean started again, at the block's first period leaves
     * a full block and no mean: the next pass commits its oldest. */
    while (d->n_pend == TONE_BLOCK) {
        block_check(d);
    }
}

/* The run ends at its last edge. */
static void run_end(tone_t *d)
{
    if (!d->in_run) {
        return;
    }
    commit(d, d->n_pend, block_pitch(d));
    emit(d, d->last_edge);
    d->in_run = false;
    d->car_low = 0;
    d->car_high = 0;
    d->car_rise = 0;
}

static void burst(tone_t *d, uint64_t t, uint64_t e_before)
{
    d->w_bursts++;
    if (!d->in_run) {
        d->in_run = true;
        d->n_pend = 0;
        beep_open(d, t, 0u);
    } else {
        period(d, t - d->burst_start, d->burst_start, d->burst_e_before);
    }
    d->burst_start = t;
    d->burst_e_before = e_before;
}

/* ---------------------------------------------------------------- edges */

/* Time reaches @p t: windows close, and a silence ends the run. */
static bool time_to(tone_t *d, uint64_t t)
{
    if (d->seen && t < d->now) {
        d->st.out_of_order++;
        return false;
    }
    if (!d->seen) {
        d->seen = true;
        window_open(d, t / d->win_ticks);
    }
    d->now = t;
    windows_to(d, t);
    if (d->in_run && t - d->last_edge >= d->gap) {
        run_end(d);
    }
    return true;
}

static void rise(tone_t *d, uint64_t t, uint64_t e_before)
{
    bool starts = true;
    if (d->in_run) {
        const uint64_t low = t - d->last_fall;
        if (low < d->glitch) {
            d->st.glitches++;
            return;
        }
        const uint64_t r = t - d->last_rise;
        if (r <= d->car_max) {
            const uint64_t high = d->last_fall - d->last_rise;
            if (d->car_low == 0u || low < d->car_low) {
                d->car_low = low;
            }
            if (high > d->car_high) {
                d->car_high = high;
            }
        }
        const uint64_t cp = carrier(d);
        if (cp != 0u) {
            starts = low * 4u >= cp * 5u;
        }
        if (low < d->start_low) {
            starts = false;
        }
        /* A rise interval inside a burst, not its first: the first may
         * start at a pulse the burst's own start cut short. */
        if (!starts && !d->rise_started && r <= d->car_max
            && (d->car_rise == 0u || r < d->car_rise)) {
            d->car_rise = r;
        }
    }
    d->last_rise = t;
    d->rise_started = starts;
    if (starts) {
        burst(d, t, e_before);
    }
}

void tone_edge(tone_t *d, uint64_t t, bool level)
{
    if (d == NULL || d->win_ticks == 0u || !time_to(d, t)) {
        return;
    }
    d->st.edges++;
    /* An edge to the level the line is already at changes nothing but the
     * time of the last edge: one was lost between. */
    if (level && !d->high) {
        d->high = true;
        rise(d, t, d->have_edge ? d->last_edge : t);
    } else if (!level && d->high) {
        d->high = false;
        d->last_fall = t;
    }
    d->have_edge = true;
    d->last_edge = t;
}

void tone_feed(tone_t *d, const tone_edge_t *e, size_t n)
{
    if (e == NULL) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        tone_edge(d, e[i].t, e[i].level);
    }
}

void tone_advance(tone_t *d, uint64_t now)
{
    if (d == NULL || d->win_ticks == 0u) {
        return;
    }
    (void)time_to(d, now);
}

void tone_flush(tone_t *d)
{
    if (d == NULL) {
        return;
    }
    run_end(d);
    /* The edges in the hole are gone, the line's level with them: the
     * first rise after it starts a run, whatever the last edge before it
     * was. */
    d->high = false;
    d->have_edge = false;
}

/* ---------------------------------------------------------------- results */

bool tone_window(const tone_t *d, tone_window_t *w)
{
    if (d == NULL || w == NULL || !d->have_window) {
        return false;
    }
    *w = d->window;
    return true;
}

bool tone_next_beep(tone_t *d, tone_beep_t *b)
{
    if (d == NULL || b == NULL || d->q_len == 0u) {
        return false;
    }
    *b = d->q[d->q_head];
    d->q_head = (d->q_head + 1u) % TONE_BEEP_QUEUE;
    d->q_len--;
    return true;
}

bool tone_busy(const tone_t *d)
{
    return d != NULL && d->in_run;
}

const tone_stats_t *tone_stats(const tone_t *d)
{
    return d != NULL ? &d->st : NULL;
}

uint64_t tone_ticks_us(const tone_t *d, uint64_t ticks)
{
    if (d == NULL || d->c.tick_hz == 0u) {
        return 0u;
    }
    const uint64_t hz = d->c.tick_hz;
    return ticks / hz * 1000000u + ticks % hz * 1000000u / hz;
}

/* ---------------------------------------------------------------- hold-off */

void tone_holdoff_init(tone_holdoff_t *h, uint32_t tick_hz, uint32_t hold_ns)
{
    if (h == NULL) {
        return;
    }
    memset(h, 0, sizeof *h);
    h->hold = ns_ticks(tick_hz, hold_ns);
}

size_t tone_holdoff_edge(tone_holdoff_t *h, uint64_t t, bool level,
                         tone_edge_t out[2])
{
    if (h == NULL || out == NULL || level == h->line) {
        return 0u;
    }
    h->line = level;
    if (!level) {
        if (h->hold == 0u) {
            out[0].t = t;
            out[0].level = false;
            h->out_t = t;
            return 1u;
        }
        h->pending = true;
        h->fall = t;
        return 0u;
    }
    size_t n = 0;
    if (h->pending) {
        h->pending = false;
        /* A low shorter than the hold-off was never reported: the pulse
         * goes on, and so does the line, as far as anyone was told. */
        if (t - h->fall < h->hold) {
            return 0u;
        }
        out[n].t = h->fall;
        out[n].level = false;
        n++;
    }
    out[n].t = t;
    out[n].level = true;
    h->out_t = t;
    return n + 1u;
}

size_t tone_holdoff_advance(tone_holdoff_t *h, uint64_t now,
                            tone_edge_t out[1])
{
    if (h == NULL || out == NULL || !h->pending || now < h->fall
        || now - h->fall < h->hold) {
        return 0u;
    }
    h->pending = false;
    out[0].t = h->fall;
    out[0].level = false;
    h->out_t = h->fall;
    return 1u;
}

uint64_t tone_holdoff_horizon(const tone_holdoff_t *h, uint64_t now)
{
    if (h == NULL) {
        return now;
    }
    const uint64_t t = now > h->hold ? now - h->hold : 0u;
    /* Never before an edge already handed out: the detector's time only
     * moves forward.  A fall still held is later than every edge handed
     * out, so this holds back nothing it is owed. */
    return t > h->out_t ? t : h->out_t;
}
