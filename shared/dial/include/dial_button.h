/**
 * @file
 * @brief The Dial's screen button: PRESS and HOLD for the panel, and on the
 *        Dial itself the switch-off with its choice and the forget of the
 *        bound panel.
 *
 * The screen button is the Dial's WAKE button as well: while it is down
 * the Dial has power, so the cell's power can only drop once it is up.
 *
 * - Released before 1,000ms (DK_HOLD_MS): PRESS for the panel.
 * - Held 1,000ms: HOLD for the panel, once, while it is down.
 * - Held 5,000ms (DIAL_OFF_MS): the warning starts (DIAL_BTN_WARN, a
 *   beep); the face blinks red, DIAL_OFF_BLINK_MS red and as long dark.
 * - Released from 5,000ms and before 10,000ms: the choice OFF / KEEP ON,
 *   the focus on KEEP ON.  A clockwise detent moves the focus to OFF, an
 *   anticlockwise one back to KEEP ON.  A press takes the focused one;
 *   a hold of 1,000ms, or DIAL_OFF_ASK_MS without a turn or a button
 *   edge, takes KEEP ON.
 * - Held 10,000ms (DK_FORGET_MS): the bound panel is forgotten
 *   (DIAL_BTN_FORGET); the warning ends, no choice follows.
 *
 * While the warning or the choice is up (dial_button_local()) no turn
 * and no gesture of the screen button goes to the panel; a press that
 * starts in the choice stays the choice's until it is released.
 *
 * Pure C: no clock read, no I/O.  The input task (firmware/dial) calls
 * dial_button_step() every 5ms; the host suite runs the same code.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dial_face.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A hold this long, ms, starts the switch-off's warning. */
#define DIAL_OFF_MS 5000u
/** @brief The warning's blink: red this long, ms, then dark as long. */
#define DIAL_OFF_BLINK_MS 500u
/** @brief The choice takes KEEP ON after this long without a turn or a
 *         button edge, ms. */
#define DIAL_OFF_ASK_MS 10000u

/** @brief What a step asks the caller to do, OR-ed. @{ */
#define DIAL_BTN_PRESS  0x01u /**< send PRESS to the panel */
#define DIAL_BTN_HOLD   0x02u /**< send HOLD to the panel */
#define DIAL_BTN_WARN   0x04u /**< the warning starts: beep */
#define DIAL_BTN_FORGET 0x08u /**< forget the bound panel */
#define DIAL_BTN_OFF    0x10u /**< the choice took OFF: switch off */
#define DIAL_BTN_KEEP   0x20u /**< the choice took KEEP ON */
/** @} */

/** @brief The screen button's state. */
typedef struct {
    uint8_t  stage;     /**< DIAL_OFF_NONE, DIAL_OFF_WARN or DIAL_OFF_ASK */
    bool     down;      /**< the button is down */
    bool     hold_sent; /**< HOLD went for this press */
    bool     forgot;    /**< the bind was forgotten for it */
    bool     own;       /**< the press started in the choice: none of it
                             goes to the panel */
    bool     focus_off; /**< the choice's focus is on OFF */
    uint32_t down_ms;   /**< when the button went down */
    uint32_t warn_ms;   /**< when the warning started */
    uint32_t idle_ms;   /**< the choice's last turn or button edge */
} dial_button_t;

/** @brief Start with the button up and nothing on the face.
 *  @param b the state */
void dial_button_init(dial_button_t *b);

/**
 * @brief One step: the button's debounced level, the detents turned since
 *        the last step, and the time.
 * @param b     the state
 * @param down  the button is down
 * @param turns detents since the last step, clockwise positive; only the
 *              choice reads them
 * @param now   the time, ms
 * @return DIAL_BTN_* bits, 0 for nothing
 */
unsigned dial_button_step(dial_button_t *b, bool down, int32_t turns,
                          uint32_t now);

/**
 * @brief The update's offer took the screen and the screen button: the
 *        warning or the choice ends without an answer, and the press under
 *        way, or one that starts while this is called, goes nowhere until
 *        it is released.  Called every step instead of dial_button_step()
 *        while the update runs.
 * @param b    the state
 * @param down the button is down
 * @param now  the time, ms
 */
void dial_button_yield(dial_button_t *b, bool down, uint32_t now);

/**
 * @brief Whether the warning or the choice is up: turns and the screen
 *        button stay on the Dial.
 * @param b the state
 * @return true while they do
 */
bool dial_button_local(const dial_button_t *b);

/**
 * @brief Fill the view's switch-off fields: off, off_lit, off_focus and
 *        off_secs.
 * @param b   the state
 * @param now the time, ms
 * @param v   the view
 */
void dial_button_view(const dial_button_t *b, uint32_t now,
                      dial_face_view_t *v);

#ifdef __cplusplus
}
#endif
