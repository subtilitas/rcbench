/*
 * Seven-segment numerals, drawn rather than stored.
 *
 * A bitmap face cannot do the one thing that makes a segment display read as
 * an instrument: showing the segments that are *off*.  A real panel has all
 * seven there in the glass whether they are lit or not, and the faint ghost
 * of an 8 behind every digit is most of why it looks like hardware.  So the
 * digits are rasterised from their segment masks, which also means one
 * routine serves every size the bench asks for instead of a table per size.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx.h"

#include <math.h>

/* a=1 b=2 c=4 d=8 e=16 f=32 g=64, in the conventional order:
 *
 *      aaaa
 *     f    b
 *      gggg
 *     e    c
 *      dddd
 */
#define SEG_A 0x01u
#define SEG_B 0x02u
#define SEG_C 0x04u
#define SEG_D 0x08u
#define SEG_E 0x10u
#define SEG_F 0x20u
#define SEG_G 0x40u
#define SEG_ALL 0x7Fu

static uint8_t mask_for(char ch)
{
    switch (ch) {
    case '0': return SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F;
    case '1': return SEG_B|SEG_C;
    case '2': return SEG_A|SEG_B|SEG_G|SEG_E|SEG_D;
    case '3': return SEG_A|SEG_B|SEG_G|SEG_C|SEG_D;
    case '4': return SEG_F|SEG_G|SEG_B|SEG_C;
    case '5': return SEG_A|SEG_F|SEG_G|SEG_C|SEG_D;
    case '6': return SEG_A|SEG_F|SEG_G|SEG_E|SEG_C|SEG_D;
    case '7': return SEG_A|SEG_B|SEG_C;
    case '8': return SEG_ALL;
    case '9': return SEG_A|SEG_B|SEG_C|SEG_D|SEG_F|SEG_G;
    case '-': return SEG_G;
    default:  return 0u;
    }
}

/*
 * One antialiased scanline.
 *
 * The interior goes through gfx_hline unchanged -- it is whole pixels and
 * there is nothing to blend -- and only the two ends, which are where a
 * fractional edge lands, read the destination back.  That keeps a segment at
 * roughly the cost of the runs it was before, with two blends per row.
 */
static void span_aa(gfx_canvas_t *c, float x0, float x1, int y,
                    gfx_color_t col)
{
    if (x1 - x0 <= 0.0f) {
        return;
    }
    const int il = (int)ceilf(x0);    /* first wholly covered pixel      */
    const int ir = (int)floorf(x1);   /* one past the last wholly covered */

    if ((float)il > x0 && il - 1 >= 0) {
        const float cov = (float)il - x0;
        gfx_pixel(c, il - 1, y,
                  gfx_lerp(gfx_pixel_get(c, il - 1, y), col,
                           (uint8_t)(cov * 255.0f)));
    }
    if (ir > il) {
        gfx_hline(c, il, y, ir - il, col);
    }
    if (x1 > (float)ir) {
        const float cov = x1 - (float)ir;
        gfx_pixel(c, ir, y,
                  gfx_lerp(gfx_pixel_get(c, ir, y), col,
                           (uint8_t)(cov * 255.0f)));
    }
}

/* The lean, as a fraction of a pixel rather than a whole one -- an integer
 * slant is a staircase down the side of every digit, which is exactly the
 * artefact the tapers below are being smoothed to avoid. */
static float slant_at(const gfx_seg_style_t *st, float y_from_foot)
{
    return (st->digit_h > 0)
               ? (float)st->slant * y_from_foot / (float)st->digit_h : 0.0f;
}

/*
 * Both bar shapes are hexagons -- square ends would butt into each other at
 * the corners and read as a rectangle with a notch, which is the look of a
 * segment display drawn by somebody who had not seen one.  The taper is half
 * the thickness at each end, so two meeting at a corner leave a clean
 * diagonal gap.
 *
 * The tapers are 45 degrees, so every one of them crosses the pixel grid
 * diagonally and every one of them staircased before this was carried in
 * floating point.
 */
static void bar_h(gfx_canvas_t *c, float x, int y, float w, int t,
                  const gfx_seg_style_t *st, int foot, gfx_color_t col)
{
    const float half = (float)t / 2.0f;
    for (int i = 0; i < t; ++i) {
        const int   yy = y + i;
        const float dy = fabsf(((float)i + 0.5f) - half);
        const float sx = slant_at(st, (float)(foot - yy));
        span_aa(c, x + dy + sx, x + w - dy + sx, yy, col);
    }
}

static void bar_v(gfx_canvas_t *c, float x, int y, int h, int t,
                  const gfx_seg_style_t *st, int foot, gfx_color_t col)
{
    const float half = (float)t / 2.0f;
    for (int j = 0; j < h; ++j) {
        const int   yy = y + j;
        const float d  = fminf((float)j + 0.5f, (float)h - ((float)j + 0.5f));
        const float ins = (half > d) ? half - d : 0.0f;
        const float sx = slant_at(st, (float)(foot - yy));
        span_aa(c, x + ins + sx, x + (float)t - ins + sx, yy, col);
    }
}

static void digit(gfx_canvas_t *c, int x, int y, const gfx_seg_style_t *st,
                  uint8_t segs, gfx_color_t col)
{
    const int w = st->digit_w, h = st->digit_h, t = st->thickness;
    const int mid = (h - t) / 2;          /* the middle bar's top edge   */
    /*
     * The verticals overlap the horizontals by one row at each end.  Butted
     * exactly, the tapers leave a notch at every corner; overlapped, the two
     * hexagons meet and the corner closes.
     */
    const int vh  = mid - t + 2;
    const int   by = y + h;               /* the slant is measured from the foot */
    /* The bar between the two verticals, which is what a horizontal spans. */
    const float hw = (float)(w - 2 * t);
    const float fx = (float)x;

    if (segs & SEG_A) { bar_h(c, fx + (float)t, y, hw, t, st, by, col); }
    if (segs & SEG_G) { bar_h(c, fx + (float)t, y + mid, hw, t, st, by, col); }
    if (segs & SEG_D) { bar_h(c, fx + (float)t, y + h - t, hw, t, st, by, col); }
    if (segs & SEG_F) { bar_v(c, fx, y + t - 1, vh, t, st, by, col); }
    if (segs & SEG_B) { bar_v(c, fx + (float)(w - t), y + t - 1, vh, t, st, by, col); }
    if (segs & SEG_E) { bar_v(c, fx, y + mid + t - 1, vh, t, st, by, col); }
    if (segs & SEG_C) { bar_v(c, fx + (float)(w - t), y + mid + t - 1, vh, t, st, by, col); }
}

/* A dot and a colon are not segments, so they get their own cells and their
 * own narrower advance -- a full digit cell of whitespace around a decimal
 * point is what makes rendered numbers look spaced by accident. */
static int punct_w(const gfx_seg_style_t *st)
{
    return st->thickness + st->thickness / 2;
}

int gfx_seg_width(const char *s, const gfx_seg_style_t *st)
{
    if (s == NULL || st == NULL) {
        return 0;
    }
    int w = 0;
    for (const char *p = s; *p != '\0'; ++p) {
        w += ((*p == '.' || *p == ':') ? punct_w(st) : st->digit_w) + st->gap;
    }
    return (w > 0) ? w - st->gap : 0;
}

int gfx_seg_text(gfx_canvas_t *c, int x, int y, const char *s,
                 const gfx_seg_style_t *st, gfx_color_t on, gfx_color_t off)
{
    if (c == NULL || s == NULL || st == NULL
        || st->digit_w <= 0 || st->digit_h <= 0 || st->thickness <= 0) {
        return 0;
    }
    const int start = x;
    const int t = st->thickness;

    for (const char *p = s; *p != '\0'; ++p) {
        if (*p == '.' || *p == ':') {
            gfx_fill_rect(c, x, y + st->digit_h - t, t, t, on);
            if (*p == ':') {
                const int cy = y + (st->digit_h - t) / 3;
                gfx_fill_rect(c, x + (int)(slant_at(st, (float)(y + st->digit_h - cy)) + 0.5f),
                              cy, t, t, on);
            }
            x += punct_w(st) + st->gap;
            continue;
        }
        const uint8_t segs = mask_for(*p);
        if (segs != 0u) {
            /* Every segment first at the unlit level, then the lit ones over
             * them.  Drawn in this order so a lit segment never has to know
             * what it is covering.
             *
             * A character that lights nothing gets no ghost either.  A real
             * display would show one -- the glass has seven bars in every
             * cell whatever is in it -- but a leading '+' then renders as a
             * ghosted 8 that reads as a stray digit, and the cell is there to
             * hold the sign's place so the number does not shift when it
             * turns negative. */
            if (st->ghost) {
                digit(c, x, y, st, SEG_ALL, off);
            }
            digit(c, x, y, st, segs, on);
        }
        x += st->digit_w + st->gap;
    }
    return x - start - st->gap;
}
