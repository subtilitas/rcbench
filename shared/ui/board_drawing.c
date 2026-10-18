/*
 * The IOmcu's drawing of its own board, on a screen.
 * See board_drawing.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "board_drawing.h"

#include <stddef.h>

static bool say(const char **why, const char *reason)
{
    if (why != NULL) {
        *why = reason;
    }
    return false;
}

bool board_drawing_agrees(const link_drawing_t *d,
                          const outbind_board_t *board, const char **why)
{
    if (why != NULL) {
        *why = "";
    }
    if (d == NULL || d->data == NULL) {
        return say(why, "no drawing");
    }
    if (board == NULL || board->pins == NULL || board->count == 0u) {
        return say(why, "a board this build does not know");
    }

    /* Every catalogue pin is a GPIO pad with its GPIO ... */
    for (uint8_t i = 0; i < board->count; ++i) {
        link_draw_pad_t p;
        if (!link_drawing_find_pad(d, board->pins[i].pad, &p)
            || p.kind != (uint8_t)LINK_DRAW_PAD_GPIO
            || p.value != board->pins[i].gpio) {
            return say(why, "a pin's pad is not that GPIO in the drawing");
        }
    }
    /* ... and no other pad is one. */
    unsigned gpio_pads = 0u, other_pads = 0u;
    for (uint8_t i = 0; i < d->pads; ++i) {
        link_draw_pad_t p;
        (void)link_drawing_pad_at(d, i, &p);
        if (p.kind == (uint8_t)LINK_DRAW_PAD_GPIO) {
            ++gpio_pads;
        } else {
            ++other_pads;
        }
    }
    if (gpio_pads != board->count) {
        return say(why, "the drawing has GPIO pads the catalogue does not");
    }

    if (board->pads != NULL) {
        for (uint8_t i = 0; i < board->pad_count; ++i) {
            const outbind_pad_t *bp = &board->pads[i];
            link_draw_pad_t p;
            if (!link_drawing_find_pad(d, bp->pad, &p) || p.kind != bp->kind
                || p.value != bp->decivolts) {
                return say(why, "a ground or rail differs from the board's");
            }
        }
        if (other_pads != board->pad_count) {
            return say(why, "the drawing has pads the board does not list");
        }
    }

    const outbind_shape_t *sh = board->shape;
    if (sh != NULL) {
        if (d->width != sh->width_cmm || d->height != sh->height_cmm) {
            return say(why, "the outline is not the board's shape");
        }
        for (uint8_t i = 0; i < d->pads; ++i) {
            link_draw_pad_t p;
            uint16_t x, y;
            (void)link_drawing_pad_at(d, i, &p);
            if (!outbind_pad_xy(sh, p.pad, &x, &y) || x != p.x || y != p.y) {
                return say(why, "a pad is off the board's pad grid");
            }
        }
    }
    return true;
}

int board_drawing_scale(uint32_t v, int px, uint16_t full)
{
    if (full == 0u || px <= 0) {
        return 0;
    }
    return (int)((v * (uint32_t)px + (uint32_t)full / 2u) / full);
}

void board_drawing_render(gfx_canvas_t *c, const link_drawing_t *d, int x,
                          int y, int w, int h, gfx_color_t hole)
{
    if (c == NULL || d == NULL || d->data == NULL || w <= 0 || h <= 0) {
        return;
    }
    const uint16_t fw = d->width, fh = d->height;
#define SX(v) (x + board_drawing_scale((uint32_t)(v), w, fw))
#define SY(v) (y + board_drawing_scale((uint32_t)(v), h, fh))
#define SL(v) board_drawing_scale((uint32_t)(v), w, fw)

    gfx_fill_round_rect(c, x, y, w, h, SL(d->corner), (gfx_color_t)d->colour);

    for (uint16_t i = 0; i < d->shapes; ++i) {
        link_draw_shape_t s;
        (void)link_drawing_shape_at(d, i, &s);
        const gfx_color_t col = (gfx_color_t)s.colour;
        switch (s.kind) {
        case LINK_DRAW_RECT:
        case LINK_DRAW_RRECT: {
            /* Both edges from their own positions, so two shapes that share
             * an edge in the drawing share it on screen. */
            const int x0 = SX(s.a), x1 = SX((uint32_t)s.a + s.c);
            const int y0 = SY(s.b), y1 = SY((uint32_t)s.b + s.d);
            const int rw = (x1 > x0) ? x1 - x0 : 1;
            const int rh = (y1 > y0) ? y1 - y0 : 1;
            if (s.kind == (uint8_t)LINK_DRAW_RRECT) {
                int r = SL(s.param);
                if (2 * r > rw) { r = rw / 2; }
                if (2 * r > rh) { r = rh / 2; }
                gfx_fill_round_rect(c, x0, y0, rw, rh, r, col);
            } else {
                gfx_fill_rect(c, x0, y0, rw, rh, col);
            }
            break;
        }
        case LINK_DRAW_DISC: {
            const int r = SL(s.c);
            gfx_fill_circle(c, SX(s.a), SY(s.b), (r > 0) ? r : 1, col);
            break;
        }
        case LINK_DRAW_LINE: {
            const int t = SL(s.param);
            gfx_thick_line(c, SX(s.a), SY(s.b), SX(s.c), SY(s.d),
                           (t > 0) ? t : 1, col);
            break;
        }
        default:
            break;    /* link_drawing_parse() lets no other kind through */
        }
    }

    const int ring = (SL(d->ring_r) > 1) ? SL(d->ring_r) : 2;
    const int hr = (SL(d->hole_r) < ring) ? SL(d->hole_r) : ring - 1;
    for (uint8_t i = 0; i < d->pads; ++i) {
        link_draw_pad_t p;
        (void)link_drawing_pad_at(d, i, &p);
        const int px = SX(p.x), py = SY(p.y);
        if (p.flags & LINK_DRAW_PAD_SQUARE) {
            gfx_fill_rect(c, px - ring, py - ring, 2 * ring + 1, 2 * ring + 1,
                          (gfx_color_t)d->ring);
        } else {
            gfx_fill_circle(c, px, py, ring, (gfx_color_t)d->ring);
        }
        if (hr > 0) {
            gfx_fill_circle(c, px, py, hr, hole);
        }
    }
#undef SX
#undef SY
#undef SL
}
