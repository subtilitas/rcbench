/*
 * The glass and the node peer of the bind suites of SETUP > WIRELESS.
 *
 * The glass: a finger DOWN and UP in panel coordinates, the middle of an
 * entry's custom row, an open popup's choice as the focus layer lists it,
 * the band's alert and a custom row's value.  The node peer: a power or a
 * balance node that answers each action of its controller at once with
 * the events NimBLE's callbacks would give (STREAM at 0x0022 with its CCCD
 * at 0x0023, CONTROL at 0x0025, INFO at 0x0027, STATUS at 0x0029), and
 * advertises its role and its name 40ms after a scan starts.
 *
 * Only static inline helpers.
 *
 * Used by:
 * - test_dial_bind.c
 * - test_pnode_bind.c
 * - test_bal_bind.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "settings_rig.h"

#include "bleuart.h"
#include "rcnode.h"
#include "ui_focus.h"
#include "ui_setlist.h"

/* ------------------------------------------------------------- the glass */

/** @brief A finger down at (@p x, @p y), panel coordinates. */
static inline void wbr_down(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y } };
    ui_router_event(&e);
}

/** @brief The finger up at (@p x, @p y). */
static inline void wbr_up(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_UP,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y } };
    ui_router_event(&e);
}

/** @brief Entry @p entry's row of the custom row @p c, -1 none: the pane's
 *         rows are the entry's, in order. */
static inline int wbr_row(int entry, const ui_setlist_custom_t *c)
{
    wl_row_t rows[WL_ENTRY_ROWS_MAX];
    const int n = wireless_pane_rows(entry, rows, WL_ENTRY_ROWS_MAX);
    for (int i = 0; i < n && i < WL_ENTRY_ROWS_MAX; ++i) {
        if (rows[i].custom == c) {
            return i;
        }
    }
    return -1;
}

/** @brief The middle of the open list's row @p row, a third in from its
 *         left, panel coordinates. */
static inline void wbr_row_at(int row, int *x, int *y)
{
    const gfx_rect_t r = settings_screen_row_rect(row);
    *x = r.x + r.w / 3;
    *y = UI_BAND_H + r.y + r.h / 2;
}

/** @brief The middle of entry @p entry's row of @p c, panel coordinates. */
static inline void wbr_row_mid(int entry, const ui_setlist_custom_t *c,
                               int *x, int *y)
{
    const int row = wbr_row(entry, c);
    CHECK(row >= 0);
    wbr_row_at(row, x, y);
}

/** @brief The open popup's choice @p k, panel coordinates, as the focus
 *         layer lists it (ui_setlist_choice_rect()); false with no popup.
 *         A list of rows names its setlist, a popup's choices do not. */
static inline bool wbr_choice(int k, gfx_rect_t *out)
{
    ui_focus_list_t f;
    memset(&f, 0, sizeof f);
    *out = gfx_rect_make(0, 0, 0, 0);
    if (!settings_screen_focus(&f) || f.sl != NULL || k < 0
        || k >= f.count) {
        return false;
    }
    *out = f.item[k].rect;
    out->y = (int16_t)(out->y + UI_BAND_H);
    return true;
}

/** @brief The band's alert, "" with none. */
static inline const char *wbr_band(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief A custom row's value text, valid until the next call. */
static inline const char *wbr_value(const ui_setlist_custom_t *c)
{
    static char buf[64];
    c->value_text(buf, sizeof buf);
    return buf;
}

/* ------------------------------------------------------- the node peer */

/** @brief The handles the node serves. @{ */
#define WBR_CONN        4u
#define WBR_SVC_START   0x0020u
#define WBR_SVC_END     0x002Bu
#define WBR_STREAM_VAL  0x0022u
#define WBR_STREAM_CCCD 0x0023u
#define WBR_CONTROL_VAL 0x0025u
#define WBR_INFO_VAL    0x0027u
#define WBR_STATUS_VAL  0x0029u
/** @} */
/** @brief A scan's start to the node's advertising, ms. */
#define WBR_ADV_MS 40u

/** @brief Where the node's events go: its controller's event call. */
typedef void (*wbr_sink_t)(const bleu_evt_t *e);

/** @brief The node. */
typedef struct {
    wbr_sink_t    sink;       /**< its controller */
    rcnode_info_t info;       /**< what INFO reads */
    uint8_t       addr[6];    /**< its address, least significant first */
    uint16_t      tok_conn;   /**< the last connect's token */
    unsigned      connects;   /**< CONNECT actions */
    unsigned      scans;      /**< SCAN actions */
    uint32_t      adv_at;     /**< when its advertising is heard */
    bool          adv_due;    /**< a scan started, its advertising not
                                   heard yet */
} wbr_peer_t;

/** @brief A node of @p role at 7C:DF:A1:12:34:01, public, whose INFO
 *         names RCNODE_PROTO, the role and the MATEK's INA228 set-up. */
static inline void wbr_peer_init(wbr_peer_t *p, uint8_t role,
                                 wbr_sink_t sink)
{
    static const uint8_t a[6] = { 0x01, 0x34, 0x12, 0xA1, 0xDF, 0x7C };
    memset(p, 0, sizeof *p);
    p->sink = sink;
    memcpy(p->addr, a, sizeof a);
    p->info.hdr.type = RCNODE_T_INFO;
    p->info.proto = RCNODE_PROTO;
    p->info.role = role;
    p->info.board = RCNODE_BOARD_C6_ZERO;
    p->info.i228_addr = 0x45u;
    p->info.adcrange = 1u;
    p->info.shunt_uohm = 200u;
}

/** @brief An event with its kind and token on the node's connection. */
static inline bleu_evt_t wbr_ev(uint8_t kind, uint16_t token)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.token = token;
    e.conn = WBR_CONN;
    return e;
}

/** @brief A characteristic of the service: its rcnode ID and handle. */
static inline void wbr_chr(const wbr_peer_t *p, uint16_t token, uint16_t id,
                           uint16_t val, uint8_t props)
{
    bleu_evt_t e = wbr_ev(BLEU_EV_CHR, token);
    rcnode_uuid(id, e.data);
    e.len = 16u;
    e.h1 = (uint16_t)(val - 1u);
    e.h2 = val;
    e.props = props;
    p->sink(&e);
}

/** @brief Answer one action of the controller as the host stack and the
 *         node would, at @p now. */
static inline void wbr_peer_act(wbr_peer_t *p, const bleu_act_t *a,
                                uint32_t now)
{
    bleu_evt_t e;
    switch (a->kind) {
    case BLEU_ACT_SCAN:
        ++p->scans;
        p->adv_due = true;
        p->adv_at = now + WBR_ADV_MS;
        break;
    case BLEU_ACT_CONNECT:
        ++p->connects;
        p->tok_conn = a->token;
        e = wbr_ev(BLEU_EV_CONNECT, a->token);
        e.h1 = 40u;
        e.h3 = 400u;
        p->sink(&e);
        break;
    case BLEU_ACT_DISC_SVC:
        e = wbr_ev(BLEU_EV_SVC, a->token);
        e.h1 = WBR_SVC_START;
        e.h2 = WBR_SVC_END;
        p->sink(&e);
        e = wbr_ev(BLEU_EV_SVC_DONE, a->token);
        p->sink(&e);
        break;
    case BLEU_ACT_DISC_CHRS:
        wbr_chr(p, a->token, RCNODE_ID_STREAM, WBR_STREAM_VAL,
                BLEU_PROP_NOTIFY);
        wbr_chr(p, a->token, RCNODE_ID_CONTROL, WBR_CONTROL_VAL,
                BLEU_PROP_WRITE | BLEU_PROP_WRITE_NR);
        wbr_chr(p, a->token, RCNODE_ID_INFO, WBR_INFO_VAL, BLEU_PROP_READ);
        wbr_chr(p, a->token, RCNODE_ID_STATUS, WBR_STATUS_VAL,
                BLEU_PROP_READ | BLEU_PROP_NOTIFY);
        e = wbr_ev(BLEU_EV_CHR_DONE, a->token);
        p->sink(&e);
        break;
    case BLEU_ACT_DISC_DSCS:
        e = wbr_ev(BLEU_EV_DSC, a->token);
        e.h1 = WBR_STREAM_CCCD;
        e.data[0] = 0x02u;
        e.data[1] = 0x29u;
        e.len = 2u;
        p->sink(&e);
        e = wbr_ev(BLEU_EV_DSC_DONE, a->token);
        p->sink(&e);
        break;
    case BLEU_ACT_MTU:
        e = wbr_ev(BLEU_EV_MTU, a->token);
        e.h1 = 247u;
        p->sink(&e);
        break;
    case BLEU_ACT_SUBSCRIBE:
    case BLEU_ACT_WRITE:
    case BLEU_ACT_WRITE_NR:
        e = wbr_ev(BLEU_EV_WRITE_DONE, a->token);
        p->sink(&e);
        break;
    case BLEU_ACT_READ:
        e = wbr_ev(BLEU_EV_READ, a->token);
        e.len = (uint8_t)rcnode_enc_info(&p->info, e.data, sizeof e.data);
        p->sink(&e);
        break;
    case BLEU_ACT_DISCONNECT:
        e = wbr_ev(BLEU_EV_DISCONNECT, p->tok_conn);
        e.status = 0x216u;
        p->sink(&e);
        break;
    default:
        break;
    }
}

/** @brief The node's advertising and scan response, once WBR_ADV_MS after
 *         a scan started; call every poll. */
static inline void wbr_peer_adv(wbr_peer_t *p, uint32_t now)
{
    if (!p->adv_due || (int32_t)(now - p->adv_at) < 0) {
        return;
    }
    p->adv_due = false;
    bleu_evt_t e = wbr_ev(BLEU_EV_ADV, 0u);
    e.len = (uint8_t)rcnode_adv_build(p->info.role, p->addr, e.data,
                                      sizeof e.data);
    memcpy(e.addr.val, p->addr, 6);
    e.addr.type = BLEU_ADDR_PUBLIC;
    e.rssi = -52;
    p->sink(&e);
    e.len = (uint8_t)rcnode_rsp_build(p->info.role, p->addr, e.data,
                                      sizeof e.data);
    p->sink(&e);
}
