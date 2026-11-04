/*
 * The limit run's rig for a Hitec servo, for test_servo_limrun_hitec.c.
 *
 * The rig: servo_sim's servo on a surface with stops at 1150 and 1880us;
 * the horn where the run's model puts it -- linear from NEUTRAL 1500us to
 * the family's full-travel pulses (servo_limrun_family_pulses()), the
 * register of that side the full travel at g.upc us a travel unit, held
 * past that pulse; g.left_lo choosing which register limits the short
 * pulses.  The programmer holds a hitec_run_servo_t in travel units and
 * keeps "the wire", the two registers or cells hitec_run_to_reg() makes of
 * a write, as the seam sends them; the INA3221 as 50ms windows of the
 * servo's 1ms current; a bench that switches to SAFE and ARMED when asked.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hitec_prog_run.h"
#include "hitec_reg.h"
#include "servo_limrun.h"
#include "servo_sim.h"

#define H_CENTRE 1500u
#define H_MIN_US 800u            /* TYPE WIDE: 800 to 2200us */
#define H_MAX_US 2200u
#define H_RUN_MS 900000u

enum { H_OP_NONE = 0, H_OP_CONNECT, H_OP_WRITE, H_OP_LEAVE };

typedef struct {
    servo_limrun_t run;
    servo_limrun_cfg_t cfg;
    servo_sim_t servo;
    hitec_run_servo_t hs;        /* the servo, travel units                */
    bool left_lo;                /* LEFT END limits the short pulses      */
    float upc;                   /* horn us a travel unit                 */
    bool square;                 /* the share of the way squared          */
    uint16_t wire[2];            /* the registers a write sent: left, right */
    uint16_t wire0[2];           /* as the run found them                 */
    uint32_t now, t0;

    /* the bench */
    bool live, safe, off;
    int  bench_to;               /* -1 none, 0 to SAFE, 1 to ARMED        */
    uint32_t bench_at;
    uint16_t cmd, cmd_lo, cmd_hi;
    unsigned offs, disarms, arms;
    bool driven;
    unsigned connects_at_drive;

    /* the programmer */
    int  op;
    uint32_t op_at, prog_ms;
    uint16_t want[2];
    unsigned connects, write_ops, leaves, values;
    bool no_servo;               /* CONNECT finds none                    */
    bool not_editable;           /* the read's end points do not edit     */

    /* the meter */
    uint32_t win_end;
    uint16_t win_no;
    float sum;
    int16_t mx;
    unsigned n;

    char report[16384];
    size_t report_len;
} hrig_t;

static hrig_t h;

/* The wire's registers from the servo's travel. */
static inline void h_wire(uint16_t *w)
{
    w[0] = hitec_run_to_reg(&h.hs, 0u, h.hs.value[0]);
    w[1] = hitec_run_to_reg(&h.hs, 1u, h.hs.value[1]);
}

/* A D series servo: NEUTRAL 0xC2 8192, both ends 4470 counts (60° at 74.5
 * a degree), 650us of horn at full travel: past both stops. */
static inline void h_init_d(uint32_t t0, bool cw)
{
    memset(&h, 0, sizeof(h));
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.limit_lo_us = 0u;
    sc.limit_hi_us = 0u;
    servo_sim_init(&h.servo, &sc);
    h.hs.ok = true;
    h.hs.series = (uint8_t)HITEC_D;
    h.hs.mid = 8192u;
    h.hs.value[0] = 4470u;
    h.hs.value[1] = 4470u;
    h.hs.lo = 1u;
    h.hs.hi = 8191u;
    h.hs.cw = cw;
    snprintf(h.hs.backup, sizeof(h.hs.backup), "%s", "HITEC001");
    h.upc = 650.0f / 4470.0f;
    h.left_lo = true;
    h.now = h.t0 = t0;
    h.live = true;
    h.bench_to = -1;
    h.prog_ms = 1200u;           /* the D series' save and reboot in it */
    h.cmd = H_CENTRE;
    h.cmd_lo = 0xFFFFu;
    h.win_end = t0 + SERVO_TEST_WIN_MS;
    h.mx = INT16_MIN;
    memset(&h.cfg, 0, sizeof(h.cfg));
    h.cfg.centre_us = H_CENTRE;
    h.cfg.min_us = H_MIN_US;
    h.cfg.max_us = H_MAX_US;
    h.cfg.read = true;
    h.cfg.program = true;
    h.cfg.family = SERVO_LIMRUN_FAM_HITEC_D;
    snprintf(h.cfg.dut, sizeof(h.cfg.dut), "%s", "rudder");
    snprintf(h.cfg.output, sizeof(h.cfg.output), "%s", "PROG PORT 1 GPIO 13");
    h.cfg.frame_hz = 50u;
    h.cfg.shunt_dmohm = 1000u;
    snprintf(h.cfg.firmware, sizeof(h.cfg.firmware), "%s", "0.25.0");
    h_wire(h.wire);
    memcpy(h.wire0, h.wire, sizeof(h.wire0));
}

/* A classic servo: both cells 200, 650us of horn at full travel. */
static inline void h_init_c(uint32_t t0, bool cw)
{
    h_init_d(t0, cw);
    h.hs.series = (uint8_t)HITEC_CLASSIC;
    h.hs.mid = 0u;
    h.hs.value[0] = 200u;
    h.hs.value[1] = 200u;
    h.hs.lo = 0u;
    h.hs.hi = 255u;
    h.upc = 650.0f / 200.0f;
    h.cfg.family = SERVO_LIMRUN_FAM_HITEC_C;
    h_wire(h.wire);
    memcpy(h.wire0, h.wire, sizeof(h.wire0));
}

static inline void h_drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t k = servo_limrun_peek(&h.run, &text);
        if (k == SERVO_TEST_OUT_NONE) {
            return;
        }
        if (k == SERVO_TEST_OUT_TXT) {
            const size_t n = strlen(text);
            if (h.report_len + n + 2u < sizeof(h.report)) {
                memcpy(h.report + h.report_len, text, n);
                h.report_len += n;
                h.report[h.report_len++] = '\n';
                h.report[h.report_len] = '\0';
            }
        }
        servo_limrun_pop(&h.run);
    }
}

/* The servo as the panel's glue hands it to the run. */
static inline void h_servo(servo_limrun_servo_t *sv)
{
    memset(sv, 0, sizeof(*sv));
    sv->ok = h.hs.ok && !h.not_editable;
    sv->angle[0] = h.hs.value[0];
    sv->angle[1] = h.hs.value[1];
    sv->lo_raw = h.hs.lo;
    sv->hi_raw = h.hs.hi;
    sv->reversion = !h.hs.cw;
    sv->type = h.hs.series;
    (void)servo_limrun_family_pulses(h.cfg.family, sv->pulse_dus);
}

static inline void h_programmer(void)
{
    if (h.op == H_OP_NONE || (uint32_t)(h.now - h.op_at) < h.prog_ms) {
        return;
    }
    const int op = h.op;
    h.op = H_OP_NONE;
    if (op == H_OP_CONNECT) {
        ++h.connects;
        if (h.no_servo) {
            servo_limrun_no_servo(&h.run, h.now);
            return;
        }
        servo_limrun_servo_t sv;
        h_servo(&sv);
        servo_limrun_connected(&h.run, &sv, h.now);
    } else if (op == H_OP_WRITE) {
        ++h.write_ops;
        /* The seam refuses, never clamps, a value past lo or hi. */
        for (int s = 0; s < 2; ++s) {
            if (h.want[s] < h.hs.lo || h.want[s] > h.hs.hi) {
                servo_limrun_refused(&h.run, h.now);
                return;
            }
        }
        h.values += (unsigned)(h.want[0] != h.hs.value[0])
                    + (unsigned)(h.want[1] != h.hs.value[1]);
        h.hs.value[0] = h.want[0];
        h.hs.value[1] = h.want[1];
        h_wire(h.wire);
        servo_limrun_backup(&h.run, h.hs.backup);
        servo_limrun_written(&h.run, true, h.hs.value, h.now);
    } else {
        ++h.leaves;
        servo_limrun_left(&h.run, true, h.now);
    }
}

static inline void h_bench(void)
{
    if (h.bench_to < 0 || (uint32_t)(h.now - h.bench_at) < 60u) {
        return;
    }
    h.live = h.bench_to == 1;
    h.safe = h.bench_to == 0;
    h.bench_to = -1;
}

/* The horn for the command: the register of the commanded side, its share of
 * the way to the full-travel pulse. */
static inline uint16_t h_horn(void)
{
    uint16_t p[3];
    (void)servo_limrun_family_pulses(h.cfg.family, p);
    const float n = (float)p[SERVO_LIMRUN_P_NEUTRAL] / 10.0f;
    const bool low = (float)h.cmd < n;
    const float span = low ? n - (float)p[SERVO_LIMRUN_P_LOWER] / 10.0f
                           : (float)p[SERVO_LIMRUN_P_UPPER] / 10.0f - n;
    const float d = low ? n - (float)h.cmd : (float)h.cmd - n;
    float share = d / span;
    if (share > 1.0f) {
        share = 1.0f;
    }
    if (h.square) {
        share *= share;
    }
    const unsigned reg = (low == h.left_lo) ? 0u : 1u;
    const float off = share * h.upc * (float)h.hs.value[reg];
    return (uint16_t)lroundf(low ? 1500.0f - off : 1500.0f + off);
}

static inline void h_meter(void)
{
    const float a = h.off ? 0.0f
                  : h.live ? servo_sim_step(&h.servo, h_horn(), h.now)
                           : 0.02f;
    const int ma = (int)lroundf(a * 1000.0f);
    h.sum += (float)ma;
    if (ma > h.mx) {
        h.mx = (int16_t)ma;
    }
    ++h.n;
    if ((int32_t)(h.now - h.win_end) < 0) {
        return;
    }
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.number = h.win_no++;
    w.current = true;
    w.voltage = true;
    w.mean_ma = (int16_t)lroundf(h.sum / (float)h.n);
    w.max_ma = h.mx;
    w.min_ma = 0;
    w.mean_mv = 6000u;
    w.min_mv = 6000u;
    w.taken_ms = h.now;
    servo_limrun_window(&h.run, &w);
    h.sum = 0.0f;
    h.n = 0u;
    h.mx = INT16_MIN;
    h.win_end += SERVO_TEST_WIN_MS;
}

static inline void h_take(const servo_limrun_do_t *d)
{
    if (d->command) {
        h.cmd = d->cmd_us;
        if (h.cmd != H_CENTRE && !h.driven) {
            h.driven = true;
            h.connects_at_drive = h.connects;
        }
        h.cmd_lo = h.cmd < h.cmd_lo ? h.cmd : h.cmd_lo;
        h.cmd_hi = h.cmd > h.cmd_hi ? h.cmd : h.cmd_hi;
    }
    if (d->off) {
        h.off = true;
        ++h.offs;
    }
    if (d->disarm) {
        ++h.disarms;
        h.bench_to = 0;
        h.bench_at = h.now;
    }
    if (d->arm) {
        ++h.arms;
        h.bench_to = 1;
        h.bench_at = h.now;
    }
    if (d->connect || d->write || d->leave) {
        h.op = d->connect ? H_OP_CONNECT : d->write ? H_OP_WRITE : H_OP_LEAVE;
        h.op_at = h.now;
        h.want[0] = d->angle[0];
        h.want[1] = d->angle[1];
    }
    /* A Hitec servo has no pulse registers: nothing asks for them. */
    CHECK(!d->pulses);
}

/* One millisecond; the run stepped every 10ms, as a panel frame. */
static inline void h_tick(void)
{
    ++h.now;
    h_meter();
    h_bench();
    h_programmer();
    if ((h.now - h.t0) % 10u == 0u) {
        servo_limrun_in_t in;
        servo_limrun_do_t d;
        memset(&in, 0, sizeof(in));
        in.live = h.live;
        in.safe = h.safe;
        servo_limrun_step(&h.run, h.now, &in, &d);
        h_take(&d);
    }
    h_drain();
}

static inline bool h_start(void)
{
    return servo_limrun_start(&h.run, &h.cfg, h.now);
}

static inline void h_run_for(uint32_t ms)
{
    for (uint32_t k = 0; k < ms && servo_limrun_running(&h.run); ++k) {
        h_tick();
    }
    for (int k = 0; k < 20; ++k) {
        h_tick();
    }
}

/* The report holds @p s. */
static inline bool h_says(const char *s)
{
    return strstr(h.report, s) != NULL;
}
