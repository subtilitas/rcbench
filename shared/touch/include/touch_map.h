/*
 * Coordinate mapping and press/move/release tracking.  Pure C; the unit tests
 * are in test/host.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t          src_w;    /**< controller's native X range              */
    int16_t          src_h;    /**< controller's native Y range              */
    bool             mirror_x; /**< applied before rotation                  */
    bool             mirror_y; /**< applied before rotation                  */
    touch_rotation_t rotation; /**< clockwise rotation applied after mirrors */
} touch_map_t;

/** Size of the mapped coordinate space (rotation may swap the axes). */
void touch_map_size(const touch_map_t *m, int16_t *w, int16_t *h);

/**
 * Map one raw controller coordinate into screen space.  Inputs outside the
 * native range are clamped, so callers never see an off-screen point.
 */
void touch_map_apply(const touch_map_t *m, int16_t x, int16_t y,
                     int16_t *out_x, int16_t *out_y);

/** Map a whole point in place (id and strength are untouched). */
void touch_map_point(const touch_map_t *m, touch_point_t *p);

/**
 * @brief Keep the mapped points inside a @p w x @p h area at the origin and drop
 * the rest, in place and in order.  A panel whose UI (user interface) draws
 * a smaller area than the glass reports drops the touches outside it.
 *
 * A contact dragged out of the area leaves the next frame as a release;
 * one dragged back in is a new press.
 *
 * @param pts The mapped points; the kept ones move to the front.
 * @param count How many there are.
 * @param w The area's width in pixels.
 * @param h The area's height in pixels.
 * @return the number of points kept; 0 for a NULL @p pts, a negative
 *         @p count or an empty area.
 */
int touch_map_crop(touch_point_t *pts, int count, int16_t w, int16_t h);

/* ----------------------------------------------------------------- tracker */

typedef struct {
    touch_point_t prev[TOUCH_MAX_POINTS];
    uint8_t       prev_count;
} touch_tracker_t;

void touch_tracker_reset(touch_tracker_t *t);

/**
 * Diff the current contact set against the previous one and emit
 * DOWN/MOVE/UP events.  A MOVE is only produced when the contact actually
 * changed position.  The UP of a contact missing from the frame carries no
 * flag: a lift and a contact the controller left out of one report look the
 * same.  The UP made for a jump carries TOUCH_FLAG_NO_TAP, and the DOWN that
 * follows it TOUCH_FLAG_JUMP.
 *
 * @return number of events written to @p out (never more than @p max_out;
 *         events past the limit are dropped, but the tracker state still
 *         advances so nothing gets stuck "down").
 */
/**
 * A contact that moves further than this between two frames is treated as a
 * lift and a new press rather than as one drag.
 *
 * The GT911 reuses track ids.  When a release frame is missed (a poll that
 * lands between the lift and the next press, or a failed I²C (Inter-Integrated
 * Circuit) read), the reused id looks like the same finger moving across the
 * panel, and the consumer gets a MOVE where it waits for an UP, which leaves
 * a drag latched.  A finger cannot cross 120 px in one 10 ms poll; a reused
 * id can.
 */
#define TOUCH_JUMP_PX 120

int touch_tracker_update(touch_tracker_t *t,
                         const touch_point_t *cur, int cur_count,
                         touch_event_t *out, int max_out);

#ifdef __cplusplus
}
#endif
