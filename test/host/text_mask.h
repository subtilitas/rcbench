/*
 * Whether a frame shows a given string in a given face somewhere in an
 * area.  The area's most common colour is taken as its ground, and each
 * other colour of the area is tried as the ink, so a case need not know
 * which of a screen's inks a state draws in: the string drawn alone in
 * that ink on that ground must match the frame pixel for pixel at one
 * place, its antialiased edges included.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gfx.h"

/** @brief The widest and tallest string text_mask_found() takes, px. */
#define TEXT_MASK_W 256
#define TEXT_MASK_H 40

/** @brief The distinct colours of an area text_mask_found() tries. */
#define TEXT_MASK_COLOURS 64

/**
 * @brief Whether the string @p ref holds, @p tw x @p th pixels, matches the
 *        frame @p px at one place in @p area.
 * @param px    the frame, @p w pixels a row
 * @param w     the frame's width
 * @param area  the rectangle the string lies inside, whole
 * @param ref   the string drawn alone, TEXT_MASK_W pixels a row
 * @param tw    its width, px
 * @param th    its height, px
 * @return true at the first place that matches
 */
static inline bool text_mask_at(const gfx_color_t *px, int w, gfx_rect_t area,
                                const gfx_color_t *ref, int tw, int th)
{
    for (int y = area.y; y + th <= area.y + area.h; ++y) {
        for (int x = area.x; x + tw <= area.x + area.w; ++x) {
            bool same = true;
            for (int j = 0; j < th && same; ++j) {
                const gfx_color_t *row = px + (size_t)(y + j) * (size_t)w
                                         + (size_t)x;
                same = memcmp(row, ref + (size_t)j * TEXT_MASK_W,
                              (size_t)tw * sizeof(gfx_color_t)) == 0;
            }
            if (same) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Whether @p text in @p font is drawn in @p area of the frame @p px.
 * @param px    the frame, @p w pixels a row
 * @param w     the frame's width
 * @param h     the frame's height
 * @param area  the rectangle the string lies inside, whole
 * @param font  the face
 * @param text  the string
 * @return true when the string is drawn there in one of the area's colours
 */
static inline bool text_mask_found(const gfx_color_t *px, int w, int h,
                                   gfx_rect_t area, const gfx_font_t *font,
                                   const char *text)
{
    static gfx_color_t ref[TEXT_MASK_W * TEXT_MASK_H];
    const int tw = gfx_text_width(font, text, 1);
    const int th = gfx_text_height(font, 1);
    if (tw <= 0 || tw > TEXT_MASK_W || th > TEXT_MASK_H
        || area.x < 0 || area.y < 0 || area.x + area.w > w
        || area.y + area.h > h) {
        return false;
    }
    /* The area's colours and how often each is there. */
    gfx_color_t col[TEXT_MASK_COLOURS];
    unsigned    cnt[TEXT_MASK_COLOURS];
    unsigned    n = 0u;
    for (int y = area.y; y < area.y + area.h; ++y) {
        for (int x = area.x; x < area.x + area.w; ++x) {
            const gfx_color_t v = px[(size_t)y * (size_t)w + (size_t)x];
            unsigned k = 0u;
            while (k < n && col[k] != v) {
                ++k;
            }
            if (k == n) {
                if (n == TEXT_MASK_COLOURS) {
                    continue;
                }
                col[n] = v;
                cnt[n++] = 0u;
            }
            ++cnt[k];
        }
    }
    unsigned g = 0u;
    for (unsigned k = 1u; k < n; ++k) {
        if (cnt[k] > cnt[g]) {
            g = k;
        }
    }
    for (unsigned k = 0u; k < n; ++k) {
        if (k == g) {
            continue;
        }
        for (size_t i = 0; i < sizeof(ref) / sizeof(ref[0]); ++i) {
            ref[i] = col[g];
        }
        gfx_canvas_t rc;
        gfx_canvas_init(&rc, ref, TEXT_MASK_W, TEXT_MASK_H, TEXT_MASK_W);
        gfx_text(&rc, 0, 0, text, font, col[k], 1);
        if (text_mask_at(px, w, area, ref, tw, th)) {
            return true;
        }
    }
    return false;
}
