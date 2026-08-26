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

typedef struct {
    touch_event_type_t type;
    touch_point_t      point;
} touch_event_t;

#ifdef __cplusplus
}
#endif
