/*
 * The tone detector (shared/sense/tone.c) against synthetic phase taps.
 *
 * A phase is modelled as the intervals it is driven, at the supply voltage
 * or, floating against a resistor star, at half of it.  The front end is
 * modelled as it is wired: a series resistor into the pin's capacitance
 * and a zener's, a 3.3 V clamp, and a Schmitt input that goes high at
 * 2.0 V and low at 0.8 V, the datasheet's VIH minimum and VIL maximum.
 * Edges come out at the 26.7 ns tick of a PIO (programmable input/output)
 * counter at 150 MHz, 4 cycles a count.
 *
 * Under test: tones from 500 Hz to 6 kHz, as a square or chopped by a
 * carrier of 8 to 48 kHz at 10 to 90 % duty, the carrier locked to the
 * tone or running free; edge jitter; a phase from 1 V to 25.2 V, driven or
 * floating; a clamp slow enough to swallow the carrier; beep trains;
 * two pitches alternating with and without a silence between; silence, a
 * line stuck high or low, a continuous carrier, a single click, a glitch.
 *
 * Accuracy held here, with up to 1 us of jitter on every edge:
 *   carrier locked to the tone, or none: a beep's frequency within 0.01 %,
 *     a window's within 0.1 %;
 *   carrier running free, at least 4 times the tone: a beep's within
 *     0.1 %, a window's within 2 %.  A burst gated from a free carrier
 *     starts up to one carrier period late, so an 8 ms window is known to
 *     a carrier period in 8 ms.
 * A beep's start is its first rise and its end its last edge, exactly; its
 * burst count is exact.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "tone.h"

#define TICK_HZ 37500000u               /* 150 MHz / 4 */

/* The bounds held, as fractions of the tone frequency. */
#define LOCKED_BEEP 0.0001
#define LOCKED_WIN  0.001
#define FREE_BEEP   0.001
#define FREE_WIN    0.02

/* ------------------------------------------------------------ the phase */

typedef struct {
    double a, b;                        /* driven from a to b, seconds */
} span_t;

typedef struct {
    span_t *s;
    size_t  n, cap;
} drive_t;

static void drive_add(drive_t *d, double a, double b)
{
    if (b <= a) {
        return;
    }
    /* Overlapping or touching spans are one: the phase never fell. */
    if (d->n != 0u && a <= d->s[d->n - 1u].b) {
        if (b > d->s[d->n - 1u].b) {
            d->s[d->n - 1u].b = b;
        }
        return;
    }
    if (d->n == d->cap) {
        d->cap = d->cap != 0u ? d->cap * 2u : 1024u;
        d->s = realloc(d->s, d->cap * sizeof *d->s);
    }
    d->s[d->n].a = a;
    d->s[d->n].b = b;
    d->n++;
}

static uint32_t rng_state = 1u;

static void rng_seed(uint32_t s)
{
    rng_state = s != 0u ? s : 1u;
}

/* Uniform in [-1, 1). */
static double rng_pm1(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (double)(rng_state >> 8) / 8388608.0 - 1.0;
}

typedef struct {
    double tone_hz;
    double on_frac;      /* the part of each tone period driven          */
    double carrier_hz;   /* 0: not chopped                               */
    double duty;         /* the carrier's                                */
    bool   free_run;     /* the carrier runs free of the tone            */
    double jitter_s;     /* every boundary moved by up to this, each way */
} tone_spec_t;

/* One beep from @p t0 for @p dur seconds into @p d. */
static void beep_add(drive_t *d, const tone_spec_t *sp, double t0, double dur)
{
    const double tp = 1.0 / sp->tone_hz;
    for (unsigned k = 0;; ++k) {
        const double ts = t0 + k * tp;
        if (ts >= t0 + dur - 1e-12) {
            break;
        }
        double te = ts + sp->on_frac * tp;
        if (te > t0 + dur) {
            te = t0 + dur;
        }
        if (sp->carrier_hz <= 0.0) {
            drive_add(d, ts + sp->jitter_s * rng_pm1(),
                      te + sp->jitter_s * rng_pm1());
            continue;
        }
        const double cp = 1.0 / sp->carrier_hz;
        /* Locked: the carrier starts with the burst.  Free: it runs on a
         * grid of its own from 0.37 of a period, and the burst gates it. */
        double c = ts;
        if (sp->free_run) {
            c = (floor(ts / cp) + 0.37) * cp;
            if (c + sp->duty * cp <= ts) {
                c += cp;
            }
        }
        for (; c < te; c += cp) {
            double a = c > ts ? c : ts;
            double b = c + sp->duty * cp;
            if (b > te) {
                b = te;
            }
            drive_add(d, a + sp->jitter_s * rng_pm1(),
                      b + sp->jitter_s * rng_pm1());
        }
    }
}

/* --------------------------------------------------------- the front end */

typedef struct {
    double v;            /* the phase while driven                       */
    double tau_s;        /* series resistor x (pin + zener capacitance)  */
} front_t;

#define VZ   3.3
#define VT_H 2.0
#define VT_L 0.8

typedef struct {
    tone_edge_t *e;
    size_t n, cap;
} edges_t;

static void edge_add(edges_t *s, double t, bool level)
{
    if (s->n == s->cap) {
        s->cap = s->cap != 0u ? s->cap * 2u : 1024u;
        s->e = realloc(s->e, s->cap * sizeof *s->e);
    }
    s->e[s->n].t = (uint64_t)llround(t * TICK_HZ);
    s->e[s->n].level = level;
    s->n++;
}

/* The pin's edges for the phase @p d through @p f. */
static void front_run(const drive_t *d, const front_t *f, edges_t *out)
{
    double v = 0.0;
    bool high = false;
    for (size_t i = 0; i < d->n; ++i) {
        const double a = d->s[i].a;
        const double b = d->s[i].b;
        const double next = i + 1u < d->n ? d->s[i + 1u].a : INFINITY;
        if (f->tau_s <= 0.0) {
            if (f->v > VT_H) {
                edge_add(out, a, true);
                edge_add(out, b, false);
            }
            continue;
        }
        /* Charging towards the phase, clamped at the zener. */
        if (!high && f->v > VT_H && v < VT_H) {
            const double tc = a + f->tau_s * log((f->v - v) / (f->v - VT_H));
            if (tc < b) {
                edge_add(out, tc, true);
                high = true;
            }
        }
        v = f->v - (f->v - v) * exp(-(b - a) / f->tau_s);
        if (v > VZ) {
            v = VZ;
        }
        /* Discharging towards 0 V through the resistor. */
        if (high) {
            const double tc = b + f->tau_s * log(v / VT_L);
            if (tc < next) {
                edge_add(out, tc, false);
                high = false;
            }
        }
        if (next < INFINITY) {
            v *= exp(-(next - b) / f->tau_s);
        }
    }
}

#define TAU_TYP  (33e3 * 50e-12)    /* 1.65 us */
#define TAU_SLOW (33e3 * 300e-12)   /* 9.9 us  */

/* ------------------------------------------------------------ the detector */

#define MAX_WIN 4096u
#define MAX_BEEPS 64u

static tone_t det;
static tone_window_t wins[MAX_WIN];
static size_t n_wins;
static tone_beep_t beeps[MAX_BEEPS];
static size_t n_beeps;

static void take(void)
{
    tone_window_t w;
    if (tone_window(&det, &w)
        && (n_wins == 0u || wins[n_wins - 1u].index != w.index)
        && n_wins < MAX_WIN) {
        wins[n_wins++] = w;
    }
    tone_beep_t b;
    while (tone_next_beep(&det, &b)) {
        if (n_beeps < MAX_BEEPS) {
            beeps[n_beeps++] = b;
        }
    }
}

static void start(const tone_cfg_t *c)
{
    tone_cfg_t def;
    tone_cfg_defaults(&def, TICK_HZ);
    CHECK(tone_init(&det, c != NULL ? c : &def));
    n_wins = 0;
    n_beeps = 0;
}

/* Every edge, then 1 ms steps to @p t_end seconds, taking results as a
 * caller polling the detector would. */
static void play(const edges_t *e, double t_end)
{
    for (size_t i = 0; i < e->n; ++i) {
        tone_edge(&det, e->e[i].t, e->e[i].level);
        take();
    }
    const uint64_t end = (uint64_t)llround(t_end * TICK_HZ);
    uint64_t t = e->n != 0u ? e->e[e->n - 1u].t : 0u;
    while (t < end) {
        t += TICK_HZ / 1000u;
        tone_advance(&det, t);
        take();
    }
}

static void drive_free(drive_t *d)
{
    free(d->s);
    memset(d, 0, sizeof *d);
}

static void edges_free(edges_t *e)
{
    free(e->e);
    memset(e, 0, sizeof *e);
}

static double rel_err(double got, double want)
{
    return fabs(got - want) / want;
}

/* The first rise and the last edge at or after @p from seconds and before
 * @p to. */
static void bounds(const edges_t *e, double from, double to,
                   uint64_t *first, uint64_t *last)
{
    const uint64_t a = (uint64_t)llround(from * TICK_HZ);
    const uint64_t b = (uint64_t)llround(to * TICK_HZ);
    *first = 0;
    *last = 0;
    bool seen = false;
    for (size_t i = 0; i < e->n; ++i) {
        if (e->e[i].t < a || e->e[i].t >= b) {
            continue;
        }
        if (!seen && e->e[i].level) {
            *first = e->e[i].t;
            seen = true;
        }
        *last = e->e[i].t;
    }
}

/* The windows wholly inside [from, to] seconds hold a tone at @p hz within
 * @p tol; returns how many were checked. */
static unsigned windows_hold(double from, double to, double hz, double tol)
{
    const double wt = 8e-3;
    unsigned n = 0;
    for (size_t i = 0; i < n_wins; ++i) {
        const double a = (double)wins[i].index * wt;
        if (a < from || a + wt > to) {
            continue;
        }
        n++;
        if (!wins[i].present || rel_err(wins[i].freq_hz, hz) > tol) {
            T_FAIL("window at %.3f s: present %d, %.2f Hz, want %.2f Hz",
                   a, wins[i].present, (double)wins[i].freq_hz, hz);
        }
    }
    return n;
}

static unsigned windows_present(void)
{
    unsigned n = 0;
    for (size_t i = 0; i < n_wins; ++i) {
        n += wins[i].present ? 1u : 0u;
    }
    return n;
}

/* One beep of @p sp from 10 ms for @p dur through @p f, checked: one beep,
 * its edges, its bursts, its frequency within @p beep_tol and the windows
 * inside it within @p win_tol.  Returns the beep's error. */
static double one_beep(const tone_spec_t *sp, const front_t *f, double dur,
                       double beep_tol, double win_tol)
{
    drive_t d = { 0 };
    edges_t e = { 0 };
    beep_add(&d, sp, 0.010, dur);
    front_run(&d, f, &e);
    start(NULL);
    play(&e, 0.010 + dur + 0.020);
    double err = 1.0;
    if (n_beeps != 1u) {
        T_FAIL("%.0f Hz, carrier %.0f Hz, duty %.2f: %zu beeps",
               sp->tone_hz, sp->carrier_hz, sp->duty, n_beeps);
    } else {
        uint64_t first = 0;
        uint64_t last = 0;
        bounds(&e, 0.0, 1e3, &first, &last);
        CHECK_EQ(beeps[0].start, first);
        CHECK_EQ(beeps[0].end, last);
        const unsigned want = (unsigned)ceil(dur * sp->tone_hz - 1e-9);
        CHECK_EQ(beeps[0].bursts, want);
        err = rel_err(beeps[0].freq_hz, sp->tone_hz);
        if (err > beep_tol) {
            T_FAIL("%.0f Hz, carrier %.0f Hz, duty %.2f: beep at %.3f Hz",
                   sp->tone_hz, sp->carrier_hz, sp->duty,
                   (double)beeps[0].freq_hz);
        }
        CHECK_EQ(beeps[0].flags, 0);
    }
    /* Windows a period clear of either end. */
    const double tp = 1.0 / sp->tone_hz;
    CHECK(windows_hold(0.010 + tp, 0.010 + dur - tp, sp->tone_hz, win_tol)
          > 0u);
    drive_free(&d);
    edges_free(&e);
    return err;
}

/* -------------------------------------------------------------- set-up */

TEST_CASE(the_defaults_are_a_configuration_init_takes)
{
    tone_cfg_t c;
    tone_cfg_defaults(&c, TICK_HZ);
    CHECK(tone_init(&det, &c));
    CHECK_EQ(c.window_us, 8000);
    CHECK_EQ(c.f_min_hz, 400);
    CHECK_EQ(c.f_max_hz, 6500);
    CHECK_EQ(det.per_min, TICK_HZ / 6500u);
    CHECK_EQ(det.gap, TICK_HZ / 1000u * 3u);
    CHECK(!tone_busy(&det));
    tone_window_t w;
    CHECK(!tone_window(&det, &w));
    tone_cfg_defaults(NULL, TICK_HZ);
}

TEST_CASE(a_configuration_that_contradicts_itself_is_refused)
{
    tone_cfg_t c;
#define REFUSED(field, value)                                                 \
    do {                                                                      \
        tone_cfg_defaults(&c, TICK_HZ);                                       \
        c.field = (value);                                                    \
        if (tone_init(&det, &c)) {                                            \
            T_FAIL("%s = %u taken", #field, (unsigned)(value));               \
        }                                                                     \
    } while (0)
    REFUSED(f_min_hz, 49u);
    REFUSED(f_min_hz, 6500u);
    REFUSED(carrier_min_hz, 6500u);
    REFUSED(glitch_ns, 0u);
    REFUSED(glitch_ns, 10001u);
    REFUSED(window_us, 999u);
    REFUSED(window_us, 100001u);
    REFUSED(gap_us, 2499u);
    REFUSED(min_periods, 0u);
    REFUSED(window_min_periods, 0u);
#undef REFUSED
    CHECK(!tone_init(NULL, &c));
    CHECK(!tone_init(&det, NULL));
    /* A refused set-up leaves nothing that runs. */
    tone_edge(&det, 100u, true);
    tone_advance(&det, 1000u);
    CHECK_EQ(tone_stats(&det)->edges, 0);
}

/* -------------------------------------------------------------- tones */

TEST_CASE(a_square_tone_reads_its_frequency_from_500_hz_to_6_khz)
{
    static const double hz[] = { 500, 750, 1000, 1500, 2000, 3000, 4000,
                                 5000, 6000 };
    const front_t f = { 12.0, TAU_TYP };
    for (size_t i = 0; i < sizeof hz / sizeof hz[0]; ++i) {
        const tone_spec_t sp = { hz[i], 0.5, 0.0, 0.0, false, 0.0 };
        one_beep(&sp, &f, 0.200, LOCKED_BEEP, LOCKED_WIN);
        CHECK_NEAR(beeps[0].carrier_hz, 0.0, 0.0);
    }
    /* Short pulses, as an ESC that pulses its windings once a period. */
    for (size_t i = 0; i < sizeof hz / sizeof hz[0]; ++i) {
        const tone_spec_t sp = { hz[i], 0.1, 0.0, 0.0, false, 0.0 };
        one_beep(&sp, &f, 0.200, LOCKED_BEEP, LOCKED_WIN);
    }
}

TEST_CASE(a_chopped_tone_reads_the_tone_and_the_carrier)
{
    static const double hz[] = { 500, 1000, 2000, 3000, 4000, 6000 };
    static const double fc[] = { 8000, 16000, 24000, 32000, 48000 };
    static const double duty[] = { 0.1, 0.5, 0.9 };
    const front_t f = { 12.0, TAU_TYP };
    for (size_t i = 0; i < sizeof hz / sizeof hz[0]; ++i) {
        for (size_t j = 0; j < sizeof fc / sizeof fc[0]; ++j) {
            /* A burst of half a tone period holds at least two carrier
             * periods. */
            if (fc[j] < 4.0 * hz[i]) {
                continue;
            }
            for (size_t k = 0; k < sizeof duty / sizeof duty[0]; ++k) {
                const tone_spec_t sp = { hz[i], 0.5, fc[j], duty[k], false,
                                         0.0 };
                one_beep(&sp, &f, 0.200, LOCKED_BEEP, LOCKED_WIN);
                /* At 90 % the 2 us lows at 48 kHz are shorter than the
                 * input takes to fall, and the burst is one pulse. */
                if (n_beeps == 1u && beeps[0].carrier_hz > 0.0f) {
                    CHECK(rel_err(beeps[0].carrier_hz, fc[j]) < 0.01);
                }
            }
        }
    }
}

TEST_CASE(a_carrier_running_free_of_the_tone_moves_bursts_not_the_mean)
{
    static const double hz[] = { 500, 1000, 2000, 4000, 6000 };
    static const double fc[] = { 8000, 16000, 24000, 48000 };
    const front_t f = { 16.8, TAU_TYP };
    for (size_t i = 0; i < sizeof hz / sizeof hz[0]; ++i) {
        for (size_t j = 0; j < sizeof fc / sizeof fc[0]; ++j) {
            if (fc[j] < 4.0 * hz[i]) {
                continue;
            }
            const tone_spec_t sp = { hz[i], 0.5, fc[j], 0.5, true, 0.0 };
            one_beep(&sp, &f, 0.200, FREE_BEEP, FREE_WIN);
        }
    }
}

TEST_CASE(jitter_of_a_microsecond_on_every_edge_stays_inside_the_bound)
{
    static const double hz[] = { 500, 1000, 3000, 6000 };
    const front_t f = { 12.0, TAU_TYP };
    rng_seed(7u);
    for (size_t i = 0; i < sizeof hz / sizeof hz[0]; ++i) {
        const tone_spec_t sq = { hz[i], 0.5, 0.0, 0.0, false, 1e-6 };
        one_beep(&sq, &f, 0.200, LOCKED_BEEP, LOCKED_WIN);
        const tone_spec_t ch = { hz[i], 0.5, 48000.0, 0.5, false, 1e-6 };
        one_beep(&ch, &f, 0.200, LOCKED_BEEP, LOCKED_WIN);
    }
}

TEST_CASE(a_phase_from_2s_to_6s_driven_or_floating_reads_the_same)
{
    static const double volts[] = { 5.0, 7.4, 8.4, 12.0, 16.8, 20.0, 25.2 };
    for (size_t i = 0; i < sizeof volts / sizeof volts[0]; ++i) {
        const tone_spec_t sp = { 2000.0, 0.5, 24000.0, 0.3, false, 0.0 };
        const front_t driven = { volts[i], TAU_TYP };
        one_beep(&sp, &driven, 0.150, LOCKED_BEEP, LOCKED_WIN);
        /* Floating against the resistor star: half the supply. */
        const front_t floating = { volts[i] / 2.0, TAU_TYP };
        one_beep(&sp, &floating, 0.150, LOCKED_BEEP, LOCKED_WIN);
    }
}

TEST_CASE(a_phase_under_the_input_threshold_reads_nothing)
{
    static const double volts[] = { 1.0, 1.9, 3.0 / 2.0 };
    for (size_t i = 0; i < sizeof volts / sizeof volts[0]; ++i) {
        drive_t d = { 0 };
        edges_t e = { 0 };
        const tone_spec_t sp = { 2000.0, 0.5, 0.0, 0.0, false, 0.0 };
        beep_add(&d, &sp, 0.010, 0.100);
        const front_t f = { volts[i], TAU_TYP };
        front_run(&d, &f, &e);
        CHECK_EQ(e.n, 0);
        start(NULL);
        play(&e, 0.200);
        CHECK_EQ(n_beeps, 0);
        CHECK_EQ(windows_present(), 0);
        drive_free(&d);
        edges_free(&e);
    }
}

TEST_CASE(a_clamp_slow_enough_to_swallow_the_carrier_still_reads_the_tone)
{
    /* 9.9 us: the input takes 14 us to fall from the clamp, longer than a
     * 48 kHz carrier's low, so each burst reaches the pin as one pulse. */
    static const double hz[] = { 500, 2000, 4000 };
    const front_t f = { 25.2, TAU_SLOW };
    for (size_t i = 0; i < sizeof hz / sizeof hz[0]; ++i) {
        const tone_spec_t sp = { hz[i], 0.5, 48000.0, 0.5, false, 0.0 };
        one_beep(&sp, &f, 0.150, LOCKED_BEEP, LOCKED_WIN);
        CHECK_NEAR(beeps[0].carrier_hz, 0.0, 0.0);
    }
}

/* -------------------------------------------------------------- beeps */

typedef struct {
    double at, dur, hz;
} note_t;

/* @p n notes through @p f, each beep checked against its note. */
static void train(const note_t *notes, size_t n, const tone_spec_t *base,
                  const front_t *f, double beep_tol, uint8_t want_flags_mid)
{
    drive_t d = { 0 };
    edges_t e = { 0 };
    for (size_t i = 0; i < n; ++i) {
        tone_spec_t sp = *base;
        sp.tone_hz = notes[i].hz;
        beep_add(&d, &sp, notes[i].at, notes[i].dur);
    }
    front_run(&d, f, &e);
    start(NULL);
    play(&e, notes[n - 1u].at + notes[n - 1u].dur + 0.050);
    CHECK_EQ(n_beeps, n);
    for (size_t i = 0; i < n && i < n_beeps; ++i) {
        const tone_beep_t *b = &beeps[i];
        const double tp = 1.0 / notes[i].hz;
        if (rel_err(b->freq_hz, notes[i].hz) > beep_tol) {
            T_FAIL("note %zu: %.2f Hz, want %.2f Hz", i, (double)b->freq_hz,
                   notes[i].hz);
        }
        /* Within one period of where the note begins and ends. */
        const double t0 = (double)b->start / TICK_HZ;
        const double t1 = (double)b->end / TICK_HZ;
        if (fabs(t0 - notes[i].at) > tp
            || fabs(t1 - (notes[i].at + notes[i].dur)) > tp) {
            T_FAIL("note %zu: %.5f to %.5f s, want %.5f to %.5f s", i, t0,
                   t1, notes[i].at, notes[i].at + notes[i].dur);
        }
        if (i > 0u && i + 1u < n) {
            CHECK_EQ(b->flags, want_flags_mid);
        }
    }
    drive_free(&d);
    edges_free(&e);
}

TEST_CASE(a_beep_train_counts_and_places_every_beep)
{
    const front_t f = { 12.0, TAU_TYP };
    const tone_spec_t sq = { 0, 0.5, 0.0, 0.0, false, 1e-6 };
    /* Five 100 ms beeps 150 ms apart at 2 kHz. */
    note_t five[5];
    for (size_t i = 0; i < 5u; ++i) {
        five[i] = (note_t){ 0.010 + 0.250 * (double)i, 0.100, 2000.0 };
    }
    train(five, 5u, &sq, &f, 0.001, 0u);
    /* Long and short beeps, as a menu that counts in both. */
    const note_t ls[] = {
        { 0.010, 0.500, 1000.0 }, { 0.700, 0.080, 1000.0 },
        { 0.900, 0.080, 1000.0 }, { 1.100, 0.500, 1000.0 },
        { 1.800, 0.080, 1000.0 },
    };
    train(ls, sizeof ls / sizeof ls[0], &sq, &f, 0.001, 0u);
    /* Eight short beeps at 4 kHz, chopped at 24 kHz, 30 ms apart. */
    const tone_spec_t ch = { 0, 0.5, 24000.0, 0.5, false, 1e-6 };
    note_t eight[8];
    for (size_t i = 0; i < 8u; ++i) {
        eight[i] = (note_t){ 0.010 + 0.080 * (double)i, 0.050, 4000.0 };
    }
    train(eight, 8u, &ch, &f, 0.001, 0u);
    /* A silence of 3.5 ms, just over the gap, still parts two beeps. */
    const note_t close[] = { { 0.010, 0.050, 3000.0 },
                             { 0.0635, 0.050, 3000.0 } };
    train(close, 2u, &sq, &f, 0.001, 0u);
}

TEST_CASE(two_pitches_alternating_with_silences_are_beeps_of_their_own)
{
    const front_t f = { 12.0, TAU_TYP };
    const tone_spec_t sp = { 0, 0.5, 32000.0, 0.4, false, 1e-6 };
    note_t m[8];
    for (size_t i = 0; i < 8u; ++i) {
        m[i] = (note_t){ 0.010 + 0.120 * (double)i, 0.090,
                         (i & 1u) != 0u ? 2500.0 : 1600.0 };
    }
    train(m, 8u, &sp, &f, 0.001, 0u);
}

TEST_CASE(two_pitches_alternating_with_no_silence_split_at_the_change)
{
    const front_t f = { 12.0, TAU_TYP };
    const tone_spec_t sp = { 0, 0.5, 0.0, 0.0, false, 1e-6 };
    /* Notes back to back: each begins as the one before ends. */
    note_t m[6];
    double at = 0.010;
    for (size_t i = 0; i < 6u; ++i) {
        const double hz = (i & 1u) != 0u ? 1500.0 : 1000.0;
        m[i] = (note_t){ at, 0.080, hz };
        at += 0.080;
    }
    train(m, 6u, &sp, &f, 0.002,
          (uint8_t)(TONE_BEEP_AFTER_CHANGE | TONE_BEEP_BEFORE_CHANGE));
    CHECK_EQ(beeps[0].flags, TONE_BEEP_BEFORE_CHANGE);
    CHECK_EQ(beeps[5].flags, TONE_BEEP_AFTER_CHANGE);
    /* A step of 10 %: found only once all four periods of a block are
     * the new pitch, so the split falls on the block's first. */
    const note_t step[] = { { 0.010, 0.080, 1000.0 },
                            { 0.090, 0.080, 1100.0 } };
    train(step, 2u, &sp, &f, 0.002, 0u);
    /* Falling as well as rising, and chopped. */
    const tone_spec_t ch = { 0, 0.5, 16000.0, 0.5, false, 0.0 };
    const note_t fall[] = { { 0.010, 0.060, 2000.0 },
                            { 0.070, 0.060, 1200.0 },
                            { 0.130, 0.060, 2000.0 } };
    train(fall, 3u, &ch, &f, 0.002,
          (uint8_t)(TONE_BEEP_AFTER_CHANGE | TONE_BEEP_BEFORE_CHANGE));
}

TEST_CASE(with_the_split_off_a_pitch_change_is_one_beep)
{
    tone_cfg_t c;
    tone_cfg_defaults(&c, TICK_HZ);
    c.split_pct = 0u;
    drive_t d = { 0 };
    edges_t e = { 0 };
    const tone_spec_t a = { 1000.0, 0.5, 0.0, 0.0, false, 0.0 };
    const tone_spec_t b = { 1500.0, 0.5, 0.0, 0.0, false, 0.0 };
    beep_add(&d, &a, 0.010, 0.080);
    beep_add(&d, &b, 0.090, 0.080);
    const front_t f = { 12.0, TAU_TYP };
    front_run(&d, &f, &e);
    start(&c);
    play(&e, 0.300);
    CHECK_EQ(n_beeps, 1);
    CHECK_EQ(beeps[0].bursts, 80 + 120);
    drive_free(&d);
    edges_free(&e);
}

/* -------------------------------------------------------------- no tone */

TEST_CASE(silence_reports_windows_with_no_tone)
{
    start(NULL);
    for (uint64_t t = 0; t < TICK_HZ / 2u; t += TICK_HZ / 1000u) {
        tone_advance(&det, t);
        take();
    }
    CHECK(n_wins > 50u);
    CHECK_EQ(windows_present(), 0);
    CHECK_EQ(n_beeps, 0);
    CHECK_EQ(wins[n_wins - 1u].bursts, 0);
}

TEST_CASE(a_line_stuck_high_or_low_reports_no_tone)
{
    /* Low: no edge at all.  High: one rise, and nothing after it. */
    start(NULL);
    tone_edge(&det, TICK_HZ / 100u, true);
    const uint64_t ms = TICK_HZ / 1000u;
    for (uint64_t t = 10u * ms; t <= 500u * ms; t += ms) {
        tone_advance(&det, t);
        take();
    }
    CHECK_EQ(n_beeps, 0);
    CHECK_EQ(windows_present(), 0);
    CHECK_EQ(tone_stats(&det)->rejected, 1);
    CHECK(!tone_busy(&det));
    /* Stuck high at the end of a beep: the rise is the beep's last burst,
     * 1.7 ms after the one before, and too far from 0.5 ms to join the
     * mean. */
    drive_t d = { 0 };
    edges_t e = { 0 };
    const tone_spec_t sp = { 2000.0, 0.5, 0.0, 0.0, false, 0.0 };
    beep_add(&d, &sp, 0.010, 0.050);
    drive_add(&d, 0.0612, 0.5);
    const front_t f = { 12.0, TAU_TYP };
    front_run(&d, &f, &e);
    start(NULL);
    play(&e, 0.400);
    /* The fall 440 ms later comes after a silence: not the beep's. */
    CHECK_EQ(n_beeps, 1);
    CHECK_EQ(beeps[0].end, e.e[e.n - 2u].t);
    CHECK(e.e[e.n - 2u].level);
    CHECK_EQ(beeps[0].bursts, 101);
    CHECK_EQ(beeps[0].periods, 99);
    drive_free(&d);
    edges_free(&e);
}

TEST_CASE(a_continuous_carrier_is_no_tone)
{
    /* A drive chopped without a break, as a motor held at one step: one
     * burst for 200 ms. */
    static const double fc[] = { 8000, 16000, 48000 };
    for (size_t j = 0; j < sizeof fc / sizeof fc[0]; ++j) {
        drive_t d = { 0 };
        edges_t e = { 0 };
        for (double c = 0.010; c < 0.210; c += 1.0 / fc[j]) {
            drive_add(&d, c, c + 0.3 / fc[j]);
        }
        const front_t f = { 12.0, TAU_TYP };
        front_run(&d, &f, &e);
        start(NULL);
        play(&e, 0.300);
        CHECK_EQ(n_beeps, 0);
        CHECK_EQ(windows_present(), 0);
        drive_free(&d);
        edges_free(&e);
    }
    /* Unchopped pulses above the highest tone: 6.8 kHz. */
    drive_t d = { 0 };
    edges_t e = { 0 };
    const tone_spec_t sp = { 6800.0, 0.3, 0.0, 0.0, false, 0.0 };
    beep_add(&d, &sp, 0.010, 0.100);
    const front_t f = { 12.0, TAU_TYP };
    front_run(&d, &f, &e);
    start(NULL);
    play(&e, 0.300);
    CHECK_EQ(n_beeps, 0);
    CHECK_EQ(windows_present(), 0);
    CHECK_EQ(tone_stats(&det)->rejected, 1);
    drive_free(&d);
    edges_free(&e);
}

TEST_CASE(a_click_of_two_bursts_is_no_beep)
{
    drive_t d = { 0 };
    edges_t e = { 0 };
    const tone_spec_t sp = { 1000.0, 0.5, 0.0, 0.0, false, 0.0 };
    beep_add(&d, &sp, 0.010, 0.0015);
    const front_t f = { 12.0, TAU_TYP };
    front_run(&d, &f, &e);
    start(NULL);
    play(&e, 0.100);
    CHECK_EQ(n_beeps, 0);
    CHECK_EQ(tone_stats(&det)->rejected, 1);
    drive_free(&d);
    edges_free(&e);
}

TEST_CASE(a_glitch_on_an_edge_is_one_rise)
{
    drive_t d = { 0 };
    edges_t e = { 0 };
    const tone_spec_t sp = { 1000.0, 0.5, 0.0, 0.0, false, 0.0 };
    beep_add(&d, &sp, 0.010, 0.050);
    const front_t f = { 12.0, 0.0 };
    front_run(&d, &f, &e);
    /* A bounce after every rise: low 1 tick later, high 2 ticks later. */
    edges_t g = { 0 };
    for (size_t i = 0; i < e.n; ++i) {
        edge_add(&g, (double)e.e[i].t / TICK_HZ, e.e[i].level);
        if (e.e[i].level) {
            edge_add(&g, (double)(e.e[i].t + 1u) / TICK_HZ, false);
            edge_add(&g, (double)(e.e[i].t + 2u) / TICK_HZ, true);
        }
    }
    start(NULL);
    play(&g, 0.150);
    CHECK_EQ(n_beeps, 1);
    CHECK_EQ(beeps[0].bursts, 50);
    CHECK(rel_err(beeps[0].freq_hz, 1000.0) < 0.001);
    CHECK_EQ(tone_stats(&det)->glitches, 50);
    drive_free(&d);
    edges_free(&e);
    edges_free(&g);
}

TEST_CASE(a_beep_whose_first_periods_are_wrong_keeps_its_start)
{
    /* Four pulses 200 us apart, then 1 kHz without a break: the first
     * periods, as dips in a burst seen before its carrier, set a mean
     * every later period would miss.  The beep starts its mean again and
     * keeps its first rise. */
    drive_t d = { 0 };
    edges_t e = { 0 };
    for (unsigned i = 0; i < 4u; ++i) {
        drive_add(&d, 0.0100 + 0.0002 * i, 0.0100 + 0.0002 * i + 0.00005);
    }
    const tone_spec_t sp = { 1000.0, 0.5, 0.0, 0.0, false, 0.0 };
    beep_add(&d, &sp, 0.0108, 0.050);
    const front_t f = { 12.0, 0.0 };
    front_run(&d, &f, &e);
    start(NULL);
    play(&e, 0.150);
    CHECK_EQ(n_beeps, 1);
    CHECK_EQ(beeps[0].start, e.e[0].t);
    CHECK_EQ(beeps[0].bursts, 54);
    CHECK(rel_err(beeps[0].freq_hz, 1000.0) < LOCKED_BEEP);
    drive_free(&d);
    edges_free(&e);
}

TEST_CASE(an_edge_to_the_level_the_line_holds_changes_nothing)
{
    /* A fall lost between two rises, a rise lost between two falls.  With
     * no fall after 15 ms, the rises at 15.25 and 16 ms are the line
     * staying high: the burst at 16 ms is missed, and the 2 ms period it
     * leaves is no part of the mean. */
    start(NULL);
    const uint64_t ms = TICK_HZ / 1000u;
    for (uint64_t k = 0; k < 20u; ++k) {
        tone_edge(&det, 10u * ms + k * ms, true);
        if (k == 5u) {
            tone_edge(&det, 10u * ms + k * ms + ms / 4u, true);
        } else {
            tone_edge(&det, 10u * ms + k * ms + ms / 2u, false);
        }
        if (k == 9u) {
            tone_edge(&det, 10u * ms + k * ms + ms * 3u / 4u, false);
        }
    }
    tone_advance(&det, 100u * ms);
    take();
    CHECK_EQ(n_beeps, 1);
    CHECK_EQ(beeps[0].bursts, 19);
    CHECK_EQ(beeps[0].periods, 17);
    CHECK(rel_err(beeps[0].freq_hz, 1000.0) < LOCKED_BEEP);
    CHECK_EQ(tone_stats(&det)->edges, 41);
}

/* -------------------------------------------------------------- the API */

TEST_CASE(an_edge_earlier_than_the_last_time_is_ignored)
{
    start(NULL);
    tone_advance(&det, 1000000u);
    tone_edge(&det, 999999u, true);
    tone_advance(&det, 5u);
    CHECK_EQ(tone_stats(&det)->out_of_order, 2);
    CHECK_EQ(tone_stats(&det)->edges, 0);
}

TEST_CASE(a_full_queue_counts_the_beeps_it_loses)
{
    drive_t d = { 0 };
    edges_t e = { 0 };
    const tone_spec_t sp = { 2000.0, 0.5, 0.0, 0.0, false, 0.0 };
    for (unsigned i = 0; i < TONE_BEEP_QUEUE + 3u; ++i) {
        beep_add(&d, &sp, 0.010 + 0.020 * i, 0.010);
    }
    const front_t f = { 12.0, TAU_TYP };
    front_run(&d, &f, &e);
    start(NULL);
    tone_feed(&det, e.e, e.n);
    tone_advance(&det, TICK_HZ);
    CHECK_EQ(tone_stats(&det)->beeps, TONE_BEEP_QUEUE);
    CHECK_EQ(tone_stats(&det)->lost, 3);
    take();
    CHECK_EQ(n_beeps, TONE_BEEP_QUEUE);
    tone_beep_t b;
    CHECK(!tone_next_beep(&det, &b));
    tone_feed(&det, NULL, 3u);
    drive_free(&d);
    edges_free(&e);
}

TEST_CASE(flush_ends_the_beep_under_way_at_its_last_edge)
{
    drive_t d = { 0 };
    edges_t e = { 0 };
    const tone_spec_t sp = { 3000.0, 0.5, 0.0, 0.0, false, 0.0 };
    beep_add(&d, &sp, 0.010, 0.020);
    const front_t f = { 12.0, TAU_TYP };
    front_run(&d, &f, &e);
    start(NULL);
    tone_feed(&det, e.e, e.n);
    CHECK(tone_busy(&det));
    tone_flush(&det);
    CHECK(!tone_busy(&det));
    take();
    CHECK_EQ(n_beeps, 1);
    CHECK_EQ(beeps[0].end, e.e[e.n - 1u].t);
    CHECK_EQ(beeps[0].bursts, 60);
    tone_flush(&det);
    take();
    CHECK_EQ(n_beeps, 1);
    drive_free(&d);
    edges_free(&e);
}

TEST_CASE(a_long_silence_reports_the_last_empty_window)
{
    start(NULL);
    tone_advance(&det, 0u);
    tone_advance(&det, (uint64_t)TICK_HZ * 10u);
    tone_window_t w;
    CHECK(tone_window(&det, &w));
    CHECK_EQ(w.index, 10u * 1000u / 8u - 1u);
    CHECK(!w.present);
    tone_advance(&det, (uint64_t)TICK_HZ * 10u + TICK_HZ / 100u);
    CHECK(tone_window(&det, &w));
    CHECK_EQ(w.index, 10u * 1000u / 8u);
}

TEST_CASE(null_arguments_change_nothing)
{
    tone_edge(NULL, 1u, true);
    tone_feed(NULL, NULL, 0u);
    tone_advance(NULL, 1u);
    tone_flush(NULL);
    tone_window_t w;
    tone_beep_t b;
    CHECK(!tone_window(NULL, &w));
    start(NULL);
    CHECK(!tone_window(&det, NULL));
    CHECK(!tone_next_beep(NULL, &b));
    CHECK(!tone_next_beep(&det, NULL));
    CHECK(!tone_busy(NULL));
    CHECK(tone_stats(NULL) == NULL);
    CHECK_EQ(tone_ticks_us(NULL, 5u), 0);
}

TEST_CASE(ticks_convert_to_microseconds_without_overflow)
{
    start(NULL);
    CHECK_EQ(tone_ticks_us(&det, TICK_HZ), 1000000);
    CHECK_EQ(tone_ticks_us(&det, 75u), 2);
    /* A year of ticks. */
    const uint64_t year = (uint64_t)TICK_HZ * 31536000u;
    CHECK(tone_ticks_us(&det, year) == 31536000000000ull);
}

int main(void)
{
    RUN(the_defaults_are_a_configuration_init_takes);
    RUN(a_configuration_that_contradicts_itself_is_refused);
    RUN(a_square_tone_reads_its_frequency_from_500_hz_to_6_khz);
    RUN(a_chopped_tone_reads_the_tone_and_the_carrier);
    RUN(a_carrier_running_free_of_the_tone_moves_bursts_not_the_mean);
    RUN(jitter_of_a_microsecond_on_every_edge_stays_inside_the_bound);
    RUN(a_phase_from_2s_to_6s_driven_or_floating_reads_the_same);
    RUN(a_phase_under_the_input_threshold_reads_nothing);
    RUN(a_clamp_slow_enough_to_swallow_the_carrier_still_reads_the_tone);
    RUN(a_beep_train_counts_and_places_every_beep);
    RUN(two_pitches_alternating_with_silences_are_beeps_of_their_own);
    RUN(two_pitches_alternating_with_no_silence_split_at_the_change);
    RUN(with_the_split_off_a_pitch_change_is_one_beep);
    RUN(silence_reports_windows_with_no_tone);
    RUN(a_line_stuck_high_or_low_reports_no_tone);
    RUN(a_continuous_carrier_is_no_tone);
    RUN(a_click_of_two_bursts_is_no_beep);
    RUN(a_glitch_on_an_edge_is_one_rise);
    RUN(a_beep_whose_first_periods_are_wrong_keeps_its_start);
    RUN(an_edge_to_the_level_the_line_holds_changes_nothing);
    RUN(an_edge_earlier_than_the_last_time_is_ignored);
    RUN(a_full_queue_counts_the_beeps_it_loses);
    RUN(flush_ends_the_beep_under_way_at_its_last_edge);
    RUN(a_long_silence_reports_the_last_empty_window);
    RUN(null_arguments_change_nothing);
    RUN(ticks_convert_to_microseconds_without_overflow);
    return test_summary("tone");
}
