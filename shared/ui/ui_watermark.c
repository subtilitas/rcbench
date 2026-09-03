/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_watermark.h"

#include <math.h>
#include <string.h>

#include "ui_theme.h"

#define TEXT "SIMULATION"

/*
 * 15 % opacity.  Below about 10 % the mark disappears against the plot grid
 * in a photograph; above about 20 % it competes with the readings.
 */
#define ALPHA 38   /* 0.15 * 255 */

/* 3,439 points on the 800x480 panel; the rest is headroom for another
 * canvas size.  A mark that does not fit is drawn by scanning instead. */
#define WM_MAX_POINTS 5120

/*
 * Left zero so the table lands in .bss rather than .data: an initialiser on
 * any member puts all 20 KB in the image.  A width of zero is no canvas, so
 * it never matches and the first call builds.
 */
static struct {
    int         w, h;
    int16_t     clip_x, clip_y, clip_w, clip_h;
    gfx_color_t colour;
    int         count;              /**< -1 when the mark does not fit */
    uint32_t    pt[WM_MAX_POINTS];
} s_cache;

void ui_watermark_invalidate(void)
{
    s_cache.w = 0;
}

void ui_watermark(gfx_canvas_t *c)
{
    if (c == NULL || c->width <= 0 || c->height <= 0) {
        return;
    }

    /*
     * Along the screen's own diagonal rather than a fixed 45 degrees, so it
     * runs corner to corner on this 5:3 panel instead of leaving two empty
     * triangles.
     */
    const float angle = -atan2f((float)c->height, (float)c->width)
                        * 180.0f / 3.14159265358979f;
    const float diag = sqrtf((float)c->width * (float)c->width
                             + (float)c->height * (float)c->height);

    /*
     * The largest whole scale whose rotated bounding box fits the canvas.
     * Sizing against the diagonal alone ignores the text's height; at 5:3
     * that overflows the canvas vertically by 26 px.
     */
    const int len = (int)strlen(TEXT);
    const float fw = (float)(len * gfx_font_16x28.width);
    const float fh = (float)gfx_font_16x28.height;
    const float ca = fabsf(cosf(angle * 3.14159265358979f / 180.0f));
    const float sa = fabsf(sinf(angle * 3.14159265358979f / 180.0f));

    const float by_w = (float)c->width  / (fw * ca + fh * sa);
    const float by_h = (float)c->height / (fw * sa + fh * ca);
    float fit = (by_w < by_h) ? by_w : by_h;
    fit *= 0.96f;   /* a hair of air, so no stroke touches an edge */

    int scale = (int)fit;
    if (scale < 1) {
        scale = 1;
    }
    (void)diag;

    const gfx_color_t fg = ui_theme_color(UI_C_TEXT);

    /*
     * The stencil is the same set of pixels on every frame: the geometry
     * comes from the canvas and the coverage from ALPHA, and neither moves.
     * Rotating and dividing per pixel costs a scan of the whole canvas to
     * write under 1% of it, so the points are recorded once and written
     * thereafter.  On an 800x480 canvas the mark covers 3,439 pixels.
     */
    if (s_cache.w != c->width || s_cache.h != c->height
        || s_cache.clip_x != c->clip.x || s_cache.clip_y != c->clip.y
        || s_cache.clip_w != c->clip.w || s_cache.clip_h != c->clip.h
        || s_cache.colour != fg) {
        s_cache.w      = c->width;
        s_cache.h      = c->height;
        s_cache.clip_x = c->clip.x;
        s_cache.clip_y = c->clip.y;
        s_cache.clip_w = c->clip.w;
        s_cache.clip_h = c->clip.h;
        s_cache.colour = fg;
        s_cache.count  = gfx_text_rotated_points(
            c, c->width / 2, c->height / 2, TEXT, &gfx_font_16x28, scale,
            ALPHA, angle, s_cache.pt, WM_MAX_POINTS);
    }

    /* A canvas whose mark does not fit the table is drawn the long way. */
    if (s_cache.count < 0) {
        gfx_text_rotated(c, c->width / 2, c->height / 2, TEXT,
                         &gfx_font_16x28, fg, scale, ALPHA, angle);
        return;
    }

    for (int i = 0; i < s_cache.count; ++i) {
        gfx_pixel(c, (int)(s_cache.pt[i] & 0xffffu),
                  (int)(s_cache.pt[i] >> 16), fg);
    }
}
