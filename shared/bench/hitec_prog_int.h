/**
 * @file
 * @brief The seam between hitec_prog.c, the session's driving,
 *        hitec_prog_write.c, the edits, writes and the record, and
 *        hitec_prog_file.c, the servo files and the session's backup.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The functions one file calls of another are prefixed hpg_ (in
 * hitec_prog.c), hpw_ (in hitec_prog_write.c) and hpf_ (in
 * hitec_prog_file.c).  Nothing outside the three files includes this
 * header; the interface is hitec_prog.h.
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
    ST_BACKUP,      /**< a write waits for the card's backup file */
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
 * @param was  What that read found; read only with @p had.
 * @return true when an address a write compares differs from @p was, or
 *         no servo was read before.
 */
bool hpw_read_done(hitec_prog_t *p, bool had, const hitec_image_t *was);

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

/**
 * @brief Start a write of @p kind to @p target: the gate, the plan, the
 *        backup file, the record, the steps.
 *
 * @param p      The programmer.
 * @param kind   hitec_prog_wr_t.
 * @param target What the servo is to hold.
 * @return HITEC_PROG_MSG_NONE when it starts, else the message said.
 */
hitec_prog_msg_id_t hpw_start(hitec_prog_t *p, uint8_t kind,
                              const hitec_image_t *target);

/**
 * @brief The write whose backup file is saved, or needs none, goes on: its
 *        record, then its steps.
 *
 * @param p The programmer.
 */
void hpw_after_backup(hitec_prog_t *p);

/**
 * @brief The parameters in which @p img differs from the servo read.
 *
 * @param p   The programmer.
 * @param img The image.
 * @return The count.
 */
unsigned hpw_differing(const hitec_prog_t *p, const hitec_image_t *img);

/**
 * @brief Every confirmation of a soft rule ends, and the edit that waits.
 *
 * @param p The programmer.
 */
void hpw_unconfirm(hitec_prog_t *p);

/* --- hitec_prog_file.c ---------------------------------------------------- */

/**
 * @brief A session starts or ends: its backup, its backup file's name and
 *        the load that waits go.  A request of the card out stays out.
 *
 * @param p The programmer.
 */
void hpf_session(hitec_prog_t *p);

/**
 * @brief A full read ended well, its record compared: the first of the
 *        session, or the first of another servo, is the session's backup.
 *
 * A D series read is of the servo before it by the session's chain only:
 * with a write unverified, the record's verdict SAME, DONE or MIX; with
 * none, or its record discarded, @p changed false.  Another servo's read
 * says HITEC_PROG_MSG_OTHER_SERVO where no record stands to say more.
 *
 * @param p       The programmer.
 * @param owed    A write of the session was unverified before this read.
 * @param changed hpw_read_done()'s answer for this read.
 */
void hpf_read_done(hitec_prog_t *p, bool owed, bool changed);

/**
 * @brief Whether @p img differs from the servo read in an address a plan
 *        writes (hitec_writable()), named by a parameter or not.
 *
 * @param p   The programmer.
 * @param img An image of the servo's series.
 * @return true when it does.
 */
bool hpf_writable_differs(const hitec_prog_t *p, const hitec_image_t *img);

/**
 * @brief A write with steps is about to go: its backup file first, where
 *        one is due and a card is mounted.
 *
 * @param p The programmer.
 * @return true when the save went out and the write waits in ST_BACKUP;
 *         false when the write goes on now.
 */
bool hpf_backup(hitec_prog_t *p);

/**
 * @brief Every feed: a request of the card without an answer within
 *        HITEC_PROG_FILES_MS is given up.
 *
 * @param p The programmer.
 */
void hpf_tick(hitec_prog_t *p);

/**
 * @brief ABORT of a write that waits for its backup file: nothing was
 *        written.
 *
 * @param p The programmer.
 * @return true when one waited.
 */
bool hpf_abort(hitec_prog_t *p);
