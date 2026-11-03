/**
 * @file
 * @brief The panel's half of the Hitec receive mode: Hitec D receive
 *        (SET_HITEC_RX), on the Hitec tab's MORE list, written to the HITEC
 *        page's register 23 (LINK_HT_W_RX_MODE, protocol 4.24), and what the
 *        list says of it.
 *
 * The mode is owed to the IOmcu at every link-up, an IOmcu restart
 * included (it starts at PULL-UP), and at every edit.  It is written while
 * owed, one write a poll, armed or not: the IOmcu takes it at its next
 * OPEN of a D series line and changes nothing that runs.  An
 * acknowledgement is the mode held.  A write nobody answered stays owed
 * and goes again HITEC_RX_LINK_RETRY_MS later.  A refusal ends the debt
 * until the next edit or link-up: the same write is refused every time.
 *
 * An IOmcu older than 4.24 is sent nothing: it refuses the register with
 * LINK_NACK_READ_ONLY and receives with the pull-up.  With PRECHARGE asked
 * the list says so (HITEC_RX_LINK_OLD); with PULL-UP asked there is nothing
 * to say.  An IOmcu older than 4.23 has no HITEC page and no Hitec line;
 * with PRECHARGE asked the list says that instead (HITEC_RX_LINK_NO_LINE).
 *
 * Pure logic: the caller makes the exchange and reports how it went.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_HITEC_RX_LINK_H
#define RCBENCH_HITEC_RX_LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How an exchange went, as tone_link.h has it: the ACK, a NACK's reason
 *        (LINK_NACK_*), or no answer.
 */
#define HITEC_RX_LINK_ACK       0
#define HITEC_RX_LINK_NO_ANSWER (-1)

/** @brief A write nobody answered goes again this much later. */
#define HITEC_RX_LINK_RETRY_MS 500u

/** @brief What the Hitec tab's list says, hitec_rx_link_mark(). */
typedef enum {
    /**
     * @brief Nothing: the IOmcu holds the mode, no IOmcu answers, or one  older
     *        than 4.24 with PULL-UP asked.
     */
    HITEC_RX_LINK_HELD = 0,
    /** @brief The mode asked is not acknowledged yet. */
    HITEC_RX_LINK_PENDING,
    /** @brief The IOmcu refused it. */
    HITEC_RX_LINK_REFUSED,
    /**
     * @brief An IOmcu of 4.23 with PRECHARGE asked: it receives with the
     *        pull-up.
     */
    HITEC_RX_LINK_OLD,
    /**
     * @brief An IOmcu older than 4.23 with PRECHARGE asked: it has no Hitec
     *        line.
     */
    HITEC_RX_LINK_NO_LINE,
} hitec_rx_link_mark_t;

/**
 * @brief The panel's state of the Hitec receive mode.
 */
typedef struct {
    uint32_t sent_ms;   /**< when the unanswered write went */
    uint8_t  want;      /**< link_hitec_rx_t asked */
    uint8_t  up;        /**< an IOmcu answers */
    uint8_t  minor;     /**< its protocol minor */
    uint8_t  owed;      /**< want is to be written */
    uint8_t  refused;   /**< the IOmcu refused want */
    uint8_t  waiting;   /**< a write went unanswered at sent_ms */
} hitec_rx_link_t;

/**
 * @brief No IOmcu, PULL-UP asked, nothing owed.
 *
 * @param l The link state to set up.
 */
void hitec_rx_link_init(hitec_rx_link_t *l);

/**
 * @brief The setting as it is now; a change owes it.
 *
 * A value above
 *  LINK_HITEC_RX_PRECHARGE is PULL-UP.
 *
 * @param l    The link state.
 * @param mode The setting; a value above LINK_HITEC_RX_PRECHARGE is PULL-UP.
 */
void hitec_rx_link_want(hitec_rx_link_t *l, uint8_t mode);

/**
 * @brief A link-up edge with an IOmcu of @p minor: the mode is owed again.
 *
 * @param l     The link state.
 * @param minor The protocol minor version of the IOmcu.
 */
void hitec_rx_link_came_up(hitec_rx_link_t *l, uint16_t minor);

/**
 * @brief The link went down.
 *
 * @param l The link state.
 */
void hitec_rx_link_lost(hitec_rx_link_t *l);

/**
 * @brief Whether to write the page's register 23 at @p now_ms, and with what.
 *
 * False while nothing is owed, no IOmcu answers, it is older than 4.24,
 *  or an unanswered write is younger than HITEC_RX_LINK_RETRY_MS.
 *
 * @param l      The link state.
 * @param now_ms The time of the poll, in ms.
 * @param value  Receives the register 23 value to write.
 * @return true when a write is due.
 */
bool hitec_rx_link_next(const hitec_rx_link_t *l, uint32_t now_ms,
                        uint16_t *value);

/**
 * @brief How the write hitec_rx_link_next() gave went: HITEC_RX_LINK_ACK, a
 *        NACK's reason, or HITEC_RX_LINK_NO_ANSWER.
 *
 * @param l      The link state.
 * @param result The write's result: an acknowledgement, a NACK's reason or
 *               HITEC_RX_LINK_NO_ANSWER.
 * @param now_ms The time of the answer, in ms.
 */
void hitec_rx_link_done(hitec_rx_link_t *l, int result, uint32_t now_ms);

/**
 * @brief What the list says now.
 *
 * @param l The link state.
 * @return The mark the list shows now.
 */
hitec_rx_link_mark_t hitec_rx_link_mark(const hitec_rx_link_t *l);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_HITEC_RX_LINK_H */
