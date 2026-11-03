/*
 * Core 1's sensor service (sense_svc.h) on the I2C transaction queue
 * against the same service on the direct wire, tick by tick.
 *
 * Two services take the same orders.  One reads fake_ina.h's parts
 * directly, on a clock that stands still within a tick, as test_sense_svc
 * does.  The other reads a copy of the same parts through
 * shared/sense/sense_txq.c, the queue and the modelled bus of
 * i2c_txq_model.c, whose clock runs 1us at a time through each
 * transaction, its reads stamped with their ends.  The station's set-up:
 * an INA3221 at 0x40, CH1 on 44.4mOhm and FAST, and an INA228 at 0x45 on
 * 200uOhm.
 *
 * Under test: every snapshot field equal at every tick, and the same
 * transactions reaching the parts, through a set-up, 50ms windows, a NACK
 * part way through a tick, a part that holds SCL (the queue's 1000us
 * timeout) and the bus clear after it, a line held after a lost
 * arbitration, the INA3221 missing for 1.5s and back, and a second
 * set-up; FAST read on every tick the bus is open and not stuck, also on
 * the ticks of a probe and of a recovery; each tick's bus time inside its
 * 1ms.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "fake_ina.h"
#include "sense_svc.h"
#include "sense_txq_rig.h"

#define I228_ADDR  0x45u
#define I3221_ADDR 0x40u
#define T0         20000000u
#define FAST_REG   0x01u        /* CH1's shunt voltage (SBOS576C 8.6.2.2) */

/* One side: the parts, the service and what it handed back. */
typedef struct {
    fake_bus_t   fb;
    fake_part_t *i228, *i3221;
    sense_svc_t  svc;
    sense_snap_t snap;
    unsigned     recoveries;
} side_t;

static side_t    da;            /* direct */
static side_t    qa;            /* on the queue */
static txq_rig_t rig;
static uint64_t  g_us;          /* the direct side's clock */
static sense_cmd_t cmd;
static unsigned  g_fast_wire;   /* FAST's reads that reached the wire */

/* ------------------------------------------------------- the parts */

static void parts(side_t *s)
{
    sense_bus_t scratch;
    fake_bus_init(&s->fb, &scratch);
    s->i228  = fake_add(&s->fb, FAKE_INA228, I228_ADDR, 200e-6);
    s->i3221 = fake_add(&s->fb, FAKE_INA3221, I3221_ADDR, 44.4e-3);
    s->i228->volts[0]  = 16.8;
    s->i228->amps[0]   = 12.0;
    s->i228->degc      = 31.0;
    s->i3221->volts[0] = 6.0;
    s->i3221->amps[0]  = 0.12;
}

/* ----------------------------------------------------- direct side */

static uint64_t d_now(void *ctx)
{
    (void)ctx;
    return g_us;
}

static void d_recover(void *ctx)
{
    (void)ctx;
    da.recoveries++;
}

static bool d_open(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    return true;
}

static void d_close(void *ctx)
{
    (void)ctx;
}

static sense_err_t d_ask(void *ctx, uint8_t addr)
{
    fake_bus_t *b = ctx;
    const sense_err_t e = fake_fault(b);
    if (e != SENSE_OK) {
        return e;
    }
    return fake_find(b, addr) != NULL ? SENSE_OK : SENSE_NACK;
}

/* ------------------------------------------------- the queue side */

/* The parts behind the modelled bus: each transaction that reaches the
 * wire is the fake's read, write or ask, and its result the wire's. */
static model_answer_t bridge(void *ctx, const i2c_txq_xfer_t *x, uint8_t *rx)
{
    fake_bus_t *b = ctx;
    sense_err_t e;

    if (x->addr == I3221_ADDR && x->wr_len == 1u && x->wr[0] == FAST_REG) {
        g_fast_wire++;
    }
    if (x->wr_len == 0u) {
        e = d_ask(b, x->addr);
    } else if (x->rd_len > 0u) {
        e = fake_read(b, x->addr, x->wr[0], rx, x->rd_len);
    } else {
        e = fake_write(b, x->addr, x->wr[0], &x->wr[1],
                       (size_t)x->wr_len - 1u);
    }
    switch (e) {
    case SENSE_OK:      return MODEL_ANSWER;
    case SENSE_NACK:    return MODEL_NACK_ADDR;
    case SENSE_TIMEOUT: return MODEL_HANG;
    case SENSE_BUS_LOW: return MODEL_ARB_LOST;
    default:            return MODEL_NACK_DATA;
    }
}

static uint64_t q_now(void *ctx)
{
    const txq_rig_t *r = ctx;
    return r->m.now;
}

static uint64_t q_done(void *ctx)
{
    txq_rig_t *r = ctx;
    return sense_txq_done_us(&r->t);
}

static void q_recover(void *ctx)
{
    txq_rig_t *r = ctx;
    qa.recoveries++;
    sense_txq_recover(&r->t);
}

static sense_err_t q_ask(void *ctx, uint8_t addr)
{
    txq_rig_t *r = ctx;
    return sense_txq_ask(&r->t, addr);
}

/* ------------------------------------------------------------ rig */

static void setup(void)
{
    memset(&da, 0, sizeof(da));
    memset(&qa, 0, sizeof(qa));
    parts(&da);
    parts(&qa);
    g_us = T0;
    txq_rig_init(&rig, T0);
    rig.m.respond = bridge;
    rig.m.respond_ctx = &qa.fb;
    rig.m.arb_hold_us = 20u;            /* SDA held past the 10us settle */

    const sense_svc_io_t dio = {
        .sched = { { fake_read, fake_write, &da.fb }, d_now, d_recover,
                   &da.fb, NULL },
        .open = d_open, .close = d_close, .ask = d_ask,
    };
    const sense_svc_io_t qio = {
        .sched = { { sense_txq_read, sense_txq_write, &rig.t }, q_now,
                   q_recover, &rig, q_done },
        .open = d_open, .close = d_close, .ask = q_ask,
    };
    sense_svc_init(&da.svc, &dio);
    sense_svc_init(&qa.svc, &qio);

    memset(&cmd, 0, sizeof(cmd));
    cmd.cfg_gen = 1u;
    cmd.sda = 16u;
    cmd.scl = 17u;
    cmd.parts.ina228_en = true;
    cmd.parts.ina228_addr = I228_ADDR;
    cmd.parts.ina228_shunt_uohm = 200u;
    cmd.parts.ina228_max_ma = 204800u;
    sense_i3221_one(&cmd.parts.i3221, true, I3221_ADDR, 44400u, 1u);
    cmd.cap.rise_ua = SENSE_CAP_RISE_AUTO;
    cmd.cap.hold_ua = 120000;
    cmd.cap.move_ua = 30000;
    cmd.cap.band_ua = 50000;
}

/* What the ticks came to. */
static unsigned g_ticks, g_differ, g_first_differ, g_over_ms;
static uint32_t g_worst_us;
static unsigned g_fast_d, g_fast_q;     /* FAST reads, each side */

/* One tick on both sides at T0 + k ms. */
static void tick(unsigned k)
{
    const uint64_t t = T0 + 1000u * (uint64_t)k;
    g_us = t;
    if (rig.m.now < t) {
        rig.m.now = t;
    }
    const uint64_t q_start = rig.m.now;
    const unsigned fd = da.i3221->reads[FAST_REG];
    const unsigned fq = qa.i3221->reads[FAST_REG];
    memset(&da.snap, 0, sizeof(da.snap));
    memset(&qa.snap, 0, sizeof(qa.snap));
    sense_svc_step(&da.svc, &cmd, &da.snap);
    sense_svc_step(&qa.svc, &cmd, &qa.snap);
    const uint64_t took = rig.m.now - q_start;
    if (took > g_worst_us) {
        g_worst_us = (uint32_t)took;
    }
    if (rig.m.now >= t + 1000u) {
        g_over_ms++;
    }
    if (da.i3221->reads[FAST_REG] != fd) {
        g_fast_d++;
    }
    if (qa.i3221->reads[FAST_REG] != fq) {
        g_fast_q++;
    }
    if (memcmp(&da.snap, &qa.snap, sizeof(da.snap)) != 0
        || da.fb.transactions != qa.fb.transactions) {
        if (g_differ++ == 0u) {
            g_first_differ = k;
        }
    }
    g_ticks++;
}

/* A test current that moves each tick, the same on both sides. */
static void currents(unsigned k)
{
    const double a = 0.12 + 0.01 * (double)(k % 37u);
    da.i3221->amps[0] = a;
    qa.i3221->amps[0] = a;
    da.i228->amps[0] = 12.0 + 0.1 * (double)(k % 11u);
    qa.i228->amps[0] = da.i228->amps[0];
}

/* A fault @p after transactions on, on both sides. */
static void fault_at(unsigned after, sense_err_t e)
{
    da.fb.fail_at = da.fb.transactions + after;
    qa.fb.fail_at = qa.fb.transactions + after;
    da.fb.fail_with = e;
    qa.fb.fail_with = e;
}

/* The next @p n transactions fail with @p e, on both sides. */
static void fault_next(unsigned n, sense_err_t e)
{
    da.fb.fail_count = n;
    qa.fb.fail_count = n;
    da.fb.fail_with = e;
    qa.fb.fail_with = e;
}

/* FAST read on a tick with the part online before and after it, the bus
 * open and not stuck: the ticks FAST is owed. */
static unsigned g_owed, g_owed_missed;
static bool     g_was_on;

static void fast_owed(unsigned before_q)
{
    const bool on = da.snap.open && !da.snap.stuck
                    && da.snap.i3221[0] == SENSE_PART_ONLINE;
    if (g_was_on && on) {
        g_owed++;
        if (g_fast_wire == before_q) {
            g_owed_missed++;
        }
    }
    g_was_on = on;
}

/* --------------------------------------------------------------- cases */

TEST_CASE(the_queue_gives_every_snapshot_the_direct_wire_gives)
{
    setup();
    g_ticks = g_differ = g_first_differ = g_over_ms = 0u;
    g_worst_us = 0u;
    g_fast_d = g_fast_q = 0u;
    g_owed = g_owed_missed = 0u;
    g_was_on = false;
    unsigned errors = 0u, stuck_ticks = 0u, offline_ticks = 0u;

    for (unsigned k = 0; k < 4000u; ++k) {
        currents(k);
        switch (k) {
        case 300u:  fault_at(3u, SENSE_NACK);    break;
        case 700u:  fault_at(2u, SENSE_TIMEOUT); break;
        case 1100u: fault_at(4u, SENSE_BUS_LOW); break;
        case 1500u: da.i3221->present = qa.i3221->present = false; break;
        case 3000u: da.i3221->present = qa.i3221->present = true;  break;
        case 3500u: cmd.cfg_gen = 2u;            break;
        default:                                 break;
        }
        const unsigned before_q = g_fast_wire;
        tick(k);
        fast_owed(before_q);
        if (k < 3500u) {
            errors = da.snap.errors;
        }
        stuck_ticks += da.snap.stuck ? 1u : 0u;
        offline_ticks += (da.snap.i3221[0] != SENSE_PART_ONLINE) ? 1u : 0u;
    }
    if (g_differ != 0u) {
        printf("      first difference at tick %u\n", g_first_differ);
    }
    CHECK_EQ(g_ticks, 4000u);
    CHECK_EQ(g_differ, 0u);
    /* The faults happened, on both sides alike: the NACK, the timeout and
     * the held line one error each, the held line one recovery; the part
     * missing its 3 failed reads and its probes each 1s. */
    CHECK(errors >= 6u);
    CHECK(stuck_ticks >= 1u);
    CHECK(offline_ticks >= 1500u);
    CHECK_EQ(da.recoveries, 1u);
    CHECK_EQ(qa.recoveries, da.recoveries);
    CHECK_EQ(qa.i3221->reads[FAST_REG], da.i3221->reads[FAST_REG]);
    CHECK_EQ(g_fast_q, g_fast_d);
    /* FAST on every tick it is owed, the probe ticks among them. */
    CHECK(g_owed >= 1900u);
    CHECK_EQ(g_owed_missed, 0u);
    CHECK(da.snap.have_win);
    CHECK_EQ(da.snap.i3221[0], SENSE_PART_ONLINE);
}

/* A part that holds SCL for SENSE_STUCK_TIMEOUTS transactions: the
 * queue's two timeouts take 1000us each, the bus is stuck, the bus clear
 * runs (115us) and the parts come back -- on both sides alike, the times
 * aside: the queue's tick runs past its millisecond. */
TEST_CASE(two_timeouts_stick_the_bus_and_the_clear_brings_it_back)
{
    setup();
    g_ticks = g_differ = g_over_ms = 0u;
    g_worst_us = 0u;
    for (unsigned k = 0; k < 200u; ++k) {
        tick(k);
    }
    CHECK_EQ(g_differ, 0u);
    fault_next(SENSE_STUCK_TIMEOUTS, SENSE_TIMEOUT);
    const uint32_t clears = rig.q.st.clears;
    const uint32_t timeouts = rig.q.st.timeouts;
    tick(200u);
    CHECK(da.snap.stuck);
    CHECK(qa.snap.stuck);
    CHECK_EQ(rig.q.st.timeouts - timeouts, SENSE_STUCK_TIMEOUTS);
    CHECK(g_worst_us >= SENSE_STUCK_TIMEOUTS * I2C_TXQ_TIMEOUT_US);
    for (unsigned k = 201u; k < 1500u; ++k) {
        tick(k);
    }
    CHECK_EQ(qa.recoveries, da.recoveries);
    CHECK(qa.recoveries >= 1u);
    CHECK_EQ(rig.q.st.clears - clears, qa.recoveries);
    CHECK(!da.snap.stuck);
    CHECK(!qa.snap.stuck);
    CHECK_EQ(qa.snap.i3221[0], SENSE_PART_ONLINE);
    CHECK_EQ(qa.snap.i228, SENSE_PART_ONLINE);
    CHECK_EQ(qa.snap.errors, da.snap.errors);
    CHECK_EQ(qa.fb.transactions, da.fb.transactions);
    /* Past the fault's tick and the one after it, the snapshots agree. */
    memset(&da.snap, 0, sizeof(da.snap));
    g_differ = 0u;
    for (unsigned k = 1500u; k < 1700u; ++k) {
        tick(k);
    }
    CHECK_EQ(g_differ, 0u);
}

/* Core 1 parked by the flash lock-out through the last millisecond of a
 * window, from the start of FAST's read on: the read ends on the wire at
 * once, its end is taken 1000us late and stamped with the computed end,
 * so it counts in the window it was read in -- as the direct side counts
 * it.  Stamped with the time the wait returned, it would count in the
 * next. */
TEST_CASE(a_read_core_1_was_parked_through_counts_in_its_window)
{
    setup();
    g_differ = 0u;
    for (unsigned k = 0; k < 149u; ++k) {
        tick(k);
    }
    CHECK_EQ(g_differ, 0u);
    const uint32_t late = rig.q.st.late_stamps;
    rig.freeze_n = 1000u;
    tick(149u);
    CHECK_EQ(rig.q.st.late_stamps - late, 1u);
    tick(150u);
    const sense_window_t *wd = &da.snap.win[SENSE_CH(0, 1)];
    const sense_window_t *wq = &qa.snap.win[SENSE_CH(0, 1)];
    CHECK_EQ(wq->number, wd->number);
    CHECK_EQ(wd->n_i, 50u);
    CHECK_EQ(wq->n_i, wd->n_i);
    CHECK_EQ(qa.snap.ring[0].n_i, da.snap.ring[0].n_i);
}

int main(void)
{
    RUN(the_queue_gives_every_snapshot_the_direct_wire_gives);
    RUN(two_timeouts_stick_the_bus_and_the_clear_brings_it_back);
    RUN(a_read_core_1_was_parked_through_counts_in_its_window);
    return test_summary("sense_txq_svc");
}
