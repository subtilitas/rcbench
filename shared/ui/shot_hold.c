/**
 * @file shot_hold.c
 * @brief The screenshot's hold (shot_hold.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "shot_hold.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gfx.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_shot.h"
#include "ui_widgets.h"

/** @brief How long the hold on the band's gap takes, ms. */
#define SHOT_HOLD_MS 1000u

/** @brief The hold's state, render task only. */
static struct {
    /**
     * @brief Every contact on the glass, whoever owns it, by track id: what a
     * screenshot's hold takes for a second contact, and what the popup
     * waits for to lift.  Set at a DOWN, cleared at its UP and at a touch
     * loss.
     */
    uint8_t  glass[256u / 8u];
    bool     hold;   /**< the hold runs                                  */
    uint8_t  id;     /**< its contact                                    */
    int16_t  x;      /**< where its contact last was, panel x            */
    int16_t  y;      /**< and panel y                                    */
    uint32_t ms;     /**< how long it has run, ms                        */
    bool     over;   /**< it began over the guide or a bubble, which stay
                          for the picture and close after it             */
} s;

void shot_hold_reset(void) { memset(&s, 0, sizeof(s)); }

/**
 * @brief Whether the point is on the empty part of the band, this moment.
 * @param x  panel x
 * @param y  panel y
 * @return true on the gap, on a screen with the band
 */
static bool gap_at(int x, int y)
{
    gfx_rect_t gap;
    return ui_router_shot_gap(&gap) && gfx_rect_contains(gap, x, y)
           && ui_band_chip_at(ui_router_status(), x, y) == UI_BAND_CHIP_NONE
           && !ui_band_guide_at(x, y)
           && !gfx_rect_contains(ui_band_stop_rect(), x, y);
}

bool shot_hold_glass_clear(void)
{
    for (size_t i = 0u; i < sizeof(s.glass); ++i) {
        if (s.glass[i] != 0u) {
            return false;
        }
    }
    return true;
}

void shot_hold_feed(const touch_event_t *evt)
{
    const uint8_t id  = evt->point.id;
    const uint8_t bit = (uint8_t)(1u << (id & 7u));
    if (evt->type == TOUCH_EVENT_DOWN) {
        s.glass[id >> 3] |= bit;
        s.over = false;
    } else if (evt->type == TOUCH_EVENT_UP) {
        s.glass[id >> 3] &= (uint8_t)~bit;
    }
    if (!s.hold) {
        return;
    }
    if (id != s.id || evt->type != TOUCH_EVENT_MOVE
        || !gap_at(evt->point.x, evt->point.y)) {
        s.hold = false;
        return;
    }
    s.x = evt->point.x;
    s.y = evt->point.y;
}

bool shot_hold_begin(const touch_event_t *evt)
{
    const uint8_t id  = evt->point.id;
    const uint8_t bit = (uint8_t)(1u << (id & 7u));
    s.glass[id >> 3] &= (uint8_t)~bit;
    const bool alone = shot_hold_glass_clear();
    s.glass[id >> 3] |= bit;
    if (alone && (evt->flags & TOUCH_FLAG_JUMP) == 0u
        && gap_at(evt->point.x, evt->point.y)) {
        s.hold = true;
        s.id   = id;
        s.x    = evt->point.x;
        s.y    = evt->point.y;
        s.ms   = 0u;
        return true;
    }
    return false;
}

bool shot_hold_over(const touch_event_t *evt, bool popup)
{
    if (popup || !shot_hold_begin(evt)) {
        return false;
    }
    s.over = true;
    return true;
}

void shot_hold_tick(float dt_s)
{
    if (!s.hold) {
        return;
    }
    if (!gap_at(s.x, s.y)) {
        s.hold = false;
        return;
    }
    s.ms += (uint32_t)(ui_hold_credit(dt_s) * 1000.0f + 0.5f);
    if (s.ms >= SHOT_HOLD_MS) {
        s.hold = false;
        ui_shot_want();
    }
}

bool shot_hold_settled(void)
{
    if (s.over && !s.hold && !ui_shot_wanted()) {
        s.over = false;
        return true;
    }
    return false;
}

void shot_hold_end(void) { s.hold = false; }

void shot_hold_lost(void)
{
    s.hold = false;
    memset(s.glass, 0, sizeof(s.glass));
}
