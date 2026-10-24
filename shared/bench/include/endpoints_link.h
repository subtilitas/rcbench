/**
 * @file
 * @brief The panel's half of the throttle's pulse endpoints, Idle pulse and
 *        Full pulse, on the CHAN_CFG (0x23) link page: when a debt is paid,
 *        what each outcome of paying it leaves, and whether a switch to ARMED
 *        may go on.
 *
 * An edit of either setting, a link-up edge and the panel's start owe the
 * IOmcu the pair.  The debt is paid at a poll whose control write put
 * ARM = 0 at the IOmcu and was acknowledged, once ENDPOINTS_SETTLE_MS
 * (300ms) have passed since the last edit: a key held on + or - steps the
 * value every few hundred milliseconds, and each write of CHAN_CFG is a
 * flash save at the IOmcu.  Paying it reads the page, puts the pair on the
 * throttle channels and writes the page when it differs.
 *
 * The IOmcu applies a CHAN_CFG write to its outputs before it
 * acknowledges it, and the acknowledgement carries what it stored.  A read
 * that already holds the pair, or an ACK whose stored throttle channels
 * hold it, is the pair in force; a read or a write nobody answered leaves
 * the debt owed.
 *
 * Any other answer ends the debt and blocks the switch to ARMED
 * (endpoints_block()): the read refused, Idle pulse not below Full pulse,
 * the write refused, or an ACK whose throttle channels hold another pair.
 * The same request refused once is refused every time, so the debt is not
 * sent again by itself.  A new edit, a new binding (endpoints_recheck())
 * or a link-up edge, an IOmcu restart or update included, owes the pair
 * again, and the payment that follows decides afresh.
 *
 * The frame that switches to ARMED carries no CHAN_CFG.  While the pair is
 * not in force -- owed or blocked -- with the link up, a switch to ARMED is
 * refused (arming_hs_facts_t.endpoints_blocked): one that went on would run
 * the throttle on whatever range the IOmcu holds.
 *
 * The debt crosses two tasks: the settings screen publishes an edit
 * (endpoints_publish()) and the control task, which owns the link, pays
 * it (endpoints_service()) and asks it before a switch to ARMED
 * (endpoints_owed()).  The pair and the time travel in atomics beside the
 * flag, the pair in one word, min in the high half, so a payment never
 * pairs one edit's minimum with another's maximum.
 *
 * No link of its own: the caller's pay function makes the exchanges and
 * reports how they went.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An edit rests this long before the pair is written. */
#define ENDPOINTS_SETTLE_MS 300u

/** @brief What the debt asks of this pass, endpoints_next(). */
typedef enum {
    /** @brief Nothing owed. */
    ENDPOINTS_PAID = 0,
    /** @brief Owed, and this pass did not put ARM = 0 at the IOmcu. */
    ENDPOINTS_WAIT_DISARM,
    /** @brief Owed, and the last edit is younger than ENDPOINTS_SETTLE_MS. */
    ENDPOINTS_WAIT_SETTLE,
    /** @brief Owed and due: read the page now. */
    ENDPOINTS_PAY,
} endpoints_next_t;

/**
 * @brief What a debt asks of a pass at @p now_ms.
 *
 * @p owed: a pair is owed.
 * @p far_disarmed: the control write of this pass put ARM = 0 at the far
 * end and was acknowledged.  @p edit_ms: the time of the last edit.
 *
 * Due once now_ms - edit_ms, taken modulo 2^32, reaches
 * ENDPOINTS_SETTLE_MS: 299ms after an edit waits, 300ms pays.
 *
 * @param owed         true while a pair is owed.
 * @param far_disarmed true when the control write of this pass put ARM = 0 at
 *                     the far end and it was acknowledged.
 * @param edit_ms      The time of the last edit, in ms.
 * @param now_ms       The time of this pass, in ms.
 * @return What the pass asks for, as endpoints_next_t.
 */
endpoints_next_t endpoints_next(bool owed, bool far_disarmed,
                                uint32_t edit_ms, uint32_t now_ms);

/** @brief How paying a debt went. */
typedef enum {
    /** @brief The read or the write ended unanswered. */
    ENDPOINTS_NO_ANSWER = 0,
    /** @brief The read came back refused: the range in force is not known. */
    ENDPOINTS_READ_REFUSED,
    /** @brief Idle pulse is not below Full pulse: nothing sent. */
    ENDPOINTS_ORDER,
    /** @brief The page already holds the pair: nothing written. */
    ENDPOINTS_HELD,
    /** @brief The write came back acknowledged: the pair is in force. */
    ENDPOINTS_TAKEN,
    /**
     * @brief The write came back refused: the IOmcu keeps the pair it held, or
     *        the frames of the page before the refused one.
     */
    ENDPOINTS_WRITE_REFUSED,
    /**
     * @brief The write came back acknowledged, and what the IOmcu stored on a
     *        throttle channel is not the pair.
     */
    ENDPOINTS_DIFFERS,
} endpoints_outcome_t;

/** @brief Why a switch to ARMED is refused for the pair, endpoints_block(). */
typedef enum {
    /** @brief The pair is in force, or nothing was ever owed. */
    ENDPOINTS_BLOCK_NONE = 0,
    /** @brief Owed: an edit, a binding or a link-up not yet paid. */
    ENDPOINTS_BLOCK_OWED,
    /** @brief The IOmcu refused the read: its range is not known. */
    ENDPOINTS_BLOCK_UNKNOWN,
    /** @brief Idle pulse is not below Full pulse: nothing was sent. */
    ENDPOINTS_BLOCK_ORDER,
    /** @brief The IOmcu refused the write. */
    ENDPOINTS_BLOCK_REFUSED,
    /** @brief The IOmcu acknowledged and stored another pair. */
    ENDPOINTS_BLOCK_DIFFERS,
} endpoints_block_t;

/**
 * @brief Whether every throttle channel of a CHAN_CFG page @p page
 *        (LINK_CC_COUNT registers) holds @p min_us and @p max_us.
 *
 * A page with no throttle channel holds any pair.  False for NULL.
 *
 * @param page   The CHAN_CFG page, LINK_CC_COUNT registers.
 * @param min_us The Idle pulse, in us.
 * @param max_us The Full pulse, in us.
 * @return true when every throttle channel holds the pair; false for NULL.
 */
bool endpoints_page_holds(const uint16_t *page, uint16_t min_us,
                          uint16_t max_us);

/**
 * @brief Whether the debt stands after an attempt that ended in @p outcome:
 *        true only for ENDPOINTS_NO_ANSWER, and for a value outside the enum.
 *
 * @param outcome The outcome of the attempt.
 * @return true when the debt stands.
 */
bool endpoints_owed_after(endpoints_outcome_t outcome);

/**
 * @brief The debt.
 *
 * Zeroed is nothing owed, no hold, no block, a pair of 0 and
 *  0.
 */
typedef struct {
    atomic_uint pair;     /**< min << 16 | max, in us                  */
    atomic_uint edit_ms;  /**< when the last edit was published        */
    atomic_bool owed;     /**< a pair is owed to the far end          */
    /**
     * @brief Control task only: raised at a link-up edge, the far end is held
     *        disarmed until the debt is paid.
     *
     * Ends with every outcome but a missing answer, and when nothing is owed.
     */
    bool hold;
    /**
     * @brief Control task only: what the last answered payment left, an
     *        endpoints_block_t in one byte.
     */
    uint8_t refused;
} endpoints_debt_t;

/**
 * @brief An edit of Idle pulse or Full pulse at @p now_ms, from any task: the
 *        pair, then the time, then the flag, so a payment that sees the flag
 *        sees the pair and the rest it starts from.
 *
 * @param d      The debt to update.
 * @param min_us The Idle pulse, in us.
 * @param max_us The Full pulse, in us.
 * @param now_ms The time of the edit, in ms.
 */
void endpoints_publish(endpoints_debt_t *d, uint16_t min_us, uint16_t max_us,
                       uint32_t now_ms);

/**
 * @brief A link-up edge: the pair is owed again and the far end held disarmed
 *        until it is paid.
 *
 * Control task only.
 *
 * @param d The debt to update.
 */
void endpoints_link_up(endpoints_debt_t *d);

/**
 * @brief Whether a pair is owed: true refuses a switch to ARMED with the link
 *        up.
 *
 * False for NULL.
 *
 * @param d The debt to read.
 * @return true when a pair is owed.
 */
bool endpoints_owed(const endpoints_debt_t *d);

/**
 * @brief A binding was written: the throttle channels may have moved, so the
 *        pair is owed again, due at the next poll once its edit has rested.
 *
 * Control task only.
 *
 * @param d The debt to update.
 */
void endpoints_recheck(endpoints_debt_t *d);

/**
 * @brief Why a switch to ARMED with the link up is refused for the pair:
 *        ENDPOINTS_BLOCK_OWED while owed, else what the last answered payment
 *        left.
 *
 * ENDPOINTS_BLOCK_NONE for NULL.
 *
 * @param d The debt to read.
 * @return The block reason; ENDPOINTS_BLOCK_NONE for NULL.
 */
endpoints_block_t endpoints_block(const endpoints_debt_t *d);

/**
 * @brief endpoints_block() is not ENDPOINTS_BLOCK_NONE.
 *
 * @param d The debt to read.
 * @return true when endpoints_block() is not ENDPOINTS_BLOCK_NONE.
 */
bool endpoints_blocked(const endpoints_debt_t *d);

/**
 * @brief Whether the far end is held disarmed for the debt.
 *
 * False for NULL.
 *
 * @param d The debt to read.
 * @return true when the far end is held disarmed.
 */
bool endpoints_held(const endpoints_debt_t *d);

/**
 * @brief The pair as last published, in us; 0 for NULL.
 *
 * @param d The debt to read.
 * @return The Idle pulse in us; 0 for NULL.
 */
uint16_t endpoints_min(const endpoints_debt_t *d);
/**
 * @brief The Full pulse as last published, in us; 0 for NULL.
 *
 * @param d The debt to read.
 * @return The Full pulse in us; 0 for NULL.
 */
uint16_t endpoints_max(const endpoints_debt_t *d);

/**
 * @brief The debt at a poll, on the control task: what endpoints_next() finds.
 *
 * On ENDPOINTS_PAY the flag has been taken, and the caller pays the pair
 * endpoints_min() and endpoints_max() read now and ends the payment with
 * endpoints_paid().  The flag is taken before the exchanges and given back
 * by a missing answer, so an edit published while they run stays owed.  A
 * switch to ARMED asks endpoints_owed() on the same task, so never between
 * the taking and the outcome.
 *
 * On ENDPOINTS_PAID the hold ends.  A NULL @p d changes nothing and
 * returns ENDPOINTS_WAIT_DISARM.
 *
 * @param d            The debt, or NULL.
 * @param far_disarmed true when the far end is disarmed.
 * @param now_ms       The time of the poll, in ms.
 * @return What endpoints_next() finds at this poll.
 */
endpoints_next_t endpoints_take(endpoints_debt_t *d, bool far_disarmed,
                                uint32_t now_ms);

/**
 * @brief The end of a payment endpoints_take() handed out: owed again on a
 *        missing answer, the hold and the block kept with it; otherwise paid,
 *        the hold ended, and the block set from @p outcome -- none for
 *        ENDPOINTS_HELD and ENDPOINTS_TAKEN.
 *
 * @param d       The debt, or NULL.
 * @param outcome The result of the payment.
 */
void endpoints_paid(endpoints_debt_t *d, endpoints_outcome_t outcome);

#ifdef __cplusplus
}
#endif
