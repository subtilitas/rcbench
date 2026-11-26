/**
 * @file node_cfg_link.h
 * @brief A node's own settings over the link, on the panel: the ask for
 *        RCNODE_T_CONFIG, the packets taken, the NAME and I228 writes.
 *        The power node's controller (pnode_ctl.h) and the balance node's
 *        (bal_ctl.h) each run one beside their central.  Pure C, under the
 *        host suite (test_node_cfg_link).
 *
 * After INFO is taken on each link, CONTROL gets RCNODE_OP_CONFIG.  A node
 * built before CONFIG refuses it (ATT error 0x13): the settings are not
 * known on that link (NCL_NONE), and the controller works as it does for
 * such a node.  A node that takes it sends RCNODE_T_CONFIG on STREAM.  When
 * none comes within NCL_WAIT_MS (2000ms) of the write's answer, CONFIG is
 * asked once more (NCL_ASKS asks in all); without a CONFIG NCL_WAIT_MS
 * after the last, the link counts as NCL_NONE (an MTU under 33, say).  A
 * later CONFIG, which the node sends after each change, is taken in any
 * state and counted (taken), so the controller decides the shunt again.
 *
 * NAME and I228 are written only with the settings known (NCL_HAVE), never
 * while the bench is ARMED or a switch to ARMED waits, one write at a
 * time, each after any write the controller owes first.  A write the node
 * refuses is counted for the band (refused), and so is a request refused
 * here (req_no, with its reason in req_why).  A NAME or I228 that waits or
 * is in flight when the link drops is not sent again on the next link: it
 * is counted for the band (dropped).  The three counts last across links.
 * The node answers a change with CONFIG.
 *
 * Times are unsigned differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bleuart.h"
#include "rcnode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The longest wait for CONFIG after its ask was taken, ms. */
#define NCL_WAIT_MS 2000u
/** @brief CONFIG asks on a link before the node counts as one without it. */
#define NCL_ASKS 2u

/** @brief Where the settings stand on this link. */
typedef enum {
    NCL_UNKNOWN = 0,  /**< not asked yet */
    NCL_ASKING,       /**< the CONFIG write is in flight */
    NCL_WAIT,         /**< taken; CONFIG not come yet */
    NCL_HAVE,         /**< a CONFIG was taken */
    NCL_NONE,         /**< the node takes no CONFIG: one built before it */
} ncl_state_t;

/** @brief What a write request got. */
typedef enum {
    NCL_REQ_OK = 0,   /**< queued */
    NCL_REQ_ARMED,    /**< refused: the bench is ARMED or an arm waits */
    NCL_REQ_UNKNOWN,  /**< refused: no CONFIG on this link (no link, or an
                           older node) */
    NCL_REQ_BAD,      /**< refused: a value rcnode_uname_ok() or
                           rcnode_i228_ok() refuses, or I228 on a node
                           without a shunt */
} ncl_req_t;

/** @brief The state.  Members are private but for the tests. */
typedef struct {
    uint8_t         st;        /**< ncl_state_t */
    bool            wr;        /**< one of its writes is in flight */
    uint32_t        asked_ms;  /**< when the ask's answer came */
    rcnode_config_t cfg;       /**< the last CONFIG */
    uint32_t        cfg_ms;    /**< when it came */
    bool            name_owed; /**< NAME waits */
    char            name[RCNODE_UNAME_MAX + 1u]; /**< its value */
    bool            i228_owed; /**< I228 waits */
    uint8_t         i228;      /**< its value */
    uint32_t        refused;   /**< NAME and I228 writes the node refused */
    uint32_t        taken;     /**< CONFIG packets taken on this link */
    uint8_t         asks;      /**< CONFIG asks on this link */
    bool            wr_set;    /**< the write in flight is NAME or I228 */
    uint32_t        req_no;    /**< NAME and I228 requests refused here */
    uint8_t         req_why;   /**< the last one's ncl_req_t */
    uint32_t        dropped;   /**< NAME and I228 writes a link's drop ended */
} ncl_t;

/**
 * @brief A new link: nothing known, nothing owed.  A NAME or I228 still
 *        owed or in flight counts in dropped; refused, req_no, req_why and
 *        dropped are kept.
 * @param n The state; NULL does nothing.
 */
void ncl_link(ncl_t *n);

/**
 * @brief One STREAM notification: a CONFIG is taken, in any state.
 * @param n      The state.
 * @param data   The bytes.
 * @param len    Their count.
 * @param now_ms The clock.
 * @return true when it was a CONFIG packet (taken, or one that would not
 *         decode): not the meter's.
 */
bool ncl_packet(ncl_t *n, const uint8_t *data, size_t len, uint32_t now_ms);

/**
 * @brief Follow the central: the answer to its own write in flight, the
 *        wait for CONFIG.
 * @param n      The state.
 * @param b      The central.
 * @param now_ms The clock.
 */
void ncl_follow(ncl_t *n, const bleu_t *b, uint32_t now_ms);

/**
 * @brief The write owed, when @p ready and the central can send: the
 *        CONFIG ask once a link, else NAME, else I228; NAME and I228 never
 *        while @p armed.
 * @param n      The state.
 * @param b      The central.
 * @param ready  INFO taken and no write of the controller's owed first.
 * @param armed  The bench is ARMED, or a switch to ARMED waits.
 * @param now_ms The clock.
 * @return true when a write was handed to the central.
 */
bool ncl_send(ncl_t *n, bleu_t *b, bool ready, bool armed, uint32_t now_ms);

/**
 * @brief Ask for a NAME write.  A refusal counts in req_no.
 * @param n     The state.
 * @param name  The name; "" clears it.
 * @param armed The bench is ARMED, or a switch to ARMED waits.
 * @return NCL_REQ_OK, or the refusal.
 */
ncl_req_t ncl_set_name(ncl_t *n, const char *name, bool armed);

/**
 * @brief Ask for an I228 write.  A refusal counts in req_no.
 * @param n     The state.
 * @param a     RCNODE_I228_AUTO or 0x40 to 0x4F.
 * @param armed The bench is ARMED, or a switch to ARMED waits.
 * @return NCL_REQ_OK, or the refusal.
 */
ncl_req_t ncl_set_i228(ncl_t *n, uint8_t a, bool armed);

/**
 * @brief Whether the node's settings are known on this link.
 * @param n The state.
 * @return true after a CONFIG.
 */
bool ncl_have(const ncl_t *n);

/**
 * @brief Whether the settings are still to come: the controller holds
 *        the SHUNT write until CONFIG or its refusal decides it.
 * @param n The state.
 * @return true before CONFIG is asked, while it is, and while it is
 *         awaited.
 */
bool ncl_pending(const ncl_t *n);

/**
 * @brief The node's shunt change's age now: CONFIG's age plus the time
 *        since it came.
 * @param n      The state.
 * @param now_ms The clock.
 * @return s, RCNODE_AGE_NONE when the node named none, 65,534 at most.
 */
uint16_t ncl_node_age_s(const ncl_t *n, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
