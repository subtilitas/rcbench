/**
 * @file print_rig.h
 * @brief The label printer's controller (print_ctl.h) on a scripted host
 *        stack with the simulated printer of sv_sim.h behind it, for the
 *        suites test_print_ctl.c, test_print_ctl_ends.c,
 *        test_print_ctl_scan.c and test_wireless_printer.c.
 *
 * The scripted stack serves the T50M Pro's GATT table as the station read
 * it (H0, test/host/fixtures/supvan/README.md): the vendor service at
 * 0x000C to 0x0014, FFE1 at 0x000E with properties 0x1C, FFE9 at 0x0011
 * with 0x0C, FFEA at 0x0013 with 0x10, each declared in 128 bits of the
 * Bluetooth base; a descriptor discovery answered with an ATT error, as
 * the printer answers Find Information; a CCCD that reads back 00 00.  A
 * connect is answered after 20ms while the printer is present; a write is
 * answered 1ms after it; the printer's answers come as notifications of
 * 20 bytes on 0x000E.  The heap block comes from malloc(), or is refused.
 * A scan is counted; its reports are the case's (pr_adv()).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PRINT_RIG_H
#define PRINT_RIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "label_layout.h"
#include "lzlit.h"
#include "print_ctl.h"
#include "supvan.h"
#include "sv_sim.h"

/** @brief The printer's handles (H0). @{ */
#define PR_SVC_START 0x000Cu
#define PR_SVC_END   0x0014u
#define PR_FFE1_VAL  0x000Eu
#define PR_FFE1_CCCD 0x000Fu
#define PR_FFE9_VAL  0x0011u
#define PR_FFEA_VAL  0x0013u
/** @} */

/** @brief The printer's address, public. */
#define PR_ADDR "A4:93:40:30:FA:71 P"

/** @brief Events the rig holds at once. */
#define PR_EV_MAX 32u

/** @brief An event due at a time. */
typedef struct {
    bleu_evt_t e;  /**< the event */
    uint32_t   at; /**< when it is due */
} pr_ev_t;

/** @brief The rig. */
typedef struct {
    print_ctl_t c;        /**< the controller under test */
    sv_sim_t    sim;      /**< the printer */
    uint32_t    now;      /**< the clock, ms */
    bool        present;  /**< connects are answered */
    bool        no_svc;   /**< the device lacks the vendor service */
    bool        no_mem;   /**< the heap block is refused */
    uint16_t    mtu;      /**< the MTU the exchange answers */
    bool        connected; /**< a link is up */
    uint16_t    conn;     /**< its handle */
    uint16_t    tok_conn; /**< the last connect's token */
    pr_ev_t     ev[PR_EV_MAX]; /**< events due */
    unsigned    nev;      /**< their count */
    unsigned    connects; /**< CONNECT actions */
    unsigned    cancels;  /**< CANCEL actions */
    unsigned    discs;    /**< DISCONNECT actions */
    unsigned    dsc_disc; /**< DISC_DSCS actions */
    unsigned    reads;    /**< READ actions */
    unsigned    subs;     /**< SUBSCRIBE actions */
    uint16_t    sub_h;    /**< the last SUBSCRIBE's handle */
    unsigned    cmds;     /**< command writes */
    unsigned    frags;    /**< data writes */
    unsigned    bad_ext;  /**< long writes the controller had no bytes for */
    unsigned    allocs;   /**< blocks given */
    unsigned    frees;    /**< blocks handed back */
    size_t      last_want; /**< the last block's size */
    unsigned    scans;    /**< SCAN actions */
    unsigned    scan_stops; /**< SCAN_STOP actions */
    uint16_t    tok_scan; /**< the last scan's token */
} pr_rig_t;

/**
 * @brief Queue an event.
 * @param r    the rig
 * @param kind bleu_ev_kind_t
 * @param tok  its token
 * @param at   when it is due
 * @return the event to fill, NULL when the queue is full
 */
static inline bleu_evt_t *pr_ev(pr_rig_t *r, uint8_t kind, uint16_t tok,
                                uint32_t at)
{
    if (r->nev >= PR_EV_MAX) {
        return NULL;
    }
    pr_ev_t *p = &r->ev[r->nev++];
    memset(p, 0, sizeof(*p));
    p->e.kind = kind;
    p->e.token = tok;
    p->e.conn = r->conn;
    p->at = at;
    return &p->e;
}

/**
 * @brief A characteristic of the table, its UUID 16 bits in the 128 of the
 *        Bluetooth base, as the printer declares it.
 * @param r     the rig
 * @param tok   the discovery's token
 * @param val   its value handle
 * @param u16   its 16-bit UUID
 * @param props its properties
 */
static inline void pr_chr(pr_rig_t *r, uint16_t tok, uint16_t val,
                          uint16_t u16, uint8_t props)
{
    static const uint8_t base[16] = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
                                      0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00 };
    bleu_evt_t *e = pr_ev(r, BLEU_EV_CHR, tok, r->now);
    if (e != NULL) {
        memcpy(e->data, base, 16);
        e->data[12] = (uint8_t)u16;
        e->data[13] = (uint8_t)(u16 >> 8);
        e->len = 16u;
        e->h1 = (uint16_t)(val - 1u);
        e->h2 = val;
        e->props = props;
    }
}

/**
 * @brief Answer one action as the host stack and the printer would.
 * @param r the rig
 * @param a the action
 */
static inline void pr_act(pr_rig_t *r, const bleu_act_t *a)
{
    bleu_evt_t *e = NULL;
    switch (a->kind) {
    case BLEU_ACT_CONNECT:
        ++r->connects;
        r->tok_conn = a->token;
        if (r->present) {
            e = pr_ev(r, BLEU_EV_CONNECT, a->token, r->now + 20u);
            if (e != NULL) {
                e->conn = (uint16_t)(r->conn + 1u);
                e->h1 = 32u;
                e->h3 = 400u;
            }
        }
        break;
    case BLEU_ACT_CANCEL:
        ++r->cancels;
        for (unsigned i = 0; i < r->nev; ++i) {
            if (r->ev[i].e.kind == BLEU_EV_CONNECT) {
                r->ev[i] = r->ev[--r->nev];
                break;
            }
        }
        e = pr_ev(r, BLEU_EV_CONNECT, r->tok_conn, r->now + 2u);
        if (e != NULL) {
            e->status = 9u;
        }
        break;
    case BLEU_ACT_DISCONNECT:
        ++r->discs;
        if (r->connected) {
            r->connected = false;
            e = pr_ev(r, BLEU_EV_DISCONNECT, r->tok_conn, r->now + 5u);
            if (e != NULL) {
                e->status = 0x16u;
            }
        }
        break;
    case BLEU_ACT_DISC_SVC:
        if (!r->no_svc) {
            e = pr_ev(r, BLEU_EV_SVC, a->token, r->now);
            if (e != NULL) {
                e->h1 = PR_SVC_START;
                e->h2 = PR_SVC_END;
            }
        }
        (void)pr_ev(r, BLEU_EV_SVC_DONE, a->token, r->now);
        break;
    case BLEU_ACT_DISC_CHRS:
        pr_chr(r, a->token, PR_FFE1_VAL, 0xFFE1u, 0x1Cu);
        pr_chr(r, a->token, PR_FFE9_VAL, 0xFFE9u, 0x0Cu);
        pr_chr(r, a->token, PR_FFEA_VAL, 0xFFEAu, 0x10u);
        (void)pr_ev(r, BLEU_EV_CHR_DONE, a->token, r->now);
        break;
    case BLEU_ACT_DISC_DSCS:
        /* Find Information: "unlikely error", as the printer answers. */
        ++r->dsc_disc;
        e = pr_ev(r, BLEU_EV_DSC_DONE, a->token, r->now);
        if (e != NULL) {
            e->status = 0x10Eu;
        }
        break;
    case BLEU_ACT_MTU:
        e = pr_ev(r, BLEU_EV_MTU, a->token, r->now);
        if (e != NULL) {
            e->h1 = r->mtu;
        }
        break;
    case BLEU_ACT_SUBSCRIBE:
        ++r->subs;
        r->sub_h = a->h1;
        (void)pr_ev(r, BLEU_EV_WRITE_DONE, a->token, r->now + 1u);
        break;
    case BLEU_ACT_READ:
        /* The CCCD reads 00 00 whatever was written. */
        ++r->reads;
        e = pr_ev(r, BLEU_EV_READ, a->token, r->now + 1u);
        if (e != NULL) {
            e->len = 2u;
        }
        break;
    case BLEU_ACT_WRITE:
    case BLEU_ACT_WRITE_EXT: {
        const uint8_t *d = a->data;
        uint16_t n = a->len;
        if (a->kind == BLEU_ACT_WRITE_EXT) {
            d = print_ctl_ext(&r->c, a);
            n = a->h2;
            if (d == NULL) {
                ++r->bad_ext;
                break;
            }
            ++r->frags;
        } else {
            ++r->cmds;
        }
        const sim_write_t w = sim_write(&r->sim, d, n, r->now);
        if (w != SIM_W_LOST) {
            e = pr_ev(r, BLEU_EV_WRITE_DONE, a->token, r->now + 1u);
            if (e != NULL && w == SIM_W_REFUSE) {
                e->status = 0x10Eu;
            }
        }
        break;
    }
    case BLEU_ACT_SCAN:
        ++r->scans;
        r->tok_scan = a->token;
        break;
    case BLEU_ACT_SCAN_STOP:
        ++r->scan_stops;
        break;
    default:
        break;
    }
}

/**
 * @brief Deliver the events due, in queue order.
 * @param r the rig
 */
static inline void pr_deliver(pr_rig_t *r)
{
    for (unsigned i = 0; i < r->nev;) {
        if ((uint32_t)(r->now - r->ev[i].at) >= 0x80000000u) {
            ++i;
            continue;
        }
        const bleu_evt_t e = r->ev[i].e;
        memmove(&r->ev[i], &r->ev[i + 1u],
                (r->nev - i - 1u) * sizeof(r->ev[0]));
        --r->nev;
        if (e.kind == BLEU_EV_CONNECT && e.status == 0u) {
            r->connected = true;
            r->conn = e.conn;
        }
        print_ctl_event(&r->c, &e, r->now);
        i = 0;
    }
}

/**
 * @brief One ms: the poll, the block, the events, the printer's
 *        notifications, every action.
 * @param r the rig
 */
static inline void pr_ms(pr_rig_t *r)
{
    print_ctl_poll(&r->c, r->now);
    const size_t want = print_ctl_block_want(&r->c);
    if (want != 0u) {
        r->last_want = want;
        void *p = r->no_mem ? NULL : malloc(want);
        if (p != NULL) {
            ++r->allocs;
        }
        print_ctl_block_give(&r->c, p, r->now);
    }
    void *back = print_ctl_block_back(&r->c);
    if (back != NULL) {
        ++r->frees;
        free(back);
    }
    pr_deliver(r);
    if (r->connected) {
        const uint8_t *b = NULL;
        unsigned k;
        while ((k = sim_notify(&r->sim, r->now, &b)) != 0u) {
            bleu_evt_t e;
            memset(&e, 0, sizeof(e));
            e.kind = BLEU_EV_NOTIFY;
            e.conn = r->conn;
            e.h1 = PR_FFE1_VAL;
            memcpy(e.data, b, k);
            e.len = (uint8_t)k;
            print_ctl_event(&r->c, &e, r->now);
        }
    }
    bleu_act_t a;
    for (int i = 0; i < 64 && print_ctl_step(&r->c, r->now, &a); ++i) {
        pr_act(r, &a);
    }
    r->now++;
}

/**
 * @brief Run until the request ends or @p ms pass.
 * @param r  the rig
 * @param ms the time
 * @return true when it ended
 */
static inline bool pr_run(pr_rig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms && print_ctl_busy(&r->c); ++k) {
        pr_ms(r);
    }
    return !print_ctl_busy(&r->c);
}

/**
 * @brief Run @p ms whatever happens.
 * @param r  the rig
 * @param ms the time
 */
static inline void pr_wait(pr_rig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        pr_ms(r);
    }
}

/**
 * @brief Run until the phase is @p phase, at most @p ms.
 * @param r     the rig
 * @param phase print_phase_t
 * @param ms    the time
 * @return true when it is
 */
static inline bool pr_run_to(pr_rig_t *r, uint8_t phase, uint32_t ms)
{
    for (uint32_t k = 0; k < ms && r->c.phase != phase; ++k) {
        pr_ms(r);
    }
    return r->c.phase == phase;
}

/**
 * @brief The link drops on the printer's side: no answer owed, the
 *        printer's pending answer and half frame lost.
 * @param r       the rig
 * @param present the printer answers the reconnects
 */
static inline void pr_drop(pr_rig_t *r, bool present)
{
    r->present = present;
    r->connected = false;
    for (unsigned i = 0; i < r->nev;) {
        if (r->ev[i].e.kind == BLEU_EV_WRITE_DONE
            || r->ev[i].e.kind == BLEU_EV_NOTIFY) {
            r->ev[i] = r->ev[--r->nev];
        } else {
            ++i;
        }
    }
    r->sim.nout = 0u;
    r->sim.sent = 0u;
    r->sim.nframe = 0u;
    bleu_evt_t e;
    memset(&e, 0, sizeof(e));
    e.kind = BLEU_EV_DISCONNECT;
    e.conn = r->conn;
    e.status = 0x08u;
    print_ctl_event(&r->c, &e, r->now);
}

/** @brief The bitmap the rig's own layout draws. */
static uint8_t s_pr_bits[LABEL_BMP_MAX];
/** @brief The layout's result. */
static label_info_t s_pr_info;
/** @brief The encoder. */
static lzlit_t s_pr_z;

/**
 * @brief The stream the controller must send for a label: the layout for
 *        @p roll, its print buffers, encoded.
 * @param roll the roll
 * @param id   the id, or NULL for the test label of @p ver
 * @param dut  DUT NAME
 * @param ver  the version of the test label
 * @param out  receives the stream
 * @param cap  its room
 * @param bmp  receives the bitmap; its bits are s_pr_bits
 * @return the stream's length, 0 on a refusal
 */
static inline size_t pr_expect(const label_roll_t *roll, const char *id,
                               const char *dut, const char *ver, uint8_t *out,
                               size_t cap, label_bmp_t *bmp)
{
    bmp->bits = s_pr_bits;
    bmp->cap = sizeof s_pr_bits;
    label_err_t e;
    if (id == NULL) {
        e = label_layout_test(roll, ver, bmp, &s_pr_info);
    } else {
        const label_text_t t = { .id = id, .dut = dut };
        e = label_layout(roll, &t, bmp, &s_pr_info);
    }
    if (e != LABEL_OK) {
        return 0u;
    }
    const uint16_t bufs = sv_buf_count(bmp->cols);
    static uint8_t buf[SV_BUF_SIZE];
    lzlit_begin(&s_pr_z, out, cap, (uint32_t)bufs * SV_BUF_SIZE);
    for (uint16_t i = 0; i < bufs; ++i) {
        (void)sv_buf_fill(buf, bmp->bits, bmp->cols, i);
        lzlit_put(&s_pr_z, buf, SV_BUF_SIZE);
    }
    return lzlit_end(&s_pr_z);
}

/**
 * @brief A fresh rig: the printer bound, present, MTU 240, Bluetooth
 *        synced, the bench SAFE, no update.
 * @param r the rig
 */
static inline void pr_init(pr_rig_t *r)
{
    memset(r, 0, sizeof(*r));
    print_ctl_init(&r->c, "0.23.17");
    sim_init(&r->sim);
    r->present = true;
    r->mtu = 240u;
    r->conn = 4u;
    r->now = 1000u;
    print_ctl_bind(&r->c, PR_ADDR, "T0205C2608247368", r->now);
    print_ctl_radio(&r->c, IMU_RADIO_READY, r->now);
    print_ctl_gate(&r->c, false, false, r->now);
}

/**
 * @brief Ask for a request.
 * @param r    the rig
 * @param kind print_req_kind_t
 * @param id   the id, NULL none
 * @param dut  DUT NAME, NULL none
 * @return print_ctl_request()'s answer
 */
static inline bool pr_ask(pr_rig_t *r, uint8_t kind, const char *id,
                          const char *dut)
{
    print_req_t q;
    memset(&q, 0, sizeof(q));
    q.kind = kind;
    if (id != NULL) {
        strncpy(q.id, id, sizeof(q.id) - 1u);
    }
    if (dut != NULL) {
        strncpy(q.dut, dut, sizeof(q.dut) - 1u);
    }
    return print_ctl_request(&r->c, &q, r->now);
}

/**
 * @brief One advertising report, due now: H0's flags, name and
 *        manufacturer data, no service UUID.
 * @param r    the rig
 * @param addr the advertiser, `A4:93:40:30:FA:71 P`
 * @param name its name, 16 characters
 * @param rssi its RSSI, dBm
 */
static inline void pr_adv(pr_rig_t *r, const char *addr, const char *name,
                          int8_t rssi)
{
    bleu_evt_t *e = pr_ev(r, BLEU_EV_ADV, 0u, r->now);
    if (e == NULL) {
        return;
    }
    (void)bleu_addr_parse(addr, &e->addr);
    e->rssi = rssi;
    const size_t k = strlen(name);
    uint8_t *d = e->data;
    d[0] = 0x02u;
    d[1] = 0x01u;
    d[2] = 0x02u;
    d[3] = (uint8_t)(k + 1u);
    d[4] = 0x09u;
    memcpy(&d[5], name, k);
    e->len = (uint8_t)(5u + k);
}

/**
 * @brief One advertising report, due now, that carries the Supvan
 *        service's 128-bit UUID and a name.
 * @param r    the rig
 * @param addr the advertiser
 * @param rssi its RSSI, dBm
 */
static inline void pr_adv_svc(pr_rig_t *r, const char *addr, int8_t rssi)
{
    static const uint8_t svc[16] = { 0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14,
                                     0x48, 0x8E, 0x93, 0xD2, 0x17, 0x3C,
                                     0xFF, 0xE0, 0x00, 0x00 };
    bleu_evt_t *e = pr_ev(r, BLEU_EV_ADV, 0u, r->now);
    if (e == NULL) {
        return;
    }
    (void)bleu_addr_parse(addr, &e->addr);
    e->rssi = rssi;
    uint8_t *d = e->data;
    d[0] = 17u;
    d[1] = 0x07u;
    memcpy(&d[2], svc, sizeof(svc));
    d[18] = 5u;
    d[19] = 0x09u;
    memcpy(&d[20], "FAKE", 4u);
    e->len = 24u;
}

#endif /* PRINT_RIG_H */
