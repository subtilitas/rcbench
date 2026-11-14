/*
 * A scripted host stack and M5Stack Dial for the suites of the panel's
 * Dial controller (dial_ctl.h).
 *
 * The rig answers each action of the controller at once with the events
 * NimBLE's callbacks would give for a peer that serves the dialknob
 * service: EVENT (NOTIFY) at 0x0012 with its CCCD at 0x0013, SCREEN (WRITE
 * NO RESPONSE) at 0x0015, INFO (READ) at 0x0017, STATUS at 0x0019.  INFO
 * reads back DK_PROTOCOL unless the case sets another.  Every panel packet
 * written to SCREEN is kept.  The clock is the rig's; drig_run() steps the
 * controller 1ms at a time.
 *
 * Only static inline helpers.
 *
 * Used by:
 * - test_dial_ctl.c
 * - test_dial_motor.c
 * - test_dial_servo.c
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DIAL_RIG_H
#define DIAL_RIG_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "dial_ctl.h"
#include "dialknob.h"

/** @brief The handles the rig serves. @{ */
#define DRIG_CONN       3u
#define DRIG_SVC_START  0x0010u
#define DRIG_SVC_END    0x0019u
#define DRIG_EVENT_VAL  0x0012u
#define DRIG_EVENT_CCCD 0x0013u
#define DRIG_SCREEN_VAL 0x0015u
#define DRIG_INFO_VAL   0x0017u
#define DRIG_STATUS_VAL 0x0019u
/** @} */

/** @brief The bound Dial's address. */
#define DRIG_ADDR "A0:B1:C2:7F:3A:10 R"
/** @brief Panel packets kept. */
#define DRIG_PKTS 512u

/** @brief The rig. */
typedef struct {
    dial_ctl_t c;           /**< the controller under test */
    uint32_t   now;         /**< the clock, ms */
    bool       armed;       /**< the bench is ARMED */
    uint8_t    proto;       /**< the protocol INFO names */
    bool       refuse;      /**< connects fail */
    uint16_t   tok_conn;    /**< the last connect's token */
    bool       linked;      /**< a connection stands */
    uint8_t    seq;         /**< the Dial's EVENT seq */
    unsigned   connects;    /**< CONNECT actions */
    unsigned   scans;       /**< SCAN actions */
    unsigned   npkt;        /**< panel packets kept */
    uint8_t    pkt[DRIG_PKTS][DK_PKT_MAX]; /**< their bytes */
    uint8_t    len[DRIG_PKTS]; /**< their lengths */
} drig_t;

/** @brief An event with its kind, token and connection. */
static inline bleu_evt_t drig_ev(uint8_t kind, uint16_t token)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.token = token;
    e.conn = DRIG_CONN;
    return e;
}

/** @brief A characteristic of the service, its UUID suffix @p xx. */
static inline void drig_chr(drig_t *r, uint16_t token, uint8_t xx,
                            uint16_t val, uint8_t props)
{
    static const uint8_t base[16] = { DK_UUID_LE(0) };
    bleu_evt_t e = drig_ev(BLEU_EV_CHR, token);
    memcpy(e.data, base, 16u);
    e.data[0] = xx;
    e.len = 16u;
    e.h1 = (uint16_t)(val - 1u);
    e.h2 = val;
    e.props = props;
    dial_ctl_event(&r->c, &e, r->now);
}

/** @brief Answer one action as the host stack and the Dial would. */
static inline void drig_act(drig_t *r, const bleu_act_t *a)
{
    bleu_evt_t e;
    switch (a->kind) {
    case BLEU_ACT_SCAN:
        ++r->scans;
        break;
    case BLEU_ACT_CONNECT:
        ++r->connects;
        r->tok_conn = a->token;
        e = drig_ev(BLEU_EV_CONNECT, a->token);
        e.status = r->refuse ? 0x23Eu : 0u;
        e.h1 = 40u;
        e.h3 = 400u;
        r->linked = !r->refuse;
        dial_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_SVC:
        e = drig_ev(BLEU_EV_SVC, a->token);
        e.h1 = DRIG_SVC_START;
        e.h2 = DRIG_SVC_END;
        dial_ctl_event(&r->c, &e, r->now);
        e = drig_ev(BLEU_EV_SVC_DONE, a->token);
        dial_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_CHRS:
        drig_chr(r, a->token, DK_UUID_EVENT, DRIG_EVENT_VAL, BLEU_PROP_NOTIFY);
        drig_chr(r, a->token, DK_UUID_SCREEN, DRIG_SCREEN_VAL,
                 BLEU_PROP_WRITE_NR);
        drig_chr(r, a->token, DK_UUID_INFO, DRIG_INFO_VAL, BLEU_PROP_READ);
        drig_chr(r, a->token, DK_UUID_STATUS, DRIG_STATUS_VAL,
                 BLEU_PROP_NOTIFY | BLEU_PROP_READ);
        e = drig_ev(BLEU_EV_CHR_DONE, a->token);
        dial_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_DSCS:
        e = drig_ev(BLEU_EV_DSC, a->token);
        e.h1 = DRIG_EVENT_CCCD;
        e.data[0] = 0x02u;
        e.data[1] = 0x29u;
        e.len = 2u;
        dial_ctl_event(&r->c, &e, r->now);
        e = drig_ev(BLEU_EV_DSC_DONE, a->token);
        dial_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_MTU:
        e = drig_ev(BLEU_EV_MTU, a->token);
        e.h1 = 247u;
        dial_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_SUBSCRIBE:
        e = drig_ev(BLEU_EV_WRITE_DONE, a->token);
        dial_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_READ: {
        dk_info_t i;
        memset(&i, 0, sizeof i);
        i.proto = r->proto;
        i.fw[1] = 1u;
        i.hw = DK_HW_DIAL11;
        memcpy(i.name, "DIAL-3A10", 10u);
        e = drig_ev(BLEU_EV_READ, a->token);
        e.len = (uint8_t)dk_info_encode(&i, e.data);
        dial_ctl_event(&r->c, &e, r->now);
        break;
    }
    case BLEU_ACT_WRITE_NR:
        if (r->npkt < DRIG_PKTS) {
            memcpy(r->pkt[r->npkt], a->data, a->len);
            r->len[r->npkt] = a->len;
            ++r->npkt;
        }
        e = drig_ev(BLEU_EV_WRITE_DONE, a->token);
        dial_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISCONNECT:
        e = drig_ev(BLEU_EV_DISCONNECT, r->tok_conn);
        e.status = 0x216u;
        r->linked = false;
        dial_ctl_event(&r->c, &e, r->now);
        break;
    default:
        break;
    }
}

/** @brief Step the controller until it hands out nothing. */
static inline void drig_step(drig_t *r)
{
    bleu_act_t a;
    for (int i = 0; i < 64 && dial_ctl_step(&r->c, r->now, r->armed, &a);
         ++i) {
        drig_act(r, &a);
    }
}

/** @brief Run @p ms of the clock, a step each ms. */
static inline void drig_run(drig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        ++r->now;
        drig_step(r);
    }
}

/** @brief Dial ON, bound to DRIG_ADDR, Bluetooth synced, at 1000ms. */
static inline void drig_init(drig_t *r)
{
    memset(r, 0, sizeof *r);
    r->now = 1000u;
    r->proto = DK_PROTOCOL;
    dial_ctl_init(&r->c);
    dial_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.en = true;
    memcpy(cfg.addr, DRIG_ADDR, sizeof DRIG_ADDR);
    cfg.thr_tenths = 10u;
    cfg.srv_tenths = 10u;
    dial_ctl_config(&r->c, &cfg, r->now);
    dial_ctl_radio(&r->c, IMU_RADIO_READY, r->now);
}

/** @brief drig_init(), and the link up with INFO read; the clock runs
 *         50ms. */
static inline void drig_up(drig_t *r)
{
    drig_init(r);
    drig_run(r, 50u);
}

/** @brief One EVENT from the Dial: the counter, the buttons held and a
 *         gesture, the next seq. */
static inline void drig_event(drig_t *r, int16_t counter, uint8_t buttons,
                              uint8_t gesture)
{
    dk_evt_t ev;
    memset(&ev, 0, sizeof ev);
    ev.seq = r->seq++;
    ev.counter = counter;
    ev.buttons = buttons;
    ev.gesture = gesture;
    ev.ms = (uint16_t)r->now;
    bleu_evt_t e = drig_ev(BLEU_EV_NOTIFY, 0u);
    e.h1 = DRIG_EVENT_VAL;
    e.len = (uint8_t)dk_evt_encode(&ev, e.data);
    dial_ctl_event(&r->c, &e, r->now);
}

/** @brief The link ends from the Dial's side. */
static inline void drig_drop(drig_t *r)
{
    bleu_evt_t e = drig_ev(BLEU_EV_DISCONNECT, r->tok_conn);
    e.status = 0x213u;
    r->linked = false;
    dial_ctl_event(&r->c, &e, r->now);
}

/** @brief The input since the last take. */
static inline dial_input_t drig_take(drig_t *r)
{
    dial_input_t in;
    dial_ctl_take(&r->c, &in);
    return in;
}

#endif /* DIAL_RIG_H */
