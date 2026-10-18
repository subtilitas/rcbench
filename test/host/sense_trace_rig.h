/*
 * The rig of the suites of the trace of CH1's 1 ms samples
 * (sense_trace.h): the modelled INA3221 of fake_ina.h read by the
 * IOmcu's sensor service, a clock the suite moves 1 ms a step, and a
 * console that takes as many bytes a pass as the suite gives it; the
 * console's lines parsed into seen; a ring of 8 fed without a reader; a
 * console that takes nothing; a log cut after its first trace.
 *
 * Taken by test_sense_trace_feed.c, test_sense_trace.c,
 * test_sense_trace_next.c, test_sense_trace_setup.c and
 * test_sense_trace_lines.c.  Every helper is static inline, and every
 * file-scope variable is read by one of them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "fake_ina.h"
#include "sense_svc.h"
#include "sense_trace.h"

#define I3221_ADDR 0x40u
#define I3221_UOHM 100000u
#define RING       4096u
#define MAX_S      20000u

static fake_bus_t   fb;
static fake_part_t *i3221;
static sense_svc_t  svc;
static sense_cmd_t  cmd;
static sense_snap_t snap;
static uint64_t     g_us;
static unsigned     g_clock_reads;

static sense_trace_t     tr;
static sense_trace_rec_t g_ring[RING];

/* What the console took. */
static char   g_log[1u << 20];
static size_t g_len;
static bool   g_partial;        /* a pump handed back less than a line */
static size_t g_most;           /* the most bytes one pump handed back */

static inline uint64_t now_us(void *ctx)
{
    (void)ctx;
    ++g_clock_reads;
    return g_us;
}

static inline void recover(void *ctx)
{
    (void)ctx;
}

static inline bool open_bus(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    return true;
}

static inline void close_bus(void *ctx)
{
    (void)ctx;
}

static inline sense_err_t ask(void *ctx, uint8_t addr)
{
    return (fake_find((fake_bus_t *)ctx, addr) != NULL) ? SENSE_OK
                                                         : SENSE_NACK;
}

/* The INA3221 with CH1 on a 0.1 Ω shunt, 0.12 A and 6.0 V; the service
 * with its bus not yet open; a trace over a ring of @p size. */
static inline void rig_size(uint32_t size)
{
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    i3221 = fake_add(&fb, FAKE_INA3221, I3221_ADDR, 0.1);
    i3221->volts[0] = 6.0;
    i3221->amps[0]  = 0.12;
    g_us = 20000000u;
    const sense_svc_io_t io = {
        .sched = { { fake_read, fake_write, &fb }, now_us, recover, &fb },
        .open = open_bus, .close = close_bus, .ask = ask,
    };
    sense_svc_init(&svc, &io);
    memset(&cmd, 0, sizeof(cmd));
    cmd.cfg_gen = 1u;
    cmd.sda = 16u;
    cmd.scl = 17u;
    cmd.parts.ina3221_en = true;
    cmd.parts.ina3221_addr = I3221_ADDR;
    cmd.parts.ina3221_shunt_uohm = I3221_UOHM;
    cmd.parts.ina3221_channels = 1u;
    CHECK(sense_trace_init(&tr, g_ring, size));
    g_len = 0u;
    g_log[0] = '\0';
    g_partial = false;
    g_most = 0u;
}

static inline void rig(void)
{
    rig_size(RING);
}

/* Core 1's tick: the service, then the trace. */
static inline void tick(void)
{
    sense_svc_step(&svc, &cmd, &snap);
    sense_trace_feed(&tr, svc.open ? &svc.sched : NULL, g_us);
    g_us += 1000u;
}

/* Core 0's pass with @p room bytes of console. */
static inline void pass_as(size_t room, bool connected)
{
    char buf[256];
    const size_t n = sense_trace_pump(&tr, g_us, connected, buf, room);
    if (n > room || n > sizeof(buf)) {
        g_partial = true;
        return;
    }
    if (n > 0u && (n < 2u || buf[n - 2u] != '\r' || buf[n - 1u] != '\n')) {
        g_partial = true;
    }
    if (n > g_most) {
        g_most = n;
    }
    if (g_len + n < sizeof(g_log)) {
        memcpy(&g_log[g_len], buf, n);
        g_len += n;
        g_log[g_len] = '\0';
    }
}

static inline void pass(void)
{
    pass_as(SENSE_TRACE_LINE_MAX, true);
}

/* @p ms of both cores, the console taking a buffer a pass. */
static inline void run(unsigned ms)
{
    for (unsigned k = 0; k < ms; ++k) {
        tick();
        pass();
    }
}

/* ------------------------------------------------- the console, parsed */

typedef struct {
    unsigned n_t, n_h, n_z, n_c, n_e, n_k, n_d, n_l, n_st, n_bad;
    unsigned n_s, n_v;
    unsigned long lost;         /* the $L lines' sum                  */
    /* The last $T, $H and $Z. */
    unsigned ver, id, len;
    char     trig[8];
    unsigned long t0, ms0;
    unsigned long shunt, cfg, on, rst;
    unsigned z_id, z_s, z_v, z_l, z_m, z_ml;
    char     z_e;
    /* The samples since the last $T. */
    uint32_t t[MAX_S];
    int32_t  code[MAX_S];
    int      first_dt;
    int      mv_last;
    unsigned long mark_t[32];   /* every trigger line's time          */
    unsigned mark_ch, mark_us;  /* the last $C line's                 */
    unsigned long dest_t;       /* the last $D line's                 */
    unsigned dest_ch, dest_us;
    unsigned long dest_cmd;
    unsigned n_mark;
    unsigned long st_on, st_rst;
    unsigned st_at;             /* sample lines before the first $S   */
} seen_t;

static seen_t seen;

static inline void parse_line(const char *l)
{
    seen_t *p = &seen;
    char e = '\0';
    int dt = 0;
    int code = 0;
    int mv = 0;
    int end = 0;
    unsigned long a = 0u;
    unsigned long cmd_t = 0u;
    unsigned b = 0u;
    unsigned c = 0u;
    if (sscanf(l, "$T v=%u n=%u trig=%7s t=%lu ms=%lu len=%u%n", &p->ver,
               &p->id, p->trig, &p->t0, &p->ms0, &p->len, &end) == 6
        && l[end] == '\0') {
        ++p->n_t;
        p->n_s = 0u;
        p->n_v = 0u;
    } else if (sscanf(l, "$H dt_us=1000 shunt_uohm=%lu cfg=0x%lx on=%lu "
                         "rst=%lu%n", &p->shunt, &p->cfg, &p->on, &p->rst,
                      &end) == 4 && l[end] == '\0') {
        ++p->n_h;
    } else if (sscanf(l, "$Z n=%u s=%u v=%u l=%u m=%u ml=%u e=%c%n",
                      &p->z_id, &p->z_s, &p->z_v, &p->z_l, &p->z_m, &p->z_ml,
                      &e, &end) == 7 && l[end] == '\0') {
        p->z_e = e;
        ++p->n_z;
    } else if (sscanf(l, "$C t=%lu ch=%u us=%u%n", &a, &b, &c, &end) == 3
               && l[end] == '\0') {
        ++p->n_c;
        p->mark_ch = b;
        p->mark_us = c;
        p->mark_t[p->n_mark++ % 32u] = a;
    } else if (sscanf(l, "$D t=%lu ch=%u us=%u cmd=%lu%n", &a, &b, &c,
                      &cmd_t, &end) == 4 && l[end] == '\0') {
        ++p->n_d;
        p->dest_t   = a;
        p->dest_ch  = b;
        p->dest_us  = c;
        p->dest_cmd = cmd_t;
    } else if (sscanf(l, "$E t=%lu%n", &a, &end) == 1 && l[end] == '\0') {
        ++p->n_e;
        p->mark_t[p->n_mark++ % 32u] = a;
    } else if (sscanf(l, "$K t=%lu%n", &a, &end) == 1 && l[end] == '\0') {
        ++p->n_k;
        p->mark_t[p->n_mark++ % 32u] = a;
    } else if (sscanf(l, "$L n=%lu%n", &a, &end) == 1 && l[end] == '\0') {
        ++p->n_l;
        p->lost += a;
    } else if (sscanf(l, "$S on=%lu rst=%lu%n", &p->st_on, &p->st_rst, &end)
               == 2 && l[end] == '\0') {
        if (p->n_st == 0u) {
            p->st_at = p->n_s;
        }
        ++p->n_st;
    } else if (sscanf(l, "v%d%n", &mv, &end) == 1 && l[end] == '\0') {
        ++p->n_v;
        p->mv_last = mv;
    } else if (sscanf(l, "%d,%d%n", &dt, &code, &end) == 2
               && l[end] == '\0') {
        const uint32_t prev = (p->n_s == 0u) ? (uint32_t)p->t0
                                             : p->t[(p->n_s - 1u) % MAX_S];
        if (p->n_s == 0u) {
            p->first_dt = dt;
        }
        p->t[p->n_s % MAX_S]    = prev + (uint32_t)dt;
        p->code[p->n_s % MAX_S] = code;
        ++p->n_s;
    } else {
        ++p->n_bad;
    }
}

/* Everything the console took, line by line, into seen. */
static inline void parse(void)
{
    memset(&seen, 0, sizeof(seen));
    const char *at = g_log;
    for (;;) {
        const char *eol = strstr(at, "\r\n");
        if (eol == NULL) {
            if (*at != '\0') {
                ++seen.n_bad;       /* a line with no end */
            }
            return;
        }
        char line[128];
        const size_t n = (size_t)(eol - at);
        if (n >= sizeof(line) || n + 2u > SENSE_TRACE_LINE_MAX) {
            ++seen.n_bad;
        } else {
            memcpy(line, at, n);
            line[n] = '\0';
            parse_line(line);
        }
        at = eol + 2;
    }
}

/* Records of @p kind the ring holds. */
static inline unsigned held_of(sense_trace_kind_t kind)
{
    unsigned n = 0u;
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    for (uint32_t k = (uint32_t)atomic_load(&tr.tail); k != head; ++k) {
        if ((g_ring[k & tr.mask].meta >> 24) == (uint32_t)kind) {
            ++n;
        }
    }
    return n;
}

/* The @p k-th sample or voltage the ring holds, from its tail: the set-up's
 * records are left out.  NULL past the last. */
static inline const sense_trace_rec_t *rec(unsigned k)
{
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    for (uint32_t i = (uint32_t)atomic_load(&tr.tail); i != head; ++i) {
        const sense_trace_rec_t *r = &g_ring[i & tr.mask];
        if ((r->meta >> 24) >= SENSE_TRACE_CFG) {
            continue;
        }
        if (k-- == 0u) {
            return r;
        }
    }
    return NULL;
}

/* Samples and voltages the ring holds. */
static inline unsigned held_data(void)
{
    return held_of(SENSE_TRACE_CURRENT) + held_of(SENSE_TRACE_BUS)
           + held_of(SENSE_TRACE_CLIP_HI) + held_of(SENSE_TRACE_CLIP_LO);
}

/* A ring of 8 with nobody reading it, fed @p n CH1 samples. */
static inline void fill(unsigned n)
{
    rig_size(8u);
    /* The first tick's records -- the set-up's two and a sample -- read
     * out, so the ring starts empty with the set-up known. */
    tick();
    pass();
    atomic_store(&tr.tail, atomic_load(&tr.head));
    tr.out.scanned = (uint32_t)atomic_load(&tr.head);
    unsigned fed = 0u;
    while (fed < n) {
        sense_svc_step(&svc, &cmd, &snap);
        /* The voltage's read is left out: samples only. */
        svc.sched.acc[SENSE_SRC_CH1].n_v = 0u;
        tr.src.n_v = 0u;
        sense_trace_feed(&tr, &svc.sched, g_us);
        g_us += 1000u;
        ++fed;
    }
}

/* The console takes nothing for @p ms. */
static inline void stall(unsigned ms)
{
    for (unsigned k = 0; k < ms; ++k) {
        tick();
        pass_as(0u, true);
    }
}

/* The log cut after its first end line, so parse() sees one trace; the
 * rest is returned. */
static inline const char *first_trace_only(void)
{
    static char rest[1u << 16];
    char *z = strstr(g_log, "$Z ");
    rest[0] = '\0';
    if (z != NULL && (z = strstr(z, "\r\n")) != NULL) {
        snprintf(rest, sizeof(rest), "%s", z + 2);
        z[2] = '\0';
    }
    return rest;
}
