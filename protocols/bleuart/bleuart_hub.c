/**
 * @file
 * @brief Several bleuart machines on one host stack (bleuart_hub.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "bleuart_hub.h"

_Static_assert(BLEU_HUB_CLIENTS <= BLEU_OWNER_MAX,
               "every client's owner fits a token's top 3 bits");
_Static_assert(BLEU_HUB_OUT <= 255u, "the out queues count in a byte");
_Static_assert(BLEU_HUB_TURN_MS >= 10u * BLEU_RETRY_TURN_MS,
               "a kept turn outlasts its client's retries");

void bleu_hub_init(bleu_hub_t *h)
{
    if (h != NULL) {
        memset(h, 0, sizeof *h);
    }
}

bool bleu_hub_add(bleu_hub_t *h, uint8_t owner)
{
    if (h == NULL || owner >= BLEU_HUB_CLIENTS) {
        return false;
    }
    h->c[owner].used = true;
    return true;
}

/**
 * @brief Whether @p owner is an added client.
 * @param h     The hub.
 * @param owner The owner.
 * @return true when it is.
 */
static bool client_ok(const bleu_hub_t *h, uint8_t owner)
{
    return owner < BLEU_HUB_CLIENTS && h->c[owner].used;
}

/**
 * @brief Queue @p e for the client @p owner, or count it stray when no such
 *        client was added.
 * @param h     The hub.
 * @param owner The client.
 * @param e     The event.
 */
static void deliver(bleu_hub_t *h, uint8_t owner, const bleu_evt_t *e)
{
    if (!client_ok(h, owner)) {
        ++h->stray;
        return;
    }
    if (h->ev_n >= BLEU_HUB_OUT) {
        ++h->drops;
        return;
    }
    bleu_hub_dlv_t *d = &h->ev[(h->ev_head + h->ev_n) % BLEU_HUB_OUT];
    ++h->ev_n;
    d->owner = owner;
    d->e = *e;
    ++h->c[owner].events;
}

/**
 * @brief Answer an action of @p owner at once.
 * @param h      The hub.
 * @param owner  The client.
 * @param kind   The answer's bleu_ev_kind_t.
 * @param token  The action's token.
 * @param conn   Its connection handle.
 * @param status The answer's status.
 */
static void answer(bleu_hub_t *h, uint8_t owner, uint8_t kind, uint16_t token,
                   uint16_t conn, uint16_t status)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.token = token;
    e.conn = conn;
    e.status = status;
    deliver(h, owner, &e);
}

/**
 * @brief Queue an action for the host stack.
 * @param h The hub.
 * @param a The action.
 */
static void pass(bleu_hub_t *h, const bleu_act_t *a)
{
    if (h->act_n >= BLEU_HUB_OUT) {
        ++h->drops;
        return;
    }
    h->act[(h->act_head + h->act_n) % BLEU_HUB_OUT] = *a;
    ++h->act_n;
}

/**
 * @brief The client whose connection @p conn is.
 * @param h    The hub.
 * @param conn The handle.
 * @return Its owner, or BLEU_HUB_CLIENTS for none.
 */
static uint8_t owner_of_conn(const bleu_hub_t *h, uint16_t conn)
{
    for (uint8_t i = 0; i < BLEU_HUB_CLIENTS; ++i) {
        if (h->c[i].used && h->c[i].linked && h->c[i].conn == conn) {
            return i;
        }
    }
    return BLEU_HUB_CLIENTS;
}

/**
 * @brief Where @p owner waits in the queue.
 * @param h     The hub.
 * @param owner The client.
 * @return Its place, or qn when it does not wait.
 */
static uint8_t q_find(const bleu_hub_t *h, uint8_t owner)
{
    for (uint8_t i = 0; i < h->qn; ++i) {
        if (h->q[i] == owner) {
            return i;
        }
    }
    return h->qn;
}

/**
 * @brief Take the place at @p at out of the queue.
 * @param h  The hub.
 * @param at The place, below qn.
 */
static void q_drop_at(bleu_hub_t *h, uint8_t at)
{
    for (uint8_t i = at; i + 1u < h->qn; ++i) {
        h->q[i] = h->q[i + 1u];
    }
    --h->qn;
}

/**
 * @brief Take @p owner out of the queue; one that does not wait is left
 *        alone.
 * @param h     The hub.
 * @param owner The client.
 */
static void q_drop(bleu_hub_t *h, uint8_t owner)
{
    const uint8_t at = q_find(h, owner);
    if (at < h->qn) {
        q_drop_at(h, at);
    }
}

/**
 * @brief Queue @p owner for a turn.  A hurried client, and one placed in
 *        front, goes behind the hurried clients that wait and ahead of
 *        every other; any other goes last.  A client that already waits
 *        keeps its place unless it comes hurried for the first time or is
 *        placed in front.
 * @param h     The hub.
 * @param owner The client.
 * @param hurry Its CONNECT carries BLEU_ACT_F_HURRY.
 * @param front Place it ahead of the unhurried clients that wait.
 */
static void enqueue(bleu_hub_t *h, uint8_t owner, bool hurry, bool front)
{
    const uint8_t was = q_find(h, owner);
    if (was < h->qn) {
        if (!front && (!hurry || h->c[owner].hurry)) {
            return;
        }
        q_drop_at(h, was);
        hurry = hurry || h->c[owner].hurry;
    }
    if (h->qn >= BLEU_HUB_CLIENTS) {
        return;
    }
    uint8_t at = 0;
    while (at < h->qn && h->c[h->q[at]].hurry) {
        ++at;
    }
    if (!hurry && !front) {
        at = h->qn;
    }
    for (uint8_t i = h->qn; i > at; --i) {
        h->q[i] = h->q[i - 1u];
    }
    h->q[at] = owner;
    ++h->qn;
    h->c[owner].hurry = hurry;
}

/**
 * @brief Keep the next turn for the client first in the queue, unless a
 *        turn is kept already or nobody waits.
 * @param h The hub.
 */
static void designate(bleu_hub_t *h)
{
    if (h->turn_set || h->qn == 0u) {
        return;
    }
    h->turn_set = true;
    h->turn_owner = h->q[0];
    h->turn_hurry = h->c[h->q[0]].hurry;
    q_drop_at(h, 0);
}

/**
 * @brief Whether @p owner, waiting for a turn, has not come back: the slot
 *        free for BLEU_HUB_TURN_MS and its CONNECT that BLEU_HUB_TURN_MS
 *        past its due time.
 * @param h      The hub.
 * @param owner  The client.
 * @param now_ms The clock.
 * @return true when it is gone.
 */
static bool gone(const bleu_hub_t *h, uint8_t owner, uint32_t now_ms)
{
    return (uint32_t)(now_ms - h->turn_t) >= BLEU_HUB_TURN_MS
           && (int32_t)(now_ms - h->c[owner].due)
                  >= (int32_t)BLEU_HUB_TURN_MS;
}

/**
 * @brief Give the slot to @p owner's SCAN or CONNECT.
 * @param h      The hub.
 * @param gap    BLEU_HUB_GAP_SCAN or BLEU_HUB_GAP_CONNECT.
 * @param owner  The client.
 * @param a      Its action.
 * @param now_ms The clock.
 */
static void take_gap(bleu_hub_t *h, uint8_t gap, uint8_t owner,
                     const bleu_act_t *a, uint32_t now_ms)
{
    h->gap = gap;
    h->gap_owner = owner;
    h->gap_token = a->token;
    h->gap_t = now_ms;
    h->gap_ms = (uint32_t)a->ms + BLEU_SETTLE_MS;
    h->yield = false;
    if (gap == BLEU_HUB_GAP_SCAN) {
        h->adv_set = true;
        h->adv_owner = owner;
    }
}

/**
 * @brief Free the slot; a SCAN that waited for it takes it and goes out.
 * @param h      The hub.
 * @param now_ms The clock.
 */
static void free_gap(bleu_hub_t *h, uint32_t now_ms)
{
    h->gap = BLEU_HUB_GAP_FREE;
    h->yield = false;
    h->turn_t = now_ms;
    if (h->held) {
        h->held = false;
        take_gap(h, BLEU_HUB_GAP_SCAN, h->held_owner, &h->held_act, now_ms);
        pass(h, &h->held_act);
    }
}

/**
 * @brief Make @p conn the connection of @p owner.  Another client that
 *        holds the same handle lost its DISCONNECT: its link is ended with
 *        one of status BLEU_STATUS_LOST, handed out before the CONNECT.
 * @param h     The hub.
 * @param owner The client.
 * @param conn  The handle.
 */
static void link_conn(bleu_hub_t *h, uint8_t owner, uint16_t conn)
{
    const uint8_t other = owner_of_conn(h, conn);
    if (other < BLEU_HUB_CLIENTS && other != owner) {
        h->c[other].linked = false;
        ++h->c[other].cut;
        answer(h, other, BLEU_EV_DISCONNECT, 0u, conn, BLEU_STATUS_LOST);
    }
    h->c[owner].linked = true;
    h->c[owner].conn = conn;
}

/**
 * @brief Take a CONNECT: end the slot it answers, pass on a connect the hub
 *        cancelled as busy, and keep the handle of one that came up.
 * @param h      The hub.
 * @param e      The event.
 * @param now_ms The clock.
 */
static void on_connect(bleu_hub_t *h, const bleu_evt_t *e, uint32_t now_ms)
{
    bleu_evt_t out = *e;
    const uint8_t owner = bleu_token_owner(e->token);
    if (h->gap == BLEU_HUB_GAP_CONNECT && e->token == h->gap_token) {
        if (h->yield && e->status != 0u && client_ok(h, owner)) {
            /* The scan cut this connect: its client waits in front. */
            out.status = BLEU_STATUS_BUSY;
            ++h->c[owner].busy;
            enqueue(h, owner, false, true);
            h->c[owner].due = now_ms + BLEU_RETRY_MS;
        }
        free_gap(h, now_ms);
    }
    if (e->status == 0u && client_ok(h, owner)) {
        link_conn(h, owner, e->conn);
    }
    deliver(h, owner, &out);
}

/**
 * @brief Take a CONN_UPDATE or a NOTIFY: to the handle's client, else to
 *        the token's.
 * @param h The hub.
 * @param e The event.
 */
static void on_conn_event(bleu_hub_t *h, const bleu_evt_t *e)
{
    uint8_t owner = owner_of_conn(h, e->conn);
    if (owner == BLEU_HUB_CLIENTS) {
        if (e->token == 0u) {
            ++h->stray;
            return;
        }
        owner = bleu_token_owner(e->token);
    }
    deliver(h, owner, e);
}

/**
 * @brief Take a DISCONNECT.  One with a client's token, the connect's the
 *        host stack keeps for the connection, goes to that client, which
 *        frees the handle only when it is that client's: a DISCONNECT the
 *        port held back can come after the handle was given out again.
 *        One without goes to the handle's client.  A DISCONNECT with the
 *        pending connect's token (a connection that failed to be
 *        established) ends the slot.
 * @param h      The hub.
 * @param e      The event.
 * @param now_ms The clock.
 */
static void on_disconnect(bleu_hub_t *h, const bleu_evt_t *e, uint32_t now_ms)
{
    if (h->gap == BLEU_HUB_GAP_CONNECT && e->token != 0u
        && e->token == h->gap_token) {
        free_gap(h, now_ms);
    }
    uint8_t owner = bleu_token_owner(e->token);
    if (e->token == 0u || !client_ok(h, owner)) {
        owner = owner_of_conn(h, e->conn);
    }
    if (owner == BLEU_HUB_CLIENTS) {
        ++h->stray;
        return;
    }
    if (h->c[owner].linked && h->c[owner].conn == e->conn) {
        h->c[owner].linked = false;
    }
    deliver(h, owner, e);
}

void bleu_hub_event(bleu_hub_t *h, const bleu_evt_t *e, uint32_t now_ms)
{
    if (h == NULL || e == NULL) {
        return;
    }
    switch (e->kind) {
    case BLEU_EV_ADV:
        if (h->gap == BLEU_HUB_GAP_SCAN) {
            deliver(h, h->gap_owner, e);
        } else if (h->adv_set) {
            deliver(h, h->adv_owner, e);
        } else {
            ++h->stray;
        }
        return;
    case BLEU_EV_SCAN_END:
        if (h->gap == BLEU_HUB_GAP_SCAN && e->token == h->gap_token) {
            free_gap(h, now_ms);
        }
        break;
    case BLEU_EV_CONNECT:
        on_connect(h, e, now_ms);
        return;
    case BLEU_EV_DISCONNECT:
        on_disconnect(h, e, now_ms);
        return;
    case BLEU_EV_CONN_UPDATE:
    case BLEU_EV_NOTIFY:
        on_conn_event(h, e);
        return;
    default:
        break;
    }
    if (e->token == 0u) {
        ++h->stray;
        return;
    }
    deliver(h, bleu_token_owner(e->token), e);
}

void bleu_hub_written(bleu_hub_t *h, const bleu_act_t *a, int status,
                      uint32_t now_ms)
{
    if (h == NULL || a == NULL || a->kind != BLEU_ACT_WRITE_NR) {
        return;
    }
    bleu_evt_t e;
    memset(&e, 0, sizeof(e));
    e.kind = BLEU_EV_WRITE_DONE;
    e.token = a->token;
    e.conn = a->conn;
    e.status = (uint16_t)status;
    bleu_hub_event(h, &e, now_ms);
}

/**
 * @brief The answer kind of an action on a connection.
 * @param kind The bleu_act_kind_t.
 * @return Its bleu_ev_kind_t, BLEU_EV_NONE for one without an answer.
 */
static uint8_t answer_kind(uint8_t kind)
{
    switch (kind) {
    case BLEU_ACT_DISC_SVC:  return BLEU_EV_SVC_DONE;
    case BLEU_ACT_DISC_CHRS: return BLEU_EV_CHR_DONE;
    case BLEU_ACT_DISC_DSCS: return BLEU_EV_DSC_DONE;
    case BLEU_ACT_MTU:       return BLEU_EV_MTU;
    case BLEU_ACT_SUBSCRIBE:
    case BLEU_ACT_WRITE:
    case BLEU_ACT_WRITE_NR:  return BLEU_EV_WRITE_DONE;
    case BLEU_ACT_READ:      return BLEU_EV_READ;
    default:                 return BLEU_EV_NONE;
    }
}

/**
 * @brief A client's SCAN: to the host stack, held for a connect the hub
 *        cancels, or answered busy.
 * @param h      The hub.
 * @param owner  The client.
 * @param a      The action.
 * @param now_ms The clock.
 */
static void act_scan(bleu_hub_t *h, uint8_t owner, const bleu_act_t *a,
                     uint32_t now_ms)
{
    if (h->gap == BLEU_HUB_GAP_FREE
        || (h->gap_owner == owner && !h->held)) {
        take_gap(h, BLEU_HUB_GAP_SCAN, owner, a, now_ms);
        pass(h, a);
    } else if (h->gap == BLEU_HUB_GAP_CONNECT && !h->held) {
        h->held = true;
        h->held_owner = owner;
        h->held_act = *a;
        h->yield = true;
        h->gap_t = now_ms;
        h->gap_ms = BLEU_SETTLE_MS;
        bleu_act_t c;
        memset(&c, 0, sizeof c);
        c.kind = BLEU_ACT_CANCEL;
        pass(h, &c);
    } else {
        ++h->c[owner].busy;
        answer(h, owner, BLEU_EV_SCAN_END, a->token, 0xFFFFu,
               BLEU_STATUS_BUSY);
    }
}

/**
 * @brief A client's CONNECT: to the host stack while the slot is its own,
 *        or free with no other client's turn kept; else its client waits
 *        in the queue, and the hub answers BLEU_STATUS_TURN when the next
 *        turn is the client's, unless an unhurried client's while a scan
 *        holds the slot, and busy otherwise.  A hurried CONNECT takes
 *        a turn kept for an unhurried client, which goes back to the
 *        queue's front.
 * @param h      The hub.
 * @param owner  The client.
 * @param a      The action.
 * @param mine   The client holds the slot.
 * @param now_ms The clock.
 */
static void act_connect(bleu_hub_t *h, uint8_t owner, const bleu_act_t *a,
                        bool mine, uint32_t now_ms)
{
    const bool hurry = (a->flags & BLEU_ACT_F_HURRY) != 0u;
    designate(h);
    if (hurry && h->turn_set && h->turn_owner != owner && !h->turn_hurry) {
        h->turn_set = false;
        enqueue(h, h->turn_owner, false, true);
        q_drop(h, owner);
        h->c[owner].hurry = true;
        h->turn_set = true;
        h->turn_owner = owner;
        h->turn_hurry = true;
    }
    const bool my_turn = h->turn_set && h->turn_owner == owner;
    if ((h->gap == BLEU_HUB_GAP_FREE && (!h->turn_set || my_turn))
        || (mine && !h->held)) {
        if (my_turn) {
            h->turn_set = false;
        }
        q_drop(h, owner);
        take_gap(h, BLEU_HUB_GAP_CONNECT, owner, a, now_ms);
        pass(h, a);
        return;
    }
    ++h->c[owner].busy;
    if (!my_turn) {
        enqueue(h, owner, hurry, false);
        designate(h);
    }
    /* A scan holds the slot for up to BLEU_SCAN_MS: an unhurried client
     * whose turn is kept comes back on its BLEU_RETRY_MS retry. */
    if (h->turn_set && h->turn_owner == owner
        && (h->gap != BLEU_HUB_GAP_SCAN || h->turn_hurry)) {
        h->c[owner].due = now_ms + BLEU_RETRY_TURN_MS;
        answer(h, owner, BLEU_EV_CONNECT, a->token, 0u, BLEU_STATUS_TURN);
        return;
    }
    h->c[owner].due = now_ms + BLEU_RETRY_MS;
    answer(h, owner, BLEU_EV_CONNECT, a->token, 0u, BLEU_STATUS_BUSY);
}

void bleu_hub_action(bleu_hub_t *h, uint8_t owner, const bleu_act_t *a,
                     uint32_t now_ms)
{
    if (h == NULL || a == NULL || !client_ok(h, owner)) {
        if (h != NULL) {
            ++h->refused;
        }
        return;
    }
    const bool mine = h->gap != BLEU_HUB_GAP_FREE && h->gap_owner == owner;
    switch (a->kind) {
    case BLEU_ACT_NONE:
        return;
    case BLEU_ACT_SCAN:
        act_scan(h, owner, a, now_ms);
        return;
    case BLEU_ACT_SCAN_STOP:
        if (h->held && h->held_owner == owner) {
            h->held = false;
        } else if (h->gap == BLEU_HUB_GAP_FREE
                   || (mine && h->gap == BLEU_HUB_GAP_SCAN)) {
            if (mine) {
                free_gap(h, now_ms);
            }
            pass(h, a);
        }
        return;
    case BLEU_ACT_CONNECT:
        act_connect(h, owner, a, mine, now_ms);
        return;
    case BLEU_ACT_CANCEL:
        if (h->gap == BLEU_HUB_GAP_FREE || mine) {
            pass(h, a);
        }
        return;
    default:
        break;
    }
    const uint8_t holder = owner_of_conn(h, a->conn);
    if (holder < BLEU_HUB_CLIENTS && holder != owner) {
        ++h->refused;
        const uint8_t k = answer_kind(a->kind);
        if (k != BLEU_EV_NONE && a->token != 0u) {
            answer(h, owner, k, a->token, a->conn, BLEU_STATUS_LOST);
        }
        return;
    }
    pass(h, a);
}

void bleu_hub_tick(bleu_hub_t *h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }
    if (h->gap != BLEU_HUB_GAP_FREE
        && (uint32_t)(now_ms - h->gap_t) >= h->gap_ms) {
        free_gap(h, now_ms);
    }
    if (h->gap != BLEU_HUB_GAP_FREE) {
        return;
    }
    /* A client that did not come back to the free slot when its retry was
     * due: it was switched off, unbound or answered. */
    if (h->turn_set && gone(h, h->turn_owner, now_ms)) {
        h->turn_set = false;
    }
    for (uint8_t i = h->qn; i > 0u; --i) {
        if (gone(h, h->q[i - 1u], now_ms)) {
            q_drop_at(h, (uint8_t)(i - 1u));
        }
    }
}

bool bleu_hub_next_event(bleu_hub_t *h, uint8_t *owner, bleu_evt_t *out)
{
    if (h == NULL || owner == NULL || out == NULL || h->ev_n == 0u) {
        return false;
    }
    *owner = h->ev[h->ev_head].owner;
    *out = h->ev[h->ev_head].e;
    h->ev_head = (uint8_t)((h->ev_head + 1u) % BLEU_HUB_OUT);
    --h->ev_n;
    return true;
}

bool bleu_hub_next_action(bleu_hub_t *h, bleu_act_t *out)
{
    if (h == NULL || out == NULL || h->act_n == 0u) {
        return false;
    }
    *out = h->act[h->act_head];
    h->act_head = (uint8_t)((h->act_head + 1u) % BLEU_HUB_OUT);
    --h->act_n;
    return true;
}
