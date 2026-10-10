/*
 * The trace of CH1's 1 ms samples (sense_trace.h), against the modelled
 * INA3221 of fake_ina.h read by the coprocessor's sensor service, a clock
 * the suite moves 1 ms a step, and a console that takes as many bytes a
 * pass as the suite gives it.
 *
 * Under test: core 1's side -- every CH1 sample and every CH1 bus voltage
 * taken once, with its time and value, across a window's close, an
 * emptied window, a set-up and a closed bus; no bus transaction and no
 * clock read added to the tick, at most 2 records a call; a ring full by
 * exactly 0, 1 and many records, the dropped ones counted and never
 * waited for.  Core 0's side -- the newest SENSE_TRACE_PRE records kept
 * while idle; a trace from each trigger with its header, its records from
 * before the trigger and its length; a trigger during a trace adding its
 * line and moving the end, never back; the watch on a slot's pulse at
 * SENSE_TRACE_HOLD_MS and one step either side; traces across the wrap
 * of the millisecond tick and of the 0.1 ms count; whole lines only,
 * whatever room the console has; a console with no room for seconds,
 * inside a trace and across its end, and one not connected; a changed set-up, a part going offline and a reset
 * during a trace, each in its place among the samples when the console
 * is behind; the console's stop, which writes what came before it; a
 * voltage kept only behind the sample of its tick; the pulse held to the
 * frame; the trigger queue full by 0 and 1;
 * every line at its longest inside SENSE_TRACE_LINE_MAX; and the shunt
 * code back from every current ina3221_current_ua() gives.
 *
 * SPDX-License-Identifier: MIT
 */
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
static sense_svc_t  v;
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

static uint64_t now_us(void *ctx)
{
    (void)ctx;
    ++g_clock_reads;
    return g_us;
}

static void recover(void *ctx)
{
    (void)ctx;
}

static bool open_bus(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    return true;
}

static void close_bus(void *ctx)
{
    (void)ctx;
}

static sense_err_t ask(void *ctx, uint8_t addr)
{
    return (fake_find((fake_bus_t *)ctx, addr) != NULL) ? SENSE_OK
                                                         : SENSE_NACK;
}

/* The INA3221 with CH1 on a 0.1 Ω shunt, 0.12 A and 6.0 V; the service
 * with its bus not yet open; a trace over a ring of @p size. */
static void rig_size(uint32_t size)
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
    sense_svc_init(&v, &io);
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

static void rig(void)
{
    rig_size(RING);
}

/* Core 1's tick: the service, then the trace. */
static void tick(void)
{
    sense_svc_step(&v, &cmd, &snap);
    sense_trace_feed(&tr, v.open ? &v.sched : NULL);
    g_us += 1000u;
}

/* Core 0's pass with @p room bytes of console. */
static void pass_as(size_t room, bool connected)
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

static void pass(void)
{
    pass_as(SENSE_TRACE_LINE_MAX, true);
}

/* @p ms of both cores, the console taking a buffer a pass. */
static void run(unsigned ms)
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
    unsigned n_mark;
    unsigned long st_on, st_rst;
    unsigned st_at;             /* sample lines before the first $S   */
} seen_t;

static seen_t seen;

static void parse_line(const char *l)
{
    seen_t *p = &seen;
    char e = '\0';
    int dt = 0;
    int code = 0;
    int mv = 0;
    int end = 0;
    unsigned long a = 0u;
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
    } else if (sscanf(l, "$D t=%lu ch=%u us=%u%n", &a, &b, &c, &end) == 3
               && l[end] == '\0') {
        ++p->n_d;
        p->dest_t  = a;
        p->dest_ch = b;
        p->dest_us = c;
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
static void parse(void)
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
static unsigned held_of(sense_trace_kind_t kind)
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
static const sense_trace_rec_t *rec(unsigned k)
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
static unsigned held_data(void)
{
    return held_of(SENSE_TRACE_CURRENT) + held_of(SENSE_TRACE_BUS);
}

/* ------------------------------------------------------------- core 1 */

TEST_CASE(a_ring_is_a_power_of_two)
{
    sense_trace_t t2;
    sense_trace_rec_t r[8];
    char out[64];
    CHECK(!sense_trace_init(&t2, NULL, 8u));
    CHECK(!sense_trace_init(&t2, r, 0u));
    CHECK(!sense_trace_init(&t2, r, 1u));
    CHECK(!sense_trace_init(&t2, r, 6u));
    CHECK(!sense_trace_init(&t2, r, (1u << 24) + 1u));
    CHECK(!sense_trace_init(&t2, r, 1u << 25));
    /* Refused, it takes nothing and gives nothing. */
    rig();
    tick();
    sense_trace_feed(&t2, &v.sched);
    sense_trace_trigger(&t2, SENSE_TRACE_TRIG_KEY, g_us, 0u, 0u);
    CHECK(!sense_trace_pulse(&t2, 0u, 5u, 1500u, g_us));
    CHECK(!sense_trace_active(&t2));
    CHECK_EQ(sense_trace_pump(&t2, g_us, true, out, sizeof(out)), 0u);
    CHECK(sense_trace_init(&t2, r, 2u));
    CHECK(sense_trace_init(&t2, r, 8u));
}

TEST_CASE(every_ch1_sample_and_voltage_is_taken_once)
{
    rig();
    for (unsigned k = 0; k < 1000u; ++k) {
        i3221->amps[0] = 0.0004 * (double)(k % 2000u);
        tick();
    }
    CHECK_EQ(i3221->reads[INA3221_SHUNT1], 1000u);
    CHECK_EQ(i3221->reads[INA3221_BUS1], 50u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), 1000u);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), 50u);
    /* Each sample 1 ms after the one before, at the value read. */
    unsigned n = 0u;
    for (unsigned k = 0u; k < held_data(); ++k) {
        const sense_trace_rec_t *r = rec(k);
        if ((r->meta >> 24) == SENSE_TRACE_BUS) {
            CHECK_EQ(r->v, 6000);
            continue;
        }
        CHECK_EQ(r->t, 200000u + 10u * n);
        CHECK_EQ(r->v, 400 * (int32_t)n);
        CHECK_EQ(r->meta & SENSE_TRACE_LOST_MAX, 0u);
        ++n;
    }
    CHECK_EQ(n, 1000u);
}

TEST_CASE(the_feed_adds_no_bus_transaction_and_reads_no_clock)
{
    rig();
    for (unsigned k = 0; k < 200u; ++k) {
        sense_svc_step(&v, &cmd, &snap);
        const unsigned bus = fb.transactions;
        const unsigned clock = g_clock_reads;
        const uint32_t held = sense_trace_held(&tr);
        sense_trace_feed(&tr, &v.sched);
        CHECK_EQ(fb.transactions, bus);
        CHECK_EQ(g_clock_reads, clock);
        /* The first tick brings the set-up's two records as well. */
        CHECK(sense_trace_held(&tr) - held <= ((k == 0u) ? 4u : 2u));
        g_us += 1000u;
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), 200u);
}

TEST_CASE(a_tick_with_no_sample_feeds_none)
{
    rig();
    i3221->present = false;
    for (unsigned k = 0; k < 100u; ++k) {
        tick();
    }
    CHECK_EQ(held_data(), 0u);
    /* The part answers from its next probe on. */
    i3221->present = true;
    for (unsigned k = 0; k < 1000u; ++k) {
        tick();
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK(held_of(SENSE_TRACE_CURRENT) > 0u);
}

TEST_CASE(a_set_up_and_a_closed_bus_lose_and_repeat_nothing)
{
    rig();
    for (unsigned k = 0; k < 130u; ++k) {
        tick();
    }
    /* A set-up: the schedule and its history start afresh. */
    ++cmd.cfg_gen;
    for (unsigned k = 0; k < 130u; ++k) {
        tick();
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), i3221->reads[INA3221_BUS1]);
    /* No part enabled: the bus closes and nothing is fed. */
    const unsigned held = held_data();
    ++cmd.cfg_gen;
    cmd.parts.ina3221_en = false;
    for (unsigned k = 0; k < 20u; ++k) {
        tick();
    }
    CHECK(!v.open);
    CHECK_EQ(held_data(), held);
    CHECK_EQ(tr.src.cfg, 0u);
    CHECK_EQ(tr.src.shunt, 0u);
    /* And open again: every read once more. */
    ++cmd.cfg_gen;
    cmd.parts.ina3221_en = true;
    for (unsigned k = 0; k < 130u; ++k) {
        tick();
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), i3221->reads[INA3221_BUS1]);
    CHECK_EQ(tr.src.shunt, I3221_UOHM);
    /* The shunt went from none to 0.1 Ω, to none with the bus closed and
     * back: a record each time, and one of the Configuration with it. */
    CHECK_EQ(held_of(SENSE_TRACE_SHUNT), 3u);
    CHECK(held_of(SENSE_TRACE_CFG) >= 3u);
}

TEST_CASE(a_voltage_is_taken_from_a_window_a_reset_emptied)
{
    rig();
    for (unsigned k = 0; k < 300u; ++k) {
        tick();
    }
    /* The part resets itself; the read-back finds it within 40 ms and
     * empties CH1's window. */
    fake_reset3221(i3221);
    for (unsigned k = 0; k < 2000u; ++k) {
        tick();
    }
    CHECK_EQ(v.sched.i3221_resets, 1u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), i3221->reads[INA3221_BUS1]);
    for (unsigned k = 0u; k < held_data(); ++k) {
        if ((rec(k)->meta >> 24) == SENSE_TRACE_BUS) {
            CHECK_EQ(rec(k)->v, 6000);
        }
    }
    CHECK_EQ(tr.src.cfg >> 24, 1u);
}

TEST_CASE(a_set_up_that_leaves_the_history_at_the_same_place)
{
    rig();
    /* 51 samples leave the history's head at 1, where the first sample
     * after a set-up leaves it again: that sample is a new one. */
    for (unsigned k = 0; k < 51u; ++k) {
        tick();
    }
    CHECK_EQ(v.sched.ch1_head, 1u);
    ++cmd.cfg_gen;
    tick();
    CHECK_EQ(v.sched.ch1_head, 1u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), 52u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
}

/* A tick by hand: a new CH1 sample 1 ms on, and the window being filled
 * holding @p n_v voltages that sum to @p v_sum. */
static void poke(uint16_t n_v, int64_t v_sum)
{
    sense_sched_t *sc = &v.sched;
    sc->ch1[sc->ch1_head].t  = (uint32_t)(g_us / 100u);
    sc->ch1[sc->ch1_head].ua = 120000;
    sc->ch1_head = (uint8_t)((sc->ch1_head + 1u) % SENSE_CH1_HISTORY);
    sc->acc[SENSE_SRC_CH1].n_v   = n_v;
    sc->acc[SENSE_SRC_CH1].v_sum = v_sum;
    sense_trace_feed(&tr, sc);
    g_us += 1000u;
}

TEST_CASE(a_voltage_is_told_from_the_one_before_by_window_and_count)
{
    rig();
    /* Two set-ups inside the first 50 ms: each schedule's first window
     * is number 0 and holds one voltage after 10 ticks. */
    for (unsigned k = 0; k < 10u; ++k) {
        tick();
    }
    CHECK_EQ(v.sched.acc[SENSE_SRC_CH1].n_v, 1u);
    ++cmd.cfg_gen;
    i3221->volts[0] = 5.0;
    for (unsigned k = 0; k < 10u; ++k) {
        tick();
    }
    CHECK_EQ(v.sched.win, 0u);
    CHECK_EQ(v.sched.acc[SENSE_SRC_CH1].n_v, 1u);
    CHECK_EQ(i3221->reads[INA3221_BUS1], 2u);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), 2u);
    int32_t mv[2] = { 0, 0 };
    unsigned n = 0u;
    for (unsigned k = 0u; k < held_data(); ++k) {
        if ((rec(k)->meta >> 24) == SENSE_TRACE_BUS) {
            mv[n++ % 2u] = rec(k)->v;
        }
    }
    CHECK_EQ(mv[0], 6000);
    CHECK_EQ(mv[1], 5000);
    /* The next window with as many voltages as the one before: its one
     * voltage is a new one, and the value is its own. */
    unsigned held = held_data();
    v.sched.win += 1u;
    poke(1u, 4800000);
    CHECK_EQ(held_data(), held + 2u);
    CHECK_EQ(rec(held + 1u)->v, 4800);
    /* A second in the same window: the sum's gain. */
    poke(2u, 4800000 + 4700000);
    CHECK_EQ(rec(held + 3u)->v, 4700);
    /* No gain: the sample alone. */
    poke(2u, 4800000 + 4700000);
    CHECK_EQ(held_data(), held + 5u);
    CHECK_EQ(rec(held + 4u)->meta >> 24, SENSE_TRACE_CURRENT);
    /* The window emptied and one voltage read since: counted from
     * nothing. */
    poke(1u, 4600000);
    CHECK_EQ(held_data(), held + 7u);
    CHECK_EQ(rec(held + 6u)->v, 4600);
}

TEST_CASE(a_voltage_with_no_current_sample_of_its_tick_is_left_out)
{
    rig();
    for (unsigned k = 0; k < 10u; ++k) {
        tick();
    }
    /* CH1's current read failed in a tick whose bus voltage read went
     * through: no sample line the voltage could stand behind. */
    const unsigned held = held_data();
    v.sched.acc[SENSE_SRC_CH1].n_v  += 1u;
    v.sched.acc[SENSE_SRC_CH1].v_sum += 5900000;
    sense_trace_feed(&tr, &v.sched);
    CHECK_EQ(held_data(), held);
    /* The voltage after it is its own, not the two together. */
    poke((uint16_t)(v.sched.acc[SENSE_SRC_CH1].n_v + 1u),
         v.sched.acc[SENSE_SRC_CH1].v_sum + 5800000);
    CHECK_EQ(held_data(), held + 2u);
    CHECK_EQ(rec(held)->meta >> 24, SENSE_TRACE_CURRENT);
    CHECK_EQ(rec(held + 1u)->v, 5800);
    /* A full ring that drops the sample drops its voltage with it. */
    rig_size(8u);
    for (unsigned k = 0; k < 8u; ++k) {
        tick();
    }
    CHECK_EQ(sense_trace_held(&tr), 8u);
    const uint32_t lost = tr.src.lost;
    poke((uint16_t)(v.sched.acc[SENSE_SRC_CH1].n_v + 1u),
         v.sched.acc[SENSE_SRC_CH1].v_sum + 5800000);
    CHECK_EQ(tr.src.lost, lost + 1u);
}

/* A ring of 8 with nobody reading it, fed @p n CH1 samples. */
static void fill(unsigned n)
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
        sense_svc_step(&v, &cmd, &snap);
        /* The voltage's read is left out: samples only. */
        v.sched.acc[SENSE_SRC_CH1].n_v = 0u;
        tr.src.n_v = 0u;
        sense_trace_feed(&tr, &v.sched);
        g_us += 1000u;
        ++fed;
    }
}

TEST_CASE(a_ring_full_by_exactly_0_1_and_many_records)
{
    fill(7u);
    CHECK_EQ(sense_trace_held(&tr), 7u);
    CHECK_EQ(tr.src.lost, 0u);
    fill(8u);                       /* full, none over */
    CHECK_EQ(sense_trace_held(&tr), 8u);
    CHECK_EQ(tr.src.lost, 0u);
    fill(9u);                       /* 1 over */
    CHECK_EQ(sense_trace_held(&tr), 8u);
    CHECK_EQ(tr.src.lost, 1u);
    fill(8u + 5000u);               /* many over */
    CHECK_EQ(sense_trace_held(&tr), 8u);
    CHECK_EQ(tr.src.lost, 5000u);
    /* The records kept are the first 8: a full ring drops the newest. */
    for (unsigned k = 0u; k < 8u; ++k) {
        CHECK_EQ(rec(k)->t, 200010u + 10u * k);
    }
}

TEST_CASE(the_dropped_records_are_named_where_they_are_missing)
{
    fill(8u + 3u);
    /* A trace reads the ring out; the next sample kept carries the 3. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_KEY, g_us, 0u, 0u);
    for (unsigned k = 0; k < 20u; ++k) {
        pass();
    }
    run(5u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.lost, 3u);
    CHECK_EQ(seen.z_l, 3u);
    CHECK_EQ(seen.n_s, 8u + 5u);
    CHECK_EQ(seen.z_s, 8u + 5u);
    /* The gap shows in the times: 8 samples, 3 missing, then the rest. */
    CHECK_EQ(seen.t[7], 200010u + 70u);
    CHECK_EQ(seen.t[8], 200010u + 110u);
    CHECK(strstr(g_log, "10,300\r\n$L n=3\r\n40,300\r\n") != NULL);
}

TEST_CASE(each_gap_in_a_trace_has_its_line)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    /* Two stalls of 20 ms on a ring of 8, a second apart. */
    for (unsigned gap = 0u; gap < 2u; ++gap) {
        for (unsigned k = 0; k < 20u; ++k) {
            tick();
            pass_as(0u, true);
        }
        run(1000u);
    }
    sense_trace_key(&tr, 'x', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_l, 2u);
    CHECK(seen.lost >= 24u && seen.lost <= 28u);
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK_EQ(seen.z_s, seen.n_s);
}

TEST_CASE(a_count_of_dropped_records_saturates)
{
    fill(8u);
    tr.src.lost = SENSE_TRACE_LOST_MAX - 1u;
    sense_svc_step(&v, &cmd, &snap);
    sense_trace_feed(&tr, &v.sched);
    g_us += 1000u;
    CHECK_EQ(tr.src.lost, SENSE_TRACE_LOST_MAX);
    sense_svc_step(&v, &cmd, &snap);
    sense_trace_feed(&tr, &v.sched);
    CHECK_EQ(tr.src.lost, SENSE_TRACE_LOST_MAX);
}

/* ------------------------------------------------------------- core 0 */

TEST_CASE(idle_keeps_the_newest_records)
{
    rig();
    run(1000u);
    CHECK_EQ(g_len, 0u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
    const uint32_t tail = (uint32_t)atomic_load(&tr.tail);
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    CHECK_EQ(g_ring[(head - 1u) & tr.mask].t, 200000u + 9990u);
    CHECK(g_ring[tail & tr.mask].t >= 200000u + 9990u - 630u);
    /* A ring smaller than twice that keeps half of itself. */
    rig_size(16u);
    run(100u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
}

TEST_CASE(the_console_command_gives_ten_seconds)
{
    rig();
    run(500u);
    sense_trace_key(&tr, 't', g_us);
    CHECK(sense_trace_active(&tr));
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    run(10100u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK(!g_partial);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.n_h, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.ver, SENSE_TRACE_FORMAT);
    CHECK_EQ(seen.id, 1u);
    CHECK_STR_EQ(seen.trig, "key");
    CHECK_EQ(seen.t0, t0);
    CHECK_EQ(seen.ms0, t0 / 10u);
    CHECK_EQ(seen.len, SENSE_TRACE_KEY_MS);
    CHECK_EQ(seen.shunt, I3221_UOHM);
    CHECK_EQ(seen.cfg, v.sched.i3221.config);
    CHECK_EQ(seen.on, 1u);
    CHECK_EQ(seen.rst, 0u);
    CHECK_EQ(seen.mark_t[0], t0);
    /* The samples from before the trigger, then 10 s of them. */
    CHECK(seen.first_dt < 0 && seen.first_dt >= -640);
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 10000u);
    CHECK_EQ(seen.t[0], t0 + (uint32_t)seen.first_dt);
    for (unsigned k = 1u; k < seen.n_s; ++k) {
        if (seen.t[k] - seen.t[k - 1u] != 10u || seen.code[k] != 300) {
            T_FAIL("sample %u at %lu reads %ld", k, (unsigned long)seen.t[k],
                   (long)seen.code[k]);
            break;
        }
    }
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 100000u - 10u);
    CHECK_EQ(seen.mv_last, 6000);
    CHECK_EQ(seen.z_id, 1u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_v, seen.n_v);
    CHECK(seen.n_v >= 500u && seen.n_v <= 504u);
    CHECK_EQ(seen.z_l, 0u);
    CHECK_EQ(seen.z_m, 1u);
    CHECK_EQ(seen.z_ml, 0u);
    CHECK_EQ(seen.z_e, 't');
    /* The lines as written. */
    char want[160];
    snprintf(want, sizeof(want),
             "$T v=1 n=1 trig=key t=%lu ms=%lu len=10000\r\n"
             "$H dt_us=1000 shunt_uohm=100000 cfg=0x%04X on=1 rst=0\r\n",
             (unsigned long)t0, (unsigned long)(t0 / 10u),
             (unsigned)v.sched.i3221.config);
    CHECK(strncmp(g_log, want, strlen(want)) == 0);
    /* Idle again: the newest records are kept for the next one. */
    run(200u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
}

TEST_CASE(a_command_and_an_edge_give_four_seconds)
{
    rig();
    run(300u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    /* The frame that carries the pulse starts 12.3 ms ahead. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 12300u, 3u, 1900u);
    run(4100u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_STR_EQ(seen.trig, "cmd");
    CHECK_EQ(seen.t0, t0 + 123u);
    CHECK_EQ(seen.len, SENSE_TRACE_EDGE_MS);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_ch, 3u);
    CHECK_EQ(seen.mark_us, 1900u);
    CHECK_EQ(seen.mark_t[0], t0 + 123u);
    /* The last sample is the last before the end: 4000 ms after the
     * frame's start. */
    CHECK(seen.t[seen.n_s - 1u] < t0 + 123u + 40000u);
    CHECK(seen.t[seen.n_s - 1u] >= t0 + 123u + 40000u - 10u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_e, 't');

    g_len = 0u;
    const uint32_t t1 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(4100u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.id, 2u);
    CHECK_STR_EQ(seen.trig, "edge");
    CHECK_EQ(seen.n_e, 1u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t1 + 40000u - 10u);
    CHECK_EQ(seen.z_id, 2u);
    CHECK_EQ(seen.z_s, seen.n_s);
}

TEST_CASE(a_trigger_during_a_trace_moves_its_end_and_never_back)
{
    rig();
    run(300u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u, 1100u);
    run(3999u);
    CHECK(sense_trace_active(&tr));
    /* 1 ms before its end: a second command, and 4 s more. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u, 1900u);
    run(3999u);
    CHECK(sense_trace_active(&tr));
    run(10u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_c, 2u);
    CHECK_EQ(seen.mark_t[1], t0 + 39990u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 39990u + 40000u - 10u);
    CHECK_EQ(seen.z_m, 2u);
    CHECK_EQ(seen.z_s, seen.n_s);

    /* A 10 s trace is not cut short by a command 1 s into it. */
    g_len = 0u;
    const uint32_t t1 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 'T', g_us);
    run(1000u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u, 1100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us + 2000u, 0u, 0u);
    run(9100u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.n_e, 1u);
    CHECK_EQ(seen.mark_t[2], t1 + 10000u + 20u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t1 + 100000u - 10u);
    CHECK_EQ(seen.z_m, 3u);
}

TEST_CASE(a_slots_pulse_is_a_command_after_it_held_still)
{
    rig();
    uint64_t us = g_us;
    /* The first pulse seen is where the watch starts. */
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 1500u, us));
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 1500u, us + 1000u));
    /* The first change is a command, whenever it comes. */
    us += 2000u;
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1900u, us));
    /* 49.9 ms after it: the same command still slewing. */
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 1890u, us + 49900u));
    /* 50.0 ms after that one: a command of its own. */
    us += 49900u;
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1100u, us + 50000u));
    us += 50000u;
    /* And 50.1 ms. */
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1900u, us + 50100u));
    us += 50100u;
    /* The bank letting go is no command; the pulse after it is one once
     * the slot has held still. */
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 0u, us + 100000u));
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1500u, us + 200000u));
    us += 200000u;
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 0u, us + 1000u));
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 1500u, us + 2000u));
    /* Each slot is watched on its own; one past the last is none. */
    CHECK(!sense_trace_pulse(&tr, 7u, 5u, 1500u, us));
    CHECK(sense_trace_pulse(&tr, 7u, 5u, 1501u, us));
    CHECK(!sense_trace_pulse(&tr, SENSE_TRACE_SLOTS, 5u, 1500u, us));
    CHECK(!sense_trace_pulse(&tr, SENSE_TRACE_SLOTS, 5u, 1501u, us));
    /* The hold across the 0.1 ms count's wrap. */
    us = ((uint64_t)1u << 32) * 100u - 20000u;
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1500u, us) == false);
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1600u, us));
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1700u, us + 49900u));
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1800u, us + 49900u + 50000u));
}

TEST_CASE(a_slewed_command_is_one_line_and_holds_the_trace_open)
{
    rig();
    run(300u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1100u, g_us));
    /* 800 us of travel at 1 us a millisecond: a change every pass. */
    unsigned commands = 0u;
    for (unsigned k = 1u; k <= 800u; ++k) {
        if (sense_trace_pulse(&tr, 0u, 5u, (uint16_t)(1100u + k), g_us)) {
            sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u,
                                (uint16_t)(1100u + k));
            ++commands;
        }
        run(1u);
    }
    CHECK_EQ(commands, 1u);
    /* Open 4 s past the last change, not past the first.  The slot
     * renders its last pulse in every pass. */
    for (unsigned k = 0; k < 3990u; ++k) {
        CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1900u, g_us));
        run(1u);
    }
    CHECK(sense_trace_active(&tr));
    run(20u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_us, 1101u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 7990u + 40000u - 10u);
    /* Where the slew ended, once the slot had held still: one line, at
     * the last change, with the pulse it ended at. */
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_ch, 5u);
    CHECK_EQ(seen.dest_us, 1900u);
    CHECK_EQ(seen.dest_t, t0 + 7990u);
    CHECK_EQ(seen.z_m, 2u);
    CHECK(strstr(g_log, "$D t=") > strstr(g_log, "$C t="));
    /* A slew that is let go before it held still ends where it was, and
     * one a new command follows ends before that command's line. */
    g_len = 0u;
    sense_trace_key(&tr, 't', g_us);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1500u, g_us) == false);
    run(100u);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1510u, g_us));
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1520u, g_us));
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 0u, g_us));
    run(10u);
    parse();
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_us, 1520u);
    CHECK_EQ(seen.dest_ch, 6u);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1600u, g_us + 100000u));
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1610u, g_us + 101000u));
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1100u, g_us + 151000u));
    run(10u);
    parse();
    CHECK_EQ(seen.n_d, 2u);
    CHECK_EQ(seen.dest_us, 1610u);
    /* A step is no slew: no $D line. */
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1100u, g_us + 400000u));
    run(10u);
    parse();
    CHECK_EQ(seen.n_d, 2u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    /* With no trace under way a slew starts none. */
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1500u, g_us));
    CHECK(sense_trace_pulse(&tr, 1u, 5u, 1501u, g_us));
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1502u, g_us + 1000u));
    CHECK(!sense_trace_active(&tr));
}

/* A trace of 4 s from an edge at @p edge_us, the clock 300 ms before it. */
static void trace_at(uint64_t edge_us)
{
    rig();
    g_us = edge_us - 300000u;
    run(300u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, edge_us, 0u, 0u);
    run(4100u);
    parse();
}

TEST_CASE(a_trace_runs_across_the_millisecond_ticks_wrap)
{
    /* The edge 1 ms before the tick wraps at 2^32 ms. */
    const uint64_t wrap_us = ((uint64_t)1u << 32) * 1000u;
    trace_at(wrap_us - 1000u);
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.ms0, 4294967295u);
    CHECK_EQ(seen.t0, (uint32_t)((wrap_us - 1000u) / 100u));
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 40000u - 10u);
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 4000u);
    /* At the wrap, and 1 ms past it. */
    trace_at(wrap_us);
    CHECK_EQ(seen.ms0, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 40000u - 10u);
    trace_at(wrap_us + 1000u);
    CHECK_EQ(seen.ms0, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 40000u - 10u);
}

TEST_CASE(a_trace_runs_across_the_tenth_millisecond_counts_wrap)
{
    /* The count the lines carry wraps at 2^32 * 0.1 ms: 1 s into this
     * trace, at its first sample, and 1 ms before its end. */
    const uint64_t wrap_us = ((uint64_t)1u << 32) * 100u;
    const uint64_t at[] = { wrap_us - 1000000u, wrap_us - 100u, wrap_us,
                            wrap_us - 3999000u, wrap_us - 4000000u };
    for (unsigned k = 0u; k < sizeof(at) / sizeof(at[0]); ++k) {
        trace_at(at[k]);
        CHECK(!sense_trace_active(&tr));
        CHECK_EQ(seen.n_bad, 0u);
        CHECK_EQ(seen.t0, (uint32_t)(at[k] / 100u));
        CHECK_EQ(seen.n_z, 1u);
        CHECK_EQ(seen.z_s, seen.n_s);
        const unsigned pre = (unsigned)(-seen.first_dt + 9) / 10u;
        CHECK_EQ(seen.n_s, pre + 4000u);
        for (unsigned j = 1u; j < seen.n_s; ++j) {
            if (seen.t[j] - seen.t[j - 1u] != 10u) {
                T_FAIL("case %u: sample %u is not 1 ms on", k, j);
                break;
            }
        }
        CHECK((uint32_t)(seen.t[seen.n_s - 1u] - (uint32_t)seen.t0)
              >= 40000u - 10u);
        CHECK((uint32_t)(seen.t[seen.n_s - 1u] - (uint32_t)seen.t0) < 40000u);
    }
}

TEST_CASE(the_pump_gives_whole_lines_into_the_room_it_has)
{
    /* Every room from none to more than a line, one pass a millisecond:
     * no pump hands back part of a line or more than its room. */
    for (size_t room = 0u; room <= 70u; ++room) {
        rig();
        run(100u);
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 15u, 2000u);
        for (unsigned k = 0; k < 300u; ++k) {
            tick();
            pass_as(room, true);
        }
        CHECK(!g_partial);
        CHECK(g_most <= room);
        parse();
        CHECK_EQ(seen.n_bad, 0u);
        /* The header's lines are the longest: under their length nothing
         * is written, and nothing is lost by waiting. */
        if (room < strlen("$T v=1 n=1 trig=cmd t=201000 ms=20100 "
                          "len=4000\r\n")) {
            CHECK_EQ(g_len, 0u);
        } else if (room >= 60u) {
            CHECK_EQ(seen.n_t, 1u);
            CHECK_EQ(seen.n_h, 1u);
            CHECK_EQ(seen.n_c, 1u);
            CHECK(seen.n_s > 300u);
        }
        CHECK(sense_trace_active(&tr));
    }
}

TEST_CASE(a_console_with_no_room_loses_samples_and_says_how_many)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    run(1000u);
    /* The console takes nothing for 6 s: the ring fills after about
     * 3.9 s and the rest is dropped.  Nothing waits: every tick returns. */
    for (unsigned k = 0; k < 6000u; ++k) {
        tick();
        pass_as(0u, true);
    }
    CHECK_EQ(sense_trace_held(&tr), RING);
    CHECK(tr.src.lost > 2000u);
    run(3200u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_v, seen.n_v);
    /* What is written and what is missing are the 10 s: 10000 samples.
     * A dropped sample's voltage is not written and not counted; a
     * voltage that found the ring full behind its sample is counted. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK(seen.lost > 2000u);
    CHECK(seen.n_s + seen.lost >= pre + 10000u);
    CHECK(seen.n_s + seen.lost <= pre + 10000u + 4u);
    /* The times run on across the gap. */
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 100000u - 10u);
}

TEST_CASE(a_console_with_no_room_across_the_end_ends_the_trace_there)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(3000u);
    /* No room from 1 s before the end to 1 s after it: the ring holds
     * the 2 s, and the samples past the end are not the trace's. */
    for (unsigned k = 0; k < 2000u; ++k) {
        tick();
        pass_as(0u, true);
    }
    CHECK(sense_trace_active(&tr));
    CHECK_EQ(tr.src.lost, 0u);
    run(400u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_l, 0u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 40000u - 10u);
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 4000u);
}

TEST_CASE(a_console_that_is_not_connected_drops_the_trace)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    for (unsigned k = 0; k < 5000u; ++k) {
        tick();
        pass_as(SENSE_TRACE_LINE_MAX, false);
    }
    CHECK_EQ(g_len, 0u);
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(tr.out.abandoned, 1u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
    CHECK_EQ(tr.src.lost, 0u);
    /* A trace under way when the console goes: dropped with no line, and
     * the next one starts with a header of its own. */
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    const size_t len = g_len;
    CHECK(len > 0u);
    tick();
    pass_as(SENSE_TRACE_LINE_MAX, false);
    CHECK_EQ(g_len, len);
    CHECK_EQ(tr.out.abandoned, 2u);
    g_len = 0u;
    sense_trace_key(&tr, 't', g_us);
    run(10100u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.id, 3u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_l, 0u);
}

TEST_CASE(the_trigger_queue_full_by_0_and_1)
{
    rig();
    run(100u);
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + k * 100u,
                            (uint16_t)k, 1500u);
    }
    CHECK_EQ(tr.out.n_mlost, 0u);
    run(50u);
    sense_trace_key(&tr, 'x', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, SENSE_TRACE_MARKS);
    CHECK_EQ(seen.z_m, SENSE_TRACE_MARKS);
    CHECK_EQ(seen.z_ml, 0u);
    CHECK_EQ(seen.z_e, 'k');

    g_len = 0u;
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS + 1u; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us + k * 100u, 0u,
                            0u);
    }
    CHECK_EQ(tr.out.n_mlost, 1u);
    run(50u);
    /* Room again: a later trigger has its line. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_KEY, g_us, 0u, 0u);
    run(50u);
    sense_trace_key(&tr, 'X', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_e, SENSE_TRACE_MARKS);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.z_m, SENSE_TRACE_MARKS + 1u);
    CHECK_EQ(seen.z_ml, 1u);
}

TEST_CASE(the_consoles_stop_and_other_characters)
{
    rig();
    run(100u);
    /* No trace: a stop is nothing, and so is any other character. */
    sense_trace_key(&tr, 'x', g_us);
    sense_trace_key(&tr, 'q', g_us);
    sense_trace_key(&tr, '\n', g_us);
    sense_trace_key(&tr, -1, g_us);
    run(10u);
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(g_len, 0u);
    sense_trace_key(&tr, 't', g_us);
    run(500u);
    sense_trace_key(&tr, 'q', g_us);
    run(10u);
    CHECK(sense_trace_active(&tr));
    sense_trace_key(&tr, 'x', g_us);
    run(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The stop is not kept for the next trace. */
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    CHECK(sense_trace_active(&tr));
}

TEST_CASE(a_changed_set_up_ends_a_trace)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(1000u);
    /* Another shunt: the codes after it are not the header's. */
    ++cmd.cfg_gen;
    cmd.parts.ina3221_shunt_uohm = 50000u;
    i3221->shunt_ohm = 0.05;
    run(5u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.shunt, I3221_UOHM);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The next trace carries the new one: 0.12 A is 150 steps of 0.8 mA. */
    g_len = 0u;
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(300u);
    parse();
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.code[seen.n_s - 1u], 150);
    /* The bus closed: the same. */
    ++cmd.cfg_gen;
    cmd.parts.ina3221_en = false;
    run(5u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.z_e, 's');
}

TEST_CASE(a_part_offline_and_a_reset_are_said_in_the_trace)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(1000u);
    /* The part stops answering: offline after 3 failed reads. */
    i3221->present = false;
    run(500u);
    parse();
    CHECK_EQ(seen.n_st, 1u);
    CHECK_EQ(seen.st_on, 0u);
    CHECK_EQ(seen.st_rst, 0u);
    const unsigned n_s = seen.n_s;
    /* And answers again from its next probe. */
    i3221->present = true;
    run(1500u);
    parse();
    CHECK_EQ(seen.n_st, 2u);
    CHECK_EQ(seen.st_on, 1u);
    CHECK(seen.n_s > n_s);
    /* A reset of its own: found, counted, set up again. */
    fake_reset3221(i3221);
    run(2000u);
    parse();
    CHECK_EQ(seen.st_rst, 1u);
    CHECK_EQ(seen.st_on, 1u);
    CHECK(seen.n_st >= 4u);
    run(6000u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The time across the outage is the samples' own. */
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 100000u - 10u);
    /* Idle after a trace that held state records: the newest records
     * stay, as before it. */
    run(100u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
}

TEST_CASE(a_trace_with_no_bus_has_a_header_and_ends_on_time)
{
    rig();
    cmd.parts.ina3221_en = false;
    run(200u);
    CHECK(!v.open);
    sense_trace_key(&tr, 't', g_us);
    /* With no record to end it, the end line waits
     * SENSE_TRACE_END_WAIT_MS past the end. */
    run(10000u + SENSE_TRACE_END_WAIT_MS - 1u);
    CHECK(sense_trace_active(&tr));
    run(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_h, 1u);
    CHECK_EQ(seen.shunt, 0u);
    CHECK_EQ(seen.cfg, 0u);
    CHECK_EQ(seen.on, 0u);
    CHECK_EQ(seen.n_s, 0u);
    CHECK_EQ(seen.z_s, 0u);
    CHECK_EQ(seen.z_v, 0u);
    CHECK_EQ(seen.z_e, 't');
}

/* The console takes nothing for @p ms. */
static void stall(unsigned ms)
{
    for (unsigned k = 0; k < ms; ++k) {
        tick();
        pass_as(0u, true);
    }
}

TEST_CASE(a_stop_under_a_backlog_writes_what_came_before_it)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* 500 ms of samples wait in the ring when the stop comes. */
    stall(500u);
    sense_trace_key(&tr, 'x', g_us);
    CHECK(sense_trace_active(&tr));
    /* A second stop and a trigger after it move nothing. */
    stall(50u);
    sense_trace_key(&tr, 'x', g_us);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(200u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.z_l, 0u);
    CHECK_EQ(seen.z_s, seen.n_s);
    /* Every sample up to the stop, 600 ms after the trigger, and none
     * after it. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 600u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 6000u - 10u);
}

TEST_CASE(a_stop_on_a_full_ring_counts_what_was_dropped_before_it)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    /* 30 ms with no room on a ring of 8: records are dropped, and the
     * count of them rides on the first record kept after the stop. */
    stall(30u);
    CHECK(tr.src.lost > 20u);
    sense_trace_key(&tr, 'x', g_us);
    run(20u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.n_l, 1u);
    CHECK(seen.lost > 20u);
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The $L line stands before the end line, after the last sample. */
    const char *l_at = strstr(g_log, "$L n=");
    const char *z_at = strstr(g_log, "$Z n=");
    CHECK(l_at != NULL && z_at != NULL && l_at < z_at);
    CHECK(strchr(l_at, ',') == NULL);
}

TEST_CASE(a_changed_set_up_under_a_backlog_ends_the_trace_where_it_changed)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* 300 ms wait in the ring; then another shunt, and 100 ms more. */
    stall(300u);
    ++cmd.cfg_gen;
    cmd.parts.ina3221_shunt_uohm = 50000u;
    i3221->shunt_ohm = 0.05;
    stall(100u);
    for (unsigned k = 0; k < 300u && sense_trace_active(&tr); ++k) {
        tick();
        pass();
    }
    CHECK(!sense_trace_active(&tr));
    /* A trigger in the pass the trace ended in, before the pump has run
     * idle: the set-up's records went with the trace they ended. */
    const size_t first_len = g_len;
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(strstr(&g_log[first_len], "shunt_uohm=50000 ") != NULL);
    CHECK(strstr(&g_log[first_len], " e=k\r\n") != NULL);
    g_log[first_len] = '\0';
    g_len = first_len;
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.shunt, I3221_UOHM);
    /* The 400 ms before the change, each at the header's scale, and no
     * sample of the new set-up. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 400u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 4000u - 10u);
    for (unsigned k = 0u; k < seen.n_s; ++k) {
        if (seen.code[k] != 300) {
            T_FAIL("sample %u reads %ld", k, (long)seen.code[k]);
            break;
        }
    }
    /* The next trace has the new set-up in its header and its samples
     * at its scale: none from before the change. */
    g_len = 0u;
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.z_e, 'k');
    for (unsigned k = 0u; k < seen.n_s; ++k) {
        if (seen.code[k] != 150) {
            T_FAIL("sample %u reads %ld", k, (long)seen.code[k]);
            break;
        }
    }
}

TEST_CASE(a_state_change_under_a_backlog_keeps_its_place)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* 200 ms wait in the ring when the part stops answering; it is
     * offline 3 failed reads later. */
    stall(200u);
    i3221->present = false;
    stall(300u);
    run(600u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_st, 1u);
    CHECK_EQ(seen.st_on, 0u);
    /* The $S line stands after the last sample taken before the outage:
     * 300 ms of them after the trigger. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.st_at, pre + 300u);
    CHECK_EQ(seen.n_s, pre + 300u);
}

TEST_CASE(a_set_up_record_with_no_room_is_written_when_there_is_room)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    stall(20u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
    /* The ring is full when the set-up changes. */
    ++cmd.cfg_gen;
    cmd.parts.ina3221_shunt_uohm = 50000u;
    i3221->shunt_ohm = 0.05;
    stall(5u);
    CHECK(tr.src.shunt_owed);
    CHECK_EQ(held_of(SENSE_TRACE_SHUNT), 0u);
    run(20u);
    CHECK(!tr.src.shunt_owed && !tr.src.cfg_owed);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK(seen.lost > 0u);
    g_len = 0u;
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    parse();
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.code[seen.n_s - 1u], 150);
}

TEST_CASE(idle_keeps_no_record_from_before_a_set_up)
{
    rig();
    run(500u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
    ++cmd.cfg_gen;
    cmd.parts.ina3221_shunt_uohm = 50000u;
    i3221->shunt_ohm = 0.05;
    run(10u);
    /* 10 samples of the new set-up, and the voltage read with them. */
    CHECK(sense_trace_held(&tr) >= 10u && sense_trace_held(&tr) <= 11u);
    CHECK_EQ(held_of(SENSE_TRACE_SHUNT) + held_of(SENSE_TRACE_CFG), 0u);
    sense_trace_key(&tr, 't', g_us);
    run(50u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.first_dt, -100);
    CHECK_EQ(seen.code[0], 150);
    CHECK_EQ(seen.z_e, 'k');
}

TEST_CASE(a_pulse_is_the_one_the_frame_can_carry)
{
    /* 560 Hz: a frame of 1785 counts, the counter wrapping at 1784. */
    CHECK_EQ(sense_trace_rendered(2000u, 1784u), 1784u);
    CHECK_EQ(sense_trace_rendered(1785u, 1784u), 1784u);
    CHECK_EQ(sense_trace_rendered(1784u, 1784u), 1784u);
    CHECK_EQ(sense_trace_rendered(1783u, 1784u), 1783u);
    CHECK_EQ(sense_trace_rendered(0u, 1784u), 0u);
    /* 50 Hz, and a counter's top above 16 bits. */
    CHECK_EQ(sense_trace_rendered(2000u, 19999u), 2000u);
    CHECK_EQ(sense_trace_rendered(65535u, 70000u), 65535u);
    /* Two endpoints past the frame render the same pulse: no command. */
    rig();
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, sense_trace_rendered(1900u, 1784u),
                             g_us));
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, sense_trace_rendered(2000u, 1784u),
                             g_us + 100000u));
    CHECK(sense_trace_pulse(&tr, 0u, 5u, sense_trace_rendered(1500u, 1784u),
                            g_us + 200000u));
}

TEST_CASE(every_line_at_its_longest_fits)
{
    rig();
    /* A clipped sample at the end of the range, and the longest numbers
     * a header, a trigger and an end line carry. */
    i3221->amps[0] = 5.0;
    g_us = ((uint64_t)UINT32_MAX - 30000u) * 100u;
    run(100u);
    v.sched.i3221.shunt_uohm = UINT32_MAX;
    run(5u);
    tr.out.id = 65534u;
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE,
                        (uint64_t)UINT32_MAX * 1000u, 0u, 0u);
    tr.out.t0 = UINT32_MAX;
    tr.out.len_ms = 123456u;
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD,
                        (uint64_t)UINT32_MAX * 100u, 65535u, 65535u);
    /* A slew's end at the longest numbers. */
    tr.out.watch[0].have = tr.out.watch[0].changed = true;
    tr.out.watch[0].slewed = true;
    tr.out.watch[0].pulse = 65535u;
    tr.out.watch[0].changed_t = UINT32_MAX;
    (void)sense_trace_pulse(&tr, 0u, 65535u, 65535u,
                            (uint64_t)(SENSE_TRACE_HOLD_MS * 10u) * 100u);
    run(20u);
    tr.out.n_s = tr.out.n_v = tr.out.n_lost = UINT32_MAX - 1u;
    tr.out.n_m = tr.out.n_mlost = UINT32_MAX;
    sense_trace_key(&tr, 'x', g_us);
    run(2u);
    CHECK(strstr(g_log, "$T v=1 n=65535 trig=edge t=4294967295 "
                        "ms=4294967295 len=99999\r\n") != NULL);
    CHECK(strstr(g_log, "$C t=4294967295 ch=65535 us=65535\r\n") != NULL);
    CHECK(strstr(g_log, "$D t=4294967295 ch=65535 us=65535\r\n") != NULL);
    CHECK(strstr(g_log, "$E t=") != NULL);
    CHECK(strstr(g_log, "$Z n=65535 s=99999999 v=9999999 l=99999999 m=9999 "
                        "ml=9999 e=k\r\n") != NULL);
    CHECK(strstr(g_log, "$H dt_us=1000 shunt_uohm=4294967295 cfg=0x") != NULL);
    parse();
    CHECK_EQ(seen.n_bad, 0u);          /* none past SENSE_TRACE_LINE_MAX */
    CHECK(!g_partial);
}

TEST_CASE(a_clipped_sample_reads_the_end_of_the_range)
{
    rig();
    i3221->amps[0] = 5.0;
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    i3221->amps[0] = -5.0;
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.code[0], 4094);
    CHECK_EQ(seen.code[seen.n_s - 1u], -4094);
}

TEST_CASE(the_code_is_the_one_the_current_came_from)
{
    static const uint32_t k_shunt[] = { 200u, 1000u, 10000u, 30000u, 50000u,
                                        100000u, 330000u, 1000000u,
                                        20000000u };
    for (unsigned k = 0u; k < sizeof(k_shunt) / sizeof(k_shunt[0]); ++k) {
        for (int32_t code = -4095; code <= 4094; ++code) {
            const sense_value_t ua = ina3221_current_ua(k_shunt[k], code);
            if (sense_trace_code(ua.value, k_shunt[k]) != code) {
                T_FAIL("shunt %lu code %ld comes back as %ld",
                       (unsigned long)k_shunt[k], (long)code,
                       (long)sense_trace_code(ua.value, k_shunt[k]));
                break;
            }
        }
    }
    CHECK_EQ(sense_trace_code(120000, 0u), 0);
}

int main(void)
{
    RUN(a_ring_is_a_power_of_two);
    RUN(every_ch1_sample_and_voltage_is_taken_once);
    RUN(the_feed_adds_no_bus_transaction_and_reads_no_clock);
    RUN(a_tick_with_no_sample_feeds_none);
    RUN(a_set_up_and_a_closed_bus_lose_and_repeat_nothing);
    RUN(a_voltage_is_taken_from_a_window_a_reset_emptied);
    RUN(a_set_up_that_leaves_the_history_at_the_same_place);
    RUN(a_voltage_is_told_from_the_one_before_by_window_and_count);
    RUN(a_voltage_with_no_current_sample_of_its_tick_is_left_out);
    RUN(a_ring_full_by_exactly_0_1_and_many_records);
    RUN(the_dropped_records_are_named_where_they_are_missing);
    RUN(each_gap_in_a_trace_has_its_line);
    RUN(a_count_of_dropped_records_saturates);
    RUN(idle_keeps_the_newest_records);
    RUN(the_console_command_gives_ten_seconds);
    RUN(a_command_and_an_edge_give_four_seconds);
    RUN(a_trigger_during_a_trace_moves_its_end_and_never_back);
    RUN(a_slots_pulse_is_a_command_after_it_held_still);
    RUN(a_slewed_command_is_one_line_and_holds_the_trace_open);
    RUN(a_trace_runs_across_the_millisecond_ticks_wrap);
    RUN(a_trace_runs_across_the_tenth_millisecond_counts_wrap);
    RUN(the_pump_gives_whole_lines_into_the_room_it_has);
    RUN(a_console_with_no_room_loses_samples_and_says_how_many);
    RUN(a_console_with_no_room_across_the_end_ends_the_trace_there);
    RUN(a_console_that_is_not_connected_drops_the_trace);
    RUN(the_trigger_queue_full_by_0_and_1);
    RUN(the_consoles_stop_and_other_characters);
    RUN(a_changed_set_up_ends_a_trace);
    RUN(a_part_offline_and_a_reset_are_said_in_the_trace);
    RUN(a_trace_with_no_bus_has_a_header_and_ends_on_time);
    RUN(a_stop_under_a_backlog_writes_what_came_before_it);
    RUN(a_stop_on_a_full_ring_counts_what_was_dropped_before_it);
    RUN(a_changed_set_up_under_a_backlog_ends_the_trace_where_it_changed);
    RUN(a_state_change_under_a_backlog_keeps_its_place);
    RUN(a_set_up_record_with_no_room_is_written_when_there_is_room);
    RUN(idle_keeps_no_record_from_before_a_set_up);
    RUN(a_pulse_is_the_one_the_frame_can_carry);
    RUN(every_line_at_its_longest_fits);
    RUN(a_clipped_sample_reads_the_end_of_the_range);
    RUN(the_code_is_the_one_the_current_came_from);
    return test_summary("sense_trace");
}
