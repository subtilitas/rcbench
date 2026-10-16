/*
 * The limit run, against a modelled servo whose travel its registers set.
 *
 * The rig: servo_sim's servo on a surface with two mechanical stops, the
 * horn where kst_sim.h's sim_horn_us() puts it for the pulse -- linear, or
 * square-law, from NEUTRAL to PULSE LOWER and PULSE UPPER, LEFT ANGLE and
 * RIGHT ANGLE the full travel, REVERSION choosing which register limits
 * which pulse side; the INA3221 as 50 ms windows of the servo's 1 ms
 * current; a bench that switches to SAFE and ARMED when asked; and a
 * programmer that answers CONNECT with the registers, stores a write of
 * the angles or of the pulses and reads it back, and gives the port back.
 *
 * Under test: the read first and the search from NEUTRAL to the servo's own
 * pulse limits, never past them; the bench's range refused; SERVO PULSES
 * at 1000-2000 and its write before anything moves, and the share from the
 * pulses written; the two writes a side, the overset from the first bound
 * pulse and the final value, with REVERSION off and on, on LEFT ANGLE and
 * on RIGHT ANGLE; the low stop from 920 to 1440 us in 3 us steps, every
 * run PASS; the overset left out near the pulse limit; a servo free
 * after the overset; a square-law servo that still binds (FAIL, RESTORE);
 * the smallest value; a servo that never binds; the write cap; a write
 * refused, and one lost on the line; every abort source at every phase,
 * the output asked off and RESTORE named once anything was written; the
 * 3.0 A ceiling, a clipped sample, 1.0 A for 400 ms, a supply changed
 * while a search drives the servo and left alone while the programmer holds
 * the port; no window for 199, 200 and 201 ms, also across the 2^32 ms tick
 * wrap; a whole run across the wrap; and the report.
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
#define MIN_US  800u
#define MAX_US  2200u
#define HI_RAW  255u
/* The bench image: NEUTRAL 1500.2 us, PULSE LOWER 900.0 us, PULSE UPPER
 * 2100.0 us, LEFT ANGLE and RIGHT ANGLE 150; at 4 us a count the full
 * travel lies 600 us from NEUTRAL, a linear servo's horn where its pulse
 * is. */
#define NEUTRAL_DUS 15002
#define LOWER_DUS   9000
#define UPPER_DUS   21000

/* --- the rig ------------------------------------------------------------- */

enum { OP_NONE = 0, OP_CONNECT, OP_WRITE, OP_PULSES, OP_LEAVE };

typedef struct {
    servo_limrun_t run;
    servo_limrun_cfg_t cfg;
    servo_sim_t servo;
    uint8_t reg[KST_REG_COUNT];
    float upc;                   /* the model's unit (sim_horn_us())      */
    bool square;                 /* a square-law servo                    */
    uint16_t lo_raw;             /* the smallest value the rules take     */
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
    uint16_t want_p[2];
    unsigned connects, write_ops, leaves, leave_asks, values;
    unsigned pulse_ops;          /* pulse writes, among write_ops         */
    uint32_t first_drive;        /* when a pulse first left the centre    */
    bool driven;
    unsigned connects_at_drive, leaves_at_drive, pulse_ops_at_drive;
    float shrink;                /* a value below 150 moves the horn this
                                    share of a linear servo's travel, 0
                                    linear                                */
    uint16_t cmd_lo, cmd_hi;     /* the extremes commanded                */
    uint16_t smallest;           /* the smallest value it was sent        */
    unsigned refuse_write;       /* this write op (1-based) is refused    */
    unsigned lose_write;         /* this one does not land                */
    bool not_kst, prog_silent, leave_fails;
    unsigned leave_fail_at;      /* this LEAVE (1-based) does not end well */
    uint32_t prog_ms;
    unsigned change_at;          /* this CONNECT (1-based) reads ...      */
    int change_kind;             /* ... another type, REVERSION or range  */

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
    bool no_volt;                /* windows without the rail voltage      */
    bool no_cur;                 /* windows without a current reading     */
    bool enc;
    bool neg;                    /* the shunt fitted the other way round  */

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

static uint16_t reg_get(kst_field_id_t id);
static void reg_set(kst_field_id_t id, uint16_t v);

/* PULSE LOWER and PULSE UPPER set to @p lo and @p hi, 0.1 us. */
static void rig_pulses(int32_t lo, int32_t hi)
{
    uint16_t raw = 0u;
    CHECK(kst_field_raw_from_display(KST_F_PULSE_LOWER, lo, &raw));
    reg_set(KST_F_PULSE_LOWER, raw);
    CHECK(kst_field_raw_from_display(KST_F_PULSE_UPPER, hi, &raw));
    reg_set(KST_F_PULSE_UPPER, raw);
}

static void pulses_of(uint16_t *p)
{
    static const kst_field_id_t k_f[3] = {
        KST_F_NEUTRAL, KST_F_PULSE_LOWER, KST_F_PULSE_UPPER,
    };
    for (int i = 0; i < 3; ++i) {
        int32_t d = 0;
        (void)kst_field_display(k_f[i], reg_get(k_f[i]), &d);
        p[i] = (uint16_t)d;
    }
}

static void rig_init(uint32_t t0, bool reversion)
{
    memset(&g, 0, sizeof(g));
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);           /* stops at 1150 and 1880 us */
    sc.limit_lo_us = 0u;               /* the horn model is the travel */
    sc.limit_hi_us = 0u;
    servo_sim_init(&g.servo, &sc);
    const kst_image_t img = sim_bench_image();
    memcpy(g.reg, img.r, sizeof(g.reg));
    {
        kst_image_t m;
        memcpy(m.r, g.reg, sizeof(m.r));
        (void)kst_field_set(&m, KST_F_REVERSION, reversion ? 1u : 0u);
        memcpy(g.reg, m.r, sizeof(g.reg));
    }
    g.upc     = 4.0f;                  /* 150 counts: 600 us of travel */
    g.lo_raw  = 1u;
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
    g.cmd_lo = 0xFFFFu;
    g.cmd_hi = 0u;
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
    g.cfg.read      = true;
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
        sv.lo_raw = g.lo_raw;
        sv.hi_raw = HI_RAW;
        pulses_of(sv.pulse_dus);
        sv.reversion = reg_get(KST_F_REVERSION) != 0u;
        sv.type = 0x0815u;
        snprintf(sv.backup, sizeof(sv.backup), "%s", "KST00001.KST");
        ++g.connects;
        if (g.connects == g.change_at) {
            switch (g.change_kind) {
            case 0: sv.type = 0x0816u; break;
            case 1: sv.reversion = !sv.reversion; break;
            case 2: sv.lo_raw = (uint16_t)(g.lo_raw + 1u); break;
            case 3: sv.hi_raw = HI_RAW - 1u; break;
            default: sv.pulse_dus[1] = (uint16_t)(sv.pulse_dus[1] + 8u); break;
            }
        }
        servo_limrun_connected(&g.run, &sv, g.now);
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
        }
        const uint16_t back[2] = { reg_get(KST_F_LEFT_ANGLE),
                                   reg_get(KST_F_RIGHT_ANGLE) };
        servo_limrun_written(&g.run, true, back, g.now);
    } else if (op == OP_PULSES) {
        ++g.write_ops;
        ++g.pulse_ops;
        if (g.write_ops == g.refuse_write) {
            servo_limrun_pulsed(&g.run, false, NULL, g.now);
            return;
        }
        if (g.write_ops != g.lose_write) {
            uint16_t now[3];
            pulses_of(now);
            g.values += (unsigned)(g.want_p[0] != now[1])
                        + (unsigned)(g.want_p[1] != now[2]);
            rig_pulses(g.want_p[0], g.want_p[1]);
        }
        uint16_t back[3];
        pulses_of(back);
        servo_limrun_pulsed(&g.run, true, back, g.now);
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

/* The registers as the horn model reads them: with g.shrink, a servo
 * whose travel is not linear in LEFT ANGLE and RIGHT ANGLE below 150. */
static uint16_t horn_cmd(void)
{
    if (g.shrink <= 0.0f) {
        return sim_horn_cmd(g.reg, g.cmd, g.upc, g.square);
    }
    uint8_t r[sizeof(g.reg)];
    kst_image_t m;
    memcpy(m.r, g.reg, sizeof(m.r));
    static const kst_field_id_t k_f[2] = {
        KST_F_LEFT_ANGLE, KST_F_RIGHT_ANGLE,
    };
    for (int i = 0; i < 2; ++i) {
        const uint16_t v = kst_field_get(&m, k_f[i]);
        if (v < 150u) {
            (void)kst_field_set(&m, k_f[i],
                                (uint16_t)lroundf((float)v * g.shrink));
        }
    }
    memcpy(r, m.r, sizeof(r));
    return sim_horn_cmd(r, g.cmd, g.upc, g.square);
}

static void meter(void)
{
    /* SAFE: no pulse, the servo holds nothing and draws its idle current. */
    float a = g.off   ? 0.0f
            : g.live  ? servo_sim_step(&g.servo, horn_cmd(), g.now)
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
        w.current  = !g.no_cur;
        w.voltage  = !g.no_volt;
        w.clipped  = g.clip;
        w.mean_ma  = (int16_t)lroundf(g.sum / (float)g.n);
        w.max_ma   = g.mx;
        w.min_ma   = g.mn;
        if (g.neg) {
            w.mean_ma = (int16_t)-w.mean_ma;
            w.max_ma  = (int16_t)-g.mn;
            w.min_ma  = (int16_t)-g.mx;
        }
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
        if (g.cmd != g.run.ctr_us && g.cmd != CENTRE && !g.driven) {
            g.driven = true;
            g.first_drive = g.now;
            g.connects_at_drive = g.connects;
            g.leaves_at_drive = g.leaves;
            g.pulse_ops_at_drive = g.pulse_ops;
        }
        if (g.cmd < g.cmd_lo) {
            g.cmd_lo = g.cmd;
        }
        if (g.cmd > g.cmd_hi) {
            g.cmd_hi = g.cmd;
        }
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
    if (d->pulses) {
        g.op = OP_PULSES;
        g.op_at = g.now;
        g.want_p[0] = d->pulse_dus[0];
        g.want_p[1] = d->pulse_dus[1];
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

/* @p ms of the rig, whether the run goes on or not. */
static void idle_for(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        tick();
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

#define RUN_MS 900000u

/* --- helpers ------------------------------------------------------------- */

/* The run as the PWM pin's: no read, the search out to PULSE MIN and PULSE
 * MAX, nothing programmed. */
static void rig_pin(void)
{
    g.cfg.read    = false;
    g.cfg.program = false;
    snprintf(g.cfg.output, sizeof(g.cfg.output), "%s", "GP2");
}

/* Where a linear servo's horn stands at full travel for value @p v on
 * @p side, us: NEUTRAL 1500.2 us, 4 us a count. */
static float full_us(int side, uint16_t v)
{
    const float n = (float)NEUTRAL_DUS / 10.0f;
    return side == SERVO_LIMRUN_HI ? n + 4.0f * (float)v
                                   : n - 4.0f * (float)v;
}

/* --- the read first ------------------------------------------------------ */

/* Nothing drives the servo before the read session is over: CONNECT, the
 * read and LEAVE come first, at PULSE CENTRE. */
static void read_before_drive(void)
{
    CHECK(g.driven);
    CHECK_EQ(g.connects_at_drive, 1u);
    CHECK_EQ(g.leaves_at_drive, 1u);
}

/* --- convergence --------------------------------------------------------- */

static void converges(bool reversion)
{
    rig_init(0u, reversion);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.state, SERVO_LIMRUN_DONE);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NONE);
    read_before_drive();
    /* Searched from NEUTRAL out to PULSE LOWER and PULSE UPPER, never past
     * them: the final check, free, reaches both exactly. */
    CHECK_EQ(g.run.ctr_us, 1500u);
    CHECK_EQ(g.run.end_us[SERVO_LIMRUN_LO], 900u);
    CHECK_EQ(g.run.end_us[SERVO_LIMRUN_HI], 2100u);
    CHECK_EQ(g.cmd_lo, 900u);
    CHECK_EQ(g.cmd_hi, 2100u);
    /* Which register limits which side: found, and as the model has it. */
    const int lo_reg = reversion ? SERVO_LIMRUN_RIGHT : SERVO_LIMRUN_LEFT;
    CHECK_EQ(g.run.reg_side[lo_reg], SERVO_LIMRUN_LO);
    CHECK_EQ(g.run.reg_side[1 - lo_reg], SERVO_LIMRUN_HI);
    const uint16_t left = reg_get(KST_F_LEFT_ANGLE);
    const uint16_t right = reg_get(KST_F_RIGHT_ANGLE);
    CHECK_EQ(g.run.cur[0], left);
    CHECK_EQ(g.run.cur[1], right);
    const uint16_t lo_v = reversion ? right : left;
    const uint16_t hi_v = reversion ? left : right;
    /* Two writes a side: the overset from the share at the first search's
     * first bound pulse, the final value from the share the overset's
     * search found free. */
    for (int s = 0; s < 2; ++s) {
        const int32_t span = s == SERVO_LIMRUN_LO ? 6002 : 5998;
        CHECK(g.run.first[s].bound);
        CHECK_EQ(g.run.d0_dus[s],
                 servo_limrun_dist(&g.run.servo, (uint8_t)s,
                                   g.run.first[s].free_us));
        CHECK_EQ(g.run.db_dus[s],
                 servo_limrun_dist(&g.run.servo, (uint8_t)s,
                                   g.run.first[s].bind_us));
        CHECK_EQ(g.run.db_dus[s], g.run.d0_dus[s] + 100);
        CHECK_EQ(servo_limrun_span(&g.run.servo, (uint8_t)s), span);
        CHECK_EQ(g.run.v1[s],
                 servo_limrun_overset(150u, g.run.db_dus[s], span, 1u));
        CHECK(g.run.v1[s] < 150u);
        CHECK(g.run.d1_dus[s] > g.run.d0_dus[s]);
        CHECK_EQ(g.run.v2[s],
                 servo_limrun_scaled(g.run.v1[s], g.run.d1_dus[s], span, 1u));
    }
    CHECK_EQ(lo_v, g.run.v2[SERVO_LIMRUN_LO]);
    CHECK_EQ(hi_v, g.run.v2[SERVO_LIMRUN_HI]);
    /* The stops at 1150 and 1880 us: 350.2 us is 87.55 counts, 379.8 us
     * 94.95; the values land inside them, within the search's 10 us. */
    CHECK_EQ(lo_v, 86u);
    CHECK_EQ(hi_v, 94u);
    CHECK(full_us(SERVO_LIMRUN_LO, lo_v) > 1150.0f);
    CHECK(full_us(SERVO_LIMRUN_HI, hi_v) < 1880.0f);
    /* Writes: counted as the programmer saw them, under the cap.  REVERSION
     * off: LEFT ANGLE's overset finds the mapping, then both registers in
     * one write, then RIGHT ANGLE's final value.  On: the first write went
     * to the other side's register and comes back, two values more. */
    CHECK_EQ(g.run.writes, g.values);
    CHECK_EQ(g.run.write_ops, g.write_ops);
    CHECK_EQ(g.run.writes, reversion ? 6u : 4u);
    CHECK_EQ(g.run.write_ops, reversion ? 4u : 3u);
    CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK_EQ(g.connects, g.run.sessions);
    CHECK_EQ(g.leaves, g.connects);
    /* The read session writes nothing: one write a session after it. */
    CHECK_EQ(g.run.sessions, g.run.write_ops + 1u);
    /* Ended well: the servo let go, the output left on. */
    CHECK_EQ(g.offs, 0u);
    CHECK_EQ(g.releases, 1u);
    CHECK(g.run.after[0].done && !g.run.after[0].bound);
    CHECK(g.run.after[1].done && !g.run.after[1].bound);
    CHECK(!servo_limrun_restore(&g.run));
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.ends, 1u);
    CHECK(strstr(g.report, "RCBENCH SERVO LIMIT REPORT") != NULL);
    uint16_t was_l = 0u, was_r = 0u;
    CHECK(servo_limrun_original(&g.run, &was_l, &was_r));
    CHECK_EQ(was_l, 150u);
    CHECK_EQ(was_r, 150u);
    CHECK(servo_limrun_original(&g.run, NULL, NULL));
}

TEST_CASE(both_sides_converge_with_reversion_off)
{
    converges(false);
}

TEST_CASE(both_sides_converge_with_reversion_on)
{
    converges(true);
}

/* servo_limrun.h's worked example: the low stop at 1000 us, the first
 * search free at 1000 us and bound at 990 us.  The share at the bound
 * pulse 510.2 / 600.2, v1 = ceil(150 x 0.850 x 1.03) = 132; the overset's
 * search is free to 930 us, v2 = floor(132 x 570.2 / 600.2) = 125. */
TEST_CASE(the_worked_example)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.servo.cfg.stop_lo_us = 1000u;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_LO].free_us, 1000u);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_LO].bind_us, 990u);
        CHECK_EQ(g.run.d0_dus[SERVO_LIMRUN_LO], 5002);
        CHECK_EQ(g.run.db_dus[SERVO_LIMRUN_LO], 5102);
        CHECK_EQ(g.run.v1[SERVO_LIMRUN_LO], 132u);
        CHECK_EQ(g.run.d1_dus[SERVO_LIMRUN_LO], 5702);
        CHECK_EQ(g.run.v2[SERVO_LIMRUN_LO], 125u);
        CHECK_EQ(reg_get(rev ? KST_F_RIGHT_ANGLE : KST_F_LEFT_ANGLE), 125u);
        CHECK(full_us(SERVO_LIMRUN_LO, 125u) > 1000.0f);
        CHECK(full_us(SERVO_LIMRUN_LO, 126u) < 1000.0f);
        CHECK(strstr(g.report, "share free 0.833, bound 0.850; overset 132, "
                               "share 0.950; final 125") != NULL);
    }
}

/* Near the pulse limit the overset is left out and the first search
 * counts as its: with the bound pulse's share above about 0.97 the overset
 * would not lie below v0; a little further in the stop would move less
 * than 20 us, a move a search cannot tell from none.  One write then,
 * floor(v0 x f0), f0 the share the first search found free. */
TEST_CASE(the_overset_is_left_out_near_the_pulse_limit)
{
    static const struct {
        uint16_t stop_lo, free_us;
        int32_t d0, db;
        uint16_t overset, v2;
    } k[] = {
        { 915u, 920u, 5802, 5902, 150u, 145u }, /* v1 not below v0     */
        { 940u, 940u, 5602, 5702, 147u, 140u }, /* the move 11.4 us    */
    };
    for (size_t i = 0; i < 2u; ++i) {
        rig_init(0u, false);
        g.servo.cfg.stop_lo_us = k[i].stop_lo;
        g.servo.cfg.stop_hi_us = 2150u;     /* the high side free */
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_LO].free_us, k[i].free_us);
        CHECK_EQ(g.run.d0_dus[SERVO_LIMRUN_LO], k[i].d0);
        CHECK_EQ(g.run.db_dus[SERVO_LIMRUN_LO], k[i].db);
        CHECK_EQ(servo_limrun_overset(150u, k[i].db, 6002, 1u), k[i].overset);
        CHECK_EQ(g.run.v1[SERVO_LIMRUN_LO], 0u);
        CHECK_EQ(g.run.d1_dus[SERVO_LIMRUN_LO], 0);
        CHECK_EQ(g.run.v2[SERVO_LIMRUN_LO], k[i].v2);
        CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), k[i].v2);
        CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), 150u);
        CHECK_EQ(g.run.writes, 1u);
        CHECK(strstr(g.report, "overset -, share -") != NULL);
    }
    /* Further in, the overset is written: bound at 950 us, the stop's
     * estimate, the last free pulse at 960 us, moves 540.2 us x 8 / 142 =
     * 30.4 us. */
    CHECK_EQ(servo_limrun_overset(150u, 5502, 6002, 1u), 142u);
}

/* The low stop from 920 to 1440 us in 3 us steps -- 580 us to 60 us from
 * NEUTRAL -- with the servo's pulses at 900 and 2100 us and with SET
 * 1000-2000 (stops from 1010 us): every run passes, the low end's full
 * travel inside the stop.  The overset from the first bound pulse keeps
 * the stop inside the pulse limit by 3 % of the share. */
TEST_CASE(every_stop_position_passes)
{
    for (int set = 0; set < 2; ++set) {
        unsigned runs = 0u;

        for (uint16_t stop = set ? 1010u : 920u; stop <= 1440u; stop += 3u) {
            rig_init(0u, false);
            g.cfg.set_pulses = set != 0;
            g.servo.cfg.stop_lo_us = stop;
            g.servo.cfg.stop_hi_us = 2150u;
            const float depth = (float)g.servo.cfg.bind_us * 0.15f
                                / (g.servo.cfg.stall_a - g.servo.cfg.free_a)
                                + 0.5f;
            CHECK(start());
            run_for(RUN_MS);
            ++runs;
            if (g.run.verdict != SERVO_LIMRUN_PASS) {
                T_FAIL("stop %u us, SET %d: verdict %d why %d", stop, set,
                       (int)g.run.verdict, (int)g.run.why);
                continue;
            }
            /* Inside the stop by the model's own resolution: a push of
             * up to 45 us x 0.15 A / 2.28 A = 3.0 us draws less than the
             * +0.15 A a bind needs, and the command rounds to 0.5 us. */
            CHECK(sim_horn_us(g.reg, set ? 1000u : 900u, g.upc, false)
                  > (float)stop - depth);
            CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
        }
        CHECK_EQ(runs, set ? 144u : 174u);
    }
}

/* Free after the overset: the stop the overset was to keep in reach is not
 * there -- the servo's travel is not linear in the register.  FAIL, no
 * final value, RESTORE offered. */
TEST_CASE(free_after_the_overset_fails)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.shrink = 0.75f;           /* 91 counts: 273 us, short of 350 */
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_STOP);
        CHECK_EQ(servo_limrun_why_str(&g.run), SERVO_STR_L_WHY_NO_STOP);
        CHECK_EQ(g.run.v2[SERVO_LIMRUN_LO], 0u);
        CHECK(servo_limrun_wrote(&g.run));
        CHECK(servo_limrun_restore(&g.run));
        CHECK_EQ(g.offs, 0u);
        CHECK(strstr(g.report, "no stop after the overset write") != NULL);
        CHECK(strstr(g.report, "RESTORE") != NULL);
    }
}

/* A square-law servo: the share the search measures is not the share of
 * the register.  The final value still binds: FAIL at the check, no write
 * after it, RESTORE offered. */
TEST_CASE(a_servo_that_is_not_linear_fails_at_the_check)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.square = true;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_STILL_BINDS);
        CHECK(g.run.after[SERVO_LIMRUN_LO].bound
              || g.run.after[SERVO_LIMRUN_HI].bound);
        /* Two writes a side and the mapping's, no correction after. */
        CHECK_EQ(g.run.write_ops, rev ? 4u : 3u);
        CHECK(servo_limrun_restore(&g.run));
        CHECK_EQ(g.offs, 0u);
        CHECK(strstr(g.report, "an end binds after programming") != NULL);
        CHECK(strstr(g.report, "RESTORE") != NULL);
    }
}

/* One side binds: the other register keeps the servo's own value, also
 * where the mapping lowered it first (REVERSION on: LEFT ANGLE limits the
 * long pulses, the bound side is the short one).  The free side's search
 * runs to its pulse limit and no further. */
TEST_CASE(one_side_bound_leaves_the_other_register_as_it_was)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.servo.cfg.stop_hi_us = 2150u;     /* past PULSE UPPER */
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        const uint16_t keep = 150u;
        if (rev == 0) {
            CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), keep);
            CHECK(reg_get(KST_F_LEFT_ANGLE) < keep);
            CHECK_EQ(g.run.map_tries, 1u);
        } else {
            CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), keep);
            CHECK(reg_get(KST_F_RIGHT_ANGLE) < keep);
            /* LEFT ANGLE tried first, and it moved nothing. */
            CHECK_EQ(g.run.map_tries, 2u);
        }
        CHECK(!g.run.first[SERVO_LIMRUN_HI].bound);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_HI].limit_us, 2100u);
        CHECK(!g.run.after[SERVO_LIMRUN_HI].bound);
        CHECK_EQ(g.cmd_hi, 2100u);
        CHECK(strstr(g.report, "free to 2100 us") != NULL);
    }
}

/* The smallest value the hard rules take: the overset and the final value
 * stop there, and a side that still binds fails as that, RESTORE named. */
TEST_CASE(a_servo_bound_at_the_smallest_value_fails_clearly)
{
    rig_init(0u, false);
    g.lo_raw = 100u;            /* 100 counts: 400 us, past both stops */
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SMALLEST);
    CHECK_EQ(g.smallest, 100u);
    CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
    CHECK_EQ(g.offs, 0u);
    CHECK(servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "binds at the smallest angle") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* LEFT ANGLE at the smallest value already: the mapping starts on RIGHT
 * ANGLE; with both there and the end still bound, FAIL without a write. */
TEST_CASE(registers_at_the_smallest_value_already)
{
    rig_init(0u, false);
    g.lo_raw = 100u;
    reg_set(KST_F_LEFT_ANGLE, 100u);
    reg_set(KST_F_RIGHT_ANGLE, 100u);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SMALLEST);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.leave_asks, 1u);     /* the read session's */
    CHECK(!servo_limrun_restore(&g.run));

    rig_init(0u, true);             /* LEFT at 60 limits the long pulses */
    g.lo_raw = 60u;
    reg_set(KST_F_LEFT_ANGLE, 60u);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.map_tries, 2u);
    CHECK_EQ(g.run.reg_side[SERVO_LIMRUN_RIGHT], SERVO_LIMRUN_LO);
    CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), 60u);
    CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), 86u);
}

/* Neither end binds out to the pulse limits: FREE, nothing written, the
 * read session the only one. */
TEST_CASE(a_servo_that_never_binds_is_not_programmed)
{
    rig_init(0u, false);
    g.servo.cfg.stop_lo_us = 850u;
    g.servo.cfg.stop_hi_us = 2150u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FREE);
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.disarms, 1u);
    CHECK_EQ(g.cmd_lo, 900u);
    CHECK_EQ(g.cmd_hi, 2100u);
    CHECK(g.run.first[0].done && !g.run.first[0].bound);
    CHECK(g.run.first[1].done && !g.run.first[1].bound);
    CHECK(strstr(g.report, "neither end binds") != NULL);
    CHECK(strstr(g.report, "RESTORE") == NULL);
}

/* A free end not past NEUTRAL: the share is 0 or less, FAIL before any
 * write.  The search starts at NEUTRAL rounded to the microsecond and
 * takes three free positions for its baseline, so a real search does not
 * end there; the test moves NEUTRAL past the high end found (white box). */
TEST_CASE(a_free_end_not_past_neutral_fails_before_a_write)
{
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
    g.run.pulse_now[SERVO_LIMRUN_P_NEUTRAL] = 19000u;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BEFORE_NEUTRAL);
    CHECK_EQ(g.write_ops, 0u);
    CHECK(!servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "free end not past NEUTRAL") != NULL);
}

/* The INA3221's shunt fitted the other way round: the windows read
 * negative, a bound servo more negative.  The search reads the magnitude,
 * as the protections do, and finds both ends; the run programs them. */
TEST_CASE(a_reversed_shunt_finds_the_bound_ends)
{
    for (int prog = 0; prog < 2; ++prog) {
        rig_init(0u, false);
        g.neg = true;
        g.cfg.program = prog != 0;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, prog ? SERVO_LIMRUN_PASS : SERVO_LIMRUN_FOUND);
        CHECK(g.run.first[SERVO_LIMRUN_LO].bound);
        CHECK(g.run.first[SERVO_LIMRUN_HI].bound);
        CHECK(g.run.first[SERVO_LIMRUN_LO].baseline_a > 0.0f);
        if (prog) {
            CHECK(full_us(SERVO_LIMRUN_LO, reg_get(KST_F_LEFT_ANGLE))
                  > 1150.0f);
            CHECK(full_us(SERVO_LIMRUN_HI, reg_get(KST_F_RIGHT_ANGLE))
                  < 1880.0f);
        }
    }
}

/* Search only on the programming port: the read, then the search out to
 * the servo's pulse limits; the endpoints, nothing written. */
TEST_CASE(search_only_reports_the_endpoints)
{
    rig_init(0u, false);
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.write_ops, 0u);
    read_before_drive();
    CHECK(g.run.first[0].bound && g.run.first[1].bound);
    CHECK(g.run.first[0].limit_us > 1150u && g.run.first[0].limit_us < 1200u);
    CHECK(g.run.first[1].limit_us < 1880u && g.run.first[1].limit_us > 1830u);
    CHECK(strstr(g.report, "search only") != NULL);
    CHECK(strstr(g.report, "Searched:       from 1500 us out to 900 us and "
                           "2100 us") != NULL);
    /* Nothing sent, so no backup: the line is left out. */
    CHECK(strstr(g.report, "Writes:         values written 0, writes "
                           "sent 0") != NULL);
    CHECK(strstr(g.report, "Backup:") == NULL);
}

/* The PWM pin: no read, no session, the search out to the bench's PULSE
 * MIN and PULSE MAX, past where a servo's own pulse limits would lie. */
TEST_CASE(the_pwm_pin_searches_out_to_min_and_max)
{
    rig_init(0u, false);
    rig_pin();
    g.servo.cfg.stop_lo_us = 850u;
    g.servo.cfg.stop_hi_us = 2150u;
    CHECK(start());
    CHECK_EQ(g.run.phase, SERVO_LIMRUN_PH_SEARCH);
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FREE);
    CHECK_EQ(g.connects, 0u);
    CHECK_EQ(g.disarms, 0u);
    CHECK_EQ(g.cmd_lo, MIN_US);
    CHECK_EQ(g.cmd_hi, MAX_US);
    CHECK(strstr(g.report, "Searched:       from 1500 us out to 800 us and "
                           "2200 us") != NULL);
    CHECK(strstr(g.report, "Servo pulses") == NULL);

    /* Bound ends: FOUND, the bench never switched. */
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.connects, 0u);
    CHECK_EQ(g.disarms, 0u);

    /* Programming and the pulse write need the read: refused. */
    rig_init(0u, false);
    rig_pin();
    g.cfg.program = true;
    CHECK(!start());
    g.cfg.program = false;
    g.cfg.set_pulses = true;
    CHECK(!start());
    CHECK(!servo_limrun_running(&g.run));
}

/* --- the servo's pulses -------------------------------------------------- */

/* The bench's PULSE MIN to PULSE MAX narrower than the servo's PULSE LOWER
 * to PULSE UPPER, on either side: refused after the read, the bench left
 * SAFE, nothing driven, both ranges named. */
TEST_CASE(a_bench_range_narrower_than_the_servos_is_refused)
{
    for (int side = 0; side < 2; ++side) {
        rig_init(0u, side != 0);
        if (side == 0) {
            g.cfg.min_us = 950u;
        } else {
            g.cfg.max_us = 2050u;
        }
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_RANGE);
        CHECK(!g.driven);
        CHECK_EQ(g.connects, 1u);
        CHECK_EQ(g.write_ops, 0u);
        CHECK_EQ(g.leave_asks, 1u);
        CHECK_EQ(g.arms, 0u);
        CHECK(g.safe);
        CHECK(!g.live);
        CHECK_EQ(g.offs, 0u);
        CHECK(!servo_limrun_restore(&g.run));
        char want[96];
        snprintf(want, sizeof(want),
                 "servo pulses 900.0-2100.0 us outside PULSE MIN/MAX "
                 "%u-%u us", g.cfg.min_us, g.cfg.max_us);
        CHECK(strstr(g.report, want) != NULL);
        CHECK(strstr(g.report, "servo pulses outside MIN/MAX") != NULL);
        /* Nothing searched: no line says where. */
        CHECK(strstr(g.report, "Searched:") == NULL);
    }
    /* Equal to them is inside. */
    rig_init(0u, false);
    g.cfg.min_us = 900u;
    g.cfg.max_us = 2100u;
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
}

/* NEUTRAL not between PULSE LOWER and PULSE UPPER at the read: no search. */
TEST_CASE(a_neutral_outside_the_pulse_limits_is_refused)
{
    rig_init(0u, false);
    rig_pulses(15500, 21000);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NEUTRAL);
    CHECK(!g.driven);
    CHECK_EQ(g.write_ops, 0u);
    CHECK(strstr(g.report, "NEUTRAL outside LOWER/UPPER") != NULL);
}

/* SERVO PULSES at 1000-2000: PULSE LOWER and PULSE UPPER written in the
 * read session before anything moves, and read back; the search runs out
 * to them; the angles are programmed after it as without it, the share
 * from the pulses written.  REVERSION on, the most a run writes: the two
 * pulses, two a side and the mapping's two. */
TEST_CASE(set_1000_2000_writes_the_pulses_before_the_search)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.cfg.set_pulses = true;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        read_before_drive();
        CHECK_EQ(g.pulse_ops_at_drive, 1u);
        CHECK_EQ(g.pulse_ops, 1u);
        uint16_t p[3];
        pulses_of(p);
        CHECK_EQ(p[0], NEUTRAL_DUS);
        CHECK_EQ(p[1], 10000u);
        CHECK_EQ(p[2], 20000u);
        CHECK_EQ(g.run.end_us[SERVO_LIMRUN_LO], 1000u);
        CHECK_EQ(g.run.end_us[SERVO_LIMRUN_HI], 2000u);
        /* The share from the pulses written, not those read: 1000.0 and
         * 2000.0 us, spans of 500.2 and 499.8 us. */
        for (int s = 0; s < 2; ++s) {
            const int32_t span = s == SERVO_LIMRUN_LO ? 5002 : 4998;
            const int32_t n = NEUTRAL_DUS;
            const int32_t b = (int32_t)g.run.first[s].bind_us * 10;
            CHECK_EQ(g.run.db_dus[s], s == SERVO_LIMRUN_LO ? n - b : b - n);
            CHECK_EQ(g.run.v1[s],
                     servo_limrun_overset(150u, g.run.db_dus[s], span, 1u));
            CHECK_EQ(g.run.v2[s],
                     servo_limrun_scaled(g.run.v1[s], g.run.d1_dus[s], span,
                                         1u));
        }
        CHECK_EQ(g.cmd_lo, 1000u);
        CHECK_EQ(g.cmd_hi, 2000u);
        CHECK_EQ(g.run.writes, rev ? 8u : 6u);
        CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
        CHECK_EQ(g.run.writes, g.values);
        CHECK(full_us(SERVO_LIMRUN_LO, reg_get(rev ? KST_F_RIGHT_ANGLE
                                                   : KST_F_LEFT_ANGLE))
              > 1150.0f);
        CHECK(strstr(g.report, "Pulse write:    PULSE LOWER 900.0 -> 1000.0 "
                               "us, PULSE UPPER 2100.0 -> 2000.0 us") != NULL);
        /* The values before the run, for RESTORE. */
        CHECK_EQ(g.run.servo.pulse_dus[SERVO_LIMRUN_P_LOWER], LOWER_DUS);
        CHECK_EQ(g.run.servo.pulse_dus[SERVO_LIMRUN_P_UPPER], UPPER_DUS);
    }
}

/* At 1000.0 and 2000.0 already: no pulse write. */
TEST_CASE(set_1000_2000_on_a_servo_there_already_writes_nothing)
{
    rig_init(0u, false);
    rig_pulses(10000, 20000);
    g.cfg.set_pulses = true;
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.run.writes, 0u);
    CHECK(strstr(g.report, "Pulse write:    PULSE LOWER 1000.0 -> 1000.0 us")
          != NULL);
}

/* Search only with SERVO PULSES 1000-2000: the pulses are the only write. */
TEST_CASE(search_only_with_set_1000_2000_writes_only_the_pulses)
{
    rig_init(0u, false);
    g.cfg.set_pulses = true;
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.write_ops, 1u);
    CHECK_EQ(g.pulse_ops, 1u);
    CHECK_EQ(g.run.writes, 2u);
    CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), 150u);
    CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), 150u);
    CHECK_EQ(g.connects, 1u);
    CHECK(strstr(g.report, "search only; PULSE LOWER") != NULL);
    CHECK(!servo_limrun_restore(&g.run));
}

/* NEUTRAL outside 1000-2000 after the write: FAIL before anything moves,
 * RESTORE offered for the pulses written. */
TEST_CASE(set_1000_2000_with_neutral_outside_fails_before_the_search)
{
    rig_init(0u, false);
    uint16_t raw = 0u;
    CHECK(kst_field_raw_from_display(KST_F_NEUTRAL, 20500, &raw));
    reg_set(KST_F_NEUTRAL, raw);
    g.cfg.set_pulses = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NEUTRAL);
    CHECK(!g.driven);
    CHECK_EQ(g.pulse_ops, 1u);
    CHECK_EQ(g.leave_asks, 1u);
    CHECK(servo_limrun_wrote(&g.run));
    CHECK(servo_limrun_restore(&g.run));
    CHECK(g.safe);
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* The pulse write refused, or lost on the line: ABORTED before anything
 * moves. */
TEST_CASE(a_pulse_write_refused_or_lost_aborts_before_the_search)
{
    rig_init(0u, false);
    g.cfg.set_pulses = true;
    g.refuse_write = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK(!g.driven);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK(servo_limrun_restore(&g.run));     /* sent: it may have landed */
    CHECK(strstr(g.report, "RESTORE") != NULL);

    rig_init(0u, false);
    g.cfg.set_pulses = true;
    g.lose_write = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
    CHECK(!g.driven);
}

/* The read fails, or the programmer does not answer it: the run ends
 * before anything is driven. */
TEST_CASE(a_read_that_fails_ends_the_run_before_a_drive)
{
    rig_init(0u, false);
    g.not_kst = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NOT_KST);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.offs, 0u);
    CHECK(!g.driven);
    CHECK(strstr(g.report, "no KST or Chaservo servo read") != NULL);

    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_CONNECT, RUN_MS));
    g.op = OP_NONE;
    servo_limrun_refused(&g.run, g.now);
    run_for(100u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK(!g.driven);

    rig_init(0u, false);
    g.prog_silent = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK(!g.driven);
    CHECK_EQ(g.offs, 1u);
}

/* --- the write cap ------------------------------------------------------- */

/* Told it has written 7 values once the first side is mapped, the run
 * needs 2 more for its next write: FAIL at the cap, never past it.  The
 * cap is reached exactly by REVERSION on with SERVO PULSES 1000-2000
 * (set_1000_2000_writes_the_pulses_before_the_search). */
TEST_CASE(the_write_cap_is_never_passed)
{
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 1u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE_CAP);
    CHECK_EQ(g.run.writes, SERVO_LIMRUN_WRITES_MAX - 1u);
    CHECK_EQ(g.write_ops, 1u);
    CHECK(servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "write cap of 8 values reached") != NULL);

    /* Room for exactly the 2: written up to the cap, then the next value
     * fails. */
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 2u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE_CAP);
    CHECK_EQ(g.run.writes, SERVO_LIMRUN_WRITES_MAX);
    CHECK_EQ(g.write_ops, 2u);

    /* One value more of room: the run ends at the cap, PASS. */
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 3u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.writes, SERVO_LIMRUN_WRITES_MAX);
}

/* --- the programmer's failures ------------------------------------------- */

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
    CHECK(servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "a write refused or lost") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);

    /* The first write fails once sent: no value is known written, but
     * part of it may have landed, so RESTORE is offered, and the
     * programmer holds the values before the run. */
    rig_init(0u, false);
    g.refuse_write = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK(servo_limrun_restore(&g.run));
    CHECK_EQ(g.run.write_ops, 1u);
    CHECK(strstr(g.report, "Writes:         values written 0, writes "
                           "sent 1") != NULL);
    CHECK(strstr(g.report, "Backup:") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);

    /* The first write refused before it is sent: nothing written, no
     * backup, no RESTORE line. */
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_WRITE, RUN_MS));
    g.op = OP_NONE;
    servo_limrun_refused(&g.run, g.now);
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK_EQ(g.run.write_ops, 0u);
    CHECK(!servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "Backup:") == NULL);
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

/* A later session reads the same two values from another servo, or from
 * one set otherwise: another type, REVERSION switched, another range,
 * another PULSE LOWER.  The run ends before it writes again, and RESTORE
 * is offered. */
TEST_CASE(another_servo_at_a_later_connect_aborts)
{
    for (int k = 0; k < 5; ++k) {
        rig_init(0u, false);
        g.change_at = 3u;          /* the second writing session */
        g.change_kind = k;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
        CHECK_EQ(g.connects, 3u);
        CHECK_EQ(g.write_ops, 1u);
        CHECK_EQ(g.offs, 1u);
        CHECK(strstr(g.report, "RESTORE") != NULL);
    }
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
    /* The read session's LEAVE: nothing written, nothing driven. */
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK(!g.driven);
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
    rig_pin();
    CHECK(start());
    run_for(1000u);
    servo_limrun_refused(&g.run, g.now);
    CHECK(servo_limrun_running(&g.run));
}

/* --- aborts at every phase ----------------------------------------------- */

/* Every phase, at each place it comes in a run: the read session, before
 * anything moves (with SERVO PULSES 1000-2000, its pulse write too); the first
 * search; the sessions and searches after the first write. */
typedef enum { AT_READ = 0, AT_PULSES, AT_SEARCH, AT_LATER } at_t;

static const struct {
    servo_limrun_phase_t ph;
    at_t at;
    bool wrote;
} k_phases[] = {
    { SERVO_LIMRUN_PH_SAFE,    AT_READ,   false },
    { SERVO_LIMRUN_PH_CONNECT, AT_READ,   false },
    { SERVO_LIMRUN_PH_LEAVE,   AT_READ,   false },
    { SERVO_LIMRUN_PH_ARM,     AT_READ,   false },
    { SERVO_LIMRUN_PH_WRITE,   AT_PULSES, false },
    { SERVO_LIMRUN_PH_LEAVE,   AT_PULSES, true },
    { SERVO_LIMRUN_PH_ARM,     AT_PULSES, true },
    { SERVO_LIMRUN_PH_SEARCH,  AT_SEARCH, false },
    { SERVO_LIMRUN_PH_SAFE,    AT_LATER,  true },
    { SERVO_LIMRUN_PH_CONNECT, AT_LATER,  true },
    { SERVO_LIMRUN_PH_WRITE,   AT_LATER,  true },
    { SERVO_LIMRUN_PH_LEAVE,   AT_LATER,  true },
    { SERVO_LIMRUN_PH_ARM,     AT_LATER,  true },
    { SERVO_LIMRUN_PH_PROBE,   AT_LATER,  true },
    { SERVO_LIMRUN_PH_CHECK,   AT_LATER,  true },
};
#define N_PHASES (sizeof(k_phases) / sizeof(k_phases[0]))

/* The run in phase @p ph at place @p at. */
static bool to_phase_at(servo_limrun_phase_t ph, at_t at)
{
    rig_init(0u, false);
    g.cfg.set_pulses = at == AT_PULSES;
    if (!start()) {
        return false;
    }
    if (at == AT_LATER && ph != SERVO_LIMRUN_PH_CHECK
        && ph != SERVO_LIMRUN_PH_PROBE) {
        /* The second session's phase: a value is written by then. */
        if (!run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS)) {
            return false;
        }
    }
    if (!run_to(ph, RUN_MS)) {
        return false;
    }
    if (at == AT_READ || at == AT_PULSES) {
        return !g.run.searched;
    }
    return g.run.searched;
}

/* The phase after the first write, as most tests below want it. */
static bool to_phase(servo_limrun_phase_t ph)
{
    return to_phase_at(ph, ph == SERVO_LIMRUN_PH_SEARCH ? AT_SEARCH
                                                        : AT_LATER);
}

/* Each reason the caller has: in every phase the run ends ABORTED, asks
 * the output off and lets the servo go, once; RESTORE is named where a
 * value was written. */
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
            if (!to_phase_at(k_phases[p].ph, k_phases[p].at)) {
                T_FAIL("phase %d at %d not reached", (int)k_phases[p].ph,
                       (int)k_phases[p].at);
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
            CHECK_EQ(wrote, k_phases[p].wrote);
            CHECK_EQ(servo_limrun_restore(&g.run), wrote);
            CHECK_EQ(strstr(g.report, "RESTORE") != NULL, wrote);
            if (k_phases[p].at == AT_READ || k_phases[p].at == AT_PULSES) {
                CHECK(!g.driven);
            }
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
        CHECK(to_phase_at(k_phases[p].ph, k_phases[p].at));
        g.windows = false;
        run_for(1000u);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
        CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
        CHECK_EQ(g.offs, 1u);
    }

    /* SAFE not taken: at the read session, and before a later one. */
    rig_init(0u, false);
    g.bench_ms = 0xFFFFFFFFu;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);
    CHECK(!g.driven);
    CHECK_EQ(g.connects, 0u);

    CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
    run_for(1u);
    g.bench_ms = 0xFFFFFFFFu;
    CHECK(run_to(SERVO_LIMRUN_PH_SAFE, RUN_MS));
    run_for(SERVO_LIMRUN_RETURN_MS + SERVO_LIMRUN_BENCH_MS + 100u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);

    /* ARMED not taken: after the read session, and after a write. */
    for (int k = 0; k < 2; ++k) {
        CHECK(to_phase_at(SERVO_LIMRUN_PH_LEAVE, k == 0 ? AT_READ : AT_LATER));
        g.bench_ms = 0xFFFFFFFFu;
        run_for(RUN_MS);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);
        CHECK_EQ(g.run.end_ms - g.run.phase_ms, SERVO_LIMRUN_BENCH_MS);
        CHECK_EQ(g.driven, k != 0);
    }

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
    rig_pin();
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

/* Before every session the servo goes back to the centre from the end the
 * search left it at, and SAFE is asked SERVO_LIMRUN_RETURN_MS later, not
 * sooner: a session never starts with the servo against an end.  A disarm
 * while it returns is the operator's and ends the run.  The read session
 * at the start holds PULSE CENTRE the same way. */
TEST_CASE(a_session_starts_with_the_servo_back_at_the_centre)
{
    for (int k = 0; k < 2; ++k) {
        rig_init(0u, false);
        CHECK(start());
        CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
        CHECK(run_to(SERVO_LIMRUN_PH_SAFE, RUN_MS));
        const uint32_t at = g.run.phase_ms;
        /* The horn still out at the end the search reached. */
        CHECK(fabsf(g.servo.position_us - (float)CENTRE) > 200.0f);
        while (g.now - at < 20u) {
            tick();
        }
        CHECK_EQ(g.cmd, CENTRE);
        CHECK(!servo_limrun_switching(&g.run));
        if (k == 1) {
            g.live = false;
            run_for(100u);
            CHECK(!servo_limrun_running(&g.run));
            CHECK_EQ(g.run.test_why, SERVO_TEST_AB_DISARMED);
            CHECK_EQ(g.disarms, 1u);        /* the read session's */
            CHECK_EQ(g.offs, 1u);
            continue;
        }
        const unsigned disarms = g.disarms;
        while (g.now - at < SERVO_LIMRUN_RETURN_MS - 1u) {
            tick();
        }
        CHECK_EQ(g.disarms, disarms);
        while (g.now - at < SERVO_LIMRUN_RETURN_MS + 10u) {
            tick();
        }
        CHECK_EQ(g.disarms, disarms + 1u);
        CHECK(servo_limrun_switching(&g.run));
        CHECK(run_to(SERVO_LIMRUN_PH_CONNECT, 5000u));
    }

    /* The start: PULSE CENTRE commanded, SAFE asked RETURN_MS later. */
    rig_init(0u, false);
    CHECK(start());
    CHECK_EQ(g.run.phase, SERVO_LIMRUN_PH_SAFE);
    tick();
    while (g.now - g.t0 < SERVO_LIMRUN_RETURN_MS - 1u) {
        tick();
    }
    CHECK_EQ(g.cmd, CENTRE);
    CHECK_EQ(g.disarms, 0u);
    while (g.now - g.t0 < SERVO_LIMRUN_RETURN_MS + 10u) {
        tick();
    }
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.driven);
}

/* --- the protections ----------------------------------------------------- */

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
        const servo_limrun_phase_t ph = k_phases[p].ph;
        const at_t at = k_phases[p].at;
        CHECK(to_phase_at(ph, at));
        servo_test_win_t w = win_at(g.now, 200, 2999);
        servo_limrun_window(&g.run, &w);
        CHECK(servo_limrun_running(&g.run));
        w = win_at(g.now, 200, 3000);
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);
        run_for(20u);
        CHECK_EQ(g.offs, 1u);

        CHECK(to_phase_at(ph, at));
        w = win_at(g.now, 200, -3000);
        w.min_ma = -3000;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);

        /* The reading's most negative value, -32768 mA, in mean, minimum
         * and maximum and not marked clipped: 32768 mA as a magnitude, not
         * a negative value under the ceiling. */
        CHECK(to_phase_at(ph, at));
        w = win_at(g.now, INT16_MIN, INT16_MIN);
        w.min_ma = INT16_MIN;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);
        run_for(20u);
        CHECK_EQ(g.offs, 1u);

        CHECK(to_phase_at(ph, at));
        w = win_at(g.now, 200, 300);
        w.clipped = 1u;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_CLIPPED);

        /* Above 1.0 A for 400 ms, in every phase too. */
        CHECK(to_phase_at(ph, at));
        const uint32_t t = g.now;
        for (uint32_t m = 1; m <= 8u; ++m) {
            w = win_at(t + 50u * m, 1001, 1100);
            servo_limrun_window(&g.run, &w);
        }
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_STALL);
        run_for(20u);
        CHECK_EQ(g.offs, 1u);
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
    CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
    run_for(500u);
    g.sup.set_v = 7.4f;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
    CHECK_EQ(g.offs, 1u);

    /* The rail 1.0 V low: runs on; 1.001 V low: ends. */
    CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
    run_for(500u);
    g.rail_mv = 5000u;
    run_for(500u);
    CHECK(servo_limrun_running(&g.run));
    g.rail_mv = 4999u;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);

    /* Output off, trip, gone: the caller's reasons, also by this path. */
    for (int k = 0; k < 3; ++k) {
        CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
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

    /* During a programmer's session -- the read session and a later one --
     * the rail dips and the set point is the session's: the run goes on,
     * and is held to the start's values again once a search drives. */
    for (int k = 0; k < 2; ++k) {
        CHECK(to_phase_at(SERVO_LIMRUN_PH_CONNECT, k == 0 ? AT_READ
                                                          : AT_LATER));
        g.rail_mv = 4200u;
        g.sup.set_v = 5.0f;
        run_for(300u);
        CHECK(servo_limrun_running(&g.run));
        g.rail_mv = 6000u;
        g.sup.set_v = 6.0f;
        CHECK(run_to(k == 0 ? SERVO_LIMRUN_PH_SEARCH : SERVO_LIMRUN_PH_PROBE,
                     RUN_MS));
        run_for(2000u);
        CHECK(servo_limrun_running(&g.run));
        g.sup.set_i = 2.0f;
        run_for(500u);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
    }
}

/* Outside the programmer's sessions the supply stays the run's: in SAFE
 * before a session and while the bench goes back to ARMED, a supply that
 * goes, trips, switches off or changes its set point ends the run with the
 * output off.  Aborted while it waits for ARMED, it asks SAFE, so an ARM
 * not yet taken does not arm the bench after the end. */
TEST_CASE(the_supply_is_watched_in_safe_and_on_the_way_to_armed)
{
    static const struct {
        servo_limrun_phase_t ph;
        at_t at;
    } k_ph[] = {
        { SERVO_LIMRUN_PH_SAFE, AT_READ }, { SERVO_LIMRUN_PH_ARM, AT_READ },
        { SERVO_LIMRUN_PH_SAFE, AT_LATER }, { SERVO_LIMRUN_PH_ARM, AT_LATER },
    };
    for (size_t p = 0; p < 4u; ++p) {
        for (int k = 0; k < 5; ++k) {
            CHECK(to_phase_at(k_ph[p].ph, k_ph[p].at));
            /* The supply's first reading is its reference. */
            while (!g.run.sup_watch) {
                tick();
            }
            CHECK_EQ(g.run.phase, k_ph[p].ph);
            /* The bench takes its time: the phase lasts past a reading. */
            g.bench_ms = 2000u;
            const unsigned disarms = g.disarms;
            if (k == 0) {
                g.sup.output = false;
            } else if (k == 1) {
                g.sup.trip = 1u;
            } else if (k == 2) {
                g.sup.online = false;
            } else if (k == 3) {
                g.sup.set_v = 7.4f;
            } else {
                g.sup.set_i = 2.0f;
            }
            run_for(300u);
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            if (k < 3) {
                CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
                CHECK_EQ(g.run.test_why, k == 0 ? SERVO_TEST_AB_SUPPLY_OFF
                                       : k == 1 ? SERVO_TEST_AB_TRIPPED
                                                : SERVO_TEST_AB_SUPPLY_LOST);
            } else {
                CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
            }
            CHECK_EQ(g.offs, 1u);
            CHECK_EQ(g.releases, 1u);
            const bool later = k_ph[p].at == AT_LATER;
            CHECK_EQ(servo_limrun_wrote(&g.run), later);
            CHECK_EQ(strstr(g.report, "RESTORE") != NULL, later);
            if (k_ph[p].ph == SERVO_LIMRUN_PH_ARM) {
                /* The bench ends SAFE, with the ARM asked before. */
                CHECK_EQ(g.disarms, disarms + 1u);
                idle_for(2100u);
                CHECK(!g.live);
                CHECK(g.safe);
            } else {
                /* Before SAFE is asked, as in a search: no SAFE asked. */
                CHECK_EQ(g.disarms, disarms);
            }
        }
    }
}

/* A supply that answers with its output off -- the rail fed from
 * elsewhere -- is watched as well: the run passes while it stays off, a
 * trip flag on it ends nothing, and its output switched on outside a
 * session ends the run as a changed supply.  Switched on inside a session,
 * the session runs on and the run ends once it is back. */
TEST_CASE(a_supply_off_at_the_start_that_switches_on_ends_the_run)
{
    rig_init(0u, false);
    g.sup.output = false;
    g.sup.trip = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK(g.run.sup_watch);
    CHECK(!g.run.sup_out);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);

    static const struct {
        servo_limrun_phase_t ph;
        at_t at;
    } k_ph[] = {
        { SERVO_LIMRUN_PH_SAFE, AT_READ },   { SERVO_LIMRUN_PH_SEARCH, AT_SEARCH },
        { SERVO_LIMRUN_PH_ARM, AT_LATER },   { SERVO_LIMRUN_PH_PROBE, AT_LATER },
        { SERVO_LIMRUN_PH_CONNECT, AT_READ }, { SERVO_LIMRUN_PH_CONNECT, AT_LATER },
    };
    for (size_t p = 0; p < sizeof(k_ph) / sizeof(k_ph[0]); ++p) {
        rig_init(0u, false);
        g.sup.output = false;
        CHECK(start());
        if (k_ph[p].at == AT_LATER && k_ph[p].ph != SERVO_LIMRUN_PH_PROBE) {
            CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
        }
        CHECK(run_to(k_ph[p].ph, RUN_MS));
        for (int k = 0; k < 1000 && !g.run.sup_watch; ++k) {
            tick();
        }
        CHECK(g.run.sup_watch);
        CHECK_EQ(g.run.phase, k_ph[p].ph);
        g.bench_ms = 2000u;
        g.sup.output = true;
        if (k_ph[p].ph == SERVO_LIMRUN_PH_CONNECT) {
            /* The session's: it runs on, and the run ends after it. */
            run_for(300u);
            CHECK(servo_limrun_running(&g.run));
        }
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
        CHECK_EQ(g.offs, 1u);
        CHECK_EQ(g.releases, 1u);
    }
}

/* A supply that does not answer at the start: the run passes while it
 * stays away.  It answers later, its output off: the run ends before the
 * next CONNECT, which would switch it on beside the rail's feed.  It
 * answers later with its output on, in SAFE, a search or on the way to
 * ARMED: the run ends as a changed supply. */
TEST_CASE(a_supply_that_answers_late_ends_the_run)
{
    rig_init(0u, false);
    g.sup.online = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK(g.run.sup_watch);
    CHECK(!g.run.sup_online);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);

    rig_init(0u, false);
    g.sup.online = false;
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
    const unsigned c0 = g.connects;
    g.sup.online = true;
    g.sup.output = false;
    for (int k = 0; k < 100000 && servo_limrun_running(&g.run)
                    && g.connects == c0; ++k) {
        tick();
    }
    CHECK(!servo_limrun_running(&g.run));
    CHECK_EQ(g.connects, c0);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
    CHECK_EQ(g.offs, 1u);

    static const servo_limrun_phase_t k_ph[] = {
        SERVO_LIMRUN_PH_SAFE, SERVO_LIMRUN_PH_SEARCH, SERVO_LIMRUN_PH_ARM,
    };
    for (size_t q = 0; q < 2u * sizeof(k_ph) / sizeof(k_ph[0]); ++q) {
        const size_t p = q / 2u;
        rig_init(0u, false);
        g.sup.online = false;
        CHECK(start());
        CHECK(run_to(k_ph[p], RUN_MS));
        for (int k = 0; k < 1000 && !g.run.sup_watch; ++k) {
            tick();
        }
        CHECK(g.run.sup_watch);
        CHECK_EQ(g.run.phase, k_ph[p]);
        g.bench_ms = 2000u;
        g.sup.online = true;
        g.sup.output = (q % 2u) != 0u;
        run_for(300u);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
        CHECK_EQ(g.offs, 1u);
    }
}

/* Every search window holds 3.0 V on the rail, the first included: a rail
 * that goes off in the read session ends the run at the first search
 * window.  The search holds the centre for SERVO_LIMRUN_RETURN_MS first;
 * the run is followed past that, so a run that went on would have
 * stepped.  3000 mV searches on and steps. */
TEST_CASE(a_rail_under_3_v_in_any_search_window_ends_the_run)
{
    for (int k = 0; k < 3; ++k) {
        rig_init(0u, false);
        CHECK(start());
        CHECK(run_to(SERVO_LIMRUN_PH_ARM, RUN_MS));
        g.rail_mv = k == 0 ? 0u
                  : k == 1 ? (uint16_t)(SERVO_LIMRUN_RAIL_MIN_MV - 1u)
                           : (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV;
        CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
        run_for(SERVO_LIMRUN_RETURN_MS + 1000u);
        if (k < 2) {
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
            /* At the first window: within one window of the search. */
            CHECK((uint32_t)(g.run.end_ms - g.run.search_ms)
                  <= SERVO_TEST_WIN_MS);
            CHECK(!g.driven);                   /* no step taken */
            CHECK_EQ(g.offs, 1u);
        } else {
            CHECK(servo_limrun_running(&g.run));
            CHECK(g.driven);
            CHECK_EQ(g.run.rail_mv, (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV);
        }
    }
    /* A search already under way: the band of 1.0 V and the floor. */
    rig_init(0u, false);
    g.rail_mv = 3500u;
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
    run_for(1000u);
    CHECK(servo_limrun_running(&g.run));
    g.rail_mv = 2999u;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
}

/* The run aborted in the step after its ARM was asked, before the bench
 * took it: SAFE is asked behind it and the bench ends SAFE. */
TEST_CASE(an_abort_behind_the_arm_leaves_the_bench_safe)
{
    static const servo_test_abort_t k_why[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_LINK, SERVO_TEST_AB_INA_METER,
        SERVO_TEST_AB_LEFT,
    };
    for (int at = 0; at < 2; ++at) {
        for (size_t w = 0; w < sizeof(k_why) / sizeof(k_why[0]); ++w) {
            CHECK(to_phase_at(SERVO_LIMRUN_PH_ARM, at ? AT_LATER : AT_READ));
            CHECK_EQ(g.bench_to, 1);
            const unsigned arms = g.arms;
            servo_limrun_abort(&g.run, k_why[w], g.now);
            servo_limrun_do_t d;
            servo_limrun_in_t in;
            memset(&in, 0, sizeof(in));
            servo_limrun_step(&g.run, g.now, &in, &d);
            CHECK(d.disarm);
            CHECK(!d.arm);
            CHECK(d.off);
            CHECK(d.release);
            take(&d);
            idle_for(500u);
            CHECK_EQ(g.arms, arms);
            CHECK(!g.live);
            CHECK(g.safe);
        }
    }
    /* Aborted in a search, ARMED stays: SAFE is the operator's there. */
    CHECK(to_phase(SERVO_LIMRUN_PH_PROBE));
    const unsigned disarms = g.disarms;
    servo_limrun_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    run_for(100u);
    CHECK_EQ(g.disarms, disarms);
}

/* --- the meter's silence ------------------------------------------------- */

/* The last window at t: the run goes on at t + 199 ms and ends at t + 200
 * ms (also when the step comes at t + 201), at tick 0 and with the wrap
 * between the window and the step; on the programming port, where the run
 * starts with the servo held at the centre for the read, and on the PWM
 * pin, where it starts with a search. */
TEST_CASE(no_window_for_200_ms_ends_the_run)
{
    static const uint32_t k_after[] = { 199u, 200u, 201u };
    static const uint32_t k_t0[] = { 0u, 0u - 100u, 0u - 200u, 0u - 199u };
    CHECK_EQ(SERVO_LIMRUN_WIN_STALE_MS, 200u);
    for (int pin = 0; pin < 2; ++pin) {
        for (size_t z = 0; z < sizeof(k_t0) / sizeof(k_t0[0]); ++z) {
            for (size_t a = 0; a < 3u; ++a) {
                rig_init(k_t0[z], false);
                if (pin) {
                    rig_pin();
                }
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

/* --- windows without a quantity ------------------------------------------ */

/* A search's window without the rail voltage ends the run before it moves
 * the search: at the run's first window, in a search, a probe and the
 * check.  A programmer's session reads no rail, so there it changes
 * nothing, and the rail's reference stays the first search window's. */
TEST_CASE(a_window_without_the_rail_voltage_ends_a_search)
{
    /* The run's first window, on the PWM pin: a search from the start. */
    rig_init(0u, false);
    rig_pin();
    g.no_volt = true;
    CHECK(start());
    run_for(1000u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
    CHECK_EQ(g.run.end_ms, g.t0 + SERVO_TEST_WIN_MS);
    CHECK(!g.run.have_rail);
    CHECK_EQ(g.offs, 1u);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK_EQ(servo_limrun_why_str(&g.run), SERVO_STR_L_WHY_NO_VOLTAGE);
    CHECK(strstr(g.report, "no rail voltage in a window") != NULL);

    /* On the programming port the first search's first window. */
    rig_init(0u, false);
    g.no_volt = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
    CHECK(g.run.searched);
    CHECK_EQ(g.connects, 1u);

    /* SEARCH, PROBE and CHECK: the window ends the run and the search does
     * not take it.  After a write the values stay and RESTORE is offered. */
    static const servo_limrun_phase_t k_search[] = {
        SERVO_LIMRUN_PH_SEARCH, SERVO_LIMRUN_PH_PROBE, SERVO_LIMRUN_PH_CHECK,
    };
    for (size_t p = 0; p < 3u; ++p) {
        CHECK(to_phase(k_search[p]));
        run_for(2000u);
        CHECK(servo_limrun_running(&g.run));
        const uint16_t cmd = g.run.cmd_us;
        const uint16_t lf = g.run.lf.cmd_us;
        const uint32_t n = g.run.windows;
        servo_test_win_t w = win_at(g.now + 1u, 200, 250);
        w.voltage = false;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
        CHECK_EQ(g.run.end_ms, g.now + 1u);
        CHECK_EQ(g.run.cmd_us, cmd);
        CHECK_EQ(g.run.lf.cmd_us, lf);
        CHECK_EQ(g.run.windows, n + 1u);
        CHECK_EQ(servo_limrun_wrote(&g.run),
                 k_search[p] != SERVO_LIMRUN_PH_SEARCH);
        run_for(100u);
        CHECK_EQ(g.offs, 1u);
    }

    /* The programmer's phases: no rail read, the run goes on. */
    for (size_t p = 0; p < N_PHASES; ++p) {
        const servo_limrun_phase_t ph = k_phases[p].ph;
        if (ph == SERVO_LIMRUN_PH_SEARCH || ph == SERVO_LIMRUN_PH_PROBE
            || ph == SERVO_LIMRUN_PH_CHECK) {
            continue;
        }
        CHECK(to_phase_at(ph, k_phases[p].at));
        servo_test_win_t w = win_at(g.now + 1u, 20, 30);
        w.voltage = false;
        servo_limrun_window(&g.run, &w);
        CHECK(servo_limrun_running(&g.run));
        CHECK_EQ(g.run.last_win_ms, g.now + 1u);
    }

    /* The rail gone through a session and back for the next search: the
     * run goes on, held to the first search window's rail. */
    CHECK(to_phase(SERVO_LIMRUN_PH_CONNECT));
    g.no_volt = true;
    run_for(300u);
    CHECK(servo_limrun_running(&g.run));
    g.no_volt = false;
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    run_for(2000u);
    CHECK(servo_limrun_running(&g.run));
    CHECK(g.run.have_rail);
    CHECK_EQ(g.run.rail_mv, 6000u);
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
}

/* The boundaries: after a window at t, one without the rail at t + 199 and
 * at t + 200 (before the step that would end the run for silence) ends it
 * for the rail; and the run's last window before it ends. */
TEST_CASE(a_window_without_the_rail_voltage_at_the_edges)
{
    static const uint32_t k_after[] = { 199u, 200u };
    static const uint32_t k_t0[] = { 0u, 0u - 120u };
    for (size_t z = 0; z < 2u; ++z) {
        for (size_t a = 0; a < 2u; ++a) {
            rig_init(k_t0[z], false);
            rig_pin();
            CHECK(start());
            const uint32_t t = k_t0[z] + 50u;
            servo_test_win_t w = win_at(t, 120, 150);
            servo_limrun_window(&g.run, &w);
            w = win_at(t + k_after[a], 120, 150);
            w.voltage = false;
            servo_limrun_window(&g.run, &w);
            servo_limrun_in_t in = { true, false, false, 0.0f };
            servo_limrun_do_t d;
            servo_limrun_step(&g.run, t + k_after[a], &in, &d);
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
            CHECK_EQ(g.run.end_ms, t + k_after[a]);
        }
    }

    /* The window the run would end on, without the rail. */
    rig_init(0u, false);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    const uint32_t last = g.run.last_win_ms;
    rig_init(0u, false);
    CHECK(start());
    while (g.now != last - 1u && servo_limrun_running(&g.run)) {
        tick();
    }
    CHECK(servo_limrun_running(&g.run));
    g.no_volt = true;
    run_for(100u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
    CHECK_EQ(g.run.end_ms, last);
    CHECK(servo_limrun_wrote(&g.run));
}

/* A window without a current reading is no window: the protections cannot
 * read it, so the meter's silence runs on through it and ends the run 200
 * ms after the last window with a current, also with one at the edge. */
TEST_CASE(a_window_without_a_current_is_not_counted)
{
    rig_init(0u, false);
    CHECK(start());
    run_for(1000u);
    CHECK(servo_limrun_running(&g.run));
    const uint32_t last = g.run.last_win_ms;
    const uint32_t n = g.run.windows;
    g.no_cur = true;
    run_for(1000u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(g.run.end_ms, last + SERVO_LIMRUN_WIN_STALE_MS);
    CHECK_EQ(g.run.windows, n);
    CHECK_EQ(g.offs, 1u);

    rig_init(0u - 120u, false);
    CHECK(start());
    const uint32_t t = g.now + 50u;
    servo_test_win_t w = win_at(t, 120, 150);
    servo_limrun_window(&g.run, &w);
    servo_limrun_in_t in = { true, false, false, 0.0f };
    servo_limrun_do_t d;
    servo_limrun_step(&g.run, t + 199u, &in, &d);
    CHECK(servo_limrun_running(&g.run));
    w = win_at(t + 200u, 2999, 2999);
    w.current = false;
    servo_limrun_window(&g.run, &w);
    CHECK(servo_limrun_running(&g.run));
    servo_limrun_step(&g.run, t + 200u, &in, &d);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(g.run.end_ms, t + 200u);
}

/* --- the clock ----------------------------------------------------------- */

/* The same run from tick 0 and from 60 s before the wrap: the same values,
 * the same writes, the same duration; with SERVO PULSES 1000-2000 as well. */
TEST_CASE(a_run_across_the_tick_wrap_ends_as_at_tick_0)
{
    for (int set = 0; set < 2; ++set) {
        rig_init(0u, false);
        g.cfg.set_pulses = set != 0;
        CHECK(start());
        run_for(RUN_MS);
        const servo_limrun_t a = g.run;
        const uint16_t left = reg_get(KST_F_LEFT_ANGLE);
        const uint16_t right = reg_get(KST_F_RIGHT_ANGLE);

        rig_init(0u - 60000u, false);
        g.cfg.set_pulses = set != 0;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        CHECK_EQ(g.run.verdict, a.verdict);
        CHECK_EQ(g.run.writes, a.writes);
        CHECK_EQ(g.run.sessions, a.sessions);
        CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), left);
        CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), right);
        CHECK_EQ(g.run.end_ms - g.run.start_ms, a.end_ms - a.start_ms);
        CHECK((int32_t)g.run.end_ms > 0);   /* it crossed the wrap */
    }
}

/* --- the report ---------------------------------------------------------- */

TEST_CASE(the_report_holds_the_run)
{
    rig_init(0u, false);
    g.enc = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    char want[128];
    CHECK(strstr(g.report, "RCBENCH SERVO LIMIT REPORT\n") == g.report);
    CHECK(strstr(g.report, "PASS") != NULL);
    CHECK(strstr(g.report, "aileron left") != NULL);
    CHECK(strstr(g.report, "PROG PORT GP13") != NULL);
    CHECK(strstr(g.report, "INA3221") != NULL);
    CHECK(strstr(g.report, "1.8") != NULL);
    CHECK(strstr(g.report, "3.0 A") != NULL);
    CHECK(strstr(g.report, "Pulse range:    centre 1500 us, 800-2200 us")
          != NULL);
    CHECK(strstr(g.report, "Searched:       from 1500 us out to 900 us and "
                           "2100 us") != NULL);
    CHECK(strstr(g.report, "Servo pulses:   NEUTRAL 1500.2 us, PULSE LOWER "
                           "900.0 us, PULSE UPPER 2100.0 us") != NULL);
    CHECK(strstr(g.report, "Pulse write") == NULL);
    snprintf(want, sizeof(want), "LEFT ANGLE:     150 before, %u after",
             (unsigned)reg_get(KST_F_LEFT_ANGLE));
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "RIGHT ANGLE:    150 before, %u after",
             (unsigned)reg_get(KST_F_RIGHT_ANGLE));
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "overset %u, share", g.run.v1[0]);
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "final %u", g.run.v2[1]);
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, "overset +3 %") != NULL);
    snprintf(want, sizeof(want), "values written %u, writes sent %u",
             g.run.writes, g.run.write_ops);
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, "KST00001.KST") != NULL);
    CHECK(strstr(g.report, "AS5600") != NULL || strstr(g.report, "deg") != NULL);
    CHECK(strstr(g.report, "unit") != NULL);
    CHECK(strstr(g.report, "linear") != NULL);
    CHECK(strstr(g.report, "bisect") == NULL);
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

    /* Search only on the PWM pin: no programming lines. */
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    run_for(RUN_MS);
    CHECK(strstr(g.report, "search only") != NULL);
    CHECK(strstr(g.report, "Not run: search only.") != NULL);
    CHECK(strstr(g.report, "LEFT ANGLE:") == NULL);
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

/* Every reason and every phase has words of its own, and a reason fits the
 * box's line of 31 characters.  No servo read: no values to give. */
TEST_CASE(every_reason_and_phase_has_its_words)
{
    servo_limrun_t r;
    servo_limrun_init(&r);
    CHECK(!servo_limrun_original(&r, NULL, NULL));
    CHECK(!servo_limrun_original(NULL, NULL, NULL));
    CHECK(!servo_limrun_restore(NULL));
    CHECK_EQ(servo_limrun_why_str(NULL), SERVO_STR_AB_NONE);
    servo_str_t seen[SERVO_LIMRUN_WHY_COUNT];
    for (int w = SERVO_LIMRUN_WHY_HARD; w < SERVO_LIMRUN_WHY_COUNT; ++w) {
        r.why = (servo_limrun_why_t)w;
        const servo_str_t id = servo_limrun_why_str(&r);
        seen[w] = id;
        CHECK(id != SERVO_STR_AB_NONE);
        CHECK(strlen(servo_str(id)) > 0u);
        CHECK(strlen(servo_str(id)) <= 31u);
        for (int k = SERVO_LIMRUN_WHY_HARD; k < w; ++k) {
            CHECK(seen[k] != id);
        }
    }
    r.why = SERVO_LIMRUN_WHY_TEST;
    r.test_why = SERVO_TEST_AB_STOP;
    CHECK_EQ(servo_limrun_why_str(&r), servo_test_abort_str(SERVO_TEST_AB_STOP));
    r.why = SERVO_LIMRUN_WHY_COUNT;
    CHECK_EQ(servo_limrun_why_str(&r), SERVO_STR_AB_NONE);
    for (int p = SERVO_LIMRUN_PH_SEARCH; p <= SERVO_LIMRUN_PH_CHECK; ++p) {
        const servo_str_t id = servo_limrun_phase_str((servo_limrun_phase_t)p);
        CHECK(id != SERVO_STR_PHASE_NONE);
        CHECK(strlen(servo_str(id)) > 0u);
        for (int k = SERVO_LIMRUN_PH_SEARCH; k < p; ++k) {
            CHECK(servo_limrun_phase_str((servo_limrun_phase_t)k) != id);
        }
    }
    CHECK_EQ(servo_limrun_phase_str(SERVO_LIMRUN_PH_NONE),
             SERVO_STR_PHASE_NONE);
    CHECK(servo_limrun_verdict_str(SERVO_LIMRUN_FOUND)
          != servo_limrun_verdict_str(SERVO_LIMRUN_FREE));
}

/* The arithmetic: the share from NEUTRAL, the overset rounded up and the
 * final value down, both within the smallest value and v0. */
TEST_CASE(the_share_the_overset_and_the_final_value)
{
    servo_limrun_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    sv.pulse_dus[SERVO_LIMRUN_P_NEUTRAL] = 15002u;
    sv.pulse_dus[SERVO_LIMRUN_P_LOWER]   = 9000u;
    sv.pulse_dus[SERVO_LIMRUN_P_UPPER]   = 21000u;
    CHECK_EQ(servo_limrun_span(&sv, SERVO_LIMRUN_LO), 6002);
    CHECK_EQ(servo_limrun_span(&sv, SERVO_LIMRUN_HI), 5998);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_LO, 1000u), 5002);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_HI, 2000u), 4998);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_LO, 1500u), 2);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_HI, 1500u), -2);

    CHECK_EQ(servo_limrun_overset(150u, 5002, 6002, 1u), 129u);
    CHECK_EQ(servo_limrun_scaled(129u, 5802, 6002, 1u), 124u);
    /* Exact products: 100 x 0.5 x 1.03 = 51.5 up to 52; 100 x 0.5 = 50. */
    CHECK_EQ(servo_limrun_overset(100u, 3000, 6000, 1u), 52u);
    CHECK_EQ(servo_limrun_scaled(100u, 3000, 6000, 1u), 50u);
    /* At most v0, at least the smallest value. */
    CHECK_EQ(servo_limrun_overset(150u, 6000, 6000, 1u), 150u);
    CHECK_EQ(servo_limrun_overset(150u, 100, 6000, 40u), 40u);
    CHECK_EQ(servo_limrun_scaled(150u, 100, 6000, 40u), 40u);
    CHECK_EQ(servo_limrun_scaled(150u, 7000, 6000, 1u), 150u);
    /* No share: the smallest value, never above v0. */
    CHECK_EQ(servo_limrun_overset(150u, 0, 6000, 1u), 1u);
    CHECK_EQ(servo_limrun_overset(150u, 3000, 0, 1u), 1u);
    CHECK_EQ(servo_limrun_scaled(150u, -5, 6000, 1u), 1u);
    CHECK_EQ(servo_limrun_scaled(20u, -5, 6000, 40u), 20u);
    CHECK_EQ(servo_limrun_overset(255u, 6000, 6000, 1u), 255u);
}

int main(void)
{
    RUN(both_sides_converge_with_reversion_off);
    RUN(both_sides_converge_with_reversion_on);
    RUN(the_worked_example);
    RUN(the_overset_is_left_out_near_the_pulse_limit);
    RUN(every_stop_position_passes);
    RUN(free_after_the_overset_fails);
    RUN(a_servo_that_is_not_linear_fails_at_the_check);
    RUN(one_side_bound_leaves_the_other_register_as_it_was);
    RUN(a_servo_bound_at_the_smallest_value_fails_clearly);
    RUN(registers_at_the_smallest_value_already);
    RUN(a_servo_that_never_binds_is_not_programmed);
    RUN(a_free_end_not_past_neutral_fails_before_a_write);
    RUN(a_reversed_shunt_finds_the_bound_ends);
    RUN(search_only_reports_the_endpoints);
    RUN(the_pwm_pin_searches_out_to_min_and_max);
    RUN(a_bench_range_narrower_than_the_servos_is_refused);
    RUN(a_neutral_outside_the_pulse_limits_is_refused);
    RUN(set_1000_2000_writes_the_pulses_before_the_search);
    RUN(set_1000_2000_on_a_servo_there_already_writes_nothing);
    RUN(search_only_with_set_1000_2000_writes_only_the_pulses);
    RUN(set_1000_2000_with_neutral_outside_fails_before_the_search);
    RUN(a_pulse_write_refused_or_lost_aborts_before_the_search);
    RUN(a_read_that_fails_ends_the_run_before_a_drive);
    RUN(the_write_cap_is_never_passed);
    RUN(a_refused_write_aborts_and_offers_restore);
    RUN(a_write_lost_on_the_line_aborts);
    RUN(other_values_at_a_later_connect_abort);
    RUN(another_servo_at_a_later_connect_aborts);
    RUN(a_failed_leave_aborts);
    RUN(a_step_the_programmer_refuses_aborts);
    RUN(every_caller_abort_ends_every_phase_with_the_output_off);
    RUN(the_runs_own_timeouts_end_it);
    RUN(a_session_starts_with_the_servo_back_at_the_centre);
    RUN(a_sample_at_3_a_ends_the_run_in_every_phase);
    RUN(above_1_a_for_400_ms_ends_the_run);
    RUN(a_changed_supply_ends_a_search_but_not_a_session);
    RUN(the_supply_is_watched_in_safe_and_on_the_way_to_armed);
    RUN(a_supply_off_at_the_start_that_switches_on_ends_the_run);
    RUN(a_supply_that_answers_late_ends_the_run);
    RUN(a_rail_under_3_v_in_any_search_window_ends_the_run);
    RUN(an_abort_behind_the_arm_leaves_the_bench_safe);
    RUN(no_window_for_200_ms_ends_the_run);
    RUN(a_window_without_the_rail_voltage_ends_a_search);
    RUN(a_window_without_the_rail_voltage_at_the_edges);
    RUN(a_window_without_a_current_is_not_counted);
    RUN(a_run_across_the_tick_wrap_ends_as_at_tick_0);
    RUN(the_report_holds_the_run);
    RUN(a_start_with_its_centre_outside_min_and_max_is_refused);
    RUN(every_reason_and_phase_has_its_words);
    RUN(the_share_the_overset_and_the_final_value);
    return test_summary("servo_limrun");
}
