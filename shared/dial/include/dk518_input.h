/**
 * @file
 * @brief The Guition JC3636K518C's input, pure: the encoder's two pulse
 *        lines turned into detents, the still touch on the glass that is
 *        the K518's button, and the vibration effects it plays.
 *
 * The encoder is the two-line "bidirectional switch" type, not
 * quadrature: GPIO8 gives one low pulse per clockwise detent, GPIO7 one
 * per anticlockwise detent.  A PCNT (pulse counter) unit per line counts
 * its falling edges.  dk518_enc_step() reads both totals every
 * DIAL_INPUT_MS (5ms): a line whose total moved after a step without an
 * edge on it is one detent; edges in consecutive steps are one pulse and
 * its bounce.  The quiet step between two pulses is the 4ms debounce of
 * Guition's code, rounded up to the 5ms step.  Limit: two detents of one
 * line less than 10ms apart (more than 100 detents/s) count as one.
 *
 * The still touch (owner's answer Q2): a finger held still on the glass
 * is the screen button of shared/dial/dial_button.c.
 * - Lifted before 1,000ms (DK_HOLD_MS) with no move over 20px
 *   (DK518_STILL_PX) from where it started: PRESS.
 * - Down 1,000ms: HOLD; 5,000ms the switch-off's warning, 10,000ms the
 *   forget, as on the M5Stack Dial.
 * - A move over 20px before 1,000ms ends the press: it goes nowhere,
 *   neither PRESS nor HOLD.  From 1,000ms the finger may move.  In the
 *   switch-off's choice the press stays the choice's and answers on its
 *   lift, moved or not.
 * - Lifted within 500ms (DK518_TAP_MS) with no move over 20px: also a TAP
 *   at the point it started, as the M5Stack Dial's glass sends one beside
 *   the screen button's PRESS.
 *
 * The update's confirming key (dk518_key_step(), dk518_key_down()) is
 * made from the still touch's events, never from the raw touch, since the
 * update's own rule (bleota) takes any short down and up as its accept and
 * any 1,000ms down as its refusal.  Only a touch that starts while the
 * offer is open (bleota's CONFIRM) moves the key:
 * - A still lift before 1,000ms: the key goes down for 50ms
 *   (DK518_KEY_PRESS_MS), which the `ota` task's 10ms polls with their
 *   2-poll debounce take as a press: the accept.
 * - Held still for 1,000ms: the key goes down until the lift, and for
 *   1,100ms (DK518_KEY_HOLD_MS) at least, so bleota's 1,000ms hold ends in
 *   the refusal whenever the finger lifts.
 * - Moved over 20px before 1,000ms: the key stays up.
 * - A window never shortens or merges: a still lift while a window runs,
 *   or within 30ms (DK518_KEY_GAP_MS) of its end, moves nothing.
 * - The offer opening or closing (an answer, its time out, a lost link),
 *   or another offer in its place (bleota's offer count), ends the
 *   window, and the touch under way moves nothing until it lifts; the key
 *   is up while no offer is open.
 * - The face must have sent that offer's question whole before a touch
 *   can count for it: an empty read of the glass counts only while the
 *   question for the offer standing is on the display (@p shown of
 *   dk518_key_step()), so no tap aimed at an earlier screen answers it.
 *   While another face covers the question (the battery's EMPTY), the
 *   key disarms: the touch under way answers nothing, and the glass must
 *   read empty again after the question returns.
 * - The offer and its identity are read before the touch controller is,
 *   on every step, and a touch counts for an offer only when a read of
 *   the glass found no finger while that offer already stood: so a
 *   finger that came down before the offer opened, however close, never
 *   answers it.
 * - The key names the offer its window was opened in
 *   (dk518_key_offer()); the `ota` task takes it for that offer alone
 *   (bleota_dev_key_for()), so an offer that replaces another between two
 *   steps gets no key from the first one's touch.
 *
 * A read of the touch controller has three results (dk518_read_t):
 * touched, not touched, failed.  dk518_glass_read() keeps the last
 * state through a failed read, so an I2C (inter-integrated circuit)
 * error never ends a press and never counts as an empty glass.  The
 * 5th failed read in a row (DK518_READ_FAILS, 100ms at the 20ms reads)
 * cancels the touch under way: dk518_still_lost() ends it without PRESS,
 * TAP or the update's accept.  A finger read after that is the same
 * touch and starts nothing until a read finds the glass empty.
 *
 * Points are the panel's native 0 to 359; EVENT carries 0 to 239
 * (dk518_touch_scale()).
 *
 * Pure C: no clock read, no I/O; the host suite runs the same code.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dial_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A move over this many native px ends a press, px. */
#define DK518_STILL_PX 20
/** @brief A lift within this long is also a TAP, ms. */
#define DK518_TAP_MS 500u
/** @brief The confirming key's press after a still lift, ms. */
#define DK518_KEY_PRESS_MS 50u
/** @brief The confirming key's least time down after a still hold, ms. */
#define DK518_KEY_HOLD_MS 1100u
/** @brief The confirming key's least time up between two windows, ms:
 *         three of the `ota` task's 10ms polls. */
#define DK518_KEY_GAP_MS 30u
/** @brief Failed reads of the touch controller in a row that cancel the
 *         touch under way. */
#define DK518_READ_FAILS 5u
/** @brief The panel's native size, px. */
#define DK518_TOUCH_SIZE 360u

/** @brief The DRV2605's ROM effects (library 6, LRA) the K518 plays. @{ */
#define DK518_FX_TICK   26u /**< "Sharp Tick 3": a detent */
#define DK518_FX_PRESS  4u  /**< "Sharp Click 100%": PRESS taken */
#define DK518_FX_STRONG 1u  /**< "Strong Click 100%": HOLD taken, an
                                 update accepted, a tone */
#define DK518_FX_BUZZ   47u /**< "Buzz 1 100%": the 5s warning */
#define DK518_FX_DOUBLE 10u /**< "Double Click 100%": the forget, twice */
/** @} */

/** @brief One read of the touch controller. */
typedef enum {
    DK518_READ_UP = 0, /**< read: no finger on the glass */
    DK518_READ_DOWN,   /**< read: a finger, at the point given */
    DK518_READ_FAIL,   /**< the read failed: nothing is known */
} dk518_read_t;

/** @brief The glass as the reads tell it. */
typedef struct {
    bool     down;  /**< a finger is on the glass, as last known */
    uint16_t x;     /**< its column, native px */
    uint16_t y;     /**< its row, native px */
    uint8_t  fails; /**< failed reads in a row */
    bool     lost;  /**< a touch was lost and no read found the glass
                         empty since: a finger read now is still that
                         touch, not a new one */
} dk518_glass_t;

/** @brief The encoder's decode. */
typedef struct {
    bool    primed;  /**< the first totals were taken */
    int32_t cw;      /**< the clockwise line's last total */
    int32_t ccw;     /**< the anticlockwise line's last total */
    bool    cw_run;  /**< the last step saw a clockwise edge */
    bool    ccw_run; /**< the last step saw an anticlockwise edge */
} dk518_enc_t;

/** @brief The still touch: the button's state and the touch's. */
typedef struct {
    dial_button_t button;  /**< PRESS, HOLD, the switch-off, the forget */
    bool          down;    /**< a finger is on the glass */
    bool          moved;   /**< it moved over DK518_STILL_PX */
    uint16_t      x0;      /**< where it started, native px */
    uint16_t      y0;      /**< where it started, native px */
    uint32_t      down_ms; /**< when it started */
    bool          held;    /**< this touch was held still 1,000ms */
} dk518_still_t;

/** @brief The update's confirming key. */
typedef struct {
    bool     offer;    /**< the offer is open, as the last step saw */
    uint32_t id;       /**< its identity, bleota's offer count */
    bool     clear;    /**< a read found the glass empty while this offer
                            stood */
    bool     touch_ok; /**< the touch under way started in this offer */
    bool     hold;     /**< its window lasts until the touch lifts */
    bool     on;       /**< a window was opened in this offer */
    uint32_t until;    /**< the window's end, past the lift */
} dk518_key_t;

/** @brief What a step of the still touch gives beside DIAL_BTN_* bits. */
typedef struct {
    bool    tap;   /**< a TAP: lifted within DK518_TAP_MS, still */
    uint8_t tap_x; /**< TAP: its column, 0 to 239 */
    uint8_t tap_y; /**< TAP: its row, 0 to 239 */
    bool    began; /**< a finger came down this step */
    bool    held;  /**< the touch reached DK_HOLD_MS still this step */
    bool    lifted; /**< the finger lifted this step */
    bool    still_lift; /**< lifted before DK_HOLD_MS, still: the press
                             an update's offer takes as its accept */
} dk518_still_out_t;

/** @brief Start the decode with no totals.  @param e the decode */
void dk518_enc_init(dk518_enc_t *e);

/**
 * @brief One step: both lines' falling-edge totals, as their PCNT units
 *        count them.  The first step takes the totals and turns nothing.
 * @param e   the decode
 * @param cw  the clockwise line's total
 * @param ccw the anticlockwise line's total
 * @return detents since the last step, clockwise positive
 */
int32_t dk518_enc_step(dk518_enc_t *e, int32_t cw, int32_t ccw);

/** @brief Start with no finger on the glass.  @param s the state */
void dk518_still_init(dk518_still_t *s);

/**
 * @brief One step, every DIAL_INPUT_MS: the touch as last read.
 * @param s      the state
 * @param touch  a finger is on the glass
 * @param x      its column, native px; read while @p touch
 * @param y      its row, native px; read while @p touch
 * @param turns  detents since the last step, for the switch-off's choice
 * @param yield  an update's offer has the screen: dial_button_yield()
 *               instead of dial_button_step()
 * @param now    the time, ms
 * @param out    the TAP and the still lift; may be NULL
 * @return DIAL_BTN_* bits, 0 for nothing (always 0 while @p yield)
 */
unsigned dk518_still_step(dk518_still_t *s, bool touch, uint16_t x,
                          uint16_t y, int32_t turns, bool yield,
                          uint32_t now, dk518_still_out_t *out);

/** @brief Start with no finger and no failed read.  @param g the glass */
void dk518_glass_init(dk518_glass_t *g);

/**
 * @brief Take one read of the touch controller.
 * @param g the glass
 * @param r the read's result
 * @param x the finger's column, native px; read for DK518_READ_DOWN
 * @param y its row, native px; read for DK518_READ_DOWN
 * @return true when this read is the DK518_READ_FAILS-th failure in a row
 *         while a finger was down: the touch is lost, g->down is false
 *         until a read finds the glass empty, and the caller ends the
 *         touch with dk518_still_lost()
 */
bool dk518_glass_read(dk518_glass_t *g, dk518_read_t r, uint16_t x,
                      uint16_t y);

/**
 * @brief End the touch under way without an answer: its lift, in the next
 *        dk518_still_step(), gives no PRESS, no TAP and no still lift,
 *        and the button takes it as yielded.  No effect without a finger.
 * @param s   the state
 * @param now the time, ms
 */
void dk518_still_lost(dk518_still_t *s, uint32_t now);

/** @brief Start with the key up and no offer.  @param k the key */
void dk518_key_init(dk518_key_t *k);

/**
 * @brief One step of the update's confirming key, after
 *        dk518_still_step().
 * @param k     the key
 * @param offer the offer is open (bleota's CONFIRM), read before this
 *              step's touch read
 * @param id    its identity (bleota_dev_offer()), read with @p offer
 * @param shown the face sent the question of offer @p id whole and no
 *              other face has covered it since, read with @p offer
 * @param empty this step read the touch controller, after @p offer, and
 *              found no finger
 * @param o     the still touch's events of this step
 * @param now   the time, ms
 * @return true when this step opened a press window: the accept's cue
 */
bool dk518_key_step(dk518_key_t *k, bool offer, uint32_t id, bool shown,
                    bool empty, const dk518_still_out_t *o, uint32_t now);

/**
 * @brief The offer the key belongs to, for bleota_dev_key_for().
 * @param k the key
 * @return the identity the last step saw
 */
uint32_t dk518_key_offer(const dk518_key_t *k);

/**
 * @brief The update's confirming key, for the `ota` task.
 * @param k   the key
 * @param now the time, ms
 * @return true while it is down
 */
bool dk518_key_down(const dk518_key_t *k, uint32_t now);

/**
 * @brief A native coordinate on EVENT's scale.
 * @param v 0 to 359; more counts as 359
 * @return v * 2 / 3, 0 to 239
 */
uint8_t dk518_touch_scale(uint16_t v);

#ifdef __cplusplus
}
#endif
