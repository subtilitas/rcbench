/**
 * @file
 * @brief gfx: an RGB565 (16-bit red, green, blue) rasteriser with no
 *        dependencies.
 *
 * Nothing in this header touches ESP-IDF, FreeRTOS or any hardware.  It draws
 * into a plain block of 16-bit pixels described by a gfx_canvas_t, so the
 * same code renders off-screen sprites and runs in the host test suite.  The
 * display component binds a canvas to the framebuffer.
 *
 * The pixel format is RGB565 in the processor's native byte order, which is
 * what the ESP32-S3 RGB peripheral expects on a 16-bit data bus.
 *
 * Three contracts hold for every primitive below:
 *
 *   Everything clips to canvas->clip.  A draw off the edge, or outside a clip
 *   a screen has set, is dropped rather than wrapped: a wrapped write lands
 *   somewhere else on the panel.
 *
 *   The _aa variants antialias their edges from coverage; the plain ones are
 *   hard-edged.  Use _aa for anything that is not axis-aligned (a disc, a
 *   capsule, a rotated glyph) and the plain rect and line primitives, which
 *   are exact, for everything that is.
 *
 *   Alpha and coverage are per call, not stored: a colour is opaque RGB565,
 *   and a blend happens where a primitive is given a coverage.  Drawing the
 *   same translucent shape twice darkens it twice, so an overlay that lands
 *   on pixels a screen does not repaint uses an ordered (Bayer) stencil,
 *   which is idempotent; see gfx_text_rotated.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ colour */

/** @brief An RGB565 color: 5 bits red, 6 bits green, 5 bits blue. */
typedef uint16_t gfx_color_t;

/**
 * @brief Pack 8-bit R/G/B into RGB565.
 * @param r  Red, 0 to 255.
 * @param g  Green, 0 to 255.
 * @param b  Blue, 0 to 255.
 */
#define GFX_RGB(r, g, b)                                                    \
    ((gfx_color_t)((((uint16_t)(r) & 0xF8u) << 8) |                         \
                   (((uint16_t)(g) & 0xFCu) << 3) |                         \
                   (((uint16_t)(b) & 0xF8u) >> 3)))

#define GFX_BLACK       GFX_RGB(0, 0, 0)         /**< black */
#define GFX_WHITE       GFX_RGB(255, 255, 255)   /**< white */
#define GFX_RED         GFX_RGB(255, 0, 0)       /**< red */
#define GFX_GREEN       GFX_RGB(0, 255, 0)       /**< green */
#define GFX_BLUE        GFX_RGB(0, 0, 255)       /**< blue */
#define GFX_YELLOW      GFX_RGB(255, 255, 0)     /**< yellow */
#define GFX_CYAN        GFX_RGB(0, 255, 255)     /**< cyan */
#define GFX_MAGENTA     GFX_RGB(255, 0, 255)     /**< magenta */
/** @brief A grey of level @p v, 0 to 255. */
#define GFX_GREY(v)     GFX_RGB(v, v, v)


/**
 * @brief Split RGB565 back into 8-bit components (values are quantised).
 * @param c  The color.
 * @param r  Receives red, 0 to 255; NULL skips it.
 * @param g  Receives green, 0 to 255; NULL skips it.
 * @param b  Receives blue, 0 to 255; NULL skips it.
 */
static inline void gfx_unpack(gfx_color_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(c & 0x1Fu);
    if (r) { *r = (uint8_t)((r5 << 3) | (r5 >> 2)); }
    if (g) { *g = (uint8_t)((g6 << 2) | (g6 >> 4)); }
    if (b) { *b = (uint8_t)((b5 << 3) | (b5 >> 2)); }
}

/**
 * @brief Linear blend of two colours, t in [0,255] where 0 == a and 255 == b.
 * @param a  The color at t = 0.
 * @param b  The color at t = 255.
 * @param t  The weight of @p b, 0 to 255.
 * @return The blend.
 */
gfx_color_t gfx_lerp(gfx_color_t a, gfx_color_t b, uint8_t t);

/* ----------------------------------------------------------------- geometry */

/** @brief A rectangle: left and top edge, width and height, in pixels. */
typedef struct {
    int16_t x /**< left edge */, y /**< top edge */,
            w /**< width */, h /**< height */;
} gfx_rect_t;

/**
 * @brief Saturate rather than truncate.
 *
 * The fields are int16_t and the arguments are int, so a plain cast draws a
 * shape that is entirely off-screen back onto the panel at a wrapped
 * position -- which is the opposite of clipping, and turns an out-of-range
 * blit's source offset into a read far outside its buffer.
 *
 * @param v  The value.
 * @return @p v limited to INT16_MIN..INT16_MAX.
 */
static inline int16_t gfx_i16(int v)
{
    if (v < INT16_MIN) { return INT16_MIN; }
    if (v > INT16_MAX) { return INT16_MAX; }
    return (int16_t)v;
}

/**
 * @brief A rectangle from int arguments, each limited to the int16_t range.
 * @param x  Left edge.
 * @param y  Top edge.
 * @param w  Width.
 * @param h  Height.
 * @return The rectangle; its far edges are limited, not its extent.
 */
static inline gfx_rect_t gfx_rect_make(int x, int y, int w, int h)
{
    /* Clamp the far edge in int, so a saturated origin cannot silently shrink
     * or grow the extent. */
    long x1 = (long)x + (long)w;
    long y1 = (long)y + (long)h;
    int16_t rx = gfx_i16(x);
    int16_t ry = gfx_i16(y);
    if (x1 > INT16_MAX) { x1 = INT16_MAX; }
    if (x1 < INT16_MIN) { x1 = INT16_MIN; }
    if (y1 > INT16_MAX) { y1 = INT16_MAX; }
    if (y1 < INT16_MIN) { y1 = INT16_MIN; }
    gfx_rect_t r = { rx, ry, gfx_i16((int)(x1 - rx)), gfx_i16((int)(y1 - ry)) };
    return r;
}

/**
 * @brief Whether a rectangle covers no pixel.
 * @param r  The rectangle.
 * @return true when its width or height is 0 or less.
 */
static inline bool gfx_rect_empty(gfx_rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

/**
 * @brief Whether a pixel lies inside a rectangle.
 * @param r  The rectangle.
 * @param x  The pixel's column.
 * @param y  The pixel's row.
 * @return true when (@p x, @p y) is inside.
 */
static inline bool gfx_rect_contains(gfx_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/**
 * @brief Intersect two rects.
 * @param a    The first rectangle.
 * @param b    The second rectangle.
 * @param out  Receives the intersection; zeroed when they miss.  May be NULL.
 * @return false when they miss.
 */
bool gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b, gfx_rect_t *out);

/* ------------------------------------------------------------------- canvas */

/** @brief A block of pixels with a clip rectangle. */
typedef struct {
    gfx_color_t *pixels;  /**< top-left pixel                                */
    int16_t      width;   /**< visible width in pixels                       */
    int16_t      height;  /**< visible height in pixels                      */
    int32_t      stride;  /**< distance between rows, in pixels (>= width)   */
    gfx_rect_t   clip;    /**< all drawing is confined to this rect          */
} gfx_canvas_t;

/**
 * @brief Bind a canvas to a pixel buffer.
 *
 * @p stride is in pixels; pass 0 to mean "tightly packed" (stride == width).
 *
 * @param c       The canvas.
 * @param pixels  The top-left pixel.
 * @param width   Visible width, pixels.
 * @param height  Visible height, pixels.
 * @param stride  Distance between rows, pixels.
 */
void gfx_canvas_init(gfx_canvas_t *c, gfx_color_t *pixels,
                     int width, int height, int32_t stride);

/**
 * @brief Carve a sub-canvas out of @p src.
 * @param src     The parent canvas.
 * @param window  The part to take, in the parent's pixels.
 * @param out     Receives the sub-canvas.
 * @return false if the window is empty.
 */
bool gfx_canvas_sub(const gfx_canvas_t *src, gfx_rect_t window, gfx_canvas_t *out);

/**
 * @brief Restrict drawing to @p r (intersected with the canvas).
 * @param c  The canvas.
 * @param r  The new clip.
 * @return false if the clip is empty.
 */
bool gfx_clip_set(gfx_canvas_t *c, gfx_rect_t r);
/**
 * @brief Restrict further, relative to the current clip.
 * @param c  The canvas.
 * @param r  The rectangle to narrow the clip to.
 * @return false if the clip is empty.
 */
bool gfx_clip_intersect(gfx_canvas_t *c, gfx_rect_t r);
/**
 * @brief Set the clip to the whole canvas.
 * @param c  The canvas.
 */
void gfx_clip_reset(gfx_canvas_t *c);
/**
 * @brief The current clip.
 * @param c  The canvas.
 * @return The clip rectangle.
 */
static inline gfx_rect_t gfx_clip_get(const gfx_canvas_t *c) { return c->clip; }

/* --------------------------------------------------------------- primitives */

/**
 * @brief Fill the clip with a color.
 * @param c      The canvas.
 * @param color  The color.
 */
void gfx_clear(gfx_canvas_t *c, gfx_color_t color);
/**
 * @brief Set one pixel.
 * @param c      The canvas.
 * @param x      Column.
 * @param y      Row.
 * @param color  The color.
 */
void gfx_pixel(gfx_canvas_t *c, int x, int y, gfx_color_t color);
/**
 * @brief Read back a pixel.
 * @param c  The canvas.
 * @param x  Column.
 * @param y  Row.
 * @return The pixel; 0 when (x,y) is outside the canvas.
 */
gfx_color_t gfx_pixel_get(const gfx_canvas_t *c, int x, int y);

/**
 * @brief Fill a rectangle.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param color  The color.
 */
void gfx_fill_rect(gfx_canvas_t *c, int x, int y, int w, int h, gfx_color_t color);
/**
 * @brief Blend @p color over a rectangle: each pixel becomes @p alpha / 255
 *        of the colour and the rest of what it was, in 33 steps (alpha 0
 *        to 3 leaves it, 252 to 255 is the colour).  Reads every pixel it
 *        writes; drawn twice it blends twice.
 * @param c     Canvas.
 * @param x     Left edge.
 * @param y     Top edge.
 * @param w     Width.
 * @param h     Height.
 * @param color The colour.
 * @param alpha Its weight, 0 to 255.
 */
void gfx_fill_rect_blend(gfx_canvas_t *c, int x, int y, int w, int h,
                         gfx_color_t color, uint8_t alpha);
/**
 * @brief Draw a 1-pixel outline just inside a rectangle.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param color  The color.
 */
void gfx_draw_rect(gfx_canvas_t *c, int x, int y, int w, int h, gfx_color_t color);
/**
 * @brief Draw a horizontal line.
 * @param c      The canvas.
 * @param x      First column.
 * @param y      Row.
 * @param w      Length, pixels; a negative length runs left from @p x.
 * @param color  The color.
 */
void gfx_hline(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color);
/**
 * @brief Draw a vertical line.
 * @param c      The canvas.
 * @param x      Column.
 * @param y      First row.
 * @param h      Length, pixels; a negative length runs up from @p y.
 * @param color  The color.
 */
void gfx_vline(gfx_canvas_t *c, int x, int y, int h, gfx_color_t color);
/**
 * @brief Draw a 1-pixel line, both end pixels included.
 * @param c      The canvas.
 * @param x0     First end, column.
 * @param y0     First end, row.
 * @param x1     Second end, column.
 * @param y1     Second end, row.
 * @param color  The color.
 */
void gfx_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1, gfx_color_t color);
void gfx_thick_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1,
                    int thickness, gfx_color_t color);

/**
 * @brief Draw a 1-pixel circle outline.
 * @param c      The canvas.
 * @param cx     Center, column.
 * @param cy     Center, row.
 * @param r      Radius, pixels; nothing is drawn below 0.
 * @param color  The color.
 */
void gfx_draw_circle(gfx_canvas_t *c, int cx, int cy, int r, gfx_color_t color);

/*
 * Antialiased disc and capsule, filled from the distance to their own center
 * or spine.  The capsule is a thick line with round ends, and its sides and
 * caps come out of one expression -- so there is no seam where they meet,
 * which a disc drawn over a rectangle cannot avoid.
 */
void gfx_fill_circle_aa(gfx_canvas_t *c, int cx, int cy, int r,
                        gfx_color_t col);
void gfx_capsule_aa(gfx_canvas_t *c, int ax, int ay, int bx, int by,
                    int thickness, gfx_color_t col);
/**
 * @brief Fill a hard-edged disc.
 * @param c      The canvas.
 * @param cx     Center, column.
 * @param cy     Center, row.
 * @param r      Radius, pixels.
 * @param color  The color.
 */
void gfx_fill_circle(gfx_canvas_t *c, int cx, int cy, int r, gfx_color_t color);

/**
 * @brief An arc of @p thickness centered on (cx, cy), from @p a0_deg to
 * @p a1_deg.
 *
 * Angles are degrees counter-clockwise from the +x axis, so 90 is straight up
 * on screen.  The radial edges are antialiased.
 *
 * @param c          The canvas.
 * @param cx         Center, column.
 * @param cy         Center, row.
 * @param r          Radius of the arc's middle line, pixels.
 * @param thickness  Width of the arc, pixels.
 * @param a0_deg     Start angle, degrees.
 * @param a1_deg     End angle, degrees.
 * @param col        The color.
 */
void gfx_arc(gfx_canvas_t *c, int cx, int cy, int r, int thickness,
             float a0_deg, float a1_deg, gfx_color_t col);

/**
 * @brief The same arc, with each end brought up out of @p into over
 * @p fade_deg and taken back down into it.
 *
 * An arc that stops dead has two bright full-width ends that read as
 * breakage; one that dissolves reads as an arc lit over part of its length.
 *
 * @param c          The canvas.
 * @param cx         Center, column.
 * @param cy         Center, row.
 * @param r          Radius of the arc's middle line, pixels.
 * @param thickness  Width of the arc, pixels.
 * @param a0_deg     Start angle, degrees.
 * @param a1_deg     End angle, degrees.
 * @param col        The color.
 * @param into       The color the ends fade into.
 * @param fade_deg   Length of each fade, degrees.
 */
void gfx_arc_fade(gfx_canvas_t *c, int cx, int cy, int r, int thickness,
                  float a0_deg, float a1_deg, gfx_color_t col,
                  gfx_color_t into, float fade_deg);

/**
 * @brief Draw a 1-pixel outline of a rounded rectangle.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param r      Corner radius, pixels; limited to 0 and half the shorter edge.
 * @param color  The color.
 */
void gfx_draw_round_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                         int r, gfx_color_t color);
/**
 * @brief Fill a rounded rectangle.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param r      Corner radius, pixels; limited to 0 and half the shorter edge.
 * @param color  The color.
 */
void gfx_fill_round_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                         int r, gfx_color_t color);

/*
 * Chamfered (cut-corner) rectangles.  The cut is a 45-degree bevel of the
 * given size, controlled per corner: cutting only the top-left and
 * bottom-right of a panel gives a different look from cutting all four.  A
 * cut of 0 leaves that corner square.
 */
/**
 * @brief Fill a rectangle with all four corners cut by the same size.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param cut    Size of each cut, pixels; limited to 0 and half the shorter edge.
 * @param color  The color.
 */
void gfx_fill_chamfer_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                           int cut, gfx_color_t color);
/**
 * @brief Draw a 1-pixel outline of a rectangle with all four corners cut by
 * the same size.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param cut    Size of each cut, pixels; limited to 0 and half the shorter edge.
 * @param color  The color.
 */
void gfx_draw_chamfer_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                           int cut, gfx_color_t color);
/**
 * @brief Fill a rectangle with a cut size per corner.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param tl     Top-left cut, pixels; each cut is limited to 0 and half the
 *               shorter edge.
 * @param tr     Top-right cut, pixels.
 * @param br     Bottom-right cut, pixels.
 * @param bl     Bottom-left cut, pixels.
 * @param color  The color.
 */
void gfx_fill_chamfer_rect_ex(gfx_canvas_t *c, int x, int y, int w, int h,
                              int tl, int tr, int br, int bl, gfx_color_t color);
/**
 * @brief Draw a 1-pixel outline of a rectangle with a cut size per corner.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param w      Width.
 * @param h      Height.
 * @param tl     Top-left cut, pixels; each cut is limited to 0 and half the
 *               shorter edge.
 * @param tr     Top-right cut, pixels.
 * @param br     Bottom-right cut, pixels.
 * @param bl     Bottom-left cut, pixels.
 * @param color  The color.
 */
void gfx_draw_chamfer_rect_ex(gfx_canvas_t *c, int x, int y, int w, int h,
                              int tl, int tr, int br, int bl, gfx_color_t color);

/**
 * @brief A trapezoid filled row by row: its top row spans @p xl_top up to
 *        @p xr_top, its bottom row @p xl_bot up to @p xr_bot, and each row
 *        between them the two edges interpolated and rounded to the
 *        nearest pixel.  A right end cut on a slant is xr_bot < xr_top.
 * @param c      the canvas
 * @param y      the top row
 * @param h      the rows, px; none for h <= 0
 * @param xl_top the top row's first column
 * @param xr_top the column after the top row's last
 * @param xl_bot the bottom row's first column
 * @param xr_bot the column after the bottom row's last
 * @param color  the fill
 */
void gfx_fill_trapezoid(gfx_canvas_t *c, int y, int h, int xl_top, int xr_top,
                        int xl_bot, int xr_bot, gfx_color_t color);

/**
 * @brief Vertical gradient fill, top colour @p a to bottom colour @p b.
 * @param c  The canvas.
 * @param x  Left edge.
 * @param y  Top edge.
 * @param w  Width.
 * @param h  Height.
 * @param a  The top row's color.
 * @param b  The bottom row's color.
 */
void gfx_fill_rect_gradient(gfx_canvas_t *c, int x, int y, int w, int h,
                            gfx_color_t a, gfx_color_t b);

/* -------------------------------------------------------------------- blits */

/**
 * @brief Copy a rectangular block of pixels.
 * @param c           The canvas.
 * @param x           Left edge of the destination.
 * @param y           Top edge of the destination.
 * @param src         The source pixels.
 * @param w           Width, pixels.
 * @param h           Height, pixels.
 * @param src_stride  Distance between the source's rows, pixels.
 */
void gfx_blit(gfx_canvas_t *c, int x, int y, const gfx_color_t *src,
              int w, int h, int32_t src_stride);
/**
 * @brief As gfx_blit(), but pixels equal to @p key are left untouched.
 * @param c           The canvas.
 * @param x           Left edge of the destination.
 * @param y           Top edge of the destination.
 * @param src         The source pixels.
 * @param w           Width, pixels.
 * @param h           Height, pixels.
 * @param src_stride  Distance between the source's rows, pixels.
 * @param key         The color that is skipped.
 */
void gfx_blit_key(gfx_canvas_t *c, int x, int y, const gfx_color_t *src,
                  int w, int h, int32_t src_stride, gfx_color_t key);
/**
 * @brief Expand a 1-bit-per-pixel mask (most significant bit first, rows
 * padded to whole bytes).
 * @param c     The canvas.
 * @param x     Left edge of the destination.
 * @param y     Top edge of the destination.
 * @param bits  The mask.
 * @param w     Width, pixels.
 * @param h     Height, pixels.
 * @param fg    The color of the set bits; the others are left untouched.
 */
void gfx_blit_1bpp(gfx_canvas_t *c, int x, int y, const uint8_t *bits,
                   int w, int h, gfx_color_t fg);

/* --------------------------------------------------------------------- text */

/*
 * Text is UTF-8 (Unicode Transformation Format, 8-bit).  Every string a
 * primitive below takes is decoded to code points, and one code point is one
 * cell: the width of a string is its code point count times the cell width,
 * not its byte count.  A font holds the contiguous block first..last and,
 * after it, `extra_count` further glyphs for the code points listed in
 * `extra` (the German letters A, O and U with diaeresis, upper and lower
 * case, and the sharp s).  A code point the font does not hold draws as '?',
 * and a byte that does not start a well-formed sequence is one '?' cell.
 */
/** @brief A monospaced bitmap font. */
typedef struct {
    const uint8_t *glyphs;  /**< (last-first+1+extra_count) * height
                             *   * bytes_per_row bytes                      */
    uint8_t width;          /**< cell width in pixels                          */
    uint8_t height;         /**< cell height in pixels                         */
    uint8_t bytes_per_row;  /**< 1bpp: (width+7)/8, top bit left. 8bpp: width */
    uint8_t bpp;            /**< 1 for a bit mask, 8 for coverage 0..255       */
    uint8_t first;          /**< first encoded code point                      */
    uint8_t last;           /**< last encoded code point                       */
    uint8_t extra_count;    /**< glyphs after the block, 0 for none            */
    const uint16_t *extra;  /**< their code points, ascending                  */
} gfx_font_t;

/** 8x16 monospaced, code points 0x20..0x7E (printable ASCII, the American
 *  Standard Code for Information Interchange) and the German letters.
 *  Labels, units, dense text. */
extern const gfx_font_t gfx_font_8x16;
/** 16x28 monospaced, ASCII 0x20..0x7E and the German letters.  Headings and
 *  medium readouts. */
extern const gfx_font_t gfx_font_16x28;
/** 24x30 digits and punctuation, 0x20..0x3A.  Hero numerals. */
extern const gfx_font_t gfx_font_num_24x30;
/** @brief 10x20 monospaced, ASCII 0x20..0x7E and the German letters.  Cap height
 *  12px.  Labels, tabs, buttons and row values on the 1024 x 600 panel. */
extern const gfx_font_t gfx_font_10x20;
/** @brief 12x24 monospaced, ASCII 0x20..0x7E and the German letters.  Cap height
 *  15px.  Prose: help text, warnings, questions, run results. */
extern const gfx_font_t gfx_font_12x24;
/** @brief 20x36 monospaced, ASCII 0x20..0x7E and the German letters.  Cap height
 *  23px.  Headings, tile names and choice faces on the 1024 x 600 panel. */
extern const gfx_font_t gfx_font_20x36;
/** @brief 32x44 digits and punctuation, 0x20..0x3A.  Cap height 40px.  Hero
 *  numerals on the 1024 x 600 panel. */
extern const gfx_font_t gfx_font_num_32x44;

/* ------------------------------------------------- seven-segment numerals */

/*
 * The look of a bench instrument rather than of a screen: digits built from
 * seven bars, with the unlit ones optionally left showing.  Covers 0-9, '-',
 * '.', ':' and space; anything else draws as blank.
 */
/** @brief The size and look of seven-segment numerals. */
typedef struct {
    int  digit_w;    /**< one digit cell, excluding the gap                 */
    int  digit_h;    /**< digit height                                      */
    int  thickness;  /**< bar thickness; about digit_h/7 looks right        */
    int  gap;        /**< between cells                                     */
    int  slant;      /**< pixels of lean over the full height, 0 upright    */
    bool ghost;      /**< draw the unlit segments in @p off                 */
} gfx_seg_style_t;

/**
 * @brief Draw @p s as segment digits.
 * @param c    The canvas.
 * @param x    Left edge.
 * @param y    Top edge.
 * @param s    The text.
 * @param st   The style.
 * @param on   The color of lit segments.
 * @param off  The color of unlit segments, drawn when the style's ghost is set.
 * @return The width drawn, pixels.
 */
int gfx_seg_text(gfx_canvas_t *c, int x, int y, const char *s,
                 const gfx_seg_style_t *st, gfx_color_t on, gfx_color_t off);
/**
 * @brief Width @p s would occupy, without drawing it.
 * @param s   The text.
 * @param st  The style.
 * @return The width, pixels.
 */
int gfx_seg_width(const char *s, const gfx_seg_style_t *st);

/**
 * @brief The next code point of a UTF-8 string, advancing past it.
 *
 * Returns 0 at the terminator without advancing.  A malformed or truncated
 * sequence returns '?' and advances one byte, so a string cut mid-character
 * costs one cell rather than the rest of the string.
 *
 * @param s  The string's position; moved past the code point.
 * @return The code point.
 */
uint32_t gfx_utf8_next(const char **s);

/**
 * @brief Cells @p s occupies in a monospaced face: its code point count.
 * @param s  The text.
 * @return The cell count.
 */
int gfx_text_cells(const char *s);

/**
 * @brief The byte length of the first @p cells cells of @p s: where to cut it
 * so the cut falls between two characters, never inside one.
 * @param s      The text.
 * @param cells  The cells to keep.
 * @return The length, bytes.
 */
size_t gfx_text_prefix(const char *s, int cells);

/**
 * @brief Pixel width of @p s at integer @p scale.
 * @param font   The font.
 * @param s      The text.
 * @param scale  The font's scale; below 1 counts as 1.
 * @return The width, pixels.
 */
int gfx_text_width(const gfx_font_t *font, const char *s, int scale);
/**
 * @brief Pixel height of one line at integer @p scale.
 * @param font   The font.
 * @param scale  The font's scale.
 * @return The height, pixels.
 */
int gfx_text_height(const gfx_font_t *font, int scale);

/**
 * @brief Draw one glyph, transparent background.
 *
 * @p ch is a single byte, so it reaches ASCII only; a string reaches the
 * rest of the font through gfx_text().
 *
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param ch     The character.
 * @param font   The font.
 * @param fg     The text color.
 * @param scale  The font's scale.
 * @return The advance, pixels.
 */
int gfx_char(gfx_canvas_t *c, int x, int y, char ch, const gfx_font_t *font,
             gfx_color_t fg, int scale);
/**
 * @brief Draw a string, transparent background.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param s      The text.
 * @param font   The font.
 * @param fg     The text color.
 * @param scale  The font's scale.
 * @return The advance, pixels.
 */
int gfx_text(gfx_canvas_t *c, int x, int y, const char *s,
             const gfx_font_t *font, gfx_color_t fg, int scale);
/**
 * @brief Draw a string over a solid background cell.
 * @param c      The canvas.
 * @param x      Left edge.
 * @param y      Top edge.
 * @param s      The text.
 * @param font   The font.
 * @param fg     The text color.
 * @param bg     The background color.
 * @param scale  The font's scale.
 * @return The advance, pixels.
 */
int gfx_text_bg(gfx_canvas_t *c, int x, int y, const char *s,
                const gfx_font_t *font, gfx_color_t fg, gfx_color_t bg, int scale);

/**
 * @brief The pixels gfx_text_rotated() would cover, packed as
 * (y << 16) | x, in scan order and already clipped.
 *
 * The stencil depends only on the geometry and the alpha, so an overlay drawn
 * on every frame records it once and writes the points thereafter, instead of
 * rotating and dividing per pixel over the whole canvas each time.
 *
 * @param c          The canvas.
 * @param cx         Center of the rotation, column.
 * @param cy         Center of the rotation, row.
 * @param s          The text.
 * @param font       The font.
 * @param scale      The font's scale.
 * @param alpha      Coverage, 0 to 255.
 * @param angle_deg  The rotation, degrees.
 * @param out        Receives the points.
 * @param max_out    Capacity of @p out, points.
 * @return The count, or -1 when more than @p max_out are covered or @p out is
 *         NULL.
 */
int gfx_text_rotated_points(gfx_canvas_t *c, int cx, int cy, const char *s,
                            const gfx_font_t *font, int scale, uint8_t alpha,
                            float angle_deg, uint32_t *out, int max_out);

/**
 * @brief Draw @p s rotated about (@p cx, @p cy) as a stencil over what is
 * already there, covering @p alpha out of 255 of the glyph pixels.
 *
 * Inverse-mapped: every destination pixel in the rotated bounding box asks
 * which glyph pixel it comes from.  Forward mapping leaves holes at any angle
 * that is not a multiple of 90 degrees.
 *
 * Only pixels the glyph sets are touched, so the cost is the ink rather than
 * the box; on this panel the frame rate is decided by how many cache lines a
 * frame dirties.
 *
 * Built for the simulation watermark.  It is not a general text transform and
 * is not fast enough for one.
 *
 * `alpha` is coverage, not weight: the glyphs are stencilled at full opacity
 * onto that fraction of pixels, chosen by an ordered dither.  Drawing this
 * twice over the same pixels equals drawing it once, which a full-screen
 * overlay over selectively repainted screens requires.
 */
void gfx_text_rotated(gfx_canvas_t *c, int cx, int cy, const char *s,
                      const gfx_font_t *font, gfx_color_t fg, int scale,
                      uint8_t alpha, float angle_deg);

/** @brief Where text sits inside a box. */
typedef enum {
    GFX_ALIGN_LEFT = 0,
    GFX_ALIGN_CENTER,
    GFX_ALIGN_RIGHT,
} gfx_align_t;

/**
 * @brief Draw @p s aligned inside @p box, vertically centered.
 * @param c     The canvas; NULL draws nothing.
 * @param box   The box the text is placed in.
 * @param s     The text; NULL draws nothing.
 * @param font  The font; NULL draws nothing.
 * @param fg    The text colour.
 * @param scale The font's scale, 1 or more; below 1 counts as 1.
 * @param align Left, center or right inside @p box.
 */
void gfx_text_in(gfx_canvas_t *c, gfx_rect_t box, const char *s,
                 const gfx_font_t *font, gfx_color_t fg, int scale,
                 gfx_align_t align);

#ifdef GFX_TEXT_TRACE
/*
 * The fit check's view of a frame, compiled in only when GFX_TEXT_TRACE is
 * defined: the panel and the host suite build without it.  The harness
 * that defines GFX_TEXT_TRACE defines these two.  Coordinates are the
 * canvas's own; the harness maps a sub-canvas back to the panel from its
 * pixel pointer.
 */
/**
 * @brief Called for every string drawn.
 * @param c    The canvas.
 * @param x    Left edge of the string.
 * @param y    Top edge of the string.
 * @param w    Width of the string.
 * @param h    Height of the string.
 * @param s    The text.
 * @param box  The box gfx_text_in() was given; NULL from the other
 *             primitives.
 */
void gfx_trace_text(const gfx_canvas_t *c, int x, int y, int w, int h,
                    const char *s, const gfx_rect_t *box);
/**
 * @brief Called for every filled rounded or chamfered rectangle: the shapes a
 * label sits inside.
 * @param c  The canvas.
 * @param x  Left edge.
 * @param y  Top edge.
 * @param w  Width.
 * @param h  Height.
 */
void gfx_trace_box(const gfx_canvas_t *c, int x, int y, int w, int h);
#endif

#ifdef __cplusplus
}
#endif
