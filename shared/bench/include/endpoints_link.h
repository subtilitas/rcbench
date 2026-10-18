/*
 * The panel's half of the throttle's pulse endpoints, Idle pulse and Full
 * pulse, on the CHAN_CFG (0x23) link page: when a debt is paid, what each
 * outcome of paying it leaves, and whether a switch to ARMED may go on.
 *
 * An edit of either setting, a link-up edge and the panel's start owe the
 * IOmcu the pair.  The debt is paid at a poll whose control write put
 * ARM = 0 at the IOmcu and was acknowledged, once ENDPOINTS_SETTLE_MS
 * (300 ms) have passed since the last edit: a key held on + or - steps the
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

/** An edit rests this long before the pair is written. */
#define ENDPOINTS_SETTLE_MS 300u

/** What the debt asks of this pass, endpoints_next(). */
typedef enum {
    /** Nothing owed. */
    ENDPOINTS_PAID = 0,
    /** Owed, and this pass did not put ARM = 0 at the IOmcu. */
    ENDPOINTS_WAIT_DISARM,
    /** Owed, and the last edit is younger than ENDPOINTS_SETTLE_MS. */
    ENDPOINTS_WAIT_SETTLE,
    /** Owed and due: read the page now. */
    ENDPOINTS_PAY,
} endpoints_next_t;

/**
 * What a debt asks of a pass at @p now_ms.  @p owed: a pair is owed.
 * @p far_disarmed: the control write of this pass put ARM = 0 at the far
 * end and was acknowledged.  @p edit_ms: the time of the last edit.
 *
 * Due once now_ms - edit_ms, taken modulo 2^32, reaches
 * ENDPOINTS_SETTLE_MS: 299 ms after an edit waits, 300 ms pays.
 */
endpoints_next_t endpoints_next(bool owed, bool far_disarmed,
                                uint32_t edit_ms, uint32_t now_ms);

/** How paying a debt went. */
typedef enum {
    /** The read or the write ended unanswered. */
    ENDPOINTS_NO_ANSWER = 0,
    /** The read came back refused: the range in force is not known. */
    ENDPOINTS_READ_REFUSED,
    /** Idle pulse is not below Full pulse: nothing sent. */
    ENDPOINTS_ORDER,
    /** The page already holds the pair: nothing written. */
    ENDPOINTS_HELD,
    /** The write came back acknowledged: the pair is in force. */
    ENDPOINTS_TAKEN,
    /** The write came back refused: the IOmcu keeps the pair it held, or
     *  the frames of the page before the refused one. */
    ENDPOINTS_WRITE_REFUSED,
    /** The write came back acknowledged, and what the IOmcu stored on a
     *  throttle channel is not the pair. */
    ENDPOINTS_DIFFERS,
} endpoints_outcome_t;

/** Why a switch to ARMED is refused for the pair, endpoints_block(). */
typedef enum {
    /** The pair is in force, or nothing was ever owed. */
    ENDPOINTS_BLOCK_NONE = 0,
    /** Owed: an edit, a binding or a link-up not yet paid. */
    ENDPOINTS_BLOCK_OWED,
    /** The IOmcu refused the read: its range is not known. */
    ENDPOINTS_BLOCK_UNKNOWN,
    /** Idle pulse is not below Full pulse: nothing was sent. */
    ENDPOINTS_BLOCK_ORDER,
    /** The IOmcu refused the write. */
    ENDPOINTS_BLOCK_REFUSED,
    /** The IOmcu acknowledged and stored another pair. */
    ENDPOINTS_BLOCK_DIFFERS,
} endpoints_block_t;

/**
 * Whether every throttle channel of a CHAN_CFG page @p page
 * (LINK_CC_COUNT registers) holds @p min_us and @p max_us.  A page with no
 * throttle channel holds any pair.  False for NULL.
 */
bool endpoints_page_holds(const uint16_t *page, uint16_t min_us,
                          uint16_t max_us);

/**
 * Whether the debt stands after an attempt that ended in @p outcome: true
 * only for ENDPOINTS_NO_ANSWER, and for a value outside the enum.
 */
bool endpoints_owed_after(endpoints_outcome_t outcome);

/** The debt.  Zeroed is nothing owed, no hold, no block, a pair of 0 and
 *  0. */
typedef struct {
    atomic_uint pair;     /**< min << 16 | max, in us                  */
    atomic_uint edit_ms;  /**< when the last edit was published        */
    atomic_bool owed;     /**< a pair is owed to the far end          */
    /**
     * Control task only: raised at a link-up edge, the far end is held
     * disarmed until the debt is paid.  Ends with every outcome but a
     * missing answer, and when nothing is owed.
     */
    bool hold;
    /** Control task only: what the last answered payment left, an
     *  endpoints_block_t in one byte. */
    uint8_t refused;
} endpoints_debt_t;

/**
 * An edit of Idle pulse or Full pulse at @p now_ms, from any task: the pair,
 * then the time, then the flag, so a payment that sees the flag sees the
 * pair and the rest it starts from.
 */
void endpoints_publish(endpoints_debt_t *d, uint16_t min_us, uint16_t max_us,
                       uint32_t now_ms);

/** A link-up edge: the pair is owed again and the far end held disarmed
 *  until it is paid.  Control task only. */
void endpoints_link_up(endpoints_debt_t *d);

/** Whether a pair is owed: true refuses a switch to ARMED with the link
 *  up.  False for NULL. */
bool endpoints_owed(const endpoints_debt_t *d);

/** A binding was written: the throttle channels may have moved, so the pair
 *  is owed again, due at the next poll once its edit has rested.  Control
 *  task only. */
void endpoints_recheck(endpoints_debt_t *d);

/** Why a switch to ARMED with the link up is refused for the pair:
 *  ENDPOINTS_BLOCK_OWED while owed, else what the last answered payment
 *  left.  ENDPOINTS_BLOCK_NONE for NULL. */
endpoints_block_t endpoints_block(const endpoints_debt_t *d);

/** endpoints_block() is not ENDPOINTS_BLOCK_NONE. */
bool endpoints_blocked(const endpoints_debt_t *d);

/** Whether the far end is held disarmed for the debt.  False for NULL. */
bool endpoints_held(const endpoints_debt_t *d);

/** The pair as last published, in us; 0 for NULL. */
uint16_t endpoints_min(const endpoints_debt_t *d);
uint16_t endpoints_max(const endpoints_debt_t *d);

/**
 * The debt at a poll, on the control task: what endpoints_next() finds.
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
 */
endpoints_next_t endpoints_take(endpoints_debt_t *d, bool far_disarmed,
                                uint32_t now_ms);

/** The end of a payment endpoints_take() handed out: owed again on a
 *  missing answer, the hold and the block kept with it; otherwise paid,
 *  the hold ended, and the block set from @p outcome -- none for
 *  ENDPOINTS_HELD and ENDPOINTS_TAKEN. */
void endpoints_paid(endpoints_debt_t *d, endpoints_outcome_t outcome);

#ifdef __cplusplus
}
#endif
