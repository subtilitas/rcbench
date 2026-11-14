/**
 * @file
 * @brief Several bleuart machines on one BLE host stack: the client table,
 *        the events routed to their machine, and the host stack's one scan
 *        or connect at a time.
 *
 * Each client is a machine (bleu_t) given its owner, 0 to
 * BLEU_HUB_CLIENTS - 1, with bleu_setup(); the owner is the top 3 bits of
 * every token the machine hands out.  The caller takes one event of the
 * host stack into bleu_hub_event(), then hands each event
 * bleu_hub_next_event() gives to that owner's machine.  It hands each
 * action a machine gives to bleu_hub_action() with the machine's owner,
 * then performs each action bleu_hub_next_action() gives, in order.
 *
 * Routing:
 *
 * | Event | Goes to |
 * | --- | --- |
 * | ADV | the owner of the scan under way, else the owner of the last one |
 * | CONNECT | the token's owner; status 0 makes the handle the owner's |
 * | DISCONNECT | the token's owner (the host stack gives every event of a connection its connect's token), else the handle's |
 * | CONN_UPDATE, NOTIFY | the owner of the handle, else the token's |
 * | every other kind | the token's owner |
 *
 * An event no added client takes is counted in stray.  A CONNECT with
 * status 0 on a handle another client holds ends that client's link with a
 * DISCONNECT of status BLEU_STATUS_LOST first: its own DISCONNECT was lost
 * and the host stack gave the handle out again.  An action on a handle
 * another client holds is refused and counted: a GATT step is answered
 * with BLEU_STATUS_LOST, a DISCONNECT is dropped.
 *
 * The host stack runs one scan or one connect at a time (NimBLE answers a
 * second with BLE_HS_EALREADY or BLE_HS_EBUSY).  The hub holds that slot:
 *
 * - a client's SCAN or CONNECT takes a free slot or one the client holds
 *   itself, and goes to the host stack as it is;
 * - a SCAN while another client scans, or while another client's scan
 *   waits, is answered SCAN_END with BLEU_STATUS_BUSY;
 * - a CONNECT while another client holds the slot is answered CONNECT with
 *   BLEU_STATUS_BUSY: the machine tries again after BLEU_RETRY_MS;
 * - a SCAN while another client connects waits: the hub cancels that
 *   connect, its CONNECT event is passed on with BLEU_STATUS_BUSY when it
 *   failed, and the SCAN goes out once it came, or after BLEU_SETTLE_MS;
 * - a CANCEL or a SCAN_STOP of a client that does not hold the slot is
 *   dropped, so it never ends another client's procedure.
 *
 * The slot ends on its answer (SCAN_END, CONNECT, or a DISCONNECT with the
 * connect's token), on the holder's SCAN_STOP, or BLEU_SETTLE_MS after the
 * action's own duration.  With one client every action and event passes as
 * it would without the hub.
 *
 * Pure C: no clock read, no I/O, no heap.  Every function of one hub is
 * called from one task.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BLEUART_HUB_H
#define BLEUART_HUB_H

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Clients a hub holds: owners 0 to 4. */
#define BLEU_HUB_CLIENTS 5u
/** @brief Events and actions each of the hub's two out queues holds. */
#define BLEU_HUB_OUT     8u

/** @brief Who holds the host stack's one scan or connect. */
typedef enum {
    BLEU_HUB_GAP_FREE = 0, /**< nobody */
    BLEU_HUB_GAP_SCAN,     /**< a scan runs */
    BLEU_HUB_GAP_CONNECT,  /**< a connect is pending */
} bleu_hub_gap_t;

/** @brief One client of the table. */
typedef struct {
    bool     used;    /**< added */
    bool     linked;  /**< conn is a connection of its */
    uint16_t conn;    /**< that connection's handle */
    uint32_t events;  /**< events handed to it */
    uint32_t busy;    /**< its scans and connects answered busy */
    uint32_t cut;     /**< its links ended for a handle given out again */
} bleu_hub_client_t;

/** @brief An event for one client. */
typedef struct {
    uint8_t    owner; /**< the client */
    bleu_evt_t e;     /**< the event */
} bleu_hub_dlv_t;

/** @brief The hub.  Its members are private but for the tests. */
typedef struct {
    bleu_hub_client_t c[BLEU_HUB_CLIENTS]; /**< the client table */
    uint8_t    gap;        /**< bleu_hub_gap_t */
    uint8_t    gap_owner;  /**< the slot's holder */
    uint16_t   gap_token;  /**< the token of its SCAN or CONNECT */
    uint32_t   gap_t;      /**< when the slot was taken or the cancel sent */
    uint32_t   gap_ms;     /**< how long it is held at most */
    bool       yield;      /**< the hub cancelled the slot's connect for
                                held_act */
    bool       held;       /**< held_act waits for the slot */
    uint8_t    held_owner; /**< its client */
    bleu_act_t held_act;   /**< a SCAN that waits */
    bool       adv_set;    /**< adv_owner holds a client */
    uint8_t    adv_owner;  /**< the last scan's client */
    bleu_hub_dlv_t ev[BLEU_HUB_OUT]; /**< events due to clients */
    uint8_t    ev_head;    /**< the first of them */
    uint8_t    ev_n;       /**< their count */
    bleu_act_t act[BLEU_HUB_OUT];    /**< actions due to the host stack */
    uint8_t    act_head;   /**< the first of them */
    uint8_t    act_n;      /**< their count */
    uint32_t   stray;      /**< events no added client takes */
    uint32_t   refused;    /**< actions on another client's handle */
    uint32_t   drops;      /**< events or actions a full queue dropped */
} bleu_hub_t;

/** @brief Clear: no client, the slot free.  @param h The hub; NULL does
 *         nothing. */
void bleu_hub_init(bleu_hub_t *h);

/**
 * @brief Add the client @p owner.
 * @param h     The hub.
 * @param owner Its owner, below BLEU_HUB_CLIENTS; the machine's
 *              bleu_setup() owner.
 * @return false for NULL or an owner out of range.
 */
bool bleu_hub_add(bleu_hub_t *h, uint8_t owner);

/**
 * @brief Take one event of the host stack: route it, and move the slot on
 *        its answer.  The events for clients go to bleu_hub_next_event(),
 *        a SCAN that waited for the slot to bleu_hub_next_action().
 * @param h      The hub.
 * @param e      The event.
 * @param now_ms The clock.
 */
void bleu_hub_event(bleu_hub_t *h, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief Take one action of the client @p owner: pass it on, hold it, or
 *        answer it at once (bleu_hub_next_event()).
 * @param h      The hub.
 * @param owner  The client whose machine gave it.
 * @param a      The action.
 * @param now_ms The clock.
 */
void bleu_hub_action(bleu_hub_t *h, uint8_t owner, const bleu_act_t *a,
                     uint32_t now_ms);

/**
 * @brief End a slot whose answer did not come within its time.  Call at
 *        least every 10ms.
 * @param h      The hub.
 * @param now_ms The clock.
 */
void bleu_hub_tick(bleu_hub_t *h, uint32_t now_ms);

/**
 * @brief The next event for a client.
 * @param h     The hub.
 * @param owner Receives the client.
 * @param out   Receives the event.
 * @return false when none waits or for NULL.
 */
bool bleu_hub_next_event(bleu_hub_t *h, uint8_t *owner, bleu_evt_t *out);

/**
 * @brief The next action for the host stack.
 * @param h   The hub.
 * @param out Receives the action.
 * @return false when none waits or for NULL.
 */
bool bleu_hub_next_action(bleu_hub_t *h, bleu_act_t *out);

#ifdef __cplusplus
}
#endif
#endif
