/*
 * SPDX-License-Identifier: MIT
 */

#include "touch_map.h"

#include <string.h>

static int16_t clamp16(int v, int lo, int hi)
{
    if (v < lo) { return (int16_t)lo; }
    if (v > hi) { return (int16_t)hi; }
    return (int16_t)v;
}

void touch_map_size(const touch_map_t *m, int16_t *w, int16_t *h)
{
    if (!m) {
        return;
    }
    bool swap = (m->rotation == TOUCH_ROTATION_90 || m->rotation == TOUCH_ROTATION_270);
    if (w) { *w = swap ? m->src_h : m->src_w; }
    if (h) { *h = swap ? m->src_w : m->src_h; }
}

void touch_map_apply(const touch_map_t *m, int16_t x, int16_t y,
                     int16_t *out_x, int16_t *out_y)
{
    if (!m) {
        return;
    }
    int16_t sw = (m->src_w > 0) ? m->src_w : 1;
    int16_t sh = (m->src_h > 0) ? m->src_h : 1;

    int px = clamp16(x, 0, sw - 1);
    int py = clamp16(y, 0, sh - 1);

    if (m->mirror_x) { px = sw - 1 - px; }
    if (m->mirror_y) { py = sh - 1 - py; }

    int rx, ry;
    switch (m->rotation) {
    case TOUCH_ROTATION_90:
        rx = sh - 1 - py;
        ry = px;
        break;
    case TOUCH_ROTATION_180:
        rx = sw - 1 - px;
        ry = sh - 1 - py;
        break;
    case TOUCH_ROTATION_270:
        rx = py;
        ry = sw - 1 - px;
        break;
    case TOUCH_ROTATION_0:
    default:
        rx = px;
        ry = py;
        break;
    }

    if (out_x) { *out_x = (int16_t)rx; }
    if (out_y) { *out_y = (int16_t)ry; }
}

void touch_map_point(const touch_map_t *m, touch_point_t *p)
{
    if (!m || !p) {
        return;
    }
    touch_map_apply(m, p->x, p->y, &p->x, &p->y);
}

/* ----------------------------------------------------------------- tracker */

void touch_tracker_reset(touch_tracker_t *t)
{
    if (t) {
        memset(t, 0, sizeof(*t));
    }
}

static const touch_point_t *find_by_id(const touch_point_t *pts, int count, uint8_t id)
{
    for (int i = 0; i < count; ++i) {
        if (pts[i].id == id) {
            return &pts[i];
        }
    }
    return NULL;
}

int touch_tracker_update(touch_tracker_t *t,
                         const touch_point_t *cur, int cur_count,
                         touch_event_t *out, int max_out)
{
    if (!t) {
        return 0;
    }
    if (!cur) {
        cur_count = 0;
    }
    if (cur_count < 0) {
        cur_count = 0;
    }
    if (cur_count > TOUCH_MAX_POINTS) {
        cur_count = TOUCH_MAX_POINTS;
    }
    if (!out) {
        max_out = 0;
    }

    int n = 0;

    /* Releases first, so a caller that only looks at the first event still
     * sees the finger lift before any new press. */
    for (int i = 0; i < t->prev_count; ++i) {
        if (!find_by_id(cur, cur_count, t->prev[i].id)) {
            if (n < max_out) {
                out[n].type = TOUCH_EVENT_UP;
                out[n].point = t->prev[i];
            }
            ++n;
        }
    }

    for (int i = 0; i < cur_count; ++i) {
        /* Two contacts sharing a track id in one frame is the controller
         * misbehaving; keep the first and drop the rest, so the pair cannot
         * become two DOWNs now and two UPs later for one finger. */
        if (find_by_id(cur, i, cur[i].id) != NULL) {
            continue;
        }
        const touch_point_t *old = find_by_id(t->prev, t->prev_count, cur[i].id);
        if (old != NULL) {
            int dx = cur[i].x - old->x;
            int dy = cur[i].y - old->y;
            if (dx < 0) { dx = -dx; }
            if (dy < 0) { dy = -dy; }
            if (dx + dy > TOUCH_JUMP_PX) {
                /* Too far for one finger in one frame: report the release the
                 * consumer is waiting for, then the new press. */
                if (n < max_out) {
                    out[n].type = TOUCH_EVENT_UP;
                    out[n].point = *old;
                }
                ++n;
                old = NULL;
            }
        }
        if (!old) {
            if (n < max_out) {
                out[n].type = TOUCH_EVENT_DOWN;
                out[n].point = cur[i];
            }
            ++n;
        } else if (old->x != cur[i].x || old->y != cur[i].y) {
            if (n < max_out) {
                out[n].type = TOUCH_EVENT_MOVE;
                out[n].point = cur[i];
            }
            ++n;
        }
    }

    int kept = 0;
    for (int i = 0; i < cur_count; ++i) {
        if (find_by_id(cur, i, cur[i].id) == NULL) {
            t->prev[kept++] = cur[i];
        }
    }
    t->prev_count = (uint8_t)kept;

    return (n < max_out) ? n : max_out;
}
