/*
 * The scripted host stack and power node that test_pnode_ctl,
 * test_pnode_shunt and test_pnode_cfg drive pnode_ctl.h with: the node's
 * service and characteristics, its INFO, its own settings and CONFIG
 * (node_store.h's rules), its answers to CONTROL writes, and the STREAM
 * packets it sends.  By default the node takes CONFIG; old_node makes it
 * one built before CONFIG, NAME and I228.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"

#include "bleuart.h"
#include "devimg.h"
#include "pnode_ctl.h"

/** @brief The handles the rig serves. @{ */
#define CONN       4u
#define SVC_START  0x0020u
#define SVC_END    0x002Bu
#define STREAM_VAL 0x0022u
#define STREAM_CCCD 0x0023u
#define CONTROL_VAL 0x0025u
#define INFO_VAL   0x0027u
#define STATUS_VAL 0x0029u
/** @} */

/** @brief The bound node's address. */
#define ADDR "7C:DF:A1:12:34:56 P"

/** @brief The rig. */
typedef struct {
    pnode_ctl_t c;        /**< the controller under test */
    uint32_t    now;      /**< the clock, ms */
    bool        armed;    /**< the bench is ARMED */
    bool        servo_run; /**< the run is SERVO's: it reads no BENCH */
    rcnode_info_t info;   /**< what INFO reads */
    unsigned    fail_info; /**< the next INFO reads that fail */
    uint16_t    tok_conn; /**< the last connect's token */
    unsigned    connects; /**< CONNECT actions */
    unsigned    discs;    /**< DISCONNECT actions */
    unsigned    scans;    /**< SCAN actions */
    unsigned    stops;    /**< SCAN_STOP actions */
    unsigned    writes;   /**< CONTROL writes but LANG */
    unsigned    langs;    /**< LANG writes */
    uint8_t     lang;     /**< the last one's language */
    uint16_t    lang_status; /**< the ATT status the node answers LANG with */
    unsigned    shunts;   /**< SHUNT writes */
    uint16_t    shunt_status; /**< the ATT status the node answers SHUNT with */
    bool        shunt_hold; /**< SHUNT's answer waits: shunt_tok holds it */
    bool        shunt_ignore; /**< the node keeps its shunt on a SHUNT */
    uint16_t    shunt_tok;  /**< the held SHUNT write's token */
    unsigned    info_reads; /**< INFO reads answered */
    uint8_t     wr[4];    /**< the last one's bytes */
    uint8_t     wr_len;   /**< its length */
    uint8_t     seq;      /**< the node's STREAM seq */
    bool        hold_read; /**< INFO reads wait for read_answer() */
    uint16_t    read_tok; /**< the waiting read's token, 0 for none */
    bool        old_node; /**< built before CONFIG: refuses it with 0x13
                               and NAME by its length */
    bool        cfg_silent; /**< takes CONFIG and sends none */
    bool        cfg_on;   /**< CONFIG was asked on this link */
    rcnode_config_t cfg;  /**< the node's own settings */
    unsigned    cfgs;     /**< CONFIG writes */
    unsigned    cfg_sent; /**< CONFIG packets sent */
    unsigned    names;    /**< NAME writes */
    unsigned    i228s;    /**< I228 writes */
} rig_t;

/** @brief One STREAM notification of @p n bytes. */
static inline void notify(rig_t *r, const uint8_t *b, size_t n);

/** @brief The node's CONFIG on STREAM, once asked on this link. */
static inline void push_cfg(rig_t *r)
{
    if (!r->cfg_on || r->cfg_silent) {
        return;
    }
    r->cfg.shunt_uohm = r->info.shunt_uohm;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_config(&r->cfg, b, sizeof b);
    ++r->cfg_sent;
    notify(r, b, n);
}

/** @brief An event with its kind and token. */
static inline bleu_evt_t ev(uint8_t kind, uint16_t token)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.token = token;
    e.conn = CONN;
    return e;
}

/** @brief A characteristic of the service. */
static inline void chr(rig_t *r, uint16_t token, uint16_t id, uint16_t val,
                uint8_t props)
{
    bleu_evt_t e = ev(BLEU_EV_CHR, token);
    rcnode_uuid(id, e.data);
    e.len = 16u;
    e.h1 = (uint16_t)(val - 1u);
    e.h2 = val;
    e.props = props;
    pnode_ctl_event(&r->c, &e, r->now);
}

/** @brief Answer one action as the host stack and the node would. */
static inline void act(rig_t *r, const bleu_act_t *a)
{
    bleu_evt_t e;
    switch (a->kind) {
    case BLEU_ACT_SCAN:      ++r->scans; break;
    case BLEU_ACT_SCAN_STOP: ++r->stops; break;
    case BLEU_ACT_CONNECT:
        ++r->connects;
        r->tok_conn = a->token;
        e = ev(BLEU_EV_CONNECT, a->token);
        e.h1 = 40u;
        e.h3 = 400u;
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_SVC:
        e = ev(BLEU_EV_SVC, a->token);
        e.h1 = SVC_START;
        e.h2 = SVC_END;
        pnode_ctl_event(&r->c, &e, r->now);
        e = ev(BLEU_EV_SVC_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_CHRS:
        chr(r, a->token, RCNODE_ID_STREAM, STREAM_VAL, BLEU_PROP_NOTIFY);
        chr(r, a->token, RCNODE_ID_CONTROL, CONTROL_VAL,
            BLEU_PROP_WRITE | BLEU_PROP_WRITE_NR);
        chr(r, a->token, RCNODE_ID_INFO, INFO_VAL, BLEU_PROP_READ);
        chr(r, a->token, RCNODE_ID_STATUS, STATUS_VAL,
            BLEU_PROP_READ | BLEU_PROP_NOTIFY);
        e = ev(BLEU_EV_CHR_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_DSCS:
        e = ev(BLEU_EV_DSC, a->token);
        e.h1 = STREAM_CCCD;
        e.data[0] = 0x02u;
        e.data[1] = 0x29u;
        e.len = 2u;
        pnode_ctl_event(&r->c, &e, r->now);
        e = ev(BLEU_EV_DSC_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_MTU:
        e = ev(BLEU_EV_MTU, a->token);
        e.h1 = 247u;
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_SUBSCRIBE:
        e = ev(BLEU_EV_WRITE_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_READ:
        if (r->hold_read) {
            r->read_tok = a->token;
            break;
        }
        e = ev(BLEU_EV_READ, a->token);
        e.len = (uint8_t)rcnode_enc_info(&r->info, e.data, sizeof e.data);
        ++r->info_reads;
        if (r->fail_info > 0u) {
            --r->fail_info;
            e.len = 0u;
            e.status = 0x10Eu;
        }
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_WRITE:
    case BLEU_ACT_WRITE_NR:
        if (a->len == 2u && a->data[0] == RCNODE_OP_LANG) {
            ++r->langs;
            r->lang = a->data[1];
            e = ev(BLEU_EV_WRITE_DONE, a->token);
            e.status = r->lang_status;
            pnode_ctl_event(&r->c, &e, r->now);
            break;
        }
        if (a->len == 3u && a->data[0] == RCNODE_OP_SHUNT) {
            /* The node takes it into INFO at once (node_pwr.c), and as
             * the agreed value (node_store.h). */
            ++r->shunts;
            r->wr_len = 3u;
            memcpy(r->wr, a->data, 3u);
            const uint16_t v = (uint16_t)(a->data[1] | (a->data[2] << 8));
            if (r->shunt_status == 0u && !r->shunt_ignore) {
                r->info.shunt_uohm = v;
                r->cfg.shunt_agreed = v;
                r->cfg.shunt_age_s = RCNODE_AGE_NONE;
            }
            if (r->shunt_hold) {
                /* The node took it; its answer is lost on the way. */
                r->shunt_tok = a->token;
                if (r->shunt_status == 0u && !r->shunt_ignore) {
                    push_cfg(r);
                }
                break;
            }
            e = ev(BLEU_EV_WRITE_DONE, a->token);
            e.status = r->shunt_status;
            pnode_ctl_event(&r->c, &e, r->now);
            if (r->shunt_status == 0u) {
                push_cfg(r);
            }
            break;
        }
        if (a->len >= 1u && (a->data[0] == RCNODE_OP_CONFIG
                             || a->data[0] == RCNODE_OP_NAME
                             || a->data[0] == RCNODE_OP_I228)) {
            const uint8_t op = a->data[0];
            r->cfgs += op == RCNODE_OP_CONFIG ? 1u : 0u;
            r->names += op == RCNODE_OP_NAME ? 1u : 0u;
            r->i228s += op == RCNODE_OP_I228 ? 1u : 0u;
            rcnode_ctl_t c;
            e = ev(BLEU_EV_WRITE_DONE, a->token);
            if (r->old_node) {
                e.status = op == RCNODE_OP_NAME ? 0x10Du : 0x113u;
            } else if (rcnode_dec_ctl(a->data, a->len, &c) != RCNODE_OK) {
                e.status = 0x113u;
            }
            pnode_ctl_event(&r->c, &e, r->now);
            if (e.status != 0u) {
                break;
            }
            if (op == RCNODE_OP_CONFIG) {
                r->cfg_on = true;
            } else if (op == RCNODE_OP_NAME) {
                memcpy(r->cfg.name, c.name, sizeof r->cfg.name);
            } else {
                r->cfg.i228_set = c.i228;
            }
            push_cfg(r);
            break;
        }
        ++r->writes;
        r->wr_len = a->len < sizeof r->wr ? a->len : (uint8_t)sizeof r->wr;
        memcpy(r->wr, a->data, r->wr_len);
        e = ev(BLEU_EV_WRITE_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISCONNECT:
        ++r->discs;
        r->cfg_on = false;
        e = ev(BLEU_EV_DISCONNECT, r->tok_conn);
        e.status = 0x216u;
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    default:
        break;
    }
}

/** @brief Step the controller until it hands out nothing. */
static inline void step(rig_t *r)
{
    bleu_act_t a;
    for (int i = 0; i < 64 && pnode_ctl_step(&r->c, r->now, r->armed, !r->servo_run, &a);
         ++i) {
        act(r, &a);
    }
}

/** @brief Answer the INFO read held by hold_read. */
static inline void read_answer(rig_t *r)
{
    bleu_evt_t e = ev(BLEU_EV_READ, r->read_tok);
    e.len = (uint8_t)rcnode_enc_info(&r->info, e.data, sizeof e.data);
    r->read_tok = 0u;
    r->hold_read = false;
    pnode_ctl_event(&r->c, &e, r->now);
}

/** @brief Run @p ms of the clock, a step each ms. */
static inline void run(rig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        ++r->now;
        step(r);
    }
}

/** @brief One STREAM notification of @p n bytes. */
static inline void notify(rig_t *r, const uint8_t *b, size_t n)
{
    bleu_evt_t e = ev(BLEU_EV_NOTIFY, 0u);
    e.h1 = STREAM_VAL;
    e.len = (uint8_t)n;
    memcpy(e.data, b, n);
    pnode_ctl_event(&r->c, &e, r->now);
}

/** @brief One window with the INA228 online: 10A, 22.2V. */
static inline void window(rig_t *r, uint8_t flags)
{
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.hdr.seq = r->seq++;
    w.flags = flags;
    w.n = 18u;
    w.i_mean = 10000000;
    w.i_max = 12000000;
    w.v_mean = 11100u;
    w.v_min = 11000u;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_pwr_win(&w, b, sizeof b);
    notify(r, b, n);
}

/** @brief One STATUS on STREAM: the cell at @p mv. */
static inline void status(rig_t *r, uint16_t mv, uint16_t flags)
{
    rcnode_status_t s;
    memset(&s, 0, sizeof s);
    s.vbat = mv;
    s.flags = flags;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_status(&s, b, sizeof b);
    notify(r, b, n);
}

/** @brief One STATUS on STREAM naming firmware @p minor.@p patch. */
static inline void status_fw(rig_t *r, uint8_t minor, uint8_t patch)
{
    rcnode_status_t s;
    memset(&s, 0, sizeof s);
    s.vbat = 3900u;
    s.fw[1] = minor;
    s.fw[2] = patch;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_status(&s, b, sizeof b);
    notify(r, b, n);
}

/** @brief POWER NODE ON with @p src_node, bound to ADDR, Bluetooth synced,
 *         at 1000ms; INFO the MATEK's. */
static inline void init(rig_t *r, bool src_node)
{
    memset(r, 0, sizeof *r);
    r->now = 1000u;
    r->info.proto = RCNODE_PROTO;
    r->info.role = RCNODE_ROLE_POWER;
    r->info.board = RCNODE_BOARD_C6_ZERO;
    r->info.i228_addr = 0x45u;
    r->info.adcrange = 1u;
    r->info.shunt_uohm = 200u;
    r->cfg.flags = RCNODE_CF_SHUNT;
    r->cfg.shunt_uohm = 200u;
    r->cfg.shunt_agreed = 200u;
    r->cfg.shunt_age_s = RCNODE_AGE_NONE;
    pnode_ctl_init(&r->c);
    pnode_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.en = true;
    cfg.src_node = src_node;
    memcpy(cfg.addr, ADDR, sizeof ADDR);
    pnode_ctl_config(&r->c, &cfg, r->now);
    pnode_ctl_radio(&r->c, IMU_RADIO_READY, r->now);
}

/** @brief init() and the link up with INFO read; 50ms of clock. */
static inline void up(rig_t *r, bool src_node)
{
    init(r, src_node);
    run(r, 50u);
}
