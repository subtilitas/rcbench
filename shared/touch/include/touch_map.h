/**
 * @file
 * @brief Coordinate mapping and press/move/release tracking.  Pure C; the unit tests
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

/**
 * @brief Size of the mapped coordinate space (rotation may swap the axes).
 * @param m The map; NULL writes nothing.
 * @param w Where the width goes, or NULL.
 * @param h Where the height goes, or NULL.
 */
void touch_map_size(const touch_map_t *m, int16_t *w, int16_t *h);

/**
 * @brief Map one raw controller coordinate into screen space.  Inputs
 *        outside the native range are clamped, so callers never see an
 *        off-screen point.
 * @param m The map; NULL writes nothing.
 * @param x The raw x.
 * @param y The raw y.
 * @param out_x Where the mapped x goes, or NULL.
 * @param out_y Where the mapped y goes, or NULL.
 */
void touch_map_apply(const touch_map_t *m, int16_t x, int16_t y,
                     int16_t *out_x, int16_t *out_y);

/**
 * @brief Map a whole point in place (id and strength are untouched).
 * @param m The map; NULL leaves the point.
 * @param p The point; NULL maps nothing.
 */
void touch_map_point(const touch_map_t *m, touch_point_t *p);

/**
 * @brief The map @p m with the picture turned by 180 degrees on top of it:
 *        a point @p m maps to (x, y) maps to (w - 1 - x, h - 1 - y), with
 *        w x h touch_map_size()'s area.
 *
 * The panel turns its picture by its scan direction (display.h), so the
 * frame buffer, and every screen with it, keeps its logical pixels; the
 * glass's map turns with it and the touch injector's does not, since the
 * injector names logical pixels.
 *
 * @param m The map; NULL gives a zeroed map.
 * @param turned True for the turned map; false gives @p m as it is.
 * @return The map, rotation advanced by TOUCH_ROTATION_180 when turned.
 */
touch_map_t touch_map_turned(const touch_map_t *m, bool turned);

/**
 * @brief Map @p n points in place, each as touch_map_point() maps it.
 *
 * Each source is mapped with its own map before the sources are merged:
 * the glass from the resolution it reports, with the board's mirrors; the
 * touch injector as panel pixels of the UI's area, unmirrored.
 *
 * @param m The map; NULL leaves the points as they are.
 * @param pts The points.
 * @param n How many; 0 or less maps nothing.
 */
void touch_map_points(const touch_map_t *m, touch_point_t *pts, int n);

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

/** @brief The contacts of the last frame, to diff the next against. */
typedef struct {
    touch_point_t prev[TOUCH_MAX_POINTS];  /**< the last frame's contacts */
    uint8_t       prev_count;              /**< how many                  */
} touch_tracker_t;

/**
 * @brief Forget every contact: the next frame's are all new.
 * @param t The tracker; NULL does nothing.
 */
void touch_tracker_reset(touch_tracker_t *t);

/**
 * @brief A contact that moves further than this between two frames is treated as a
 * lift and a new press rather than as one drag.
 *
 * The GT911 reuses track ids.  When a release frame is missed (a poll that
 * lands between the lift and the next press, or a failed I²C (Inter-Integrated
 * Circuit) read), the reused id looks like the same finger moving across the
 * panel, and the consumer gets a MOVE where it waits for an UP, which leaves
 * a drag latched.  A finger cannot cross 120px in one 10ms poll; a reused
 * id can.
 */
#define TOUCH_JUMP_PX 120

/**
 * @brief Diff the current contact set against the previous one and emit
 * DOWN/MOVE/UP events.
 *
 * A MOVE is only produced when the contact actually changed position.  The
 * UP of a contact missing from the frame carries no flag: a lift and a
 * contact the controller left out of one report look the same.  The UP
 * made for a jump carries TOUCH_FLAG_NO_TAP, and the DOWN that follows it
 * TOUCH_FLAG_JUMP.
 *
 * @param t The tracker; NULL gives 0.
 * @param cur This frame's contacts; NULL for none.
 * @param cur_count How many, at most TOUCH_MAX_POINTS taken.
 * @param out Where the events go; NULL for none.
 * @param max_out Its room.
 * @return number of events written to @p out (never more than @p max_out;
 *         events past the limit are dropped, but the tracker state still
 *         advances so nothing gets stuck "down").
 */
int touch_tracker_update(touch_tracker_t *t,
                         const touch_point_t *cur, int cur_count,
                         touch_event_t *out, int max_out);

#ifdef __cplusplus
}
#endif
