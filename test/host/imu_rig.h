/*
 * The rig of the Bluetooth IMU's suites: WitMotion frames built byte by byte
 * from the layout in witmotion.h, and the controller (imu_ctl.h) run 1ms at
 * a time against the scripted HC-05 of hc05_sim.h.  A SEND the controller
 * hands out while no AT operation runs is a sensor command: it is logged
 * apart and the module does not see it.
 *
 * Functions are `static`, under the sim's unused-function pragma.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef IMU_RIG_H
#define IMU_RIG_H

#include <math.h>
#include <string.h>

#include "greatest.h"
#include "hc05_sim.h"
#include "imu.h"
#include "imu_ctl.h"
#include "witmotion.h"

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
    sim_t     sim;
    imu_ctl_t c;
    uint32_t  t;                            /* the clock, ms */
    uint8_t   sent[RIG_SENT_MAX][WIT_CMD_LEN]; /* sensor commands */
    uint32_t  sent_at[RIG_SENT_MAX];
    unsigned  nsent;
    unsigned  at_key_low;                   /* AT bytes while KEY low */
} rig_t;

/** A rig on a 2.0 module at @p baud, the clock at 1000ms. */
static void rig_init(rig_t *r, int fw, uint32_t baud)
{
    memset(r, 0, sizeof *r);
    sim_init(&r->sim, fw, baud);
    imu_ctl_init(&r->c);
    r->t = 1000u;
}

/** The module's answers due now, into the controller (sim_deliver()'s). */
static void rig_deliver(rig_t *r)
{
    sim_t *s = &r->sim;
    if (s->restart_due && (int32_t)(r->t - s->restart_at) >= 0) {
        s->restart_due = false;
        s->restarted_key = s->key;
        s->baud = s->key ? HC05_BAUD_ALT : s->uart_set;
        s->inited = false;
        ++s->restarts;
    }
    unsigned keep = 0;
    for (unsigned i = 0; i < s->nout; ++i) {
        const sim_out_t *o = &s->out[i];
        if ((int32_t)(r->t - o->at) >= 0) {
            if (s->uart == s->baud || o->text[0] == '\xF8') {
                imu_ctl_feed(&r->c, (const uint8_t *)o->text, o->n, r->t);
            }
        } else {
            s->out[keep++] = *o;
        }
    }
    s->nout = keep;
}

/** One ms: the answers due, then every action of that ms. */
static void rig_tick(rig_t *r)
{
    rig_deliver(r);
    hc05_act_t a;
    for (int i = 0; i < 16 && imu_ctl_step(&r->c, r->t, &a); ++i) {
        if (a.kind == HC05_ACT_SEND && !hc05_busy(&r->c.bt)) {
            if (r->nsent < RIG_SENT_MAX && a.len == WIT_CMD_LEN) {
                memcpy(r->sent[r->nsent], a.data, WIT_CMD_LEN);
                r->sent_at[r->nsent] = r->t;
                ++r->nsent;
            }
            continue;
        }
        if (a.kind == HC05_ACT_SEND && !r->sim.key) {
            ++r->at_key_low;
        }
        sim_take(&r->sim, &a, r->t);
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

/** Run until no operation runs and KEY is low, @p max ms at most.
 *  @return the ms it took. */
static uint32_t rig_settle(rig_t *r, uint32_t max)
{
    uint32_t k = 0;
    for (; k < max; ++k) {
        rig_tick(r);
        if (!hc05_busy(&r->c.bt) && !r->c.bt.key && !r->c.op_open) {
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

/** Angle frames every @p period_ms for @p ms ms, as the sensor streams. */
static void rig_stream(rig_t *r, uint32_t ms, uint32_t period_ms, float roll,
                       float pitch, float yaw)
{
    uint8_t f[WIT_FRAME_LEN];
    rig_angle(f, roll, pitch, yaw);
    for (uint32_t k = 0; k < ms; ++k) {
        if (k % period_ms == 0u) {
            imu_ctl_feed(&r->c, f, sizeof f, r->t);
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
