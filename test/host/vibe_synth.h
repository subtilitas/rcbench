/*
 * Synthetic balance-node signals for test_vibe and test_vibe_solve: a
 * vibration channel of 1x, 2x and blade-pass sines with noise, quantised
 * as a 12-bit ADC with 1g = 436 counts around 2048, an index channel
 * sampled half a sample later (the second channel of a two-channel ADC
 * pattern), and the exact index edges with optional jitter, missing and
 * extra pulses.  A phase p puts the harmonic's peak p degrees of rotation
 * after the index edge.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vibe_demod.h"
#include "vibe_edge.h"

#define SYN_FS     10000.0
#define SYN_DC     2048.0
#define SYN_PER_G  436.0
#define SYN_PI     3.14159265358979323846

/** One run's signal. */
typedef struct {
    double rpm;
    double a1, p1;      /* 1x, g and degrees */
    double a2, p2;      /* 2x */
    double ab, pb;      /* blade-pass */
    int    blades;
    double noise;       /* g rms */
    double t0;          /* the first index edge, s */
    int    jitter;      /* edges moved by up to this many whole samples */
    long   drop_from;   /* edges drop_from to drop_to are not given */
    long   drop_to;
    long   extra;       /* an extra edge half a revolution after this one */
    double pulse;       /* the mark's width, turns */
    uint64_t seed;
} syn_t;

/** The samples and edges of a run. */
typedef struct {
    int16_t  *vib;
    int16_t  *idx;
    size_t    n;
    uint32_t *edges;
    size_t    ne;
} syn_run_t;

/** A signal with nothing in it but the rotor turning. */
static inline syn_t syn_plain(double rpm)
{
    syn_t s;
    memset(&s, 0, sizeof(s));
    s.rpm = rpm;
    s.t0 = 0.0123;
    s.drop_from = -1;
    s.drop_to = -1;
    s.extra = -1;
    s.pulse = 0.032;
    s.seed = 0x9E3779B97F4A7C15ull;
    return s;
}

static inline uint64_t syn_next(uint64_t *st)
{
    uint64_t x = *st;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *st = x;
    return x;
}

static inline double syn_uniform(uint64_t *st)
{
    return ((double)(syn_next(st) >> 11) + 0.5) / 9007199254740992.0;
}

static inline double syn_gauss(uint64_t *st)
{
    const double u = syn_uniform(st);
    const double v = syn_uniform(st);
    return sqrt(-2.0 * log(u)) * cos(2.0 * SYN_PI * v);
}

static inline int16_t syn_adc(double v)
{
    double c = floor(v + 0.5);
    if (c < 0.0) {
        c = 0.0;
    } else if (c > 4095.0) {
        c = 4095.0;
    }
    return (int16_t)c;
}

/** The vibration in g at time t. */
static inline double syn_vib(const syn_t *s, double t)
{
    const double th = 2.0 * SYN_PI * s->rpm / 60.0 * (t - s->t0);
    const double k = SYN_PI / 180.0;
    return s->a1 * cos(th - s->p1 * k) + s->a2 * cos(2.0 * th - s->p2 * k)
           + s->ab * cos((double)s->blades * th - s->pb * k);
}

/** Generates @p seconds of the signal. */
static inline void syn_gen(const syn_t *s, double seconds, syn_run_t *r)
{
    uint64_t st = s->seed;
    r->n = (size_t)(seconds * SYN_FS);
    r->vib = malloc(r->n * sizeof(int16_t));
    r->idx = malloc(r->n * sizeof(int16_t));
    const double f = s->rpm / 60.0;
    const size_t cap = (size_t)(seconds * f) + 8u;
    r->edges = malloc(cap * sizeof(uint32_t));
    if (r->vib == NULL || r->idx == NULL || r->edges == NULL) {
        abort();
    }
    for (size_t k = 0; k < r->n; ++k) {
        const double t = (double)k / SYN_FS;
        const double v = syn_vib(s, t) + s->noise * syn_gauss(&st);
        r->vib[k] = syn_adc(SYN_DC + SYN_PER_G * v);
        const double ti = ((double)k + 0.5) / SYN_FS;
        double turn = f * (ti - s->t0);
        turn -= floor(turn);
        r->idx[k] = (turn < s->pulse) ? 3000 : 400;
    }
    r->ne = 0;
    for (long e = 0;; ++e) {
        const double t = s->t0 + (double)e / f;
        if (t * SYN_FS >= (double)r->n) {
            break;
        }
        long j = 0;
        if (s->jitter != 0) {
            j = (long)(syn_next(&st) % (uint64_t)(2 * s->jitter + 1)) - s->jitter;
        }
        if (!(e >= s->drop_from && e <= s->drop_to)) {
            r->edges[r->ne++] = (uint32_t)(llround(t * SYN_FS * 256.0) + j * 256);
        }
        if (e == s->extra && r->ne < cap) {
            r->edges[r->ne++] = (uint32_t)llround((t + 0.5 / f) * SYN_FS * 256.0);
        }
    }
}

static inline void syn_free(syn_run_t *r)
{
    free(r->vib);
    free(r->idx);
    free(r->edges);
    memset(r, 0, sizeof(*r));
}

/** Reports collected by syn_collect(). */
typedef struct {
    vibe_report_t r[512];
    size_t n;
} syn_reports_t;

static inline void syn_collect(const vibe_report_t *r, void *ctx)
{
    syn_reports_t *o = ctx;
    if (o->n < sizeof(o->r) / sizeof(o->r[0])) {
        o->r[o->n++] = *r;
    }
}

/**
 * Feeds a run in blocks of @p block samples with the edges in each.  With @p channel the edges come from the index channel
 * through vibe_edge; otherwise the exact stamps are used.
 */
static inline void syn_feed(vibe_demod_t *d, const syn_run_t *r, size_t block,
                            int channel)
{
    vibe_edge_t ed;
    vibe_edge_init(&ed, 1200, 2200, true);
    size_t next_edge = 0;
    uint32_t *found = malloc((block / 8u + 2u) * sizeof(uint32_t));
    if (found == NULL) {
        abort();
    }
    for (size_t k0 = 0; k0 < r->n; k0 += block) {
        const size_t m = (r->n - k0 < block) ? r->n - k0 : block;
        if (channel) {
            const size_t got = vibe_edge_scan(&ed, r->idx + k0, m, found,
                                              block / 8u + 2u);
            vibe_demod_feed(d, r->vib + k0, m, found, got);
        } else {
            const uint64_t end = (uint64_t)(k0 + m) << 8;
            const size_t first = next_edge;
            while (next_edge < r->ne && (uint64_t)r->edges[next_edge] < end) {
                ++next_edge;
            }
            vibe_demod_feed(d, r->vib + k0, m, r->edges + first,
                            next_edge - first);
        }
    }
    free(found);
}

/** A demodulator with the node's defaults and @p blades. */
static inline void syn_demod(vibe_demod_t *d, syn_reports_t *out, int blades,
                             int32_t shift)
{
    vibe_cfg_t c;
    vibe_cfg_default(&c);
    c.blades = (uint8_t)blades;
    c.edge_shift_q8 = shift;
    out->n = 0;
    vibe_demod_init(d, &c, syn_collect, out);
}

/** An angle difference in degrees, -180 to 180. */
static inline double syn_ddeg(double a, double b)
{
    double d = fmod(a - b, 360.0);
    if (d > 180.0) {
        d -= 360.0;
    } else if (d < -180.0) {
        d += 360.0;
    }
    return d;
}
