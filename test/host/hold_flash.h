/*
 * The flash of a 2s hold as the screen draws it, for the hold flash walk.
 *
 * A screen's hold button fires after 2s of contact and flashes: white,
 * black, settled, twice, 50ms a phase (UI_HOLD_FLASH_MS, 300ms).  The fade
 * before it is a blend of two fills and never pure white or pure black, so
 * a body that is mostly GFX_WHITE on one frame and mostly GFX_BLACK on a
 * later one is the flash.  hold_flash_walk() runs the screen frame by
 * frame, draws each frame, and records what the button's body was.
 *
 * Used by the test files that drive a screen with a hold; each has a case
 * named *_flashes_as_it_fires.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gfx.h"

/** @brief How the walk runs the screen under test. */
typedef struct {
    void (*tick)(float dt_s);     /**< one frame of the screen            */
    void (*draw)(void);           /**< the screen drawn into @ref fb      */
    const gfx_color_t *fb;        /**< the canvas's pixels                */
    int stride;                   /**< pixels a row                       */
} hold_flash_rig_t;

/**
 * @brief What the body of @p r shows: 'W' over half of it white, 'B' over
 *        half black, '.' else.
 * @param rig the rig
 * @param r   the button, in the canvas's coordinates
 * @return the class
 */
static inline char hold_flash_body(const hold_flash_rig_t *rig, gfx_rect_t r)
{
    size_t white = 0u, black = 0u, all = 0u;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            const gfx_color_t px = rig->fb[(size_t)y * (size_t)rig->stride
                                           + (size_t)x];
            white += (px == GFX_WHITE);
            black += (px == GFX_BLACK);
            ++all;
        }
    }
    if (all == 0u) {
        return '.';
    }
    return (white * 2u > all) ? 'W' : (black * 2u > all) ? 'B' : '.';
}

/**
 * @brief Runs @p seconds of frames at 60Hz, each drawn, and keeps the
 *        classes of @p r's body, repeats folded, in @p out.
 * @param rig     the rig
 * @param r       the button
 * @param seconds how long
 * @param out     the classes, NUL-terminated
 * @param n       its size
 */
static inline void hold_flash_record(const hold_flash_rig_t *rig, gfx_rect_t r,
                                     float seconds, char *out, size_t n)
{
    size_t k = 0u;
    const int frames = (int)(seconds * 60.0f + 0.5f);
    for (int i = 0; i < frames && k + 1u < n; ++i) {
        rig->tick(1.0f / 60.0f);
        rig->draw();
        const char ch = hold_flash_body(rig, r);
        if (k == 0u || out[k - 1u] != ch) {
            out[k++] = ch;
        }
    }
    out[k] = '\0';
}

/**
 * @brief Whether the recorded classes hold the flash: white, then black,
 *        twice, in that order.
 * @param seen what hold_flash_record() kept
 * @return true for the flash
 */
static inline bool hold_flash_in(const char *seen)
{
    const char *w1 = strchr(seen, 'W');
    const char *b1 = (w1 != NULL) ? strchr(w1, 'B') : NULL;
    const char *w2 = (b1 != NULL) ? strchr(b1, 'W') : NULL;
    const char *b2 = (w2 != NULL) ? strchr(w2, 'B') : NULL;
    return b2 != NULL;
}

/**
 * @brief The walk for one hold: the press already down on the button,
 *        @p seconds of frames (at least 2.4s: the hold and its flash), and
 *        whether the button flashed.
 * @param rig     the rig
 * @param r       the button
 * @param seconds how long
 * @param seen    the classes kept, for the failure's message
 * @param n       its size
 * @return true when it flashed
 */
static inline bool hold_flash_walk(const hold_flash_rig_t *rig, gfx_rect_t r,
                                   float seconds, char *seen, size_t n)
{
    hold_flash_record(rig, r, seconds, seen, n);
    return hold_flash_in(seen);
}
