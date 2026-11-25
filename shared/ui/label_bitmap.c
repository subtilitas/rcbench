/**
 * @file
 * @brief The label's 1-bit canvas (label_bitmap.h): the roll's size in dots,
 *        the mapping of reading coordinates to the printer's column-major
 *        layout, filled rectangles and text in the panel's faces.
 *
 * SPDX-License-Identifier: MIT
 */

#include "label_bitmap.h"

#include <string.h>

/** @brief Coverage from which an 8-bit glyph pixel is black. */
#define LABEL_INK 128u

label_err_t label_roll_check(const label_roll_t *roll, uint16_t *w,
                             uint16_t *cols)
{
    if (roll == NULL || roll->w_mm == 0u || roll->h_mm == 0u) {
        return LABEL_NO_LABEL;
    }
    if (roll->w_mm > LABEL_W_MAX_MM || roll->h_mm > LABEL_H_MAX_MM) {
        return LABEL_TOO_LARGE;
    }
    if (roll->w_mm < LABEL_W_MIN_MM || roll->h_mm < LABEL_H_MIN_MM) {
        return LABEL_TOO_SMALL;
    }
    if (w != NULL) {
        *w = (uint16_t)(roll->w_mm * LABEL_DOTS_MM);
    }
    if (cols != NULL) {
        *cols = (uint16_t)(roll->h_mm * LABEL_DOTS_MM - 2u * LABEL_MARGIN);
    }
    return LABEL_OK;
}

size_t label_bmp_bytes(const label_roll_t *roll)
{
    uint16_t cols = 0u;
    if (label_roll_check(roll, NULL, &cols) != LABEL_OK) {
        return 0u;
    }
    return (size_t)cols * LABEL_COL_BYTES;
}

label_err_t label_bmp_init(label_bmp_t *b, const label_roll_t *roll)
{
    uint16_t w = 0u;
    uint16_t cols = 0u;
    const label_err_t err = label_roll_check(roll, &w, &cols);
    if (err != LABEL_OK) {
        return err;
    }
    const size_t need = (size_t)cols * LABEL_COL_BYTES;
    if (b->bits == NULL || b->cap < need) {
        return LABEL_MEMORY;
    }
    b->cols = cols;
    b->w    = w;
    b->off  = (uint16_t)((LABEL_HEAD_DOTS - w) / 2u);
    memset(b->bits, 0, need);
    return LABEL_OK;
}

/**
 * @brief The byte and bit of reading (x, y), false outside the label.
 * @param b    the bitmap
 * @param x    from the left edge
 * @param y    from the top edge
 * @param at   out: the byte's index
 * @param bit  out: the bit's mask
 * @return whether the dot is on the label
 */
static bool dot_of(const label_bmp_t *b, int x, int y, size_t *at,
                   uint8_t *bit)
{
    if (x < 0 || y < 0 || x >= (int)b->w || y >= (int)b->cols) {
        return false;
    }
    const unsigned row = (unsigned)b->off + (unsigned)(b->w - 1u) - (unsigned)x;
    *at  = (size_t)y * LABEL_COL_BYTES + row / 8u;
    *bit = (uint8_t)(1u << (row % 8u));
    return true;
}

void label_px(label_bmp_t *b, int x, int y)
{
    size_t  at  = 0u;
    uint8_t bit = 0u;
    if (dot_of(b, x, y, &at, &bit)) {
        b->bits[at] |= bit;
    }
}

bool label_px_at(const label_bmp_t *b, int x, int y)
{
    size_t  at  = 0u;
    uint8_t bit = 0u;
    return dot_of(b, x, y, &at, &bit) && (b->bits[at] & bit) != 0u;
}

void label_fill(label_bmp_t *b, int x, int y, int w, int h)
{
    const int x0 = x < 0 ? 0 : x;
    const int y0 = y < 0 ? 0 : y;
    const int x1 = (x + w > (int)b->w) ? (int)b->w : x + w;
    const int y1 = (y + h > (int)b->cols) ? (int)b->cols : y + h;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            label_px(b, xx, yy);
        }
    }
}

/**
 * @brief The glyph rows of code point @p cp, '?' where the face has none.
 * @param font  the face
 * @param cp    the code point
 * @return the glyph's first row, or NULL when the face has no '?' either
 *         or no glyph table
 */
static const uint8_t *glyph_of(const gfx_font_t *font, uint32_t cp)
{
    if (font->glyphs == NULL) {
        return NULL;
    }
    for (int pass = 0; pass < 2; pass++) {
        if (cp >= font->first && cp <= font->last) {
            const size_t idx = cp - font->first;
            return font->glyphs + idx * font->height * font->bytes_per_row;
        }
        for (unsigned i = 0u; i < font->extra_count; i++) {
            if (font->extra[i] == cp) {
                const size_t idx = (size_t)(font->last - font->first) + 1u + i;
                return font->glyphs + idx * font->height * font->bytes_per_row;
            }
        }
        cp = (uint32_t)'?';
    }
    return NULL;
}

/**
 * @brief Whether pixel @p col of a glyph row is ink.
 * @param font  the face
 * @param line  the glyph row
 * @param col   the pixel, 0 left
 * @return true for a set bit or a coverage of 128 and above
 */
static bool ink_at(const gfx_font_t *font, const uint8_t *line, int col)
{
    if (font->bpp == 8u) {
        return line[col] >= LABEL_INK;
    }
    return (line[col / 8] & (0x80u >> (col % 8))) != 0u;
}

/**
 * @brief Whether a glyph has a dot of ink.
 * @param font  the face
 * @param rows  the glyph's first row
 * @return true when a pixel is ink
 */
static bool glyph_inked(const gfx_font_t *font, const uint8_t *rows)
{
    for (int r = 0; r < (int)font->height; r++) {
        const uint8_t *line = rows + (size_t)r * font->bytes_per_row;
        for (int c = 0; c < (int)font->width; c++) {
            if (ink_at(font, line, c)) {
                return true;
            }
        }
    }
    return false;
}

bool label_face_draws(const gfx_font_t *font, const char *s)
{
    for (uint32_t cp = gfx_utf8_next(&s); cp != 0u; cp = gfx_utf8_next(&s)) {
        if (cp == (uint32_t)' ') {
            continue;
        }
        const uint8_t *rows = glyph_of(font, cp);
        if (rows == NULL || !glyph_inked(font, rows)) {
            return false;
        }
    }
    return true;
}

int label_text(label_bmp_t *b, int x, int y, const gfx_font_t *font,
               const char *s, int x_end)
{
    int cells = 0;
    int cx = x;
    for (uint32_t cp = gfx_utf8_next(&s); cp != 0u; cp = gfx_utf8_next(&s)) {
        if (cx + (int)font->width > x_end) {
            break;
        }
        const uint8_t *rows = glyph_of(font, cp);
        for (int r = 0; rows != NULL && r < (int)font->height; r++) {
            const uint8_t *line = rows + (size_t)r * font->bytes_per_row;
            for (int c = 0; c < (int)font->width; c++) {
                if (ink_at(font, line, c)) {
                    label_px(b, cx + c, y + r);
                }
            }
        }
        cx += font->width;
        cells++;
    }
    return cells;
}
