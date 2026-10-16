/*
 * gfx text: UTF-8 (Unicode Transformation Format, 8-bit) decoding, glyph
 * drawing, string measuring and the rotated dither stencil.  See
 * include/gfx.h and gfx_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx_int.h"

#include <math.h>

/* -------------------------------------------------------------------- text */

uint32_t gfx_utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    const unsigned c0 = p[0];
    if (c0 == 0u) {
        return 0u;
    }
    if (c0 < 0x80u) {
        *s += 1;
        return c0;
    }
    /* The sequence length from the lead byte.  A continuation byte, or a
     * lead byte UTF-8 does not use, is one malformed cell. */
    unsigned len;
    uint32_t cp;
    if ((c0 & 0xE0u) == 0xC0u && c0 >= 0xC2u) {
        len = 2u;
        cp = c0 & 0x1Fu;
    } else if ((c0 & 0xF0u) == 0xE0u) {
        len = 3u;
        cp = c0 & 0x0Fu;
    } else if ((c0 & 0xF8u) == 0xF0u && c0 <= 0xF4u) {
        len = 4u;
        cp = c0 & 0x07u;
    } else {
        *s += 1;
        return '?';
    }
    for (unsigned i = 1u; i < len; ++i) {
        /* The terminator is not a continuation byte, so a sequence cut short
         * stops here rather than reading past the end. */
        if ((p[i] & 0xC0u) != 0x80u) {
            *s += 1;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    /* An overlong three- or four-byte form, a surrogate, or a code point
     * above U+10FFFF is malformed. */
    if ((len == 3u && cp < 0x800u) || (len == 4u && cp < 0x10000u)
        || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
        *s += 1;
        return '?';
    }
    *s += len;
    return cp;
}

int gfx_text_cells(const char *s)
{
    if (s == NULL) {
        return 0;
    }
    int n = 0;
    while (gfx_utf8_next(&s) != 0u) {
        ++n;
    }
    return n;
}

size_t gfx_text_prefix(const char *s, int cells)
{
    if (s == NULL) {
        return 0u;
    }
    const char *p = s;
    for (int i = 0; i < cells; ++i) {
        if (gfx_utf8_next(&p) == 0u) {
            break;
        }
    }
    return (size_t)(p - s);
}

/* The glyph index of code point @p cp, or -1 when the font has none.  The
 * extra list is a handful long, so a scan costs less than a table would. */
static int glyph_index(const gfx_font_t *font, uint32_t cp)
{
    if (cp >= font->first && cp <= font->last) {
        return (int)(cp - font->first);
    }
    for (unsigned i = 0u; i < font->extra_count; ++i) {
        if (font->extra[i] == cp) {
            return (int)((unsigned)(font->last - font->first) + 1u + i);
        }
    }
    return -1;
}

static const uint8_t *glyph_rows(const gfx_font_t *font, uint32_t cp)
{
    int idx = glyph_index(font, cp);
    if (idx < 0) {
        idx = glyph_index(font, '?');
        if (idx < 0) {
            return NULL;
        }
    }
    size_t stride = font->bytes_per_row ? font->bytes_per_row : 1u;
    return font->glyphs + (size_t)idx * font->height * stride;
}

/*
 * One glyph pixel's coverage, 0 transparent to 255 solid.
 *
 * Both formats go through here so the blitters below do not each carry a
 * branch on the font's depth: a 1bpp face answers 0 or 255 and an 8bpp one
 * answers what it stored.
 */
static uint8_t glyph_cov(const gfx_font_t *font, const uint8_t *line, int col)
{
    if (font->bpp == 8) {
        return line[col];
    }
    return (line[col >> 3] & (uint8_t)(0x80u >> (col & 7))) ? 255u : 0u;
}

int gfx_text_height(const gfx_font_t *font, int scale)
{
    if (!font) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    return font->height * scale;
}

int gfx_text_width(const gfx_font_t *font, const char *s, int scale)
{
    if (!font || !s) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    return gfx_text_cells(s) * font->width * scale;
}

/* One glyph by code point; gfx_char() and gfx_text() both come here. */
static int draw_glyph(gfx_canvas_t *c, int x, int y, uint32_t cp,
                      const gfx_font_t *font, gfx_color_t fg, int scale)
{
    const uint8_t *rows = glyph_rows(font, cp);
    if (!rows) {
        /* '?' is the documented fallback, but the numeric face does not
         * contain one -- and a glyph that silently renders as whitespace makes
         * a fault look like a gap.  Draw the missing-glyph box instead. */
        int gw = font->width * scale;
        int gh = font->height * scale;
        if (gw > 2 && gh > 2) {
            gfx_draw_rect(c, x + 1, y + 1, gw - 2, gh - 2, fg);
        }
        return gw;
    }
    int stride = font->bytes_per_row ? font->bytes_per_row : 1;
    for (int row = 0; row < font->height; ++row) {
        const uint8_t *bits = rows + (size_t)row * stride;
        int col = 0;
        while (col < font->width) {
            const uint8_t cov = glyph_cov(font, bits, col);
            if (cov == 0u) {
                ++col;
                continue;
            }
            if (cov < 255u) {
                /*
                 * An edge pixel, blended against whatever it lands on.  Only
                 * these read the destination back; the solid interior below
                 * still writes straight through, which is what keeps
                 * antialiasing from costing a read per glyph pixel.
                 */
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        const int px = x + col * scale + sx;
                        const int py = y + row * scale + sy;
                        gfx_pixel_inl(c, px, py,
                                      gfx_lerp(gfx_pixel_get_inl(c, px, py),
                                               fg, cov));
                    }
                }
                ++col;
                continue;
            }
            /* Runs of solid pixels become one fill instead of N writes --
             * worth it for the large faces, where a stem is 3-4 pixels wide. */
            int start = col;
            while (col < font->width
                   && glyph_cov(font, bits, col) == 255u) {
                ++col;
            }
            int run = col - start;
            if (scale == 1) {
                gfx_hline_inl(c, x + start, y + row, run, fg);
            } else {
                gfx_fill_rect(c, x + start * scale, y + row * scale,
                              run * scale, scale, fg);
            }
        }
    }
    return font->width * scale;
}

int gfx_char(gfx_canvas_t *c, int x, int y, char ch, const gfx_font_t *font,
             gfx_color_t fg, int scale)
{
    if (!font) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    /* A byte at 0x80 or above is half of a character, never one. */
    const unsigned char b = (unsigned char)ch;
    return draw_glyph(c, x, y, (b < 0x80u) ? b : (uint32_t)'?', font, fg,
                      scale);
}

/* The string, decoded; the public calls below trace it first. */
static int text_run(gfx_canvas_t *c, int x, int y, const char *s,
                    const gfx_font_t *font, gfx_color_t fg, int scale)
{
    int advance = 0;
    TRACE_HOLD();
    for (uint32_t cp = gfx_utf8_next(&s); cp != 0u; cp = gfx_utf8_next(&s)) {
        advance += draw_glyph(c, x + advance, y, cp, font, fg, scale);
    }
    TRACE_RELEASE();
    return advance;
}

int gfx_text(gfx_canvas_t *c, int x, int y, const char *s,
             const gfx_font_t *font, gfx_color_t fg, int scale)
{
    if (!font || !s) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
#ifdef GFX_TEXT_TRACE
    gfx_trace_text(c, x, y, gfx_text_width(font, s, scale),
                   gfx_text_height(font, scale), s, NULL);
#endif
    return text_run(c, x, y, s, font, fg, scale);
}

int gfx_text_bg(gfx_canvas_t *c, int x, int y, const char *s,
                const gfx_font_t *font, gfx_color_t fg, gfx_color_t bg, int scale)
{
    if (!font || !s) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    int w = gfx_text_width(font, s, scale);
    gfx_fill_rect(c, x, y, w, gfx_text_height(font, scale), bg);
    return gfx_text(c, x, y, s, font, fg, scale);
}

void gfx_text_in(gfx_canvas_t *c, gfx_rect_t box, const char *s,
                 const gfx_font_t *font, gfx_color_t fg, int scale,
                 gfx_align_t align)
{
    /* Every other primitive tolerates a NULL canvas through canvas_ok(); this
     * one reads c->clip below, so it has to say so itself. */
    if (!c || !font || !s) {
        return;
    }
    if (scale < 1) {
        scale = 1;
    }
    int tw = gfx_text_width(font, s, scale);
    int th = gfx_text_height(font, scale);
    int x = box.x;
    switch (align) {
    case GFX_ALIGN_CENTER: x = box.x + (box.w - tw) / 2; break;
    case GFX_ALIGN_RIGHT:  x = box.x + box.w - tw;       break;
    case GFX_ALIGN_LEFT:
    default:                                             break;
    }
    int y = box.y + (box.h - th) / 2;

#ifdef GFX_TEXT_TRACE
    gfx_trace_text(c, x, y, tw, th, s, &box);
#endif
    gfx_rect_t saved = c->clip;
    if (gfx_clip_intersect_inl(c, box)) {
        text_run(c, x, y, s, font, fg, scale);
    }
    c->clip = saved;
}

/* ------------------------------------------------------------- rotated text */

/*
 * An 8x8 ordered-dither threshold, used by gfx_text_rotated below.
 */
static const uint8_t k_bayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21,
};

/*
 * One scan of the rotated stencil.  With `out` NULL the covered pixels are
 * written; otherwise they are recorded as packed y:x and nothing is drawn,
 * and the return is the count, or -1 when more than `max_out` are covered.
 *
 * The mark covers under 1% of a canvas it spans corner to corner, so a caller
 * that draws it every frame records the points once and replays them rather
 * than rotating and dividing per pixel across the whole canvas again.
 */
static int rotated_stencil(gfx_canvas_t *c, int cx, int cy, const char *s,
                           const gfx_font_t *font, gfx_color_t fg, int scale,
                           uint8_t alpha, float angle_deg,
                           uint32_t *out, int max_out)
{
    int n = 0;

    if (!canvas_ok(c) || s == NULL || font == NULL || alpha == 0) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }

    /* Decoded once into cells, so a glyph is found by its column.  The mark
     * is one short word; a longer string is cut at the buffer. */
    uint32_t cps[32];
    int len = 0;
    for (uint32_t cp = gfx_utf8_next(&s);
         cp != 0u && len < (int)(sizeof(cps) / sizeof(cps[0]));
         cp = gfx_utf8_next(&s)) {
        cps[len++] = cp;
    }
    if (len <= 0) {
        return 0;
    }
    /* Rounded, so a caller's alpha maps to the nearest 1/64 of coverage. */
    const int thresh = ((int)alpha * 64 + 127) / 255;
    if (thresh <= 0) {
        return 0;
    }
    const int tw = len * font->width * scale;
    const int th = font->height * scale;

    const float rad = angle_deg * 3.14159265358979f / 180.0f;
    const float ca = cosf(rad);
    const float sa = sinf(rad);

    /* The rotated box, so the scan covers the glyphs and little else. */
    const float hw = (fabsf((float)tw * ca) + fabsf((float)th * sa)) * 0.5f;
    const float hh = (fabsf((float)tw * sa) + fabsf((float)th * ca)) * 0.5f;

    int x0 = cx - (int)hw - 1, x1 = cx + (int)hw + 1;
    int y0 = cy - (int)hh - 1, y1 = cy + (int)hh + 1;
    if (x0 < c->clip.x) { x0 = c->clip.x; }
    if (y0 < c->clip.y) { y0 = c->clip.y; }
    if (x1 > c->clip.x + c->clip.w) { x1 = c->clip.x + c->clip.w; }
    if (y1 > c->clip.y + c->clip.h) { y1 = c->clip.y + c->clip.h; }

    for (int dy = y0; dy < y1; ++dy) {
        const float v = (float)(dy - cy);
        for (int dx = x0; dx < x1; ++dx) {
            const float u = (float)(dx - cx);

            /* Rotate the destination point back into the string's own frame. */
            const float fx = u * ca + v * sa + (float)tw * 0.5f;
            const float fy = -u * sa + v * ca + (float)th * 0.5f;
            if (fx < 0.0f || fy < 0.0f) {
                continue;
            }
            const int tx = (int)fx;
            const int ty = (int)fy;
            if (tx >= tw || ty >= th) {
                continue;
            }

            const int idx = tx / (font->width * scale);
            const int gi = glyph_index(font, cps[idx]);
            if (gi < 0) {
                continue;
            }
            const int col = (tx % (font->width * scale)) / scale;
            const int row = ty / scale;

            const uint8_t *glyph = font->glyphs
                + (size_t)gi * font->height * font->bytes_per_row;
            /* Thresholded, not blended: this draws an idempotent stencil,
             * and a soft edge would put half-covered pixels into it that a
             * second pass would darken again. */
            if (glyph_cov(font, glyph + (size_t)row * font->bytes_per_row,
                          col) < 128u) {
                continue;
            }

            /*
             * A stencil at full opacity, not a blend, because this is the one
             * piece of drawing applied over pixels the screen beneath may not
             * have repainted this frame.
             *
             * gfx_lerp reads the destination, so a 15% blend laid over a
             * pixel that already carries last frame's 15% gives 28%, then
             * 39%, and the watermark is solid within a second wherever the
             * background is not repainted every frame.
             *
             * Writing a fixed colour to a fixed subset of pixels is
             * idempotent: applying it twice is applying it once, however
             * many frames pass without the screen beneath it changing.
             * `alpha` is the fraction of pixels covered rather than the
             * weight of a blend.
             */
            if (k_bayer8[((dy & 7) << 3) + (dx & 7)] >= thresh) {
                continue;
            }
            if (out == NULL) {
                gfx_pixel(c, dx, dy, fg);
                continue;
            }
            if (n >= max_out) {
                return -1;
            }
            out[n++] = ((uint32_t)dy << 16) | (uint32_t)dx;
        }
    }
    return n;
}

void gfx_text_rotated(gfx_canvas_t *c, int cx, int cy, const char *s,
                      const gfx_font_t *font, gfx_color_t fg, int scale,
                      uint8_t alpha, float angle_deg)
{
    (void)rotated_stencil(c, cx, cy, s, font, fg, scale, alpha, angle_deg,
                          NULL, 0);
}

int gfx_text_rotated_points(gfx_canvas_t *c, int cx, int cy, const char *s,
                            const gfx_font_t *font, int scale, uint8_t alpha,
                            float angle_deg, uint32_t *out, int max_out)
{
    if (out == NULL || max_out <= 0) {
        return -1;
    }
    return rotated_stencil(c, cx, cy, s, font, 0, scale, alpha, angle_deg,
                           out, max_out);
}
