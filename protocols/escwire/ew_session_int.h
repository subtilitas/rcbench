/**
 * @file ew_session_int.h
 * @brief What the session's two source files share: its phases, operations and
 *        actions, and the moves both make.  Not for callers of the module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_SESSION_INT_H
#define EW_SESSION_INT_H

#include "ew_session.h"

/** @brief The session's phases. */
enum {
    PH_IDLE = 0,  /**< no operation; the last result stands         */
    PH_HELD,      /**< the line high, waiting for ew_session_enter() */
    PH_HOLD,      /**< a hold before a first contact or `info` runs  */
    PH_XFER,      /**< a transaction runs                            */
    PH_LEAVE_IDLE, /**< the line idle after a leave frame            */
    PH_LOW,       /**< the line low after a leave                    */
};

/** @brief The operations. */
enum { OP_NONE = 0, OP_ENTER, OP_READ, OP_WRITE, OP_LEAVE };

/** @brief The actions.  Every list ends in one that ends the operation. */
enum {
    A_CONTACT = 0,
    A_SET_BASE,
    A_SET_NAME,
    A_SET_RECORD,
    A_READ_ID,
    A_READ_NAME,
    A_READ_RECORD,
    A_READ_PAGE,
    A_CHECK_BEFORE,
    A_ERASE,
    A_DATA,
    A_PROGRAM,
    A_CHECK_AFTER,
    A_LEAVE,
    A_FINISH_ID,
    A_FINISH_READ,
    A_TEXT_INFO,
    A_TEXT_SHOW,
    A_TEXT_SET,
    A_TEXT_SAVE,
};

/**
 * @brief An operation ends.  A stop that waited for it parks the line now.
 * @param s the session
 * @param r the result
 */
void ew_ses_finish(ew_session_t *s, ew_ses_t r);

/**
 * @brief The guard refused what the session built: the session stops.
 * @param s the session
 * @param r the refusal
 */
void ew_ses_refused(ew_session_t *s, ew_refuse_t r);

/**
 * @brief Hand @c x to the driver and start its clock.
 * @param s the session
 */
void ew_ses_launch(ew_session_t *s);

/**
 * @brief A try of the running action failed.
 * @param s the session
 * @param a the action
 */
void ew_ses_try_failed(ew_session_t *s, uint8_t a);

/**
 * @brief Send the line of `info`, `show` or `save`, guarded.
 * @param s the session
 * @param a the action
 */
void ew_ses_text_act(ew_session_t *s, uint8_t a);

/**
 * @brief Send the `set` of the first key that differs, guarded.
 * @param s the session
 * @return false when no key differs and nothing was sent
 */
bool ew_ses_text_set(ew_session_t *s);

/**
 * @brief A transaction of the text interface ended: judge its reply.
 * @param s the session
 * @param a the action
 * @param c its capture
 */
void ew_ses_text_done(ew_session_t *s, uint8_t a, const ew_capture_t *c);

#endif /* EW_SESSION_INT_H */
