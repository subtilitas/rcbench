/*
 * Plain data types shared by the touch controller driver, the coordinate
 * mapper and the event tracker.  No ESP-IDF dependencies, so the pure logic
 * can be unit-tested on the host.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The GT911 reports at most five simultaneous contacts. */
#define TOUCH_MAX_POINTS 5

typedef struct {
    uint8_t  id;       /**< contact/track id assigned by the controller */
    int16_t  x;
    int16_t  y;
    uint16_t strength; /**< contact area as reported by the controller  */
} touch_point_t;

typedef enum {
    TOUCH_ROTATION_0 = 0,
    TOUCH_ROTATION_90,
    TOUCH_ROTATION_180,
    TOUCH_ROTATION_270,
} touch_rotation_t;

typedef enum {
    TOUCH_EVENT_DOWN = 0,
    TOUCH_EVENT_MOVE,
    TOUCH_EVENT_UP,
} touch_event_type_t;

/**
 * On an UP: the contact ends for its owner, and the finger did not lift at
 * that point.  The tracker sets it on the UP it makes when it splits a jump
 * of more than TOUCH_JUMP_PX, the router on the UP it makes for a contact
 * that reaches the band.  Such an UP ends a press or a drag and activates no
 * control.
 */
#define TOUCH_FLAG_NO_TAP 0x01u

/**
 * On a DOWN: the tracker made it for a contact that jumped more than
 * TOUCH_JUMP_PX between two reports.  It is a new press whose predecessor's
 * release went missing, or a finger that travels that fast; the tracker
 * cannot tell which.  A control in a list that scrolls by a drag takes such
 * a DOWN for the drag and not for a press on it.  Every other control takes
 * it as a press.
 */
#define TOUCH_FLAG_JUMP   0x02u

typedef struct {
    touch_event_type_t type;
    touch_point_t      point;
    uint8_t            flags;   /**< TOUCH_FLAG_*; 0 on a MOVE */
} touch_event_t;

/** Whether @p evt is an UP that may activate the control it ends on. */
static inline bool touch_event_is_tap_up(const touch_event_t *evt)
{
    return evt->type == TOUCH_EVENT_UP
           && (evt->flags & TOUCH_FLAG_NO_TAP) == 0u;
}

#ifdef __cplusplus
}
#endif
