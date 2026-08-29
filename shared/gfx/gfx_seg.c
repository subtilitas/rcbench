/*
 * Seven-segment numerals, drawn rather than stored.
 *
 * A bitmap face cannot do the one thing that makes a segment display read as
 * an instrument: showing the segments that are *off*.  A real panel has all
 * seven there in the glass whether they are lit or not, and the faint ghost
 * of an 8 behind every digit is most of why it looks like hardware.  So the
 * digits are rasterised from their segment masks, which also means one
 * routine serves every size the bench asks for instead of a table per size.
 */

#include "gfx.h"

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
 * Both bar shapes are hexagons -- square ends would butt into each other at
 * the corners and read as a rectangle with a notch, which is the look of a
 * segment display drawn by somebody who had not seen one.  The taper is half
 * the thickness at each end, so two meeting at a corner leave a clean
 * diagonal gap.
 */
static void bar_h(gfx_canvas_t *c, int x, int y, int w, int t,
                  int slant_num, int slant_den, int base_y, gfx_color_t col)
{
    for (int i = 0; i < t; ++i) {
        const int d   = (i < t - 1 - i) ? i : (t - 1 - i);
        const int ins = (t - 1) / 2 - d;
        const int yy  = y + i;
        const int sx  = (slant_den > 0)
                            ? (base_y - yy) * slant_num / slant_den : 0;
        if (w - 2 * ins > 0) {
            gfx_hline(c, x + ins + sx, yy, w - 2 * ins, col);
        }
    }
}

static void bar_v(gfx_canvas_t *c, int x, int y, int h, int t,
                  int slant_num, int slant_den, int base_y, gfx_color_t col)
{
    for (int j = 0; j < h; ++j) {
        const int d   = (j < h - 1 - j) ? j : (h - 1 - j);
        int ins = (t - 1) / 2 - d;
        if (ins < 0) {
            ins = 0;
        }
        const int yy = y + j;
        const int sx = (slant_den > 0)
                           ? (base_y - yy) * slant_num / slant_den : 0;
        if (t - 2 * ins > 0) {
            gfx_hline(c, x + ins + sx, yy, t - 2 * ins, col);
        }
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
    const int sn  = st->slant, sd = h;
    const int by  = y + h;                /* the slant is measured from the foot */
    /* The bar between the two verticals, which is what a horizontal spans. */
    const int hw  = w - 2 * t;

    if (segs & SEG_A) { bar_h(c, x + t, y, hw, t, sn, sd, by, col); }
    if (segs & SEG_G) { bar_h(c, x + t, y + mid, hw, t, sn, sd, by, col); }
    if (segs & SEG_D) { bar_h(c, x + t, y + h - t, hw, t, sn, sd, by, col); }
    if (segs & SEG_F) { bar_v(c, x, y + t - 1, vh, t, sn, sd, by, col); }
    if (segs & SEG_B) { bar_v(c, x + w - t, y + t - 1, vh, t, sn, sd, by, col); }
    if (segs & SEG_E) { bar_v(c, x, y + mid + t - 1, vh, t, sn, sd, by, col); }
    if (segs & SEG_C) { bar_v(c, x + w - t, y + mid + t - 1, vh, t, sn, sd, by, col); }
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
            const int sx = (st->slant > 0)
                               ? (t) * st->slant / st->digit_h : 0;
            gfx_fill_rect(c, x + sx, y + st->digit_h - t, t, t, on);
            if (*p == ':') {
                gfx_fill_rect(c, x + sx * 3, y + (st->digit_h - t) / 3, t, t, on);
            }
            x += punct_w(st) + st->gap;
            continue;
        }
        if (*p != ' ') {
            /* Every segment first at the unlit level, then the lit ones over
             * them.  Drawn in this order so a lit segment never has to know
             * what it is covering. */
            if (st->ghost) {
                digit(c, x, y, st, SEG_ALL, off);
            }
            digit(c, x, y, st, mask_for(*p), on);
        }
        x += st->digit_w + st->gap;
    }
    return x - start - st->gap;
}
