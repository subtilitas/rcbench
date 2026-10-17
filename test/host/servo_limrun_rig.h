/*
 * The limit run's rig, for test_servo_limrun.c, test_servo_limrun_abort.c
 * and test_servo_limrun_report.c.
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
 * SPDX-License-Identifier: MIT
 */
#pragma once

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

static inline uint16_t reg_get(kst_field_id_t id);
static inline void reg_set(kst_field_id_t id, uint16_t v);

/* PULSE LOWER and PULSE UPPER set to @p lo and @p hi, 0.1 us. */
static inline void rig_pulses(int32_t lo, int32_t hi)
{
    uint16_t raw = 0u;
    CHECK(kst_field_raw_from_display(KST_F_PULSE_LOWER, lo, &raw));
    reg_set(KST_F_PULSE_LOWER, raw);
    CHECK(kst_field_raw_from_display(KST_F_PULSE_UPPER, hi, &raw));
    reg_set(KST_F_PULSE_UPPER, raw);
}

static inline void pulses_of(uint16_t *p)
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

static inline void rig_init(uint32_t t0, bool reversion)
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

static inline uint16_t reg_get(kst_field_id_t id)
{
    kst_image_t m;
    memcpy(m.r, g.reg, sizeof(m.r));
    return kst_field_get(&m, id);
}

static inline void reg_set(kst_field_id_t id, uint16_t v)
{
    kst_image_t m;
    memcpy(m.r, g.reg, sizeof(m.r));
    (void)kst_field_set(&m, id, v);
    memcpy(g.reg, m.r, sizeof(g.reg));
}

static inline void drain(void)
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

static inline void programmer(void)
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

static inline void bench(void)
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
static inline uint16_t horn_cmd(void)
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

static inline void meter(void)
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

static inline void take(const servo_limrun_do_t *d)
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
static inline void tick(void)
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

static inline bool start(void)
{
    g.cfg.enc_on = g.enc;
    return servo_limrun_start(&g.run, &g.cfg, g.now);
}

/* Until the run ends or @p ms pass. */
static inline void run_for(uint32_t ms)
{
    for (uint32_t k = 0; k < ms && servo_limrun_running(&g.run); ++k) {
        tick();
    }
    for (int k = 0; k < 20; ++k) {
        tick();             /* the outbox drains */
    }
}

/* @p ms of the rig, whether the run goes on or not. */
static inline void idle_for(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        tick();
    }
}

/* Until the run is in phase @p ph, at most @p ms. */
static inline bool run_to(servo_limrun_phase_t ph, uint32_t ms)
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
static inline void rig_pin(void)
{
    g.cfg.read    = false;
    g.cfg.program = false;
    snprintf(g.cfg.output, sizeof(g.cfg.output), "%s", "GP2");
}
