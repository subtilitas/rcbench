/**
 * @file
 * @brief The seam between hitec_prog.c, the session's driving, and
 *        hitec_prog_write.c, the edits, writes and the record.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The functions one file calls of the other are prefixed hpg_ (in
 * hitec_prog.c) and hpw_ (in hitec_prog_write.c).  Nothing outside the two
 * files includes this header; the interface is hitec_prog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "hitec_prog.h"

/** @brief The request phases: what the model waits on the link for. */
enum {
    ST_IDLE = 0,    /**< nothing of this model runs               */
    ST_OPENING,     /**< OPEN                                     */
    ST_SES,         /**< a session operation, step by step        */
    ST_CLOSING,     /**< CLOSE after a leave                      */
    ST_CLEARING,    /**< CLEAR                                    */
    ST_RELEASING,   /**< CLOSE of another session's port          */
    ST_RECORD,      /**< a write waits for the card's record      */
};

/** @brief The session operations. */
enum { OP_NONE = 0, OP_ENTER, OP_READ, OP_LEAVE, OP_WRITE };

/* --- hitec_prog.c --------------------------------------------------------- */

/**
 * @brief Say message @p id.
 *
 * @param p   The programmer.
 * @param id  hitec_prog_msg_id_t.
 * @param arg Its argument.
 * @param reg Its second argument.
 * @return @p id.
 */
hitec_prog_msg_id_t hpg_say2(hitec_prog_t *p, hitec_prog_msg_id_t id,
                             unsigned arg, unsigned reg);

/**
 * @brief Whether an operation of the held port may start now.
 *
 * @param p    The programmer.
 * @param safe true for one that sends frames: not with STOP latched, not
 *             armed, not on a STOPPED port.
 * @return HITEC_PROG_MSG_NONE when it may, else why not.
 */
hitec_prog_msg_id_t hpg_held_idle(const hitec_prog_t *p, bool safe);

/**
 * @brief The message of a session operation that did not end well.
 *
 * @param p The programmer.
 * @param r Its result.
 */
void hpg_ses_say(hitec_prog_t *p, hitec_ses_t r);

/**
 * @brief The session's next step goes out, or its operation's end is taken.
 *
 * @param p The programmer.
 */
void hpg_pump(hitec_prog_t *p);

/* --- hitec_prog_write.c --------------------------------------------------- */

/**
 * @brief A CONNECT starts: the switch off, the edits and the
 *        confirmations gone; the record stays.
 *
 * @param p The programmer.
 */
void hpw_connect(hitec_prog_t *p);

/**
 * @brief The write switch goes off: a LEAVE, a lost link, a stop.
 *
 * @param p The programmer.
 */
void hpw_off(hitec_prog_t *p);

/**
 * @brief A full read ended well: the draft is the servo again, the switch
 *        goes off when the servo changed, and a record stands against it.
 *
 * @param p    The programmer.
 * @param had  A servo was read before.
 * @param was  What that read found; NULL with none.
 */
void hpw_read_done(hitec_prog_t *p, bool had, const hitec_image_t *was);

/**
 * @brief A write operation ended with @p r.
 *
 * @param p The programmer.
 * @param r Its result.
 */
void hpw_write_done(hitec_prog_t *p, hitec_ses_t r);

/**
 * @brief Every feed: a stop switches the switch off, and a write that
 *        waits for the card gives up after HITEC_PROG_CARD_MS.
 *
 * @param p The programmer.
 */
void hpw_tick(hitec_prog_t *p);

/**
 * @brief ABORT of a write that waits for the card: nothing was written.
 *
 * @param p The programmer.
 * @return true when one waited.
 */
bool hpw_abort(hitec_prog_t *p);
