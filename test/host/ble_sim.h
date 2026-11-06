/*
 * A scripted BLE peer and host stack for the suites that test
 * protocols/bleuart.
 *
 * The sim plays the host stack and the sensor at once: it takes the core's
 * actions and answers with the events NimBLE's callbacks would give.  It
 * advertises a list of reports during a scan, accepts or refuses a connect
 * after a set time or never answers it, serves the ISSC service's GATT
 * table (variants: without the service, without 8841, without the CCCD,
 * WRITE NO RESPONSE only, 1E4D without NOTIFY, 1E4D last), answers each
 * discovery step after `lat` ms or ignores the steps in `silent`, streams a
 * byte sequence as notifications cut at a cycle of lengths, answers or
 * ignores writes, and drops the link on request.  A disconnect answers the
 * GATT steps still pending with status 7 (NimBLE's BLE_HS_ENOTCONN) before
 * its own event, as NimBLE does.
 *
 * The driver runs the core 1ms at a time: the events due are handed in,
 * a notification is sent when one is due, then every action of that ms is
 * taken.  Stream bytes the core passes on go to a WitMotion parser.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BLE_SIM_H
#define BLE_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bleuart.h"
#include "witmotion.h"

#pragma GCC diagnostic ignored "-Wunused-function"

#define BSIM_EV_MAX   96u
#define BSIM_LOG_MAX  256u
#define BSIM_ADV_MAX  16u
#define BSIM_CHR_MAX  6u
#define BSIM_DSC_MAX  4u
#define BSIM_RX_MAX   1024u

/* The silent mask: steps the peer leaves unanswered. */
#define BSIM_Q_SVC   0x01u
#define BSIM_Q_CHR   0x02u
#define BSIM_Q_DSC   0x04u
#define BSIM_Q_MTU   0x08u
#define BSIM_Q_SUB   0x10u
#define BSIM_Q_READ  0x20u
#define BSIM_Q_WRITE 0x40u

/* The status the sim's host stack gives a cancelled connect and a GATT
 * step a disconnect broke, NimBLE's BLE_HS_EAPP and BLE_HS_ENOTCONN. */
#define BSIM_ST_CANCELLED 9u
#define BSIM_ST_NOTCONN   7u
/* A refused connect: HCI 0x3E, failed to be established, as NimBLE gives
 * it (0x200 + HCI code). */
#define BSIM_ST_REFUSED   0x23Eu

/* The handles of the standard table. */
#define H_SVC_START 0x0010u
#define H_SVC_END   0x001Au
#define H_TX_DEF    0x0011u
#define H_TX_VAL    0x0012u
#define H_TX_CCCD   0x0013u
#define H_RX_DEF    0x0014u
#define H_RX_VAL    0x0015u
#define H_AIR_DEF   0x0016u
#define H_AIR_VAL   0x0017u
#define H_AIR_CCCD  0x0018u
#define H_PAR_DEF   0x0019u
#define H_PAR_VAL   0x001Au

/* 6DAA's value as the phone read it. */
static const uint8_t k_param[8] = { 0x00, 0x0C, 0x00, 0x0C,
                                    0x00, 0x00, 0x00, 0x02 };

typedef struct {
    uint32_t at;
    bleu_evt_t e;
} bsim_ev_t;

typedef struct {
    uint32_t t;
    bleu_act_t a;
} bsim_log_t;

typedef struct {
    uint32_t at;          /* after the SCAN action, ms */
    bleu_addr_t addr;
    int8_t rssi;
    uint8_t ad[40];
    uint8_t len;
} bsim_adv_t;

typedef struct {
    uint16_t def, val;
    uint8_t props;
    const uint8_t *uuid;
} bsim_chr_t;

typedef struct {
    uint16_t h;
    uint16_t uuid16;
} bsim_dsc_t;

typedef struct {
    /* The peer. */
    bool absent;           /* a connect is never answered */
    uint32_t conn_ms;      /* CONNECT to its event */
    uint16_t conn_status;  /* not 0: the connect fails with it */
    bool conn_lost;        /* the failure comes as a DISCONNECT with the
                              connect's token, as NimBLE gives HCI 0x3E */
    bool cancel_silent;    /* a CANCEL gives no event */
    bool cancel_race;      /* a CANCEL's event is a connection, status 0 */
    bool scan_fail;        /* a SCAN fails at once, status 2 */
    uint32_t lat;          /* a GATT answer's latency, ms */
    uint8_t silent;        /* BSIM_Q_* left unanswered */
    uint8_t fail;          /* BSIM_Q_* answered with fail_status */
    uint16_t fail_status;
    uint16_t mtu;          /* the MTU the peer answers */
    bool no_svc;           /* the service is missing */
    bsim_chr_t chr[BSIM_CHR_MAX];
    unsigned nchr;
    bsim_dsc_t dsc[BSIM_DSC_MAX];
    unsigned ndsc;
    uint16_t tx_cccd;      /* 1E4D's CCCD in the table */
    bsim_adv_t adv[BSIM_ADV_MAX];
    unsigned nadv;
    /* The stream. */
    const uint8_t *stream;
    size_t stream_n, stream_pos;
    const uint8_t *cuts;   /* notification lengths, cycled */
    size_t ncuts, cut_i;
    uint32_t period;       /* ms between notifications */
    uint32_t next_notify;
    bool notify_unsub;     /* notify before the CCCD write */
    /* The sim's state. */
    bool connected, connecting, subscribed, scanning;
    uint16_t conn, next_conn, conn_token, scan_token;
    bsim_ev_t ev[BSIM_EV_MAX];
    unsigned nev;
    bsim_log_t log[BSIM_LOG_MAX];
    unsigned nlog;
    uint8_t rx[BSIM_RX_MAX];
    size_t nrx;
    unsigned notifies_sent;
    /* The stream as the WitMotion parser reads it. */
    wit_parser_t wp;
    unsigned angles;
    wit_angle_t angle;
} bsim_t;

/* The standard table: 1E4D, 8841, ACA3, 6DAA, in that order. */
static void bsim_table(bsim_t *s)
{
    const bsim_chr_t chr[4] = {
        { H_TX_DEF, H_TX_VAL, BLEU_PROP_NOTIFY, bleu_uuid_tx },
        { H_RX_DEF, H_RX_VAL, BLEU_PROP_WRITE | BLEU_PROP_WRITE_NR,
          bleu_uuid_rx },
        { H_AIR_DEF, H_AIR_VAL,
          BLEU_PROP_NOTIFY | BLEU_PROP_READ | BLEU_PROP_WRITE, bleu_uuid_air },
        { H_PAR_DEF, H_PAR_VAL, BLEU_PROP_READ | BLEU_PROP_WRITE,
          bleu_uuid_param },
    };
    memcpy(s->chr, chr, sizeof chr);
    s->nchr = 4;
    s->dsc[0] = (bsim_dsc_t){ H_TX_CCCD, BLEU_UUID16_CCCD };
    s->dsc[1] = (bsim_dsc_t){ H_AIR_CCCD, BLEU_UUID16_CCCD };
    s->ndsc = 2;
    s->tx_cccd = H_TX_CCCD;
}

static void bsim_init(bsim_t *s)
{
    memset(s, 0, sizeof *s);
    s->conn_ms = 30;
    s->lat = 20;
    s->mtu = 23;
    s->period = 10;
    s->next_conn = 1;
    bsim_table(s);
    wit_parser_init(&s->wp);
}

/* Find the characteristic with @p uuid, or NULL. */
static bsim_chr_t *bsim_chr(bsim_t *s, const uint8_t *uuid)
{
    for (unsigned i = 0; i < s->nchr; ++i) {
        if (s->chr[i].uuid == uuid) {
            return &s->chr[i];
        }
    }
    return NULL;
}

/* Take the characteristic with @p uuid out of the table. */
static void bsim_drop_chr(bsim_t *s, const uint8_t *uuid)
{
    for (unsigned i = 0; i < s->nchr; ++i) {
        if (s->chr[i].uuid == uuid) {
            memmove(&s->chr[i], &s->chr[i + 1u],
                    (s->nchr - i - 1u) * sizeof s->chr[0]);
            --s->nchr;
            return;
        }
    }
}

/* Move 1E4D behind 6DAA: ACA3 0x11, 8841 0x14, 6DAA 0x16, 1E4D 0x18 with
 * its CCCD at 0x1A, the service's last handle. */
static void bsim_table_tx_last(bsim_t *s)
{
    const bsim_chr_t chr[4] = {
        { 0x0011u, 0x0012u,
          BLEU_PROP_NOTIFY | BLEU_PROP_READ | BLEU_PROP_WRITE, bleu_uuid_air },
        { 0x0014u, 0x0015u, BLEU_PROP_WRITE, bleu_uuid_rx },
        { 0x0016u, 0x0017u, BLEU_PROP_READ, bleu_uuid_param },
        { 0x0018u, 0x0019u, BLEU_PROP_NOTIFY, bleu_uuid_tx },
    };
    memcpy(s->chr, chr, sizeof chr);
    s->nchr = 4;
    s->dsc[0] = (bsim_dsc_t){ 0x0013u, BLEU_UUID16_CCCD };
    s->dsc[1] = (bsim_dsc_t){ 0x001Au, BLEU_UUID16_CCCD };
    s->ndsc = 2;
    s->tx_cccd = 0x001Au;
}

/* Take 1E4D's CCCD out of the table. */
static void bsim_drop_cccd(bsim_t *s)
{
    for (unsigned i = 0; i < s->ndsc; ++i) {
        if (s->dsc[i].h == s->tx_cccd) {
            s->dsc[i] = s->dsc[--s->ndsc];
            return;
        }
    }
}

/* Add an advertising report @p at ms into a scan. */
static void bsim_adv(bsim_t *s, uint32_t at, const bleu_addr_t *a,
                     int8_t rssi, const uint8_t *ad, size_t len)
{
    if (s->nadv >= BSIM_ADV_MAX || len > sizeof s->adv[0].ad) {
        return;
    }
    bsim_adv_t *v = &s->adv[s->nadv++];
    v->at = at;
    v->addr = *a;
    v->rssi = rssi;
    memcpy(v->ad, ad, len);
    v->len = (uint8_t)len;
}

/* Stream @p n bytes of @p bytes, cycled, cut at @p cuts. */
static void bsim_stream(bsim_t *s, const uint8_t *bytes, size_t n,
                        const uint8_t *cuts, size_t ncuts)
{
    s->stream = bytes;
    s->stream_n = n;
    s->stream_pos = 0;
    s->cuts = cuts;
    s->ncuts = ncuts;
    s->cut_i = 0;
}

static bleu_evt_t *bsim_queue(bsim_t *s, uint32_t at, uint8_t kind)
{
    if (s->nev >= BSIM_EV_MAX) {
        return NULL;
    }
    bsim_ev_t *v = &s->ev[s->nev++];
    memset(v, 0, sizeof *v);
    v->at = at;
    v->e.kind = kind;
    v->e.conn = s->conn;
    return &v->e;
}

/* Queue a GATT answer: @p kind with @p token and @p status. */
static bleu_evt_t *bsim_answer(bsim_t *s, uint32_t at, uint8_t kind,
                               uint16_t token, uint16_t status)
{
    bleu_evt_t *e = bsim_queue(s, at, kind);
    if (e != NULL) {
        e->token = token;
        e->status = status;
    }
    return e;
}

/* Drop the link from the peer's side at @p at with @p reason. */
static void bsim_drop(bsim_t *s, uint32_t at, uint16_t reason)
{
    if (!s->connected) {
        return;
    }
    for (unsigned i = 0; i < s->nev; ++i) {
        bleu_evt_t *e = &s->ev[i].e;
        if (e->conn == s->conn && e->token != 0u
            && e->kind != BLEU_EV_CONNECT) {
            /* A pending step ends with ENOTCONN at once. */
            s->ev[i].at = at;
            e->status = BSIM_ST_NOTCONN;
            if (e->kind == BLEU_EV_SVC) {
                e->kind = BLEU_EV_SVC_DONE;
            } else if (e->kind == BLEU_EV_CHR) {
                e->kind = BLEU_EV_CHR_DONE;
            } else if (e->kind == BLEU_EV_DSC) {
                e->kind = BLEU_EV_DSC_DONE;
            }
        }
    }
    bleu_evt_t *e = bsim_queue(s, at, BLEU_EV_DISCONNECT);
    if (e != NULL) {
        e->status = reason;
    }
    s->connected = false;
    s->subscribed = false;
}

/* The peer's answer to one action at @p t. */
static void bsim_act(bsim_t *s, uint32_t t, const bleu_act_t *a)
{
    const uint32_t at = t + s->lat;
    const uint8_t q = a->kind == BLEU_ACT_DISC_SVC   ? BSIM_Q_SVC
                      : a->kind == BLEU_ACT_DISC_CHRS ? BSIM_Q_CHR
                      : a->kind == BLEU_ACT_DISC_DSCS ? BSIM_Q_DSC
                      : a->kind == BLEU_ACT_MTU       ? BSIM_Q_MTU
                      : a->kind == BLEU_ACT_SUBSCRIBE ? BSIM_Q_SUB
                      : a->kind == BLEU_ACT_READ      ? BSIM_Q_READ
                      : a->kind == BLEU_ACT_WRITE     ? BSIM_Q_WRITE : 0u;
    if ((s->silent & q) != 0u) {
        return;
    }
    const uint16_t st = (s->fail & q) != 0u ? s->fail_status : 0u;
    bleu_evt_t *e;
    switch (a->kind) {
    case BLEU_ACT_SCAN:
        s->scanning = true;
        s->scan_token = a->token;
        if (s->scan_fail) {
            (void)bsim_answer(s, t, BLEU_EV_SCAN_END, a->token, 2u);
            s->scanning = false;
            break;
        }
        for (unsigned i = 0; i < s->nadv; ++i) {
            e = bsim_queue(s, t + s->adv[i].at, BLEU_EV_ADV);
            if (e != NULL) {
                e->addr = s->adv[i].addr;
                e->rssi = s->adv[i].rssi;
                e->len = s->adv[i].len;
                memcpy(e->data, s->adv[i].ad, s->adv[i].len);
            }
        }
        break;
    case BLEU_ACT_SCAN_STOP:
        s->scanning = false;
        for (unsigned i = 0; i < s->nev;) {
            if (s->ev[i].e.kind == BLEU_EV_ADV) {
                s->ev[i] = s->ev[--s->nev];
            } else {
                ++i;
            }
        }
        break;
    case BLEU_ACT_CONNECT:
        s->connecting = true;
        s->conn_token = a->token;
        if (!s->absent) {
            const bool lost = s->conn_lost && s->conn_status != 0u;
            e = bsim_answer(s, t + s->conn_ms,
                            lost ? BLEU_EV_DISCONNECT : BLEU_EV_CONNECT,
                            a->token, s->conn_status);
            if (e != NULL && s->conn_status == 0u) {
                /* The interval the controller picks: the most asked. */
                e->conn = s->next_conn;
                e->h1 = BLEU_ITVL_MAX;
                e->h2 = BLEU_LATENCY;
                e->h3 = BLEU_SUPERVISION;
            }
        }
        break;
    case BLEU_ACT_CANCEL:
        if (!s->connecting) {
            break;
        }
        s->connecting = false;
        for (unsigned i = 0; i < s->nev; ++i) {
            if (s->ev[i].e.kind == BLEU_EV_CONNECT
                && s->ev[i].e.token == s->conn_token) {
                s->ev[i] = s->ev[--s->nev];
                break;
            }
        }
        if (s->cancel_race) {
            e = bsim_answer(s, t + 2u, BLEU_EV_CONNECT, s->conn_token, 0);
            if (e != NULL) {
                e->conn = s->next_conn;
            }
        } else if (!s->cancel_silent) {
            (void)bsim_answer(s, t + 2u, BLEU_EV_CONNECT, s->conn_token,
                              BSIM_ST_CANCELLED);
        }
        break;
    case BLEU_ACT_DISCONNECT:
        /* A connection that is gone already gives no event. */
        if (s->connected && a->conn == s->conn) {
            bsim_drop(s, t + 5u, 0x16u);
        }
        break;
    case BLEU_ACT_DISC_SVC:
        if (!s->no_svc && st == 0u) {
            e = bsim_answer(s, at, BLEU_EV_SVC, a->token, 0);
            if (e != NULL) {
                e->h1 = H_SVC_START;
                e->h2 = H_SVC_END;
                e->len = 16;
                memcpy(e->data, bleu_uuid_svc, 16);
            }
        }
        (void)bsim_answer(s, at, BLEU_EV_SVC_DONE, a->token, st);
        break;
    case BLEU_ACT_DISC_CHRS:
        for (unsigned i = 0; i < s->nchr && st == 0u; ++i) {
            const bsim_chr_t *c = &s->chr[i];
            if (c->def < a->h1 || c->def > a->h2) {
                continue;
            }
            e = bsim_answer(s, at, BLEU_EV_CHR, a->token, 0);
            if (e != NULL) {
                e->h1 = c->def;
                e->h2 = c->val;
                e->props = c->props;
                e->len = 16;
                memcpy(e->data, c->uuid, 16);
            }
        }
        (void)bsim_answer(s, at, BLEU_EV_CHR_DONE, a->token, st);
        break;
    case BLEU_ACT_DISC_DSCS:
        for (unsigned i = 0; i < s->ndsc && st == 0u; ++i) {
            if (s->dsc[i].h <= a->h1 || s->dsc[i].h > a->h2) {
                continue;
            }
            e = bsim_answer(s, at, BLEU_EV_DSC, a->token, 0);
            if (e != NULL) {
                e->h1 = s->dsc[i].h;
                e->len = 2;
                e->data[0] = (uint8_t)(s->dsc[i].uuid16 & 0xFFu);
                e->data[1] = (uint8_t)(s->dsc[i].uuid16 >> 8);
            }
        }
        (void)bsim_answer(s, at, BLEU_EV_DSC_DONE, a->token, st);
        break;
    case BLEU_ACT_MTU:
        e = bsim_answer(s, at, BLEU_EV_MTU, a->token, st);
        if (e != NULL) {
            e->h1 = s->mtu < BLEU_MTU ? s->mtu : (uint16_t)BLEU_MTU;
        }
        break;
    case BLEU_ACT_SUBSCRIBE:
        if (st == 0u && a->h1 == s->tx_cccd && a->len == 2u
            && a->data[0] == 1u && a->data[1] == 0u) {
            s->subscribed = true;
            s->next_notify = at + s->period;
        }
        (void)bsim_answer(s, at, BLEU_EV_WRITE_DONE, a->token, st);
        break;
    case BLEU_ACT_READ:
        e = bsim_answer(s, at, BLEU_EV_READ, a->token, st);
        if (e != NULL && st == 0u) {
            e->len = sizeof k_param;
            memcpy(e->data, k_param, sizeof k_param);
        }
        break;
    case BLEU_ACT_WRITE:
        (void)bsim_answer(s, at, BLEU_EV_WRITE_DONE, a->token, st);
        break;
    case BLEU_ACT_WRITE_NR:
        /* The glue answers with the call's own status, at once. */
        (void)bsim_answer(s, t, BLEU_EV_WRITE_DONE, a->token, 0);
        break;
    default:
        break;
    }
}

/* Mark the connection the sim's next CONNECT event opens. */
static void bsim_open(bsim_t *s, const bleu_evt_t *e)
{
    if (e->kind == BLEU_EV_CONNECT && e->status == 0u) {
        s->connected = true;
        s->connecting = false;
        s->conn = e->conn;
        s->next_conn = (uint16_t)(s->next_conn + 1u);
    } else if (e->kind == BLEU_EV_CONNECT) {
        s->connecting = false;
    }
}

/* Hand @p e to the core; stream bytes go to the WitMotion parser. */
static void bsim_feed(bsim_t *s, bleu_t *b, const bleu_evt_t *e, uint32_t t)
{
    if (!bleu_event(b, e, t)) {
        return;
    }
    for (unsigned i = 0; i < e->len; ++i) {
        if (s->nrx < BSIM_RX_MAX) {
            s->rx[s->nrx++] = e->data[i];
        }
        wit_frame_t f;
        if (wit_parser_feed(&s->wp, e->data[i], &f)
            && wit_decode_angle(&f, &s->angle)) {
            ++s->angles;
        }
    }
}

/* Send the next notification of the stream on 1E4D. */
static void bsim_notify(bsim_t *s, bleu_t *b, uint32_t t)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_NOTIFY;
    e.conn = s->conn;
    const bsim_chr_t *tx = bsim_chr(s, bleu_uuid_tx);
    e.h1 = tx != NULL ? tx->val : H_TX_VAL;
    size_t n = s->ncuts ? s->cuts[s->cut_i++ % s->ncuts] : 20u;
    if (n > BLEU_EVT_DATA) {
        n = BLEU_EVT_DATA;
    }
    for (size_t i = 0; i < n; ++i) {
        e.data[i] = s->stream[s->stream_pos++ % s->stream_n];
    }
    e.len = (uint8_t)n;
    ++s->notifies_sent;
    bsim_feed(s, b, &e, t);
}

/* Whether @p at is due at @p t, across the wrap. */
static bool bsim_due(uint32_t at, uint32_t t)
{
    return (uint32_t)(t - at) < 0x80000000u;
}

/* One ms at @p t: the events due, in time order, then a notification,
 * then every action. */
static void bsim_tick(bsim_t *s, bleu_t *b, uint32_t t)
{
    for (;;) {
        unsigned best = s->nev;
        for (unsigned i = 0; i < s->nev; ++i) {
            if (bsim_due(s->ev[i].at, t)
                && (best == s->nev
                    || (int32_t)(s->ev[i].at - s->ev[best].at) < 0)) {
                best = i;
            }
        }
        if (best == s->nev) {
            break;
        }
        const bleu_evt_t e = s->ev[best].e;
        memmove(&s->ev[best], &s->ev[best + 1u],
                (s->nev - best - 1u) * sizeof s->ev[0]);
        --s->nev;
        bsim_open(s, &e);
        bsim_feed(s, b, &e, t);
    }
    /* Stream notifications go on 1E4D's value handle of the table. */
    if (s->stream != NULL && s->connected
        && (s->subscribed || s->notify_unsub)
        && bsim_due(s->next_notify, t)) {
        bsim_notify(s, b, t);
        s->next_notify = t + s->period;
    }
    bleu_act_t a;
    while (bleu_step(b, t, &a)) {
        if (s->nlog < BSIM_LOG_MAX) {
            s->log[s->nlog].t = t;
            s->log[s->nlog].a = a;
            ++s->nlog;
        }
        bsim_act(s, t, &a);
    }
}

/* Run from @p from up to, not including, @p to. */
static void bsim_run(bsim_t *s, bleu_t *b, uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t != to; ++t) {
        bsim_tick(s, b, t);
    }
}

/* Run from @p from until @p until holds or @p ms have passed; @return the
 * tick it held at, or from + ms. */
static uint32_t bsim_until(bsim_t *s, bleu_t *b, uint32_t from, uint32_t ms,
                           bool (*until)(const bleu_t *b, const bsim_t *s))
{
    for (uint32_t t = from; t != from + ms; ++t) {
        bsim_tick(s, b, t);
        if (until(b, s)) {
            return t;
        }
    }
    return from + ms;
}

/* The n-th logged action of @p kind, or NULL. */
static const bsim_log_t *bsim_nth(const bsim_t *s, uint8_t kind, unsigned n)
{
    for (unsigned i = 0; i < s->nlog; ++i) {
        if (s->log[i].a.kind == kind && n-- == 0u) {
            return &s->log[i];
        }
    }
    return NULL;
}

/* How many logged actions are of @p kind. */
static unsigned bsim_count(const bsim_t *s, uint8_t kind)
{
    unsigned n = 0;
    for (unsigned i = 0; i < s->nlog; ++i) {
        n += s->log[i].a.kind == kind;
    }
    return n;
}

/* The last logged action of @p kind, or NULL. */
static const bsim_log_t *bsim_last(const bsim_t *s, uint8_t kind)
{
    for (unsigned i = s->nlog; i-- > 0u;) {
        if (s->log[i].a.kind == kind) {
            return &s->log[i];
        }
    }
    return NULL;
}

/* Whether every write in the log went to 1E4D's CCCD or 8841's value and
 * every read to 6DAA's value: ACA3 and 6DAA are never written. */
static bool bsim_writes_ok(const bsim_t *s)
{
    const bsim_chr_t *rx = NULL, *par = NULL;
    for (unsigned i = 0; i < s->nchr; ++i) {
        if (s->chr[i].uuid == bleu_uuid_rx) {
            rx = &s->chr[i];
        } else if (s->chr[i].uuid == bleu_uuid_param) {
            par = &s->chr[i];
        }
    }
    for (unsigned i = 0; i < s->nlog; ++i) {
        const bleu_act_t *a = &s->log[i].a;
        if (a->kind == BLEU_ACT_SUBSCRIBE && a->h1 != s->tx_cccd) {
            return false;
        }
        if ((a->kind == BLEU_ACT_WRITE || a->kind == BLEU_ACT_WRITE_NR)
            && (rx == NULL || a->h1 != rx->val)) {
            return false;
        }
        if (a->kind == BLEU_ACT_READ && (par == NULL || a->h1 != par->val)) {
            return false;
        }
    }
    return true;
}

/* Whether the link is up. */
static bool bsim_is_up(const bleu_t *b, const bsim_t *s)
{
    (void)s;
    return bleu_link_up(b);
}

/* Whether the 6DAA read is done and a command can go. */
static bool bsim_can_send(const bleu_t *b, const bsim_t *s)
{
    (void)s;
    return bleu_can_send(b);
}

/* Switch on with the capture's sensor bound at @p t0 and run until a
 * command can go; @return that tick. */
static uint32_t bsim_up(bsim_t *s, bleu_t *b, const bleu_addr_t *a,
                        uint32_t t0)
{
    bleu_init(b);
    bleu_bind(b, a, t0);
    bleu_enable(b, true, t0);
    return bsim_until(s, b, t0, 2000u, bsim_can_send);
}

#endif
