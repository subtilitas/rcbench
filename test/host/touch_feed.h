/*
 * Touch input as the panel produces it: frames of contacts (id, x, y), one
 * frame per controller report, put through touch_tracker_update() and every
 * event it emits handed on in order.  The firmware's path is the same:
 * the GT911 driver's report, the tracker, then ui_router_event().
 *
 * No event is written by hand, so a sequence the tracker never emits cannot
 * be tested by accident: a contact that travels more than TOUCH_JUMP_PX
 * between two reports arrives as an UP and a DOWN, and a contact missing
 * from a report arrives as an UP where it was last reported.
 *
 * Coordinates are the panel's.  The events go to ui_router_event(), or to
 * one screen's event() with the band's height removed (feed_to_screen()),
 * which is what the router hands a screen.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "touch_map.h"
#include "ui_screen.h"

/* The track id a lone finger is given on every touch.  The cases that need
 * two touches to share an id assume the controller reuses it; which id the
 * GT911 gives is not measured on a panel. */
#define FEED_LONE 0u

static touch_tracker_t    feed_trk;
static touch_point_t      feed_cur[TOUCH_MAX_POINTS];
static int                feed_ncur;
static const ui_screen_t *feed_scr;        /* NULL: the router            */
static int                feed_lose;       /* events still to be lost     */
static int                feed_downs, feed_moves, feed_ups;

/* A fresh controller with nothing on the glass, feeding the router. */
static inline void feed_reset(void)
{
    touch_tracker_reset(&feed_trk);
    feed_ncur = 0;
    feed_scr  = NULL;
    feed_lose = 0;
    feed_downs = feed_moves = feed_ups = 0;
}

/* Hand the events to @p scr itself, in its own coordinates, and not to the
 * router: a screen's own rules, with no router in front of them. */
static inline void feed_to_screen(const ui_screen_t *scr) { feed_scr = scr; }

/* The next @p n events the tracker emits reach nobody, as when the panel's
 * queue is full.  The tracker has emitted them all the same. */
static inline void feed_lose_next(int n) { feed_lose = n; }

/* One controller report: the contacts on the glass at this moment. */
static inline void feed_report(void)
{
    touch_event_t ev[TOUCH_MAX_POINTS * 2];
    const int n = touch_tracker_update(&feed_trk, feed_cur, feed_ncur, ev,
                                       (int)(sizeof(ev) / sizeof(ev[0])));
    for (int i = 0; i < n; ++i) {
        if (ev[i].type == TOUCH_EVENT_DOWN) { ++feed_downs; }
        if (ev[i].type == TOUCH_EVENT_MOVE) { ++feed_moves; }
        if (ev[i].type == TOUCH_EVENT_UP)   { ++feed_ups; }
        if (feed_lose > 0) {
            --feed_lose;
            continue;
        }
        if (feed_scr == NULL) {
            ui_router_event(&ev[i]);
        } else if (feed_scr->event != NULL) {
            touch_event_t local = ev[i];
            local.point.y = (int16_t)(local.point.y - UI_BAND_H);
            feed_scr->event(&local);
        }
    }
}

static inline touch_point_t *feed_find(uint8_t id)
{
    for (int i = 0; i < feed_ncur; ++i) {
        if (feed_cur[i].id == id) {
            return &feed_cur[i];
        }
    }
    return NULL;
}

/* A finger arrives at (x, y), or is reported there next: one report. */
static inline void finger(uint8_t id, int x, int y)
{
    touch_point_t *p = feed_find(id);
    if (p == NULL) {
        if (feed_ncur >= TOUCH_MAX_POINTS) {
            return;
        }
        p = &feed_cur[feed_ncur++];
        p->id = id;
        p->strength = 40;
    }
    p->x = (int16_t)x;
    p->y = (int16_t)y;
    feed_report();
}

/* The finger travels to (x, y), at most @p step px per axis per report. */
static inline void glide(uint8_t id, int x, int y, int step)
{
    touch_point_t *p = feed_find(id);
    while (p != NULL && (p->x != x || p->y != y)) {
        int dx = x - p->x, dy = y - p->y;
        if (dx > step)  { dx = step; }
        if (dx < -step) { dx = -step; }
        if (dy > step)  { dy = step; }
        if (dy < -step) { dy = -step; }
        p->x = (int16_t)(p->x + dx);
        p->y = (int16_t)(p->y + dy);
        feed_report();
    }
}

/* The finger leaves the glass: the next report no longer carries it. */
static inline void lift(uint8_t id)
{
    touch_point_t *p = feed_find(id);
    if (p != NULL) {
        *p = feed_cur[--feed_ncur];
    }
    feed_report();
}

/* Down and up in one place. */
static inline void feed_tap(uint8_t id, int x, int y)
{
    finger(id, x, y);
    lift(id);
}
