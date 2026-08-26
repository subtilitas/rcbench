/*
 * gfx -- a small, dependency-free RGB565 rasteriser.
 *
 * Nothing in this header touches ESP-IDF, FreeRTOS or any hardware.  It draws
 * into a plain block of 16-bit pixels described by a gfx_canvas_t, which makes
 * it (a) reusable for off-screen sprites and (b) unit-testable on the host.
 * The display component is what binds a canvas to an actual framebuffer.
 *
 * Pixel format is RGB565 in the CPU's native byte order, which is what the
 * ESP32-S3 RGB peripheral expects for a 16-bit data bus.
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

typedef uint16_t gfx_color_t;

/** Pack 8-bit R/G/B into RGB565. */
#define GFX_RGB(r, g, b)                                                    \
    ((gfx_color_t)((((uint16_t)(r) & 0xF8u) << 8) |                         \
                   (((uint16_t)(g) & 0xFCu) << 3) |                         \
                   (((uint16_t)(b) & 0xF8u) >> 3)))

#define GFX_BLACK       GFX_RGB(0, 0, 0)
#define GFX_WHITE       GFX_RGB(255, 255, 255)
#define GFX_RED         GFX_RGB(255, 0, 0)
#define GFX_GREEN       GFX_RGB(0, 255, 0)
#define GFX_BLUE        GFX_RGB(0, 0, 255)
#define GFX_YELLOW      GFX_RGB(255, 255, 0)
#define GFX_CYAN        GFX_RGB(0, 255, 255)
#define GFX_MAGENTA     GFX_RGB(255, 0, 255)
#define GFX_GREY(v)     GFX_RGB(v, v, v)


/** Split RGB565 back into 8-bit components (values are quantised). */
static inline void gfx_unpack(gfx_color_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(c & 0x1Fu);
    if (r) { *r = (uint8_t)((r5 << 3) | (r5 >> 2)); }
    if (g) { *g = (uint8_t)((g6 << 2) | (g6 >> 4)); }
    if (b) { *b = (uint8_t)((b5 << 3) | (b5 >> 2)); }
}

/** Linear blend of two colours, t in [0,255] where 0 == a and 255 == b. */
gfx_color_t gfx_lerp(gfx_color_t a, gfx_color_t b, uint8_t t);

/* ----------------------------------------------------------------- geometry */

typedef struct {
    int16_t x, y, w, h;
} gfx_rect_t;

/*
 * Saturate rather than truncate.  The fields are int16_t and the arguments are
 * int, so a plain cast draws a shape that is entirely off-screen back onto the
 * panel at a wrapped position -- which is the opposite of clipping, and turns
 * an out-of-range blit's source offset into a read far outside its buffer.
 */
static inline int16_t gfx_i16(int v)
{
    if (v < INT16_MIN) { return INT16_MIN; }
    if (v > INT16_MAX) { return INT16_MAX; }
    return (int16_t)v;
}

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

static inline bool gfx_rect_empty(gfx_rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

static inline bool gfx_rect_contains(gfx_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** Intersect two rects.  Returns false (and zeroes *out) when they miss. */
bool gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b, gfx_rect_t *out);

/* ------------------------------------------------------------------- canvas */

typedef struct {
    gfx_color_t *pixels;  /**< top-left pixel                                */
    int16_t      width;   /**< visible width in pixels                       */
    int16_t      height;  /**< visible height in pixels                      */
    int32_t      stride;  /**< distance between rows, in pixels (>= width)   */
    gfx_rect_t   clip;    /**< all drawing is confined to this rect          */
} gfx_canvas_t;

/**
 * Bind a canvas to a pixel buffer.  @p stride is in pixels; pass 0 to mean
 * "tightly packed" (stride == width).
 */
void gfx_canvas_init(gfx_canvas_t *c, gfx_color_t *pixels,
                     int width, int height, int32_t stride);

/** Carve a sub-canvas out of @p src.  Returns false if the window is empty. */
bool gfx_canvas_sub(const gfx_canvas_t *src, gfx_rect_t window, gfx_canvas_t *out);

/** Restrict drawing to @p r (intersected with the canvas).  False if empty. */
bool gfx_clip_set(gfx_canvas_t *c, gfx_rect_t r);
/** Restrict further, relative to the current clip.  False if empty. */
bool gfx_clip_intersect(gfx_canvas_t *c, gfx_rect_t r);
void gfx_clip_reset(gfx_canvas_t *c);
static inline gfx_rect_t gfx_clip_get(const gfx_canvas_t *c) { return c->clip; }

/* --------------------------------------------------------------- primitives */

void gfx_clear(gfx_canvas_t *c, gfx_color_t color);
void gfx_pixel(gfx_canvas_t *c, int x, int y, gfx_color_t color);
/** Reads back a pixel; returns 0 when (x,y) is outside the canvas. */
gfx_color_t gfx_pixel_get(const gfx_canvas_t *c, int x, int y);

void gfx_fill_rect(gfx_canvas_t *c, int x, int y, int w, int h, gfx_color_t color);
void gfx_draw_rect(gfx_canvas_t *c, int x, int y, int w, int h, gfx_color_t color);
void gfx_hline(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color);
void gfx_vline(gfx_canvas_t *c, int x, int y, int h, gfx_color_t color);
void gfx_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1, gfx_color_t color);
void gfx_thick_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1,
                    int thickness, gfx_color_t color);

void gfx_draw_circle(gfx_canvas_t *c, int cx, int cy, int r, gfx_color_t color);
void gfx_fill_circle(gfx_canvas_t *c, int cx, int cy, int r, gfx_color_t color);

void gfx_draw_round_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                         int r, gfx_color_t color);
void gfx_fill_round_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                         int r, gfx_color_t color);

/*
 * Chamfered ("cut corner") rectangles.  The cut is a 45-degree bevel of the
 * given size; per-corner control is what makes the HUD look -- cutting only
 * the top-left and bottom-right of a panel reads very differently from
 * cutting all four.  A cut of 0 leaves that corner square.
 */
void gfx_fill_chamfer_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                           int cut, gfx_color_t color);
void gfx_draw_chamfer_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                           int cut, gfx_color_t color);
void gfx_fill_chamfer_rect_ex(gfx_canvas_t *c, int x, int y, int w, int h,
                              int tl, int tr, int br, int bl, gfx_color_t color);
void gfx_draw_chamfer_rect_ex(gfx_canvas_t *c, int x, int y, int w, int h,
                              int tl, int tr, int br, int bl, gfx_color_t color);

/** Vertical gradient fill, top colour @p a to bottom colour @p b. */
void gfx_fill_rect_gradient(gfx_canvas_t *c, int x, int y, int w, int h,
                            gfx_color_t a, gfx_color_t b);

/* -------------------------------------------------------------------- blits */

/** Copy a rectangular block of pixels.  @p src_stride is in pixels. */
void gfx_blit(gfx_canvas_t *c, int x, int y, const gfx_color_t *src,
              int w, int h, int32_t src_stride);
/** As gfx_blit(), but pixels equal to @p key are left untouched. */
void gfx_blit_key(gfx_canvas_t *c, int x, int y, const gfx_color_t *src,
                  int w, int h, int32_t src_stride, gfx_color_t key);
/** Expand a 1-bit-per-pixel mask (MSB first, rows padded to whole bytes). */
void gfx_blit_1bpp(gfx_canvas_t *c, int x, int y, const uint8_t *bits,
                   int w, int h, gfx_color_t fg);

/* --------------------------------------------------------------------- text */

typedef struct {
    const uint8_t *glyphs;  /**< (last-first+1) * height * bytes_per_row bytes */
    uint8_t width;          /**< cell width in pixels                          */
    uint8_t height;         /**< cell height in pixels                         */
    uint8_t bytes_per_row;  /**< (width + 7) / 8; MSB of byte 0 is leftmost    */
    uint8_t first;          /**< first encoded code point                      */
    uint8_t last;           /**< last encoded code point                       */
} gfx_font_t;

/** 8x16 monospaced, ASCII 0x20..0x7E.  Labels, units, dense text. */
extern const gfx_font_t gfx_font_8x16;
/** 16x28 monospaced, ASCII 0x20..0x7E.  Headings and medium readouts. */
extern const gfx_font_t gfx_font_16x28;
/** 24x30 digits and punctuation, 0x20..0x3A.  Hero numerals. */
extern const gfx_font_t gfx_font_num_24x30;

/** Pixel width of @p s at integer @p scale (scale < 1 is treated as 1). */
int gfx_text_width(const gfx_font_t *font, const char *s, int scale);
/** Pixel height of one line at integer @p scale. */
int gfx_text_height(const gfx_font_t *font, int scale);

/** Draw one glyph, transparent background.  Returns the advance in pixels. */
int gfx_char(gfx_canvas_t *c, int x, int y, char ch, const gfx_font_t *font,
             gfx_color_t fg, int scale);
/** Draw a string, transparent background.  Returns the advance in pixels. */
int gfx_text(gfx_canvas_t *c, int x, int y, const char *s,
             const gfx_font_t *font, gfx_color_t fg, int scale);
/** Draw a string over a solid background cell.  Returns the advance. */
int gfx_text_bg(gfx_canvas_t *c, int x, int y, const char *s,
                const gfx_font_t *font, gfx_color_t fg, gfx_color_t bg, int scale);

typedef enum {
    GFX_ALIGN_LEFT = 0,
    GFX_ALIGN_CENTER,
    GFX_ALIGN_RIGHT,
} gfx_align_t;

/** Draw @p s aligned inside @p box, vertically centred. */
void gfx_text_in(gfx_canvas_t *c, gfx_rect_t box, const char *s,
                 const gfx_font_t *font, gfx_color_t fg, int scale,
                 gfx_align_t align);

#ifdef __cplusplus
}
#endif
