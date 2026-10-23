/**
 * @file
 * @brief Screen rotation 180° in the flush.  See include/gfx_turn.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx_turn.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/** @brief What the panel shows (gfx_turn_shown()): written by the task that
 *         flips, read by the touch task. */
static atomic_bool s_shown;

void gfx_turn_init(gfx_turn_t *t, int width, int height)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof(*t));
    t->width = (int16_t)width;
    t->height = (int16_t)height;
    atomic_store(&s_shown, false);
}

void gfx_turn_request(gfx_turn_t *t, bool turned)
{
    if (t != NULL) {
        t->requested = turned;
    }
}

void gfx_turn_fail(gfx_turn_t *t, gfx_turn_fail_t why)
{
    if (t == NULL || why == GFX_TURN_FAIL_NONE) {
        return;
    }
    if (!t->failed) {
        t->why = why;
    }
    t->failed = true;
}

bool gfx_turn_drawing(const gfx_turn_t *t)
{
    return t != NULL && t->drawing;
}

void gfx_turn_begin(gfx_turn_t *t, const gfx_canvas_t *logical)
{
    if (t == NULL || !t->drawing || logical == NULL) {
        gfx_damage_attach(NULL);
        return;
    }
    gfx_damage_init(&t->damage, logical);
    gfx_damage_attach(&t->damage);
}

/**
 * @brief Fall back for this frame: @p logical into @p fb as drawn.
 * @param t the state
 * @param logical the logical buffer
 * @param fb the frame buffer
 * @param why the reason
 * @return GFX_TURN_SHOW_FALLBACK
 */
static gfx_turn_show_t fall_back(gfx_turn_t *t, const gfx_color_t *logical,
                                 gfx_color_t *fb, gfx_turn_fail_t why)
{
    memcpy(fb, logical,
           (size_t)t->width * (size_t)t->height * sizeof(gfx_color_t));
    gfx_turn_fail(t, why);
    t->drawing = false;
    t->repaint = true;
    return GFX_TURN_SHOW_FALLBACK;
}

gfx_turn_show_t gfx_turn_flush(gfx_turn_t *t, const gfx_color_t *logical,
                               gfx_color_t *fb, const gfx_turn_ops_t *ops)
{
    gfx_damage_attach(NULL);
    if (t == NULL) {
        return GFX_TURN_SHOW_NORMAL;
    }
    t->rects = 0u;
    t->pixels = 0u;
    t->copy_us = 0u;
    if (!t->drawing) {
        return GFX_TURN_SHOW_NORMAL;
    }
    if (logical == NULL || fb == NULL || ops == NULL || ops->copy == NULL
        || ops->now_us == NULL) {
        /* Nothing drawn reaches the frame buffer: present it as it is and
         * draw NORMAL from the next frame. */
        gfx_turn_fail(t, GFX_TURN_FAIL_ENGINE);
        t->drawing = false;
        t->repaint = true;
        return GFX_TURN_SHOW_NORMAL;
    }

    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    int n;
    if (t->full_left > 0u) {
        r[0] = gfx_rect_make(0, 0, t->width, t->height);
        n = 1;
    } else {
        n = gfx_damage_rects(&t->damage, r, GFX_DAMAGE_MAX_RECTS);
    }

    const uint32_t start = ops->now_us(ops->ctx);
    for (int i = 0; i < n; ++i) {
        const uint32_t spent = ops->now_us(ops->ctx) - start;
        if (spent >= GFX_TURN_BUDGET_US) {
            t->copy_us = spent;
            return fall_back(t, logical, fb, GFX_TURN_FAIL_TIMEOUT);
        }
        const gfx_turn_copy_t got =
            ops->copy(ops->ctx, logical, fb, r[i], GFX_TURN_BUDGET_US - spent);
        if (got != GFX_TURN_COPY_DONE) {
            t->copy_us = ops->now_us(ops->ctx) - start;
            return fall_back(t, logical, fb,
                             (got == GFX_TURN_COPY_TIMEOUT)
                                 ? GFX_TURN_FAIL_TIMEOUT
                                 : GFX_TURN_FAIL_ERROR);
        }
        t->rects = (uint16_t)(t->rects + 1u);
        t->pixels += (uint32_t)r[i].w * (uint32_t)r[i].h;
    }
    t->copy_us = ops->now_us(ops->ctx) - start;
    if (t->full_left > 0u) {
        --t->full_left;
    }
    return GFX_TURN_SHOW_TURNED;
}

bool gfx_turn_presented(gfx_turn_t *t, gfx_turn_show_t show)
{
    (void)t;
    const bool turned = (show == GFX_TURN_SHOW_TURNED);
    return atomic_exchange(&s_shown, turned) != turned;
}

void gfx_turn_next(gfx_turn_t *t)
{
    if (t == NULL) {
        return;
    }
    const bool want = t->requested && !t->failed;
    if (t->requested && t->failed && !t->alerted) {
        t->alerted = true;
        t->alert = true;
    }
    if (want == t->drawing) {
        return;
    }
    t->drawing = want;
    t->repaint = true;
    t->full_left = want ? (uint8_t)GFX_TURN_FULL_FLIPS : 0u;
}

bool gfx_turn_take_repaint(gfx_turn_t *t)
{
    if (t == NULL || !t->repaint) {
        return false;
    }
    t->repaint = false;
    return true;
}

bool gfx_turn_take_alert(gfx_turn_t *t)
{
    if (t == NULL || !t->alert) {
        return false;
    }
    t->alert = false;
    return true;
}

bool gfx_turn_shown(void)
{
    return atomic_load(&s_shown);
}
