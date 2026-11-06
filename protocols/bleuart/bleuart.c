/**
 * @file
 * @brief The BLE central's machine: scan, connect, discovery, subscribe,
 *        the write slot and the retries, run by bleu_event() and
 *        bleu_step().
 *
 * Every action that the host stack answers carries a token, and only the
 * event with the token of the step under way moves the machine: a late
 * answer after a timeout, an abort or a drop is counted in bleu_t.stray
 * and changes nothing.  A connect that succeeds after it was given up is
 * disconnected.  A cancelled connect and a disconnect the central asked
 * for settle before the next connect or scan starts: the host stack runs
 * one GAP (generic access profile) procedure at a time.  The settle ends
 * on its event or after BLEU_SETTLE_MS.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "bleuart.h"

_Static_assert(sizeof(bleu_evt_t) <= 64u, "an event is one 64-byte element");
_Static_assert(BLEU_ITVL_MIN >= 6u && BLEU_ITVL_MAX <= 3200u
               && BLEU_ITVL_MIN <= BLEU_ITVL_MAX,
               "the interval is 7.5ms to 4s");
_Static_assert(BLEU_SUPERVISION >= 10u && BLEU_SUPERVISION <= 3200u,
               "the supervision timeout is 100ms to 32s");
_Static_assert(BLEU_SUPERVISION * 10u * 4u
               > (1u + BLEU_LATENCY) * BLEU_ITVL_MAX * 5u * 2u,
               "the supervision timeout exceeds twice the latency's span");
_Static_assert(BLEU_CONN_SCAN_ITVL >= 4u && BLEU_CONN_SCAN_ITVL <= 0x4000u
               && BLEU_CONN_SCAN_WINDOW >= 4u
               && BLEU_CONN_SCAN_WINDOW <= BLEU_CONN_SCAN_ITVL,
               "the connect's scan window is 2.5ms to its interval");
_Static_assert(BLEU_MTU >= 23u && BLEU_MTU <= 517u, "ATT MTU is 23 to 517");
_Static_assert(BLEU_SCAN_MS <= 0xFFFFu && BLEU_CONNECT_MS <= 0xFFFFu,
               "bleu_act_t.ms holds the scan and connect durations");
_Static_assert(BLEU_WRITE_MAX + 3u <= 23u,
               "a command write fits the smallest MTU");

/** @brief bleu_t.settle. @{ */
#define SETTLE_NONE   0u /**< nothing to settle */
#define SETTLE_CANCEL 1u /**< a cancelled connect's event is due */
#define SETTLE_CLOSE  2u /**< the disconnect of close_conn is due */
/** @} */

/**
 * @brief Hand out the next token; 0 is never one.
 * @param b The machine.
 * @return The token.
 */
static uint16_t next_token(bleu_t *b)
{
    ++b->token;
    if (b->token == 0u) {
        b->token = 1u;
    }
    return b->token;
}

/**
 * @brief Queue an action of @p kind for bleu_step().
 * @param b    The machine.
 * @param kind A bleu_act_kind_t.
 * @return The queued action, filled with the kind and the link's
 *         connection handle, or NULL when the queue is full (counted).
 */
static bleu_act_t *push(bleu_t *b, uint8_t kind)
{
    if (b->q_n >= BLEU_ACT_QUEUE) {
        ++b->act_drops;
        return NULL;
    }
    bleu_act_t *a = &b->q[(b->q_head + b->q_n) % BLEU_ACT_QUEUE];
    ++b->q_n;
    memset(a, 0, sizeof *a);
    a->kind = kind;
    a->conn = b->conn;
    return a;
}

/**
 * @brief Queue a link step that the host stack answers, and time it.
 * @param b      The machine.
 * @param st     The state the step stands for.
 * @param kind   The action.
 * @param h1     Its first handle.
 * @param h2     Its second handle.
 * @param now_ms The clock.
 * @return The queued action, or NULL when the queue is full.
 */
static bleu_act_t *step_to(bleu_t *b, bleu_state_t st, uint8_t kind,
                           uint16_t h1, uint16_t h2, uint32_t now_ms)
{
    b->st = st;
    b->t_link = now_ms;
    b->tok_link = next_token(b);
    bleu_act_t *a = push(b, kind);
    if (a != NULL) {
        a->token = b->tok_link;
        a->h1 = h1;
        a->h2 = h2;
    }
    return a;
}

/**
 * @brief The state with no link: WAIT for the next attempt after @p gap
 *        ms when on and bound, IDLE when on and unbound, OFF when off.
 * @param b      The machine.
 * @param on     The central is on.
 * @param gap    The retry gap.
 * @param now_ms The clock.
 */
static void unlinked(bleu_t *b, bool on, uint32_t gap, uint32_t now_ms)
{
    b->tok_link = 0;
    b->conn = 0;
    b->param_due = false;
    b->rd_busy = false;
    if (b->wr == BLEU_WR_BUSY) {
        b->wr = BLEU_WR_FAILED;
        b->wr_status = BLEU_STATUS_LOST;
    }
    b->tok_wr = 0;
    b->st = !on ? BLEU_ST_OFF : b->bound ? BLEU_ST_WAIT : BLEU_ST_IDLE;
    b->t_gap = now_ms;
    b->gap_ms = gap;
}

/**
 * @brief Whether the link holds a connection.
 * @param st The state.
 * @return true from DISC_SVC to UP.
 */
static bool connected(bleu_state_t st)
{
    return st >= BLEU_ST_DISC_SVC && st <= BLEU_ST_UP;
}

/**
 * @brief End the link as it stands: cancel a connect, disconnect a
 *        connection, and let either settle.
 * @param b      The machine.
 * @param now_ms The clock.
 */
static void tear_down(bleu_t *b, uint32_t now_ms)
{
    if (b->st == BLEU_ST_CONNECTING) {
        (void)push(b, BLEU_ACT_CANCEL);
        b->settle = SETTLE_CANCEL;
        b->tok_cancel = b->tok_link;
        b->t_settle = now_ms;
    } else if (connected(b->st)) {
        (void)push(b, BLEU_ACT_DISCONNECT);
        b->settle = SETTLE_CLOSE;
        b->close_conn = b->conn;
        b->t_settle = now_ms;
    }
}

/**
 * @brief End the link on a failure: tear it down, keep @p err, and retry
 *        after BLEU_RETRY_MS, or never for BLEU_ERR_NOT_WIT.
 * @param b      The machine.
 * @param err    Why.
 * @param status The status that ended it, 0 for a timeout.
 * @param now_ms The clock.
 */
static void fail(bleu_t *b, bleu_err_t err, uint16_t status, uint32_t now_ms)
{
    tear_down(b, now_ms);
    b->err = err;
    b->err_status = status;
    unlinked(b, true, BLEU_RETRY_MS, now_ms);
    if (err == BLEU_ERR_NOT_WIT) {
        b->st = BLEU_ST_NOT_WIT;
    }
}

bool bleu_evt_admit(uint8_t kind, uint32_t free)
{
    if (kind == BLEU_EV_NOTIFY || kind == BLEU_EV_ADV) {
        return free > BLEU_EVQ_RESERVE;
    }
    return free > 0u;
}

void bleu_init(bleu_t *b)
{
    if (b != NULL) {
        memset(b, 0, sizeof *b);
    }
}

void bleu_scan_stop(bleu_t *b, uint32_t now_ms)
{
    (void)now_ms;
    if (b != NULL && b->scanning) {
        if (b->tok_scan != 0u) {
            (void)push(b, BLEU_ACT_SCAN_STOP);
        }
        b->scanning = false;
        b->tok_scan = 0;
    }
}

void bleu_enable(bleu_t *b, bool on, uint32_t now_ms)
{
    if (b == NULL || on == (b->st != BLEU_ST_OFF)) {
        return;
    }
    if (!on) {
        bleu_scan_stop(b, now_ms);
        tear_down(b, now_ms);
    }
    b->err = BLEU_ERR_NONE;
    unlinked(b, on, 0, now_ms);
}

void bleu_bind(bleu_t *b, const bleu_addr_t *addr, uint32_t now_ms)
{
    if (b == NULL) {
        return;
    }
    const bool same = addr != NULL && b->bound && bleu_addr_eq(addr, &b->addr);
    if (same && b->st >= BLEU_ST_CONNECTING && b->st <= BLEU_ST_UP) {
        return;
    }
    tear_down(b, now_ms);
    b->bound = addr != NULL;
    if (addr != NULL) {
        b->addr = *addr;
    }
    b->err = BLEU_ERR_NONE;
    unlinked(b, b->st != BLEU_ST_OFF, 0, now_ms);
}

bool bleu_scan(bleu_t *b, uint32_t now_ms)
{
    if (b == NULL || b->st == BLEU_ST_OFF || b->scanning) {
        return false;
    }
    if (b->st == BLEU_ST_CONNECTING) {
        tear_down(b, now_ms);
        unlinked(b, true, 0, now_ms);
    }
    bleu_list_clear(&b->list);
    b->scanning = true;
    b->scan_status = 0;
    b->t_scan = now_ms;
    b->tok_scan = 0;
    return true;
}

bool bleu_link_up(const bleu_t *b)
{
    return b != NULL && b->st == BLEU_ST_UP;
}

bool bleu_can_send(const bleu_t *b)
{
    return bleu_link_up(b) && b->wr != BLEU_WR_BUSY && !b->rd_busy
           && !b->param_due;
}

bool bleu_send(bleu_t *b, const uint8_t *data, size_t len, uint32_t now_ms)
{
    if (!bleu_can_send(b) || data == NULL || len == 0u
        || len > BLEU_WRITE_MAX) {
        return false;
    }
    const bool rsp = (b->rx_props & BLEU_PROP_WRITE) != 0u;
    bleu_act_t *a = push(b, rsp ? BLEU_ACT_WRITE : BLEU_ACT_WRITE_NR);
    if (a == NULL) {
        return false;
    }
    b->tok_wr = next_token(b);
    a->token = b->tok_wr;
    a->h1 = b->rx_val;
    a->len = (uint8_t)len;
    memcpy(a->data, data, len);
    b->wr = BLEU_WR_BUSY;
    b->wr_status = 0;
    b->t_wr = now_ms;
    return true;
}

/**
 * @brief Whether @p e's data is the 16-bit UUID @p u16.
 * @param e   The event.
 * @param u16 The UUID.
 * @return true for a 2-byte UUID equal to it.
 */
static bool uuid16_is(const bleu_evt_t *e, uint16_t u16)
{
    return e->len == 2u && (uint16_t)(e->data[0] | e->data[1] << 8) == u16;
}

/**
 * @brief Whether @p e's data is the 128-bit UUID @p u.
 * @param e The event.
 * @param u The UUID, least significant byte first.
 * @return true for a 16-byte UUID equal to it.
 */
static bool uuid128_is(const bleu_evt_t *e, const uint8_t *u)
{
    return e->len == 16u && memcmp(e->data, u, 16u) == 0;
}

/**
 * @brief Take one characteristic of the service.  The declarations come
 *        in ascending handle order (ATT Read By Type), so the first one
 *        after 1E4D ends 1E4D's descriptors.
 * @param b The machine.
 * @param e The CHR event.
 */
static void take_chr(bleu_t *b, const bleu_evt_t *e)
{
    if (b->tx_val != 0u && b->tx_end == b->svc_end && e->h1 > b->tx_val) {
        b->tx_end = (uint16_t)(e->h1 - 1u);
    }
    if (uuid128_is(e, bleu_uuid_tx)) {
        b->tx_val = e->h2;
        b->tx_props = e->props;
        b->tx_end = b->svc_end;
    } else if (uuid128_is(e, bleu_uuid_rx)) {
        b->rx_val = e->h2;
        b->rx_props = e->props;
    } else if (uuid128_is(e, bleu_uuid_param)
               && (e->props & BLEU_PROP_READ) != 0u) {
        b->param_val = e->h2;
    }
}

/**
 * @brief The end of a discovery step: on to @p next, or fail.
 * @param b      The machine.
 * @param e      The DONE event.
 * @param ok     The step found what it needs.
 * @param now_ms The clock.
 * @return true when the machine goes on.
 */
static bool step_done(bleu_t *b, const bleu_evt_t *e, bool ok,
                      uint32_t now_ms)
{
    if (e->status != 0u) {
        fail(b, BLEU_ERR_GATT, e->status, now_ms);
        return false;
    }
    if (!ok) {
        fail(b, BLEU_ERR_NOT_WIT, 0, now_ms);
        return false;
    }
    return true;
}

/**
 * @brief Take an event that answers the link step under way.
 * @param b      The machine.
 * @param e      The event, its token the link's.
 * @param now_ms The clock.
 */
static void link_event(bleu_t *b, const bleu_evt_t *e, uint32_t now_ms)
{
    switch (e->kind) {
    case BLEU_EV_SVC:
        if (b->st == BLEU_ST_DISC_SVC && b->svc_start == 0u) {
            b->svc_start = e->h1;
            b->svc_end = e->h2;
        }
        break;
    case BLEU_EV_SVC_DONE:
        if (b->st == BLEU_ST_DISC_SVC
            && step_done(b, e, b->svc_start != 0u, now_ms)) {
            (void)step_to(b, BLEU_ST_DISC_CHR, BLEU_ACT_DISC_CHRS,
                          b->svc_start, b->svc_end, now_ms);
        }
        break;
    case BLEU_EV_CHR:
        if (b->st == BLEU_ST_DISC_CHR) {
            take_chr(b, e);
        }
        break;
    case BLEU_EV_CHR_DONE:
        if (b->st == BLEU_ST_DISC_CHR
            && step_done(b, e,
                         b->tx_val != 0u
                         && (b->tx_props & BLEU_PROP_NOTIFY) != 0u
                         && b->rx_val != 0u
                         && (b->rx_props & (BLEU_PROP_WRITE
                                            | BLEU_PROP_WRITE_NR)) != 0u,
                         now_ms)) {
            (void)step_to(b, BLEU_ST_DISC_DSC, BLEU_ACT_DISC_DSCS,
                          b->tx_val, b->tx_end, now_ms);
        }
        break;
    case BLEU_EV_DSC:
        if (b->st == BLEU_ST_DISC_DSC && b->cccd == 0u && e->h1 > b->tx_val
            && e->h1 <= b->tx_end && uuid16_is(e, BLEU_UUID16_CCCD)) {
            b->cccd = e->h1;
        }
        break;
    case BLEU_EV_DSC_DONE:
        if (b->st == BLEU_ST_DISC_DSC
            && step_done(b, e, b->cccd != 0u, now_ms)) {
            (void)step_to(b, BLEU_ST_MTU, BLEU_ACT_MTU, 0, 0, now_ms);
        }
        break;
    case BLEU_EV_MTU:
        if (b->st == BLEU_ST_MTU) {
            b->mtu = e->status == 0u ? e->h1 : 0u;
            bleu_act_t *a = step_to(b, BLEU_ST_SUBSCRIBE, BLEU_ACT_SUBSCRIBE,
                                    b->cccd, 0, now_ms);
            if (a != NULL) {
                a->data[0] = 0x01u;
                a->data[1] = 0x00u;
                a->len = 2u;
            }
        }
        break;
    case BLEU_EV_WRITE_DONE:
        if (b->st == BLEU_ST_SUBSCRIBE
            && step_done(b, e, true, now_ms)) {
            b->st = BLEU_ST_UP;
            b->tok_link = 0;
            b->t_setup = now_ms - b->t_conn_at;
            b->err = BLEU_ERR_NONE;
            b->param_due = b->param_val != 0u;
        }
        break;
    case BLEU_EV_READ:
        if (b->rd_busy) {
            b->rd_busy = false;
            b->tok_link = 0;
            b->param_status = e->status;
            b->param_len = 0;
            if (e->status == 0u) {
                const size_t n = e->len < BLEU_PARAM_MAX ? e->len
                                                         : BLEU_PARAM_MAX;
                memcpy(b->param, e->data, n);
                b->param_len = (uint8_t)n;
            }
        }
        break;
    default:
        break;
    }
}

/**
 * @brief Take a CONNECT event.
 * @param b      The machine.
 * @param e      The event.
 * @param now_ms The clock.
 */
static void on_connect(bleu_t *b, const bleu_evt_t *e, uint32_t now_ms)
{
    const bool live = b->st == BLEU_ST_CONNECTING && e->token == b->tok_link
                      && e->token != 0u;
    const bool cancelled = b->settle == SETTLE_CANCEL
                           && e->token == b->tok_cancel;
    if (cancelled) {
        b->settle = SETTLE_NONE;
    }
    if (!live) {
        if (!cancelled) {
            ++b->stray;
        }
        if (e->status == 0u) {
            /* A connect given up on that came through all the same. */
            bleu_act_t *a = push(b, BLEU_ACT_DISCONNECT);
            if (a != NULL) {
                a->conn = e->conn;
            }
            if (b->settle == SETTLE_NONE) {
                b->settle = SETTLE_CLOSE;
                b->close_conn = e->conn;
                b->t_settle = now_ms;
            }
        }
        return;
    }
    if (e->status != 0u) {
        b->err = BLEU_ERR_CONNECT;
        b->err_status = e->status;
        unlinked(b, true, BLEU_RETRY_CONNECT_MS, now_ms);
        return;
    }
    b->t_connect = now_ms - b->t_link;
    b->t_conn_at = now_ms;
    b->t_setup = 0;
    b->conn = e->conn;
    b->itvl = e->h1;
    b->latency = e->h2;
    b->supervision = e->h3;
    b->svc_start = b->svc_end = 0;
    b->tx_val = b->tx_end = b->cccd = b->rx_val = b->param_val = 0;
    b->tx_props = b->rx_props = 0;
    b->mtu = 0;
    b->param_len = 0;
    b->param_status = 0;
    b->wr = BLEU_WR_IDLE;
    (void)step_to(b, BLEU_ST_DISC_SVC, BLEU_ACT_DISC_SVC, 0, 0, now_ms);
}

/**
 * @brief Take a DISCONNECT event.  One with the token of the connect in
 *        flight ends that connect as failed (HCI 0x3E, failed to be
 *        established): retried after BLEU_RETRY_CONNECT_MS.
 * @param b      The machine.
 * @param e      The event.
 * @param now_ms The clock.
 */
static void on_disconnect(bleu_t *b, const bleu_evt_t *e, uint32_t now_ms)
{
    if (b->st == BLEU_ST_CONNECTING && e->token != 0u
        && e->token == b->tok_link) {
        b->reason = e->status;
        b->err = BLEU_ERR_CONNECT;
        b->err_status = e->status;
        unlinked(b, true, BLEU_RETRY_CONNECT_MS, now_ms);
        return;
    }
    if (b->settle == SETTLE_CLOSE && e->conn == b->close_conn) {
        b->settle = SETTLE_NONE;
        b->reason = e->status;
        return;
    }
    if (!connected(b->st) || e->conn != b->conn) {
        ++b->stray;
        return;
    }
    b->reason = e->status;
    b->err = BLEU_ERR_LOST;
    b->err_status = e->status;
    unlinked(b, true, BLEU_RETRY_MS, now_ms);
}

bool bleu_event(bleu_t *b, const bleu_evt_t *e, uint32_t now_ms)
{
    if (b == NULL || e == NULL) {
        return false;
    }
    switch (e->kind) {
    case BLEU_EV_ADV:
        if (b->scanning) {
            bleu_list_report(&b->list, &e->addr, e->rssi, e->data,
                             e->len < BLEU_EVT_DATA ? e->len : BLEU_EVT_DATA);
        } else {
            ++b->stray;
        }
        return false;
    case BLEU_EV_SCAN_END:
        if (b->scanning && e->token == b->tok_scan && e->token != 0u) {
            b->scanning = false;
            b->tok_scan = 0;
            b->scan_status = e->status;
        } else {
            ++b->stray;
        }
        return false;
    case BLEU_EV_CONNECT:
        on_connect(b, e, now_ms);
        return false;
    case BLEU_EV_DISCONNECT:
        on_disconnect(b, e, now_ms);
        return false;
    case BLEU_EV_CONN_UPDATE:
        if (connected(b->st) && e->conn == b->conn) {
            b->itvl = e->h1;
            b->latency = e->h2;
            b->supervision = e->h3;
        } else {
            ++b->stray;
        }
        return false;
    case BLEU_EV_NOTIFY:
        if (connected(b->st) && e->conn == b->conn && b->tx_val != 0u
            && e->h1 == b->tx_val && e->len <= BLEU_EVT_DATA) {
            ++b->notifies;
            return true;
        }
        ++b->stray;
        return false;
    case BLEU_EV_WRITE_DONE:
        if (b->wr == BLEU_WR_BUSY && e->token == b->tok_wr
            && e->token != 0u) {
            b->wr = e->status == 0u ? BLEU_WR_OK : BLEU_WR_FAILED;
            b->wr_status = e->status;
            b->tok_wr = 0;
            return false;
        }
        break;
    default:
        break;
    }
    if (e->token != 0u && e->token == b->tok_link) {
        link_event(b, e, now_ms);
    } else {
        ++b->stray;
    }
    return false;
}

/**
 * @brief Whether @p ms have passed since @p since.
 * @param now_ms The clock.
 * @param since  The start.
 * @param ms     The wait.
 * @return true at and past the wait, across the clock's wrap.
 */
static bool elapsed(uint32_t now_ms, uint32_t since, uint32_t ms)
{
    return (uint32_t)(now_ms - since) >= ms;
}

/**
 * @brief Run the timers and start what is due.
 * @param b      The machine.
 * @param now_ms The clock.
 */
static void run_timers(bleu_t *b, uint32_t now_ms)
{
    if (b->settle != SETTLE_NONE
        && elapsed(now_ms, b->t_settle, BLEU_SETTLE_MS)) {
        b->settle = SETTLE_NONE;
    }
    if (b->scanning && b->tok_scan != 0u
        && elapsed(now_ms, b->t_scan, BLEU_SCAN_MS)) {
        bleu_scan_stop(b, now_ms);
    }
    if (b->wr == BLEU_WR_BUSY && elapsed(now_ms, b->t_wr, BLEU_WRITE_MS)) {
        b->wr = BLEU_WR_FAILED;
        b->wr_status = BLEU_STATUS_TIMEOUT;
        b->tok_wr = 0;
    }
    if (b->st == BLEU_ST_CONNECTING
        && elapsed(now_ms, b->t_link, BLEU_CONNECT_MS)) {
        b->err = BLEU_ERR_CONNECT;
        b->err_status = 0;
        tear_down(b, now_ms);
        unlinked(b, true, BLEU_RETRY_MS, now_ms);
    } else if (b->tok_link != 0u && b->st != BLEU_ST_CONNECTING
               && elapsed(now_ms, b->t_link, BLEU_GATT_MS)) {
        fail(b, BLEU_ERR_GATT, 0, now_ms);
    }
    if (b->st == BLEU_ST_UP && b->param_due && b->wr != BLEU_WR_BUSY) {
        b->param_due = false;
        b->rd_busy = true;
        b->t_link = now_ms;
        b->tok_link = next_token(b);
        bleu_act_t *a = push(b, BLEU_ACT_READ);
        if (a != NULL) {
            a->token = b->tok_link;
            a->h1 = b->param_val;
        }
    }
    if (b->settle != SETTLE_NONE) {
        return;
    }
    /* The GAP procedures, one at a time: the scan first. */
    if (b->scanning && b->tok_scan == 0u) {
        b->tok_scan = next_token(b);
        b->t_scan = now_ms;
        bleu_act_t *a = push(b, BLEU_ACT_SCAN);
        if (a != NULL) {
            a->token = b->tok_scan;
            a->ms = (uint16_t)BLEU_SCAN_MS;
        }
    } else if (b->st == BLEU_ST_WAIT && !b->scanning
               && elapsed(now_ms, b->t_gap, b->gap_ms)) {
        ++b->attempts;
        bleu_act_t *a = step_to(b, BLEU_ST_CONNECTING, BLEU_ACT_CONNECT, 0, 0,
                                now_ms);
        if (a != NULL) {
            a->addr = b->addr;
            a->ms = (uint16_t)BLEU_CONNECT_MS;
        }
    }
}

bool bleu_step(bleu_t *b, uint32_t now_ms, bleu_act_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    if (b == NULL) {
        return false;
    }
    if (b->st != BLEU_ST_OFF) {
        run_timers(b, now_ms);
    } else if (b->settle != SETTLE_NONE
               && elapsed(now_ms, b->t_settle, BLEU_SETTLE_MS)) {
        b->settle = SETTLE_NONE;
    }
    if (b->q_n == 0u || out == NULL) {
        return false;
    }
    *out = b->q[b->q_head];
    b->q_head = (uint8_t)((b->q_head + 1u) % BLEU_ACT_QUEUE);
    --b->q_n;
    return true;
}
