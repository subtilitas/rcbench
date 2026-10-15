/*
 * The limit run, against a modelled servo whose travel its registers limit.
 *
 * The rig: servo_sim's servo on a surface with two mechanical stops, its
 * LEFT ANGLE and RIGHT ANGLE turned into travel limits by kst_sim.h's
 * sim_travel() after every write, REVERSION choosing which register limits
 * which pulse side; the INA3221 as 50 ms windows of the servo's 1 ms
 * current; a bench that switches to SAFE and ARMED when asked; and a
 * programmer that answers CONNECT with the registers, stores a write and
 * reads it back, and gives the port back.
 *
 * Under test: the bisection converging on both sides with REVERSION off and
 * on, a register's side found by the run and never assumed; one side bound
 * and the other register put back; a servo bound at the smallest value the
 * hard rules take (FAIL, nothing written below it); a servo that never
 * binds (nothing written); the write cap, with and without a free value to
 * settle on; a write refused, and one lost on the line; every abort source
 * at every phase, the output asked off and RESTORE named once anything was
 * written; the 3.0 A ceiling, a clipped sample, 1.0 A for 400 ms, a supply
 * changed while a search drives the servo and left alone while the
 * programmer holds the port; no window for 199, 200 and 201 ms, also across
 * the 2^32 ms tick wrap; a whole run across the wrap; and the report.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "kst_sim.h"
#include "servo_limrun.h"
#include "servo_sim.h"

#define CENTRE  1500u
#define MIN_US  1000u
#define MAX_US  2000u
#define LO_RAW  1u
#define HI_RAW  255u

/* --- the rig --------------------------------------------------------------- */

enum { OP_NONE = 0, OP_CONNECT, OP_WRITE, OP_LEAVE };

typedef struct {
    servo_limrun_t run;
    servo_limrun_cfg_t cfg;
    servo_sim_t servo;
    uint8_t reg[KST_REG_COUNT];
    float zero_us, upc;          /* the model's unit (sim_travel())       */
    uint32_t now, t0;

    /* the bench */
    bool live, safe;
    int  bench_to;               /* -1 none, 0 to SAFE, 1 to ARMED        */
    uint32_t bench_at;
    uint32_t bench_ms;           /* how long it takes; 0xFFFFFFFF never   */
    uint16_t cmd;
    bool released, off;
    unsigned offs, releases, disarms, arms;

    /* the programmer */
    int  op;
    uint32_t op_at;
    uint16_t want[2];
    unsigned connects, write_ops, leaves, leave_asks, values;
    uint16_t smallest;           /* the smallest value it was sent        */
    unsigned refuse_write;       /* this write op (1-based) is refused    */
    unsigned lose_write;         /* this one does not land                */
    bool not_kst, prog_silent, leave_fails;
    unsigned leave_fail_at;      /* this LEAVE (1-based) does not end well */
    uint32_t prog_ms;

    /* the meter */
    bool windows;
    uint32_t win_end;
    uint16_t win_no;
    float sum;
    int16_t mx, mn;
    unsigned n;
    int extra_ma;                /* added to every sample                 */
    uint8_t clip;
    uint16_t rail_mv;
    bool enc;

    /* the supply */
    servo_test_reading_t sup;
    bool sup_feed;

    /* what came out */
    char report[16384];
    size_t report_len;
    unsigned opens, lines, ends;
    unsigned phases;             /* a bit per phase seen                  */
} rig_t;

static rig_t g;

static void rig_limits(void)
{
    uint16_t lo, hi;
    sim_travel(g.reg, CENTRE, g.zero_us, g.upc, &lo, &hi);
    g.servo.cfg.limit_lo_us = lo;
    g.servo.cfg.limit_hi_us = hi;
}

static void rig_init(uint32_t t0, bool reversion)
{
    memset(&g, 0, sizeof(g));
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);           /* stops at 1150 and 1880 us */
    servo_sim_init(&g.servo, &sc);
    const kst_image_t img = sim_bench_image();
    memcpy(g.reg, img.r, sizeof(g.reg));
    {
        kst_image_t m;
        memcpy(m.r, g.reg, sizeof(m.r));
        (void)kst_field_set(&m, KST_F_REVERSION, reversion ? 1u : 0u);
        memcpy(g.reg, m.r, sizeof(g.reg));
    }
    g.zero_us = 0.0f;
    g.upc     = 4.0f;                  /* 150 counts: 600 us of travel */
    rig_limits();
    g.now = g.t0 = t0;
    g.live = true;
    g.bench_to = -1;
    g.bench_ms = 60u;
    g.prog_ms  = 700u;
    g.cmd = CENTRE;
    g.windows = true;
    g.win_end = t0 + SERVO_TEST_WIN_MS;
    g.mx = INT16_MIN;
    g.mn = INT16_MAX;
    g.rail_mv = 6000u;
    g.smallest = 0xFFFFu;
    g.sup.online = true;
    g.sup.output = true;
    g.sup.ok = true;
    g.sup.set_v = 6.0f;
    g.sup.set_i = 3.0f;
    g.sup.v = 6.0f;
    g.sup_feed = true;

    memset(&g.cfg, 0, sizeof(g.cfg));
    g.cfg.centre_us = CENTRE;
    g.cfg.min_us    = MIN_US;
    g.cfg.max_us    = MAX_US;
    g.cfg.program   = true;
    snprintf(g.cfg.dut, sizeof(g.cfg.dut), "%s", "aileron left");
    snprintf(g.cfg.output, sizeof(g.cfg.output), "%s", "PROG PORT GP13");
    g.cfg.frame_hz    = 333u;
    g.cfg.shunt_dmohm = 1000u;
    snprintf(g.cfg.firmware, sizeof(g.cfg.firmware), "%s", "0.19.0");
}

static uint16_t reg_get(kst_field_id_t id)
{
    kst_image_t m;
    memcpy(m.r, g.reg, sizeof(m.r));
    return kst_field_get(&m, id);
}

static void reg_set(kst_field_id_t id, uint16_t v)
{
    kst_image_t m;
    memcpy(m.r, g.reg, sizeof(m.r));
    (void)kst_field_set(&m, id, v);
    memcpy(g.reg, m.r, sizeof(g.reg));
}

static void drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t k = servo_limrun_peek(&g.run, &text);
        if (k == SERVO_TEST_OUT_NONE) {
            return;
        }
        if (k == SERVO_TEST_OUT_OPEN_TXT) {
            ++g.opens;
        } else if (k == SERVO_TEST_OUT_END) {
            ++g.ends;
        } else if (k == SERVO_TEST_OUT_TXT) {
            const size_t n = strlen(text);
            ++g.lines;
            if (g.report_len + n + 2u < sizeof(g.report)) {
                memcpy(g.report + g.report_len, text, n);
                g.report_len += n;
                g.report[g.report_len++] = '\n';
                g.report[g.report_len] = '\0';
            }
        }
        servo_limrun_pop(&g.run);
    }
}

static void programmer(void)
{
    if (g.op == OP_NONE || g.prog_silent
        || (uint32_t)(g.now - g.op_at) < g.prog_ms) {
        return;
    }
    const int op = g.op;
    g.op = OP_NONE;
    if (op == OP_CONNECT) {
        servo_limrun_servo_t sv;
        memset(&sv, 0, sizeof(sv));
        sv.ok = !g.not_kst;
        sv.angle[0] = reg_get(KST_F_LEFT_ANGLE);
        sv.angle[1] = reg_get(KST_F_RIGHT_ANGLE);
        sv.lo_raw = LO_RAW;
        sv.hi_raw = HI_RAW;
        sv.reversion = reg_get(KST_F_REVERSION) != 0u;
        sv.type = 0x0815u;
        snprintf(sv.backup, sizeof(sv.backup), "%s", "KST00001.KST");
        ++g.connects;
        servo_limrun_read(&g.run, &sv, g.now);
    } else if (op == OP_WRITE) {
        ++g.write_ops;
        if (g.write_ops == g.refuse_write) {
            servo_limrun_written(&g.run, false, NULL, g.now);
            return;
        }
        if (g.write_ops != g.lose_write) {
            g.values += (unsigned)(g.want[0] != reg_get(KST_F_LEFT_ANGLE))
                        + (unsigned)(g.want[1] != reg_get(KST_F_RIGHT_ANGLE));
            reg_set(KST_F_LEFT_ANGLE, g.want[0]);
            reg_set(KST_F_RIGHT_ANGLE, g.want[1]);
            for (int k = 0; k < 2; ++k) {
                if (g.want[k] < g.smallest) {
                    g.smallest = g.want[k];
                }
            }
            rig_limits();
        }
        const uint16_t back[2] = { reg_get(KST_F_LEFT_ANGLE),
                                   reg_get(KST_F_RIGHT_ANGLE) };
        servo_limrun_written(&g.run, true, back, g.now);
    } else {
        ++g.leaves;
        servo_limrun_left(&g.run,
                          !g.leave_fails && g.leaves != g.leave_fail_at,
                          g.now);
    }
}

static void bench(void)
{
    if (g.bench_to < 0 || (uint32_t)(g.now - g.bench_at) < g.bench_ms) {
        return;
    }
    if (g.bench_to == 0) {
        g.live = false;
        g.safe = true;
    } else {
        g.live = true;
        g.safe = false;
    }
    g.bench_to = -1;
}

static void meter(void)
{
    /* SAFE: no pulse, the servo holds nothing and draws its idle current. */
    float a = g.off   ? 0.0f
            : g.live  ? servo_sim_step(&g.servo, g.cmd, g.now)
                      : 0.02f;
    int ma = (int)lroundf(a * 1000.0f) + g.extra_ma;
    if (ma > INT16_MAX) {
        ma = INT16_MAX;
    }
    g.sum += (float)ma;
    if (ma > g.mx) {
        g.mx = (int16_t)ma;
    }
    if (ma < g.mn) {
        g.mn = (int16_t)ma;
    }
    ++g.n;
    if ((int32_t)(g.now - g.win_end) < 0) {
        return;
    }
    if (g.windows) {
        servo_test_win_t w;
        memset(&w, 0, sizeof(w));
        w.number   = g.win_no++;
        w.current  = true;
        w.voltage  = true;
        w.clipped  = g.clip;
        w.mean_ma  = (int16_t)lroundf(g.sum / (float)g.n);
        w.max_ma   = g.mx;
        w.min_ma   = g.mn;
        w.mean_mv  = g.rail_mv;
        w.min_mv   = g.rail_mv;
        w.taken_ms = g.now;
        servo_limrun_window(&g.run, &w);
    }
    g.sum = 0.0f;
    g.n = 0u;
    g.mx = INT16_MIN;
    g.mn = INT16_MAX;
    g.win_end += SERVO_TEST_WIN_MS;
}

static void take(const servo_limrun_do_t *d)
{
    if (d->command) {
        g.cmd = d->cmd_us;
    }
    if (d->release) {
        g.released = true;
        ++g.releases;
    }
    if (d->off) {
        g.off = true;
        ++g.offs;
    }
    if (d->disarm) {
        ++g.disarms;
        g.bench_to = 0;
        g.bench_at = g.now;
    }
    if (d->arm) {
        ++g.arms;
        g.bench_to = 1;
        g.bench_at = g.now;
    }
    if (d->connect) {
        g.op = OP_CONNECT;
        g.op_at = g.now;
    }
    if (d->write) {
        g.op = OP_WRITE;
        g.op_at = g.now;
        g.want[0] = d->angle[0];
        g.want[1] = d->angle[1];
    }
    if (d->leave) {
        ++g.leave_asks;
        g.op = OP_LEAVE;
        g.op_at = g.now;
    }
}

/* One millisecond of the rig; the run is stepped every 10 ms, as a panel
 * frame would. */
static void tick(void)
{
    ++g.now;
    meter();
    bench();
    programmer();
    if (g.sup_feed && (g.now - g.t0) % 100u == 0u) {
        g.sup.taken_ms = g.now;
        servo_limrun_supply(&g.run, &g.sup, g.now);
    }
    if ((g.now - g.t0) % 10u == 0u) {
        servo_limrun_in_t in;
        servo_limrun_do_t d;
        memset(&in, 0, sizeof(in));
        in.live = g.live;
        in.safe = g.safe;
        in.enc_valid = g.enc;
        in.enc_deg = (g.servo.position_us - (float)CENTRE) * 0.09f;
        servo_limrun_step(&g.run, g.now, &in, &d);
        take(&d);
        if (servo_limrun_running(&g.run)) {
            g.phases |= 1u << g.run.phase;
        }
    }
    drain();
}

static bool start(void)
{
    g.cfg.enc_on = g.enc;
    return servo_limrun_start(&g.run, &g.cfg, g.now);
}

/* Until the run ends or @p ms pass. */
static void run_for(uint32_t ms)
{
    for (uint32_t k = 0; k < ms && servo_limrun_running(&g.run); ++k) {
        tick();
    }
    for (int k = 0; k < 20; ++k) {
        tick();             /* the outbox drains */
    }
}

/* Until the run is in phase @p ph, at most @p ms. */
static bool run_to(servo_limrun_phase_t ph, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        if (servo_limrun_running(&g.run) && g.run.phase == ph) {
            return true;
        }
        if (!servo_limrun_running(&g.run)) {
            return false;
        }
        tick();
    }
    return false;
}

/* The pulse width the servo's limit sits at for register value @p v. */
static float lo_edge(uint16_t v)
{
    return (float)CENTRE - g.zero_us - g.upc * (float)v;
}

static float hi_edge(uint16_t v)
{
    return (float)CENTRE + g.zero_us + g.upc * (float)v;
}

#define RUN_MS 900000u

/* --- convergence ------------------------------------------------------------- */

static void converges(bool reversion)
{
    rig_init(0u, reversion);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.state, SERVO_LIMRUN_DONE);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NONE);
    /* Which register limits which side: found, and as the model has it. */
    const int lo_reg = reversion ? SERVO_LIMRUN_RIGHT : SERVO_LIMRUN_LEFT;
    CHECK_EQ(g.run.reg_side[lo_reg], SERVO_LIMRUN_LO);
    CHECK_EQ(g.run.reg_side[1 - lo_reg], SERVO_LIMRUN_HI);
    /* Both ends inside the stops now, and the values in the servo are the
     * ones reported. */
    const uint16_t left = reg_get(KST_F_LEFT_ANGLE);
    const uint16_t right = reg_get(KST_F_RIGHT_ANGLE);
    CHECK_EQ(g.run.cur[0], left);
    CHECK_EQ(g.run.cur[1], right);
    const uint16_t lo_v = reversion ? right : left;
    const uint16_t hi_v = reversion ? left : right;
    CHECK(lo_edge(lo_v) > 1150.0f);
    CHECK(hi_edge(hi_v) < 1880.0f);
    /* Not further in than the margin asks: the free value found lies at
     * most 2 counts below the first bound one, and the margin of 5 % is
     * below it. */
    const uint16_t lo_free = g.run.lo[SERVO_LIMRUN_LO];
    const uint16_t hi_free = g.run.lo[SERVO_LIMRUN_HI];
    CHECK_EQ(lo_v, servo_limrun_final(lo_free, LO_RAW));
    CHECK_EQ(hi_v, servo_limrun_final(hi_free, LO_RAW));
    CHECK(g.run.hi[SERVO_LIMRUN_LO] - lo_free <= (int)SERVO_LIMRUN_RES_RAW);
    CHECK(g.run.hi[SERVO_LIMRUN_HI] - hi_free <= (int)SERVO_LIMRUN_RES_RAW);
    /* The model's bound point: 350 us to the low stop is 87.5 counts, 380 us
     * to the high one 95.  The free value lies within a few counts of it. */
    CHECK(lo_free >= 80u && lo_free <= 88u);
    CHECK(hi_free >= 87u && hi_free <= 95u);
    /* Writes: counted as the programmer saw them, under the cap. */
    CHECK_EQ(g.run.writes, g.values);
    CHECK_EQ(g.run.write_ops, g.write_ops);
    CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
    CHECK(g.smallest >= LO_RAW);
    CHECK_EQ(g.connects, g.run.sessions);
    CHECK_EQ(g.leaves, g.connects);
    /* The first session reads only: one write a session after it. */
    CHECK_EQ(g.run.sessions, g.run.write_ops + 1u);
    /* Ended well: the servo let go, the output left on. */
    CHECK_EQ(g.offs, 0u);
    CHECK_EQ(g.releases, 1u);
    CHECK(g.run.after[0].done && !g.run.after[0].bound);
    CHECK(g.run.after[1].done && !g.run.after[1].bound);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.ends, 1u);
    CHECK(strstr(g.report, "RCBENCH SERVO LIMIT REPORT") != NULL);
}

TEST_CASE(both_sides_converge_with_reversion_off)
{
    converges(false);
}

TEST_CASE(both_sides_converge_with_reversion_on)
{
    converges(true);
}

/* One side binds: the other register goes back to the servo's own value,
 * also where the mapping lowered it first (REVERSION on: LEFT ANGLE limits
 * the long pulses, the bound side is the short one). */
TEST_CASE(one_side_bound_leaves_the_other_register_as_it_was)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.servo.cfg.stop_hi_us = 2150u;     /* past PULSE MAX */
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        const uint16_t keep = 150u;
        if (rev == 0) {
            CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), keep);
            CHECK(reg_get(KST_F_LEFT_ANGLE) < keep);
            CHECK(!g.run.map_both);
        } else {
            CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), keep);
            CHECK(reg_get(KST_F_RIGHT_ANGLE) < keep);
            CHECK(g.run.map_both);          /* LEFT tried first, moved nothing */
        }
        CHECK(!g.run.first[SERVO_LIMRUN_HI].bound);
        CHECK(!g.run.after[SERVO_LIMRUN_HI].bound);
        CHECK(strstr(g.report, "not bound") != NULL
              || strstr(g.report, "free") != NULL);
    }
}

/* A servo bound at the smallest value the hard rules take: FAIL, nothing
 * sent below that value, and RESTORE named for what was written. */
TEST_CASE(a_servo_bound_at_the_smallest_value_fails_clearly)
{
    rig_init(0u, false);
    g.zero_us = 400.0f;         /* even 1 count leaves 404 us of travel */
    rig_limits();
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SMALLEST);
    CHECK_EQ(g.smallest, LO_RAW);
    CHECK(g.run.writes <= 2u);
    CHECK_EQ(g.offs, 0u);
    CHECK(strstr(g.report, "binds at the smallest angle") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* LEFT ANGLE at the smallest value already: the mapping starts on RIGHT
 * ANGLE; with both there and the end still bound, FAIL without a write. */
TEST_CASE(registers_at_the_smallest_value_already)
{
    rig_init(0u, false);
    reg_set(KST_F_LEFT_ANGLE, LO_RAW);
    reg_set(KST_F_RIGHT_ANGLE, LO_RAW);
    g.zero_us = 400.0f;
    rig_limits();
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SMALLEST);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.leave_asks, 1u);     /* the session is given back */

    rig_init(0u, true);             /* LEFT at 1 limits the long pulses */
    reg_set(KST_F_LEFT_ANGLE, LO_RAW);
    rig_limits();
    g.servo.cfg.stop_hi_us = 2150u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.reg_side[SERVO_LIMRUN_RIGHT], SERVO_LIMRUN_LO);
    CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), LO_RAW);
}

TEST_CASE(a_servo_that_never_binds_is_not_programmed)
{
    rig_init(0u, false);
    g.servo.cfg.stop_lo_us = 900u;
    g.servo.cfg.stop_hi_us = 2100u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FREE);
    CHECK_EQ(g.connects, 0u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.disarms, 0u);
    CHECK(g.run.first[0].done && !g.run.first[0].bound);
    CHECK(g.run.first[1].done && !g.run.first[1].bound);
    CHECK(strstr(g.report, "neither end binds") != NULL);
    CHECK(strstr(g.report, "RESTORE") == NULL);
}

/* Search only: the endpoints, nothing programmed, the bench never switched. */
TEST_CASE(search_only_reports_the_endpoints)
{
    rig_init(0u, false);
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.connects, 0u);
    CHECK_EQ(g.disarms, 0u);
    CHECK(g.run.first[0].bound && g.run.first[1].bound);
    CHECK(g.run.first[0].limit_us > 1150u && g.run.first[0].limit_us < 1200u);
    CHECK(g.run.first[1].limit_us < 1880u && g.run.first[1].limit_us > 1830u);
    CHECK(strstr(g.report, "search only") != NULL);
}

/* --- the write cap ------------------------------------------------------------ */

/* After the mapping, the run is told it has written 20 values: the next
 * bisection value would leave less than the 2 the final values need, so the
 * low side settles on its free value and the high side, without one, fails
 * the run.  Never more than the cap. */
TEST_CASE(the_write_cap_settles_or_fails_and_is_never_passed)
{
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 4u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE_CAP);
    CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
    CHECK(strstr(g.report, "write cap of 24 values reached") != NULL);

    /* One side only: it settles on its free value and the run passes. */
    rig_init(0u, false);
    g.servo.cfg.stop_hi_us = 2150u;
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 3u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
    CHECK(lo_edge(reg_get(KST_F_LEFT_ANGLE)) > 1150.0f);
}

/* --- the programmer's failures ---------------------------------------------- */

TEST_CASE(a_refused_write_aborts_and_offers_restore)
{
    rig_init(0u, false);
    g.refuse_write = 2u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK_EQ(g.offs, 1u);
    CHECK(servo_limrun_wrote(&g.run));
    CHECK(strstr(g.report, "a write refused or lost") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);

    /* The first write refused: nothing written, no RESTORE line. */
    rig_init(0u, false);
    g.refuse_write = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK(strstr(g.report, "RESTORE") == NULL);
}

/* A write the line lost: the read back holds the old values. */
TEST_CASE(a_write_lost_on_the_line_aborts)
{
    rig_init(0u, false);
    g.lose_write = 3u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
    CHECK_EQ(g.offs, 1u);
    CHECK_EQ(g.run.write_ops, 3u);
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* The servo changed between sessions. */
TEST_CASE(other_values_at_a_later_connect_abort)
{
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    reg_set(KST_F_RIGHT_ANGLE, 120u);
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
}

TEST_CASE(no_kst_servo_read_fails_before_a_write)
{
    rig_init(0u, false);
    g.not_kst = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NOT_KST);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.offs, 0u);
    CHECK(strstr(g.report, "no KST or Chaservo servo read") != NULL);
}

TEST_CASE(a_failed_leave_aborts)
{
    rig_init(0u, false);
    g.leave_fails = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK_EQ(g.offs, 1u);
    /* The read-only first session's LEAVE: nothing was written. */
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK(strstr(g.report, "RESTORE") == NULL);

    /* The LEAVE after the first write: the value stays, RESTORE named. */
    rig_init(0u, false);
    g.leave_fail_at = 2u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK_EQ(g.write_ops, 1u);
    CHECK(servo_limrun_wrote(&g.run));
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* A CONNECT, write or LEAVE the programmer does not take ends the run. */
TEST_CASE(a_step_the_programmer_refuses_aborts)
{
    static const struct {
        servo_limrun_phase_t ph;
        servo_limrun_why_t why;
    } k[] = {
        { SERVO_LIMRUN_PH_CONNECT, SERVO_LIMRUN_WHY_PROG },
        { SERVO_LIMRUN_PH_WRITE,   SERVO_LIMRUN_WHY_WRITE },
        { SERVO_LIMRUN_PH_LEAVE,   SERVO_LIMRUN_WHY_PROG },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rig_init(0u, false);
        CHECK(start());
        CHECK(run_to(k[i].ph, RUN_MS));
        g.op = OP_NONE;
        servo_limrun_refused(&g.run, g.now);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, k[i].why);
        run_for(100u);
        CHECK_EQ(g.offs, 1u);
    }
    /* Outside the programmer's phases it changes nothing. */
    rig_init(0u, false);
    CHECK(start());
    run_for(1000u);
    servo_limrun_refused(&g.run, g.now);
    CHECK(servo_limrun_running(&g.run));
}

/* --- aborts at every phase -------------------------------------------------- */

static const servo_limrun_phase_t k_phases[] = {
    SERVO_LIMRUN_PH_SEARCH, SERVO_LIMRUN_PH_SAFE, SERVO_LIMRUN_PH_CONNECT,
    SERVO_LIMRUN_PH_WRITE, SERVO_LIMRUN_PH_LEAVE, SERVO_LIMRUN_PH_ARM,
    SERVO_LIMRUN_PH_PROBE, SERVO_LIMRUN_PH_CHECK,
};
#define N_PHASES (sizeof(k_phases) / sizeof(k_phases[0]))

/* The run in phase @p ph, after its first write for a phase that comes
 * after one. */
static bool to_phase(servo_limrun_phase_t ph)
{
    rig_init(0u, false);
    if (!start()) {
        return false;
    }
    if (ph == SERVO_LIMRUN_PH_CHECK) {
        return run_to(ph, RUN_MS);
    }
    if (ph != SERVO_LIMRUN_PH_SEARCH && ph != SERVO_LIMRUN_PH_PROBE) {
        /* The second session's phase: a value is written by then. */
        if (!run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS)) {
            return false;
        }
    }
    return run_to(ph, RUN_MS);
}

/* Each reason the caller has: in every phase the run ends ABORTED, asks
 * the output off and lets the servo go, once. */
TEST_CASE(every_caller_abort_ends_every_phase_with_the_output_off)
{
    static const servo_test_abort_t k_why[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_DISARMED, SERVO_TEST_AB_LINK,
        SERVO_TEST_AB_LEFT, SERVO_TEST_AB_OPERATOR, SERVO_TEST_AB_SETTINGS,
        SERVO_TEST_AB_SUPPLY_LOST, SERVO_TEST_AB_SUPPLY_OFF,
        SERVO_TEST_AB_TRIPPED, SERVO_TEST_AB_INA_METER,
    };
    for (size_t p = 0; p < N_PHASES; ++p) {
        for (size_t w = 0; w < sizeof(k_why) / sizeof(k_why[0]); ++w) {
            if (!to_phase(k_phases[p])) {
                T_FAIL("phase %d not reached", (int)k_phases[p]);
                continue;
            }
            const bool wrote = servo_limrun_wrote(&g.run);
            servo_limrun_abort(&g.run, k_why[w], g.now);
            run_for(100u);
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
            CHECK_EQ(g.run.test_why, k_why[w]);
            CHECK_EQ(g.offs, 1u);
            CHECK_EQ(g.releases, 1u);
            CHECK_EQ(g.ends, 1u);
            CHECK_EQ(strstr(g.report, "RESTORE") != NULL, wrote);
            CHECK_EQ(wrote, k_phases[p] != SERVO_LIMRUN_PH_SEARCH);
            /* Nothing it is fed afterwards moves it. */
            servo_limrun_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
            CHECK_EQ(g.run.test_why, k_why[w]);
        }
    }
}

/* The run's own ends: the meter's silence in every phase; the bench that
 * does not switch; the programmer that does not answer; a search past its
 * cap; the bench dropping out of ARMED during a search. */
TEST_CASE(the_runs_own_timeouts_end_it)
{
    for (size_t p = 0; p < N_PHASES; ++p) {
        CHECK(to_phase(k_phases[p]));
        g.windows = false;
        run_for(1000u);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
        CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
        CHECK_EQ(g.offs, 1u);
    }

    CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
    run_for(1u);
    g.bench_ms = 0xFFFFFFFFu;
    CHECK(run_to(SERVO_LIMRUN_PH_SAFE, RUN_MS));
    run_for(SERVO_LIMRUN_BENCH_MS + 100u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);

    CHECK(to_phase(SERVO_LIMRUN_PH_LEAVE));
    g.bench_ms = 0xFFFFFFFFu;
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);
    CHECK_EQ(g.run.end_ms - g.run.phase_ms, SERVO_LIMRUN_BENCH_MS);

    for (int k = 0; k < 3; ++k) {
        const servo_limrun_phase_t ph = k == 0 ? SERVO_LIMRUN_PH_CONNECT
                                      : k == 1 ? SERVO_LIMRUN_PH_WRITE
                                               : SERVO_LIMRUN_PH_LEAVE;
        CHECK(to_phase(ph));
        g.prog_silent = true;
        run_for(SERVO_LIMRUN_PROG_MS + 1000u);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
        CHECK_EQ(g.run.end_ms - g.run.phase_ms, SERVO_LIMRUN_PROG_MS);
    }

    /* A search past its cap: a servo too slow to arrive inside a window
     * keeps the search on its baseline. */
    rig_init(0u, false);
    g.cfg.program = false;
    CHECK(start());
    g.run.search_ms = g.now - SERVO_LIMRUN_SEARCH_MS + 50u;
    run_for(200u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SEARCH_LONG);
    CHECK_EQ(g.offs, 1u);

    /* Out of ARMED in the middle of a search. */
    CHECK(to_phase(SERVO_LIMRUN_PH_PROBE));
    run_for(500u);
    g.live = false;
    run_for(100u);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_DISARMED);
    CHECK_EQ(g.offs, 1u);
}

/* --- the protections ------------------------------------------------------- */

static servo_test_win_t win_at(uint32_t t, int16_t mean, int16_t max)
{
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.current = true;
    w.voltage = true;
    w.mean_ma = mean;
    w.max_ma = max;
    w.min_ma = mean;
    w.mean_mv = 6000u;
    w.min_mv = 6000u;
    w.taken_ms = t;
    return w;
}

TEST_CASE(a_sample_at_3_a_ends_the_run_in_every_phase)
{
    for (size_t p = 0; p < N_PHASES; ++p) {
        CHECK(to_phase(k_phases[p]));
        servo_test_win_t w = win_at(g.now, 200, 2999);
        servo_limrun_window(&g.run, &w);
        CHECK(servo_limrun_running(&g.run));
        w = win_at(g.now, 200, 3000);
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);
        run_for(20u);
        CHECK_EQ(g.offs, 1u);

        CHECK(to_phase(k_phases[p]));
        w = win_at(g.now, 200, -3000);
        w.min_ma = -3000;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);

        CHECK(to_phase(k_phases[p]));
        w = win_at(g.now, 200, 300);
        w.clipped = 1u;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_CLIPPED);
    }
}

/* Window means above 1.0 A: 399 ms of them run on, 400 ms end it, timed
 * from the start of the first window. */
TEST_CASE(above_1_a_for_400_ms_ends_the_run)
{
    for (int k = 0; k < 2; ++k) {
        rig_init(k == 0 ? 0u : 0u - 200u, false);
        CHECK(start());
        const uint32_t t = g.now + 50u;
        for (uint32_t m = 0; m < 7u; ++m) {
            servo_test_win_t w = win_at(t + 50u * m, 1001, 1100);
            servo_limrun_window(&g.run, &w);
        }
        /* 350 ms from the first window's start */
        CHECK(servo_limrun_running(&g.run));
        servo_test_win_t w = win_at(t + 349u, 1001, 1100);
        servo_limrun_window(&g.run, &w);
        CHECK(servo_limrun_running(&g.run));
        w = win_at(t + 350u, 1001, 1100);
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_STALL);
        CHECK_EQ(g.run.end_ms, t + 350u);

        /* 1000 mA is not above it; one window under resets the time. */
        rig_init(k == 0 ? 0u : 0u - 200u, false);
        CHECK(start());
        for (uint32_t m = 0; m < 20u; ++m) {
            w = win_at(g.now + 50u * (m + 1u),
                       (m % 7u == 6u) ? 999 : (m < 10u ? 1000 : 1001), 1100);
            servo_limrun_window(&g.run, &w);
        }
        CHECK(servo_limrun_running(&g.run));
    }
}

/* The supply held while a search drives the servo; the programmer's
 * session left to it. */
TEST_CASE(a_changed_supply_ends_a_search_but_not_a_session)
{
    /* The set point moved during the first search. */
    rig_init(0u, false);
    CHECK(start());
    run_for(500u);
    g.sup.set_v = 7.4f;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
    CHECK_EQ(g.offs, 1u);

    /* The rail 1.0 V low: runs on; 1.001 V low: ends. */
    rig_init(0u, false);
    CHECK(start());
    run_for(500u);
    g.rail_mv = 5000u;
    run_for(500u);
    CHECK(servo_limrun_running(&g.run));
    g.rail_mv = 4999u;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);

    /* Output off, trip, gone: the caller's reasons, also by this path. */
    for (int k = 0; k < 3; ++k) {
        rig_init(0u, false);
        CHECK(start());
        run_for(500u);
        if (k == 0) {
            g.sup.output = false;
        } else if (k == 1) {
            g.sup.trip = 1u;
        } else {
            g.sup.online = false;
        }
        run_for(500u);
        CHECK_EQ(g.run.test_why, k == 0 ? SERVO_TEST_AB_SUPPLY_OFF
                                 : k == 1 ? SERVO_TEST_AB_TRIPPED
                                          : SERVO_TEST_AB_SUPPLY_LOST);
    }

    /* During the programmer's session the rail dips and the set point is
     * the session's: the run goes on, and is held to the start's values
     * again once a search drives. */
    CHECK(to_phase(SERVO_LIMRUN_PH_CONNECT));
    g.rail_mv = 4200u;
    g.sup.set_v = 5.0f;
    run_for(300u);
    CHECK(servo_limrun_running(&g.run));
    g.rail_mv = 6000u;
    g.sup.set_v = 6.0f;
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    run_for(2000u);
    CHECK(servo_limrun_running(&g.run));
    g.sup.set_i = 2.0f;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
}

/* --- the meter's silence ------------------------------------------------------ */

/* The last window at t: the run goes on at t + 199 ms and ends at t + 200
 * ms (also when the step comes at t + 201), at tick 0 and with the wrap
 * between the window and the step. */
TEST_CASE(no_window_for_200_ms_ends_the_run)
{
    static const uint32_t k_after[] = { 199u, 200u, 201u };
    static const uint32_t k_t0[] = { 0u, 0u - 100u, 0u - 200u, 0u - 199u };
    CHECK_EQ(SERVO_LIMRUN_WIN_STALE_MS, 200u);
    for (size_t z = 0; z < sizeof(k_t0) / sizeof(k_t0[0]); ++z) {
        for (size_t a = 0; a < 3u; ++a) {
            rig_init(k_t0[z], false);
            CHECK(start());
            const uint32_t t = k_t0[z] + 50u;
            servo_test_win_t w = win_at(t, 120, 150);
            servo_limrun_window(&g.run, &w);
            servo_limrun_in_t in = { true, false, false, 0.0f };
            servo_limrun_do_t d;
            servo_limrun_step(&g.run, t + k_after[a], &in, &d);
            if (k_after[a] < 200u) {
                CHECK(servo_limrun_running(&g.run));
                CHECK(!d.off);
            } else {
                CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
                CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
                CHECK(d.off && d.release);
                CHECK_EQ(g.run.end_ms, t + k_after[a]);
            }
        }
    }
    /* No window at all: 200 ms from the start. */
    rig_init(0u - 150u, false);
    CHECK(start());
    servo_limrun_in_t in = { true, false, false, 0.0f };
    servo_limrun_do_t d;
    servo_limrun_step(&g.run, g.now + 199u, &in, &d);
    CHECK(servo_limrun_running(&g.run));
    servo_limrun_step(&g.run, g.now + 200u, &in, &d);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
}

/* --- the clock --------------------------------------------------------------- */

/* The same run from tick 0 and from 60 s before the wrap: the same values,
 * the same writes, the same duration. */
TEST_CASE(a_run_across_the_tick_wrap_ends_as_at_tick_0)
{
    rig_init(0u, false);
    CHECK(start());
    run_for(RUN_MS);
    const servo_limrun_t a = g.run;
    const uint16_t left = reg_get(KST_F_LEFT_ANGLE);
    const uint16_t right = reg_get(KST_F_RIGHT_ANGLE);

    rig_init(0u - 60000u, false);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, a.verdict);
    CHECK_EQ(g.run.writes, a.writes);
    CHECK_EQ(g.run.sessions, a.sessions);
    CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), left);
    CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), right);
    CHECK_EQ(g.run.end_ms - g.run.start_ms, a.end_ms - a.start_ms);
    CHECK((int32_t)g.run.end_ms > 0);       /* it crossed the wrap */
}

/* --- the report ---------------------------------------------------------------- */

TEST_CASE(the_report_holds_the_run)
{
    rig_init(0u, false);
    g.enc = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    char want[96];
    CHECK(strstr(g.report, "RCBENCH SERVO LIMIT REPORT\n") == g.report);
    CHECK(strstr(g.report, "PASS") != NULL);
    CHECK(strstr(g.report, "aileron left") != NULL);
    CHECK(strstr(g.report, "PROG PORT GP13") != NULL);
    CHECK(strstr(g.report, "INA3221") != NULL);
    CHECK(strstr(g.report, "1.8") != NULL);
    CHECK(strstr(g.report, "3.0 A") != NULL);
    snprintf(want, sizeof(want), "LEFT ANGLE:     150 before, %u after",
             (unsigned)reg_get(KST_F_LEFT_ANGLE));
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "RIGHT ANGLE:    150 before, %u after",
             (unsigned)reg_get(KST_F_RIGHT_ANGLE));
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "%u values in %u writes", g.run.writes,
             g.run.write_ops);
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, "KST00001.KST") != NULL);
    CHECK(strstr(g.report, "5 %") != NULL);
    CHECK(strstr(g.report, "AS5600") != NULL || strstr(g.report, "deg") != NULL);
    CHECK(strstr(g.report, "unit") != NULL);
    CHECK(strstr(g.report, "RESTORE") == NULL);
    /* Every line fits the line buffer and the report ends once. */
    CHECK_EQ(g.ends, 1u);
    CHECK(g.lines > 20u);
    for (unsigned k = 0; k < 200u; ++k) {
        char buf[SERVO_TEST_LINE_MAX];
        if (!servo_limrun_report_line(&g.run, k, buf, sizeof(buf))) {
            CHECK_EQ(k, g.lines);
            break;
        }
        CHECK(strlen(buf) < sizeof(buf) - 1u);
    }

    /* German. */
    rig_init(0u, false);
    g.cfg.text = NULL;
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK(strstr(g.report, "search only") != NULL);
}

TEST_CASE(a_start_with_its_centre_outside_min_and_max_is_refused)
{
    rig_init(0u, false);
    g.cfg.centre_us = MAX_US;
    CHECK(!start());
    CHECK(!servo_limrun_running(&g.run));
    g.cfg.centre_us = CENTRE;
    CHECK(start());
    CHECK(!start());                    /* one at a time */
    servo_limrun_step(NULL, 0u, NULL, NULL);
    servo_limrun_window(NULL, NULL);
    CHECK(!servo_limrun_running(NULL));
    CHECK(servo_limrun_drained(NULL));
}

/* The margin: 5 % of the free value, at least 2 counts, never below the
 * smallest value. */
TEST_CASE(the_margin)
{
    CHECK_EQ(servo_limrun_final(100u, 1u), 95u);
    CHECK_EQ(servo_limrun_final(60u, 1u), 57u);
    CHECK_EQ(servo_limrun_final(39u, 1u), 37u);
    CHECK_EQ(servo_limrun_final(20u, 1u), 18u);
    CHECK_EQ(servo_limrun_final(2u, 1u), 1u);
    CHECK_EQ(servo_limrun_final(1u, 1u), 1u);
    CHECK_EQ(servo_limrun_final(255u, 1u), 243u);
}

int main(void)
{
    RUN(both_sides_converge_with_reversion_off);
    RUN(both_sides_converge_with_reversion_on);
    RUN(one_side_bound_leaves_the_other_register_as_it_was);
    RUN(a_servo_bound_at_the_smallest_value_fails_clearly);
    RUN(registers_at_the_smallest_value_already);
    RUN(a_servo_that_never_binds_is_not_programmed);
    RUN(search_only_reports_the_endpoints);
    RUN(the_write_cap_settles_or_fails_and_is_never_passed);
    RUN(a_refused_write_aborts_and_offers_restore);
    RUN(a_write_lost_on_the_line_aborts);
    RUN(other_values_at_a_later_connect_abort);
    RUN(no_kst_servo_read_fails_before_a_write);
    RUN(a_failed_leave_aborts);
    RUN(a_step_the_programmer_refuses_aborts);
    RUN(every_caller_abort_ends_every_phase_with_the_output_off);
    RUN(the_runs_own_timeouts_end_it);
    RUN(a_sample_at_3_a_ends_the_run_in_every_phase);
    RUN(above_1_a_for_400_ms_ends_the_run);
    RUN(a_changed_supply_ends_a_search_but_not_a_session);
    RUN(no_window_for_200_ms_ends_the_run);
    RUN(a_run_across_the_tick_wrap_ends_as_at_tick_0);
    RUN(the_report_holds_the_run);
    RUN(a_start_with_its_centre_outside_min_and_max_is_refused);
    RUN(the_margin);
    return test_summary("servo_limrun");
}
