/**
 * @file shot_hold.h
 * @brief The screenshot's hold: every contact on the glass, and the one
 *        contact that stays in the band's gap for 1000ms.
 *
 * The router asks and acts: this file keeps the table and the hold, and asks
 * for the picture. Render task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "touch_types.h"

/** @brief Forget the glass and the hold, and what the hold began over. */
void shot_hold_reset(void);

/**
 * @brief Keep the glass's table, and end the hold on anything that is not its
 *        contact staying in the gap: its release, another contact's event of
 *        any kind, a DOWN of its id (a new contact), a slide out of the gap.
 *        A DOWN meets the guide or a bubble a hold began over by their own
 *        rules.
 * @param evt  the event
 */
void shot_hold_feed(const touch_event_t *evt);

/**
 * @brief A press came down on the band and no control of it: the hold starts
 *        when it is the only contact, in the gap.
 * @param evt  the DOWN
 * @return true when the hold started
 */
bool shot_hold_begin(const touch_event_t *evt);

/**
 * @brief A press came down over the guide or a bubble: in the gap, alone, it
 *        starts the hold and leaves them up for the picture.  The popup ends
 *        by its own keys alone and takes no hold.
 * @param evt    the DOWN
 * @param popup  the popup is open
 * @return true when the hold started
 */
bool shot_hold_over(const touch_event_t *evt, bool popup);

/**
 * @brief The hold, a frame on: it ends where the gap no longer holds the
 *        contact, a chip that appeared under it included, and asks for the
 *        picture at 1000ms.  A late frame counts for no more than any hold's
 *        (ui_hold_credit()).
 * @param dt_s  the frame's time, s
 */
void shot_hold_tick(float dt_s);

/**
 * @brief Whether the guide or the bubble a hold began over closes now: the
 *        hold has ended and any picture it asked for is taken.  Answers true
 *        once.
 * @return true when the router closes them
 */
bool shot_hold_settled(void);

/** @brief End the hold; the glass stays as it is. */
void shot_hold_end(void);

/** @brief The touch events were lost: the hold is over and the glass empty. */
void shot_hold_lost(void);

/**
 * @brief No contact is on the glass.
 * @return true when none is
 */
bool shot_hold_glass_clear(void);
