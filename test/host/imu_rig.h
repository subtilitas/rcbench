/*
 * The rig of the Bluetooth IMU's suites: WitMotion frames built byte by byte
 * from the layout in witmotion.h, and the controller (imu_ctl.h) run 1ms at
 * a time against the scripted host stack and sensor of ble_sim.h.  A write
 * on 8841 the controller hands out is a sensor command: it is logged apart.
 * The sensor's frames come as notifications on 1E4D while the sim is
 * connected; before that they reach nothing, as on the air.
 *
 * Functions are `static`, under the sim's unused-function pragma.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef IMU_RIG_H
#define IMU_RIG_H

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ble_sim.h"
#include "greatest.h"
#include "imu.h"
#include "imu_ctl.h"
#include "witmotion.h"

/** The sensor's address as the owner's phone read it: 00:0C:BF:08:19:29,
 *  public, least significant byte first. */
static const bleu_addr_t k_rig_addr = {
    { 0x29u, 0x19u, 0x08u, 0xBFu, 0x0Cu, 0x00u }, BLEU_ADDR_PUBLIC
};
/** That address as SETUP keeps it. */
#define RIG_ADDR "00:0C:BF:08:19:29 P"

/** The sensor's advertising data as the station captured it: flags 0x0A,
 *  the 16-bit UUID list 0x18F0, the complete name `HC-06` padded with NUL
 *  bytes to 22. */
static const uint8_t k_rig_adv[31] = {
    0x02, 0x01, 0x0A, 0x03, 0x03, 0xF0, 0x18, 0x17, 0x09,
    'H', 'C', '-', '0', '6',
};

/** Build one frame: sync, type, four little-endian words, sum. */
static void rig_frame(uint8_t f[WIT_FRAME_LEN], uint8_t type, int16_t a,
                      int16_t b, int16_t c, int16_t d)
{
    const int16_t w[4] = { a, b, c, d };
    uint8_t sum = 0x55u;
    f[0] = 0x55u;
    f[1] = type;
    sum = (uint8_t)(sum + type);
    for (unsigned i = 0; i < 4u; ++i) {
        f[2u + 2u * i] = (uint8_t)((uint16_t)w[i] & 0xFFu);
        f[3u + 2u * i] = (uint8_t)((uint16_t)w[i] >> 8);
        sum = (uint8_t)(sum + f[2u + 2u * i] + f[3u + 2u * i]);
    }
    f[10] = sum;
}

/** The angle word of @p deg: 32768 is 180°. */
static int16_t rig_deg_word(float deg)
{
    const long w = lround((double)deg * 32768.0 / 180.0);
    return (int16_t)((w > 32767) ? 32767 : (w < -32768) ? -32768 : w);
}

/** An angle frame of roll, pitch and yaw, version 0x1234. */
static void rig_angle(uint8_t f[WIT_FRAME_LEN], float roll, float pitch,
                      float yaw)
{
    rig_frame(f, WIT_T_ANGLE, rig_deg_word(roll), rig_deg_word(pitch),
              rig_deg_word(yaw), 0x1234);
}

/** Feed @p m one angle frame at @p t. */
static void rig_feed_angle(imu_t *m, float roll, float pitch, float yaw,
                           uint32_t t)
{
    uint8_t f[WIT_FRAME_LEN];
    rig_angle(f, roll, pitch, yaw);
    imu_feed(m, f, sizeof f, t);
}

#define RIG_SENT_MAX 32u

typedef struct {
    bsim_t    sim;
    imu_ctl_t c;
    uint32_t  t;                            /* the clock, ms */
    uint8_t   sent[RIG_SENT_MAX][WIT_CMD_LEN]; /* sensor commands */
    uint32_t  sent_at[RIG_SENT_MAX];
    unsigned  nsent;
    unsigned  scans;                        /* SCAN actions */
} rig_t;

/** A rig with the sensor advertising at -50dBm 200ms into a scan, the
 *  panel's Bluetooth synced on a C6 2.13.0, the clock at 1000ms. */
static void rig_init(rig_t *r)
{
    memset(r, 0, sizeof *r);
    bsim_init(&r->sim);
    bsim_adv(&r->sim, 200u, &k_rig_addr, -50, k_rig_adv, sizeof k_rig_adv);
    imu_ctl_init(&r->c);
    r->t = 1000u;
    imu_ctl_radio(&r->c, IMU_RADIO_READY, "2.13.0", r->t);
}

/** The events due now, in time order, into the controller. */
static void rig_deliver(rig_t *r)
{
    bsim_t *s = &r->sim;
    for (;;) {
        unsigned best = s->nev;
        for (unsigned i = 0; i < s->nev; ++i) {
            if (bsim_due(s->ev[i].at, r->t)
                && (best == s->nev
                    || (int32_t)(s->ev[i].at - s->ev[best].at) < 0)) {
                best = i;
            }
        }
        if (best == s->nev) {
            return;
        }
        const bleu_evt_t e = s->ev[best].e;
        memmove(&s->ev[best], &s->ev[best + 1u],
                (s->nev - best - 1u) * sizeof s->ev[0]);
        --s->nev;
        bsim_open(s, &e);
        imu_ctl_event(&r->c, &e, r->t);
    }
}

/** One ms: the events due, then every action of that ms, logged as
 *  bsim_tick() logs them. */
static void rig_tick(rig_t *r)
{
    rig_deliver(r);
    bleu_act_t a;
    for (int i = 0; i < 16 && imu_ctl_step(&r->c, r->t, &a); ++i) {
        if ((a.kind == BLEU_ACT_WRITE || a.kind == BLEU_ACT_WRITE_NR)
            && a.len == WIT_CMD_LEN && r->nsent < RIG_SENT_MAX) {
            memcpy(r->sent[r->nsent], a.data, WIT_CMD_LEN);
            r->sent_at[r->nsent] = r->t;
            ++r->nsent;
        }
        r->scans += a.kind == BLEU_ACT_SCAN;
        if (r->sim.nlog < BSIM_LOG_MAX) {
            r->sim.log[r->sim.nlog].t = r->t;
            r->sim.log[r->sim.nlog].a = a;
            ++r->sim.nlog;
        }
        bsim_act(&r->sim, r->t, &a);
    }
    ++r->t;
}

/** Run @p ms ms. */
static void rig_run(rig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        rig_tick(r);
    }
}

/** Run until a sensor command can go, @p max ms at most.
 *  @return the ms it took. */
static uint32_t rig_settle(rig_t *r, uint32_t max)
{
    uint32_t k = 0;
    for (; k < max; ++k) {
        rig_tick(r);
        if (imu_ble_can_send(&r->c.ble)) {
            break;
        }
    }
    return k;
}

/** SETUP's values: IMU @p en, bound to @p addr, RATE option @p rate,
 *  6-AXIS with @p six. */
static void rig_cfg(rig_t *r, bool en, const char *addr, int rate, bool six)
{
    imu_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.en = en;
    snprintf(cfg.addr, sizeof cfg.addr, "%s", addr);
    cfg.rate_code = imu_ctl_rate_code(rate);
    cfg.six = six;
    imu_ctl_config(&r->c, &cfg, r->t);
}

/** @p n bytes as notifications on 1E4D of the sim's connection, cut at
 *  @p cut bytes each; they reach nothing while the sim is not connected. */
static void rig_notify(rig_t *r, const uint8_t *b, size_t n, size_t cut)
{
    if (!r->sim.connected) {
        return;
    }
    for (size_t at = 0; at < n; at += cut) {
        bleu_evt_t e;
        memset(&e, 0, sizeof e);
        e.kind = BLEU_EV_NOTIFY;
        e.conn = r->sim.conn;
        e.h1 = H_TX_VAL;
        e.len = (uint8_t)((n - at) < cut ? (n - at) : cut);
        memcpy(e.data, &b[at], e.len);
        imu_ctl_event(&r->c, &e, r->t);
    }
}

/** Angle frames every @p period_ms for @p ms ms, as the sensor streams. */
static void rig_stream(rig_t *r, uint32_t ms, uint32_t period_ms, float roll,
                       float pitch, float yaw)
{
    uint8_t f[WIT_FRAME_LEN];
    rig_angle(f, roll, pitch, yaw);
    for (uint32_t k = 0; k < ms; ++k) {
        if (k % period_ms == 0u) {
            rig_notify(r, f, sizeof f, 20u);
        }
        rig_tick(r);
    }
}

/** Whether sensor command @p i is FF AA @p reg @p lo @p hi. */
static bool rig_sent_is(const rig_t *r, unsigned i, uint8_t reg, uint8_t lo,
                        uint8_t hi)
{
    return i < r->nsent && r->sent[i][0] == 0xFFu && r->sent[i][1] == 0xAAu
           && r->sent[i][2] == reg && r->sent[i][3] == lo
           && r->sent[i][4] == hi;
}

/** Whether sensor command @p i is the unlock FF AA 69 88 B5. */
static bool rig_sent_unlock(const rig_t *r, unsigned i)
{
    return rig_sent_is(r, i, 0x69u, 0x88u, 0xB5u);
}

#endif
