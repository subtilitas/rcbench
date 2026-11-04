/*
 * The AUTO end points run's rig, for test_servo_rauto.c and
 * test_servo_rauto_guard.c.
 *
 * The rig: a surface on a servo whose deflection at 1000us and at 2000us
 * is linear in the register that moves that side (map[]), dir[] giving
 * each side's sign; between the end pulses and PULSE CENTRE the deflection
 * is linear in the pulse.  The surface travels at 200°/s and is still once
 * it moved less than 0.5° for a time, as the IMU path counts still_ms.  A
 * bind on a side holds the surface at that deflection and the servo then
 * draws 1500mA.  The INA3221 as 50ms windows, the angle source read every
 * 20ms, a bench that switches to SAFE and ARMED when asked, and a
 * programmer that answers CONNECT with the registers, stores a write of
 * the values or of the pulses and reads it back, and gives the port back.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "servo_rudder_auto.h"

#define RCENTRE 1500u

enum { OP_NONE = 0, OP_CONNECT, OP_WRITE, OP_PULSES, OP_LEAVE };

typedef struct {
    servo_rauto_t run;
    servo_rauto_cfg_t cfg;
    uint32_t now, t0;

    /* the servo and the surface */
    uint16_t v[2];               /* the registers                         */
    uint16_t v0[2];              /* as at the start                       */
    uint16_t lo, hi;
    uint16_t pulses[3];          /* NEUTRAL, LOWER, UPPER, 0.1us          */
    int      map[2];             /* the register that moves each side     */
    float    dir[2];             /* each side's sign                      */
    float    gain;               /* ° a count                             */
    float    resp;               /* a write moves this share of the
                                    linear change: 1 linear               */
    float    bind[2];            /* a bind at this deflection, 0 none     */
    bool     jitter;             /* the surface never still               */
    float    angle;              /* the surface now, °                    */
    float    still_ref;
    uint32_t still_from;
    uint16_t cmd;
    bool     commanded;
    bool     off, released;
    unsigned offs, releases, disarms, arms;
    uint16_t cmd_lo, cmd_hi;     /* the extremes commanded                */
    bool     pulses_before_drive;/* the pulse write before an end pulse   */
    bool     driven;

    /* the bench */
    bool live, safe;
    int  bench_to;
    uint32_t bench_at, bench_ms;

    /* the meter */
    bool windows;
    int  extra_ma;
    float sum; int16_t mx, mn; unsigned n;
    uint32_t win_end;

    /* the angle source */
    bool angle_on;               /* fed at all                            */
    uint32_t lost_from, lost_to; /* not valid in [from, to)               */

    /* the programmer */
    int  op;
    uint32_t op_at, prog_ms;
    bool prog_silent;
    uint16_t want[2], want_p[2];
    unsigned connects, write_ops, leaves, leave_asks, pulse_ops;
    unsigned refuse_connect;     /* this CONNECT (1-based) is refused     */
    unsigned no_servo_at;        /* this one finds no servo               */
    unsigned refuse_write;
    unsigned lose_write;         /* this write does not land              */
    unsigned change_at;          /* this CONNECT reads another value      */
    bool leave_fails;
    bool not_servo;
    bool pulses_lost;

    /* the outbox */
    char report[16384];
    size_t report_len;
    char csv[4096];
    size_t csv_len;
    unsigned opens, ends, lines, rows;
} rauto_rig_t;

static rauto_rig_t g;

/* The deflection the servo holds at pulse @p us. */
static inline float rig_target(uint16_t us)
{
    float e[2];
    for (int s = 0; s < 2; ++s) {
        const int r = g.map[s];
        const float lin = g.gain * (float)g.v0[r]
                          + g.resp * g.gain * ((float)g.v[r] - (float)g.v0[r]);
        e[s] = g.dir[s] * lin;
    }
    if (us <= 1000u) {
        return e[0];
    }
    if (us >= 2000u) {
        return e[1];
    }
    if (us <= RCENTRE) {
        return e[0] * (float)(RCENTRE - us) / (float)(RCENTRE - 1000u);
    }
    return e[1] * (float)(us - RCENTRE) / (float)(2000u - RCENTRE);
}

static inline void rig_init(uint32_t t0)
{
    memset(&g, 0, sizeof(g));
    g.now = g.t0 = t0;
    g.v[0] = g.v[1] = 150u;
    g.lo = 50u;
    g.hi = 255u;
    g.pulses[0] = 15000u;
    g.pulses[1] = 9000u;
    g.pulses[2] = 21000u;
    g.map[0] = 0;
    g.map[1] = 1;
    g.dir[0] = -1.0f;
    g.dir[1] = 1.0f;
    g.gain = 0.2f;               /* 150 counts: 30° */
    g.resp = 1.0f;
    g.cmd = RCENTRE;
    g.live = true;
    g.bench_to = -1;
    g.bench_ms = 20u;
    g.prog_ms = 200u;
    g.windows = true;
    g.angle_on = true;
    g.win_end = t0 + SERVO_TEST_WIN_MS;
    g.mx = INT16_MIN;
    g.mn = INT16_MAX;
    g.cmd_lo = 0xFFFFu;
    g.still_from = t0;
    servo_rauto_init(&g.run);
    memset(&g.cfg, 0, sizeof(g.cfg));
    g.cfg.target_deg[0] = 25.0f;
    g.cfg.target_deg[1] = 25.0f;
    g.cfg.margin_pct = 10u;
    g.cfg.tol_ddeg = 5u;
    g.cfg.family = SERVO_RAUTO_FAM_KST;
    g.cfg.centre_us = RCENTRE;
    snprintf(g.cfg.dut, sizeof(g.cfg.dut), "%s", "AILERON L");
    snprintf(g.cfg.type, sizeof(g.cfg.type), "%s", "DIGITAL");
    snprintf(g.cfg.output, sizeof(g.cfg.output), "%s", "PROG PORT 1 GPIO 13");
    snprintf(g.cfg.meter, sizeof(g.cfg.meter), "%s", "INA3221 0x41 CH1");
    snprintf(g.cfg.source, sizeof(g.cfg.source), "%s", "IMU X+");
    snprintf(g.cfg.firmware, sizeof(g.cfg.firmware), "%s", "0.23.9");
}

/* The registers as they start, once the test set them. */
static inline void rig_fix(void)
{
    g.v0[0] = g.v[0];
    g.v0[1] = g.v[1];
    g.angle = rig_target(g.cmd);
    g.still_ref = g.angle;
}

static inline void drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t k = servo_rauto_peek(&g.run, &text);
        if (k == SERVO_TEST_OUT_NONE) {
            return;
        }
        if (k == SERVO_TEST_OUT_OPEN) {
            ++g.opens;
        } else if (k == SERVO_TEST_OUT_END) {
            ++g.ends;
        } else {
            const size_t n = strlen(text);
            char *buf = k == SERVO_TEST_OUT_TXT ? g.report : g.csv;
            size_t *len = k == SERVO_TEST_OUT_TXT ? &g.report_len : &g.csv_len;
            const size_t cap = k == SERVO_TEST_OUT_TXT ? sizeof(g.report)
                                                       : sizeof(g.csv);
            if (k == SERVO_TEST_OUT_TXT) {
                ++g.lines;
            } else {
                ++g.rows;
            }
            if (*len + n + 2u < cap) {
                memcpy(buf + *len, text, n);
                *len += n;
                buf[(*len)++] = '\n';
                buf[*len] = '\0';
            }
        }
        servo_rauto_pop(&g.run);
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
        ++g.connects;
        if (g.connects == g.refuse_connect) {
            servo_rauto_refused(&g.run, g.now);
            return;
        }
        if (g.connects == g.no_servo_at) {
            servo_rauto_no_servo(&g.run, g.now);
            return;
        }
        servo_rauto_servo_t sv;
        memset(&sv, 0, sizeof(sv));
        sv.ok = !g.not_servo;
        sv.value[0] = g.v[0];
        sv.value[1] = g.v[1];
        if (g.connects == g.change_at) {
            sv.value[1] = (uint16_t)(sv.value[1] + 1u);
        }
        sv.lo = g.lo;
        sv.hi = g.hi;
        memcpy(sv.pulse_dus, g.pulses, sizeof(sv.pulse_dus));
        servo_rauto_connected(&g.run, &sv, g.now);
    } else if (op == OP_WRITE) {
        ++g.write_ops;
        if (g.write_ops == g.refuse_write) {
            servo_rauto_written(&g.run, false, NULL, g.now);
            return;
        }
        if (g.write_ops != g.lose_write) {
            g.v[0] = g.want[0];
            g.v[1] = g.want[1];
        }
        servo_rauto_written(&g.run, true, g.v, g.now);
    } else if (op == OP_PULSES) {
        ++g.write_ops;
        ++g.pulse_ops;
        if (!g.pulses_lost) {
            g.pulses[1] = g.want_p[0];
            g.pulses[2] = g.want_p[1];
        }
        servo_rauto_pulsed(&g.run, true, g.pulses, g.now);
    } else {
        ++g.leaves;
        servo_rauto_left(&g.run, !g.leave_fails, g.now);
    }
}

static inline void bench(void)
{
    if (g.bench_to < 0 || (uint32_t)(g.now - g.bench_at) < g.bench_ms) {
        return;
    }
    g.live = g.bench_to == 1;
    g.safe = g.bench_to == 0;
    g.bench_to = -1;
}

/* The surface toward the servo's position, 0.2° a ms; a bind holds it.
 * Returns the current drawn, mA. */
static inline int surface(void)
{
    if (g.off) {
        return 0;
    }
    if (!g.live) {
        return 20;
    }
    float want = rig_target(g.cmd);
    bool bound = false;
    for (int s = 0; s < 2; ++s) {
        const float b = g.bind[s] * g.dir[s];
        if (g.bind[s] > 0.0f && want * g.dir[s] > g.bind[s]) {
            want = b;
            bound = true;
        }
    }
    const float d = want - g.angle;
    const bool moving = fabsf(d) > 0.2f;
    g.angle = moving ? g.angle + (d > 0.0f ? 0.2f : -0.2f) : want;
    if (g.jitter) {
        g.angle += ((g.now / 100u) % 2u == 0u) ? 1.0f : -1.0f;
    }
    if (fabsf(g.angle - g.still_ref) > 0.5f) {
        g.still_ref  = g.angle;
        g.still_from = g.now;
    }
    return bound ? 1500 : moving ? 600 : 150;
}

static inline void meter(int ma)
{
    ma += g.extra_ma;
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
        w.current  = true;
        w.voltage  = true;
        w.mean_ma  = (int16_t)lroundf(g.sum / (float)g.n);
        w.max_ma   = g.mx;
        w.min_ma   = g.mn;
        w.mean_mv  = 5000u;
        w.min_mv   = 5000u;
        w.taken_ms = g.now;
        servo_rauto_window(&g.run, &w);
    }
    g.sum = 0.0f;
    g.n = 0u;
    g.mx = INT16_MIN;
    g.mn = INT16_MAX;
    g.win_end += SERVO_TEST_WIN_MS;
}

static inline bool angle_ok(void)
{
    return g.angle_on
        && !((int32_t)(g.now - g.lost_from) >= 0
             && (int32_t)(g.now - g.lost_to) < 0);
}

static inline uint16_t still_ms(void)
{
    const uint32_t ms = g.now - g.still_from;
    return ms > 60000u ? 60000u : (uint16_t)ms;
}

static inline void take(const servo_rauto_do_t *d)
{
    if (d->command) {
        g.cmd = d->cmd_us;
        if (g.cmd != RCENTRE && !g.driven) {
            g.driven = true;
            g.pulses_before_drive = g.pulse_ops > 0u;
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
        g.want[0] = d->value[0];
        g.want[1] = d->value[1];
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

/* The run's inputs now. */
static inline servo_rauto_in_t rig_in(void)
{
    servo_rauto_in_t in;
    memset(&in, 0, sizeof(in));
    in.live = g.live;
    in.safe = g.safe;
    in.angle_valid = angle_ok();
    in.angle_deg = g.angle;
    in.still_ms = still_ms();
    return in;
}

/* One millisecond of the rig; the run is stepped every ms. */
static inline void tick(void)
{
    ++g.now;
    meter(surface());
    bench();
    programmer();
    if ((g.now - g.t0) % 20u == 0u) {
        servo_angle_in_t a;
        memset(&a, 0, sizeof(a));
        a.valid = angle_ok();
        a.deg = g.angle;
        a.still_ms = still_ms();
        a.taken_ms = g.now;
        if (g.angle_on) {
            servo_rauto_angle(&g.run, &a);
        }
    }
    const servo_rauto_in_t in = rig_in();
    servo_rauto_do_t d;
    servo_rauto_step(&g.run, g.now, &in, &d);
    take(&d);
    drain();
}

static inline bool start(void)
{
    rig_fix();
    return servo_rauto_start(&g.run, &g.cfg, g.now);
}

/* Until the run ends or @p ms pass. */
static inline void run_for(uint32_t ms)
{
    for (uint32_t k = 0; k < ms && servo_rauto_running(&g.run); ++k) {
        tick();
    }
    for (int k = 0; k < 50; ++k) {
        tick();             /* the bench switches, the outbox drains */
    }
}

static inline void idle_for(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        tick();
    }
}

/* Until the run is in phase @p ph, at most @p ms. */
static inline bool run_to(servo_rauto_phase_t ph, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        if (servo_rauto_running(&g.run) && g.run.phase == ph) {
            return true;
        }
        if (!servo_rauto_running(&g.run)) {
            return false;
        }
        tick();
    }
    return false;
}

#define RAUTO_MS 600000u

/* Whether the report holds @p s. */
static inline bool in_report(const char *s)
{
    return strstr(g.report, s) != NULL;
}

/* The run ended with every output off and SAFE asked. */
static inline bool ended_safe(void)
{
    return !servo_rauto_running(&g.run) && g.offs > 0u && g.released
        && g.disarms > 0u && !g.live;
}
