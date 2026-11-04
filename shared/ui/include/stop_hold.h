/**
 * @file stop_hold.h
 * @brief The hold that resets a latched STOP: a press on the band's STOP that
 *        began with the stop already latched and stays on STOP for UI_HOLD_S
 *        (2s).
 *
 * The press that latches a stop is never also the hold that resets it: the
 * hold begins only on a DOWN the panel's control task marked
 * TOUCH_FLAG_STOP_LATCHED, a stop latched before the press.  The hold
 * completes only on one unbroken contact on STOP for 2s: its release, a
 * slide off STOP, a press that takes the band from it, lost touch events,
 * a touch controller that stops answering (stop_hold_cancel()) and a
 * screen change or anything else that drops the router's press on STOP
 * (stop_hold_tick()'s @p contact) end it.  The DOWN is a STOP all the
 * same: the control task applies it at the press.  The hold
 * fills and flashes as every hold in the tree does (ui_hold_t); a release
 * before 2s shows the hint, and a press that slides off STOP or is lost
 * asks for nothing.  What it asks for is the reset (stop_latch.h), which the
 * control task carries out.
 *
 * The router asks and acts; this file keeps the hold.  Render task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "touch_types.h"

/** @brief Nothing held, nothing asked. */
void stop_hold_reset(void);

/**
 * @brief A DOWN on STOP: the hold starts from zero when @p latched.  A reset
 *        asked and not yet taken is dropped: this press is a later STOP.
 * @param latched a stop was latched before this press
 *        (TOUCH_FLAG_STOP_LATCHED)
 */
void stop_hold_press(bool latched);

/**
 * @brief The press moved: off STOP it is over and asks for nothing.
 * @param x panel x of the contact
 * @param y panel y of the contact
 */
void stop_hold_slide(int x, int y);

/**
 * @brief The press lifted, or was dropped (@p up NULL).  A release on STOP
 *        before 2s shows the hint.
 * @param up the UP of the press, or NULL
 * @return true when it was the press that fired
 */
bool stop_hold_release(const touch_event_t *up);

/** @brief The touch events were lost, or the touch controller stopped
 *         answering: the hold is over, and a reset asked and not yet taken
 *         is dropped. */
void stop_hold_cancel(void);

/**
 * @brief Time passed.  The hold fires at 2s and asks for the reset once; a
 *        stop that is no longer latched under a hold that fired flashes it.
 *        A running hold whose contact is gone ends without asking.
 * @param dt_s    the frame's time, s
 * @param latched the stop is latched now
 * @param contact the router still owns the press on STOP
 * @return true on the frame the hold fires
 */
bool stop_hold_tick(float dt_s, bool latched, bool contact);

/**
 * @brief The reset the hold asked for, once.
 * @return true when one was asked since the last call
 */
bool stop_hold_take_reset(void);

/**
 * @brief How long the press has been held, s; 0 with no hold running.
 * @return the time held
 */
float stop_hold_held_s(void);

/**
 * @brief The flash's time still to run, ms; 0 for none.
 * @return the time left
 */
int stop_hold_flash_ms(void);
