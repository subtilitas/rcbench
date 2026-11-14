/*
 * A scripted host stack and balance node for the suites of the panel's
 * balance node controller (bal_ctl.h).
 *
 * The rig answers each action of the controller at once with the events
 * NimBLE's callbacks would give for a peer that serves the rcnode service:
 * STREAM (NOTIFY) at 0x0012 with its CCCD at 0x0013, STATUS (READ, NOTIFY)
 * at 0x0015, CONTROL (WRITE, WRITE NO RESPONSE) at 0x0017, INFO (READ) at
 * 0x0019.  INFO names RCNODE_PROTO and RCNODE_ROLE_BALANCE unless the case
 * sets others; STATUS, read or sent on STREAM by brig_status(), names the
 * rig's cell voltage and flags.  Every
 * CONTROL write is kept with its time.  The clock is the rig's; brig_run()
 * steps the controller 1ms at a time.
 *
 * Only static inline helpers.
 *
 * Used by:
 * - test_bal_ctl.c
 * - test_node_balance.c
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BAL_RIG_H
#define BAL_RIG_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bal_ctl.h"
#include "rcnode.h"

/** @brief The handles the rig serves. @{ */
#define BRIG_CONN        3u
#define BRIG_SVC_START   0x0010u
#define BRIG_SVC_END     0x0019u
#define BRIG_STREAM_VAL  0x0012u
#define BRIG_STREAM_CCCD 0x0013u
#define BRIG_STATUS_VAL  0x0015u
#define BRIG_CONTROL_VAL 0x0017u
#define BRIG_INFO_VAL    0x0019u
/** @} */

/** @brief The bound node's address. */
#define BRIG_ADDR "A0:B1:C2:7F:3A:10 R"
/** @brief CONTROL writes kept. */
#define BRIG_WRITES 256u

/** @brief The rig. */
typedef struct {
    bal_ctl_t    c;          /**< the controller under test */
    uint32_t     now;        /**< the clock, ms */
    uint32_t     boot_ms;    /**< the node's last start on that clock */
    bool         armed;      /**< the bench is ARMED */
    uint8_t      proto;      /**< the protocol INFO names */
    uint8_t      role;       /**< the role INFO names */
    uint16_t     vbat;       /**< the cell STATUS names, mV */
    uint16_t     st_flags;   /**< the flags STATUS names */
    uint16_t     tok_conn;   /**< the last connect's token */
    unsigned     connects;   /**< CONNECT actions */
    unsigned     scans;      /**< SCAN actions */
    unsigned     stops;      /**< SCAN_STOP actions */
    unsigned     info_reads; /**< READs of INFO */
    unsigned     st_reads;   /**< READs of STATUS: the panel makes none */
    unsigned     fail_info;  /**< the next INFO reads that fail */
    unsigned     fail_writes;/**< the next CONTROL writes the node refuses */
    uint8_t      seq;        /**< the node's STREAM seq */
    unsigned     nw;         /**< CONTROL writes kept */
    rcnode_ctl_t w[BRIG_WRITES];    /**< their decoded contents */
    uint32_t     w_ms[BRIG_WRITES]; /**< their times */
} brig_t;

/** @brief An event with its kind, token and connection. */
static inline bleu_evt_t brig_ev(uint8_t kind, uint16_t token)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.token = token;
    e.conn = BRIG_CONN;
    return e;
}

/** @brief A characteristic of the service: its rcnode ID and handle. */
static inline void brig_chr(brig_t *r, uint16_t token, uint16_t id,
                            uint16_t val, uint8_t props)
{
    bleu_evt_t e = brig_ev(BLEU_EV_CHR, token);
    rcnode_uuid(id, e.data);
    e.len = 16u;
    e.h1 = (uint16_t)(val - 1u);
    e.h2 = val;
    e.props = props;
    bal_ctl_event(&r->c, &e, r->now);
}

/** @brief Answer one action as the host stack and the node would. */
static inline void brig_act(brig_t *r, const bleu_act_t *a)
{
    bleu_evt_t e;
    switch (a->kind) {
    case BLEU_ACT_SCAN:
        ++r->scans;
        break;
    case BLEU_ACT_SCAN_STOP:
        ++r->stops;
        break;
    case BLEU_ACT_CONNECT:
        ++r->connects;
        r->tok_conn = a->token;
        e = brig_ev(BLEU_EV_CONNECT, a->token);
        e.h1 = 40u;
        e.h3 = 400u;
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_SVC:
        e = brig_ev(BLEU_EV_SVC, a->token);
        e.h1 = BRIG_SVC_START;
        e.h2 = BRIG_SVC_END;
        bal_ctl_event(&r->c, &e, r->now);
        e = brig_ev(BLEU_EV_SVC_DONE, a->token);
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_CHRS:
        brig_chr(r, a->token, RCNODE_ID_STREAM, BRIG_STREAM_VAL,
                 BLEU_PROP_NOTIFY);
        brig_chr(r, a->token, RCNODE_ID_STATUS, BRIG_STATUS_VAL,
                 BLEU_PROP_READ | BLEU_PROP_NOTIFY);
        brig_chr(r, a->token, RCNODE_ID_CONTROL, BRIG_CONTROL_VAL,
                 BLEU_PROP_WRITE | BLEU_PROP_WRITE_NR);
        brig_chr(r, a->token, RCNODE_ID_INFO, BRIG_INFO_VAL, BLEU_PROP_READ);
        e = brig_ev(BLEU_EV_CHR_DONE, a->token);
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_DSCS:
        e = brig_ev(BLEU_EV_DSC, a->token);
        e.h1 = BRIG_STREAM_CCCD;
        e.data[0] = 0x02u;
        e.data[1] = 0x29u;
        e.len = 2u;
        bal_ctl_event(&r->c, &e, r->now);
        e = brig_ev(BLEU_EV_DSC_DONE, a->token);
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_MTU:
        e = brig_ev(BLEU_EV_MTU, a->token);
        e.h1 = 247u;
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_SUBSCRIBE:
        e = brig_ev(BLEU_EV_WRITE_DONE, a->token);
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_READ:
        e = brig_ev(BLEU_EV_READ, a->token);
        if (a->h1 == BRIG_INFO_VAL) {
            rcnode_info_t i;
            memset(&i, 0, sizeof i);
            i.hdr.type = RCNODE_T_INFO;
            i.proto = r->proto;
            i.role = r->role;
            i.board = RCNODE_BOARD_C6_ZERO;
            i.hdr.t_node = (r->now - r->boot_ms) * 1000u;
            ++r->info_reads;
            e.len = (uint8_t)rcnode_enc_info(&i, e.data, sizeof e.data);
            if (r->fail_info > 0u) {
                --r->fail_info;
                e.len = 0u;
                e.status = 0x10Eu;      /* an ATT error */
            }
        } else {
            rcnode_status_t st;
            memset(&st, 0, sizeof st);
            st.hdr.type = RCNODE_T_STATUS;
            st.vbat = r->vbat;
            st.flags = r->st_flags;
            ++r->st_reads;
            e.len = (uint8_t)rcnode_enc_status(&st, e.data, sizeof e.data);
        }
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_WRITE:
    case BLEU_ACT_WRITE_NR:
        e = brig_ev(BLEU_EV_WRITE_DONE, a->token);
        if (r->fail_writes > 0u) {
            --r->fail_writes;
            e.status = 0x10Eu;
        } else if (r->nw < BRIG_WRITES
                   && rcnode_dec_ctl(a->data, a->len, &r->w[r->nw])
                          == RCNODE_OK) {
            r->w_ms[r->nw] = r->now;
            ++r->nw;
        }
        bal_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISCONNECT:
        e = brig_ev(BLEU_EV_DISCONNECT, r->tok_conn);
        e.status = 0x216u;
        bal_ctl_event(&r->c, &e, r->now);
        break;
    default:
        break;
    }
}

/** @brief Step the controller until it hands out nothing. */
static inline void brig_step(brig_t *r)
{
    bleu_act_t a;
    for (int i = 0; i < 64 && bal_ctl_step(&r->c, r->now, r->armed, &a);
         ++i) {
        brig_act(r, &a);
    }
}

/** @brief Run @p ms of the clock, a step each ms. */
static inline void brig_run(brig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        ++r->now;
        brig_step(r);
    }
}

/** @brief Node ON, bound to BRIG_ADDR, Bluetooth synced, at 1000ms. */
static inline void brig_init(brig_t *r)
{
    memset(r, 0, sizeof *r);
    r->now = 1000u;
    r->proto = RCNODE_PROTO;
    r->role = RCNODE_ROLE_BALANCE;
    r->vbat = 3912u;
    bal_ctl_init(&r->c);
    bal_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.en = true;
    memcpy(cfg.addr, BRIG_ADDR, sizeof BRIG_ADDR);
    bal_ctl_config(&r->c, &cfg, r->now);
    bal_ctl_radio(&r->c, IMU_RADIO_READY, r->now);
}

/** @brief brig_init(), and the link up with INFO read; the clock runs
 *         50ms. */
static inline void brig_up(brig_t *r)
{
    brig_init(r);
    brig_run(r, 50u);
}

/** @brief A report from the node: its fields, the next seq. */
static inline void brig_report(brig_t *r, const rcnode_bal_report_t *in)
{
    rcnode_bal_report_t p = *in;
    p.hdr.type = RCNODE_T_BAL_REPORT;
    p.hdr.seq = r->seq++;
    p.hdr.t_node = (r->now - r->boot_ms) * 1000u;
    bleu_evt_t e = brig_ev(BLEU_EV_NOTIFY, 0u);
    e.h1 = BRIG_STREAM_VAL;
    e.len = (uint8_t)rcnode_enc_bal_report(&p, e.data, sizeof e.data);
    bal_ctl_event(&r->c, &e, r->now);
}

/** @brief A STATUS on STREAM, as the node sends it beside its reports:
 *         the rig's cell and flags, its own seq @p seq. */
static inline void brig_status(brig_t *r, uint8_t seq)
{
    rcnode_status_t st;
    memset(&st, 0, sizeof st);
    st.hdr.type = RCNODE_T_STATUS;
    st.hdr.seq = seq;
    st.hdr.t_node = (r->now - r->boot_ms) * 1000u;
    st.vbat = r->vbat;
    st.flags = r->st_flags;
    bleu_evt_t e = brig_ev(BLEU_EV_NOTIFY, 0u);
    e.h1 = BRIG_STREAM_VAL;
    e.len = (uint8_t)rcnode_enc_status(&st, e.data, sizeof e.data);
    bal_ctl_event(&r->c, &e, r->now);
}

/** @brief A steady report: valid, the index seen, quality 95%. */
static inline rcnode_bal_report_t brig_steady(uint16_t mag_mg,
                                              uint16_t phase_tenths,
                                              uint16_t rpm)
{
    rcnode_bal_report_t p;
    memset(&p, 0, sizeof p);
    p.flags = RCNODE_BF_VALID | RCNODE_BF_INDEX_SEEN
              | RCNODE_BF_INDEX_STEADY | RCNODE_BF_DC_OK
              | RCNODE_BF_BLADE_OK | RCNODE_BF_QUALITY;
    p.quality = 95u;
    p.revs = 30u;
    p.rpm = rpm;
    p.mag_1x = mag_mg;
    p.phase_1x = phase_tenths;
    return p;
}

/** @brief The link ends from the node's side. */
static inline void brig_drop(brig_t *r)
{
    bleu_evt_t e = brig_ev(BLEU_EV_DISCONNECT, r->tok_conn);
    e.status = 0x213u;
    bal_ctl_event(&r->c, &e, r->now);
}

#endif /* BAL_RIG_H */
