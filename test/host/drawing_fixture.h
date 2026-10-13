/*
 * Board drawings (LINK_ART_DRAWING) for the tests, written byte by byte the
 * way link_pages.h lays them out, so a test can build a valid one and then
 * spoil exactly one field of it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <string.h>

#include "link_pages.h"

static inline void fx_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/* The header, with the outline corner 0, rings 40 and holes 20. */
static inline void fx_draw_head(uint8_t *b, uint8_t pads, uint16_t shapes,
                                uint16_t w, uint16_t h)
{
    memset(b, 0, LINK_DRAW_HEAD_BYTES);
    b[0] = (uint8_t)LINK_DRAW_VERSION;
    b[1] = pads;
    fx_put16(b + 2, shapes);
    fx_put16(b + 4, w);
    fx_put16(b + 6, h);
    fx_put16(b + 8, 0x1A52u);   /* board */
    fx_put16(b + 10, 0xCE79u);  /* rings */
    b[12] = 0u;                 /* corner */
    b[13] = 40u;                /* ring radius */
    b[14] = 20u;                /* hole radius */
}

/* Where pad record @p i starts. */
static inline uint8_t *fx_pad_at(uint8_t *b, unsigned i)
{
    return b + LINK_DRAW_HEAD_BYTES + i * LINK_DRAW_PAD_BYTES;
}

static inline void fx_draw_pad(uint8_t *b, unsigned i, uint8_t pad,
                               uint8_t kind, uint8_t value, uint8_t flags,
                               uint16_t x, uint16_t y)
{
    uint8_t *p = fx_pad_at(b, i);
    p[0] = pad;
    p[1] = kind;
    p[2] = value;
    p[3] = flags;
    fx_put16(p + 4, x);
    fx_put16(p + 6, y);
}

/* Where shape record @p i starts, after @p pads pad records. */
static inline uint8_t *fx_shape_at(uint8_t *b, unsigned pads, unsigned i)
{
    return b + LINK_DRAW_HEAD_BYTES + pads * LINK_DRAW_PAD_BYTES
           + i * LINK_DRAW_SHAPE_BYTES;
}

static inline void fx_draw_shape(uint8_t *b, unsigned pads, unsigned i,
                                 uint8_t kind, uint8_t param, uint16_t colour,
                                 uint16_t a, uint16_t bb, uint16_t c,
                                 uint16_t d)
{
    uint8_t *p = fx_shape_at(b, pads, i);
    p[0] = kind;
    p[1] = param;
    fx_put16(p + 2, colour);
    fx_put16(p + 4, a);
    fx_put16(p + 6, bb);
    fx_put16(p + 8, c);
    fx_put16(p + 10, d);
}

#define FX_SMALL_PADS   4u
#define FX_SMALL_SHAPES 4u
#define FX_SMALL_W      1000u
#define FX_SMALL_H      500u
#define FX_SMALL_BYTES  (LINK_DRAW_HEAD_BYTES \
                         + FX_SMALL_PADS * LINK_DRAW_PAD_BYTES \
                         + FX_SMALL_SHAPES * LINK_DRAW_SHAPE_BYTES)

/*
 * A board 10 mm by 5 mm with a GPIO pad, a ground, a 3.3 V rail and a RUN
 * pad, and one shape of every kind.  Returns its length, FX_SMALL_BYTES.
 */
static inline uint32_t fx_draw_small(uint8_t *b)
{
    memset(b, 0, FX_SMALL_BYTES);
    fx_draw_head(b, FX_SMALL_PADS, FX_SMALL_SHAPES, FX_SMALL_W, FX_SMALL_H);
    fx_draw_pad(b, 0, 1u, (uint8_t)LINK_DRAW_PAD_GPIO, 0u,
                (uint8_t)LINK_DRAW_PAD_SQUARE, 100u, 100u);
    fx_draw_pad(b, 1, 2u, (uint8_t)LINK_PAD_GROUND, 0u, 0u, 300u, 100u);
    fx_draw_pad(b, 2, 3u, (uint8_t)LINK_PAD_POWER, 33u, 0u, 300u, 400u);
    fx_draw_pad(b, 3, 4u, (uint8_t)LINK_PAD_OTHER, 0u, 0u, 100u, 400u);
    fx_draw_shape(b, FX_SMALL_PADS, 0, (uint8_t)LINK_DRAW_RECT, 0u, 0x2945u,
                  500u, 100u, 200u, 300u);
    fx_draw_shape(b, FX_SMALL_PADS, 1, (uint8_t)LINK_DRAW_RRECT, 20u, 0x2945u,
                  750u, 100u, 200u, 100u);
    fx_draw_shape(b, FX_SMALL_PADS, 2, (uint8_t)LINK_DRAW_DISC, 0u, 0xFFFFu,
                  850u, 350u, 50u, 0u);
    fx_draw_shape(b, FX_SMALL_PADS, 3, (uint8_t)LINK_DRAW_LINE, 30u, 0x2B54u,
                  100u, 250u, 450u, 250u);
    return FX_SMALL_BYTES;
}
