/**
 * @file label_bitmap.h
 * @brief The 1-bit canvas of a label in the label printer's own layout, drawn
 *        in reading coordinates.
 *
 * The printer for Supvan T50M Pro labels takes its image column by column:
 * 48 bytes a column for the 384 dots across its 48mm head, least significant
 * bit first, 1 = black.  Dot `row` of column `col` is bit `row % 8` of byte
 * `col * 48 + row / 8`.  A column runs across the label; the columns follow
 * each other along the feed.
 *
 * A label of `w_mm` x `h_mm` (8 dots/mm) is `W = w_mm * 8` dots across the
 * head, centered at head row `(384 - W) / 2`, and `h_mm * 8 - 16` image
 * columns long: the print buffer's header adds a top and a bottom margin of
 * 8 dots each.  40 x 30mm is W 320 at row 32 and 224 columns.
 *
 * Reading coordinates: x from the label's left edge, y from its top edge,
 * as the label is read.  Head row 0 is the label's right edge and column 0
 * its top edge (station report 2026-11-25, confirmed on paper), so
 *
 *     column = y,   row = (384 - W) / 2 + (W - 1 - x).
 *
 * label_px() is the one place that knows it.  A dot outside the label is
 * dropped, so no drawing call reaches a head row outside W.
 *
 * Pure C: no heap, no clock, no vendor header.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name The print head and the label limits
 *  @{ */
#define LABEL_HEAD_DOTS   384u  /**< dots across the 48mm head             */
#define LABEL_COL_BYTES   48u   /**< bytes a column: 384 / 8              */
#define LABEL_DOTS_MM     8u    /**< dots a millimetre, both directions   */
#define LABEL_MARGIN      8u    /**< top and bottom margin, dots: the
                                     buffer header's, not in the image     */
#define LABEL_W_MIN_MM    15u   /**< narrowest roll printed, mm           */
#define LABEL_W_MAX_MM    48u   /**< widest roll: the head, mm            */
#define LABEL_H_MIN_MM    20u   /**< shortest label printed, mm           */
#define LABEL_H_MAX_MM    100u  /**< longest label printed, mm            */
/** @brief Image columns of the longest label: 100 * 8 - 16. */
#define LABEL_COLS_MAX    (LABEL_H_MAX_MM * LABEL_DOTS_MM - 2u * LABEL_MARGIN)
/** @brief Bitmap bytes of the longest label: 784 columns, 37,632 bytes. */
#define LABEL_BMP_MAX     ((size_t)LABEL_COLS_MAX * LABEL_COL_BYTES)
/** @} */

/** @brief Why a label is not drawn. */
typedef enum {
    LABEL_OK = 0,      /**< drawn                                          */
    LABEL_NO_LABEL,    /**< the roll's record is zeroed: no label loaded   */
    LABEL_TOO_SMALL,   /**< narrower than 15mm, shorter than 20mm, or a QR
                            module under 4 dots                            */
    LABEL_TOO_LARGE,   /**< wider than 48mm or longer than 100mm           */
    LABEL_MEMORY,      /**< the caller's buffer holds less than the label  */
    LABEL_QR           /**< the text does not fit a QR code of version 3   */
} label_err_t;

/** @brief The loaded roll as the printer's material record gives it. */
typedef struct {
    uint8_t w_mm;   /**< label width across the head, mm; 0 unknown      */
    uint8_t h_mm;   /**< label length along the feed, mm; 0 unknown      */
} label_roll_t;

/** @brief A label's bitmap in the printer's layout. */
typedef struct {
    uint8_t *bits;  /**< the caller's buffer, @c cols * 48 bytes used      */
    size_t   cap;   /**< its size, bytes                                   */
    uint16_t cols;  /**< image columns: the label's height in dots, y      */
    uint16_t w;     /**< dots across the label: its width, x               */
    uint16_t off;   /**< head row of the label's right edge: (384 - w) / 2 */
} label_bmp_t;

/**
 * @brief Whether a roll can be printed, and its size in dots.
 * @param roll  the roll
 * @param w     out: dots across, or NULL
 * @param cols  out: image columns, or NULL
 * @return LABEL_OK, LABEL_NO_LABEL (a zero in the record), LABEL_TOO_SMALL
 *         or LABEL_TOO_LARGE
 */
label_err_t label_roll_check(const label_roll_t *roll, uint16_t *w,
                             uint16_t *cols);

/**
 * @brief Size the bitmap for @p roll in the buffer @c b->bits of
 *        @c b->cap bytes and clear it to white.
 * @param b     the bitmap; @c bits and @c cap set by the caller
 * @param roll  the roll
 * @return LABEL_OK, a refusal of label_roll_check(), or LABEL_MEMORY
 */
label_err_t label_bmp_init(label_bmp_t *b, const label_roll_t *roll);

/**
 * @brief Bytes the bitmap of @p roll takes, 0 for a roll that is refused.
 * @param roll  the roll
 * @return @c cols * 48
 */
size_t label_bmp_bytes(const label_roll_t *roll);

/**
 * @brief Set the dot at reading (x, y) black; a dot outside the label is
 *        dropped.
 * @param b  the bitmap
 * @param x  from the left edge, dots
 * @param y  from the top edge, dots
 */
void label_px(label_bmp_t *b, int x, int y);

/**
 * @brief Whether the dot at reading (x, y) is black.
 * @param b  the bitmap
 * @param x  from the left edge, dots
 * @param y  from the top edge, dots
 * @return false outside the label
 */
bool label_px_at(const label_bmp_t *b, int x, int y);

/**
 * @brief Fill a rectangle black, cut to the label.
 * @param b  the bitmap
 * @param x  left edge, dots
 * @param y  top edge, dots
 * @param w  width, dots
 * @param h  height, dots
 */
void label_fill(label_bmp_t *b, int x, int y, int w, int h);

/**
 * @brief Whether @p font leaves a mark for every character of @p s but the
 *        space.
 *
 * A glyph without a pixel of ink at the label's threshold would print as a
 * gap, `KST_X10-` as `KST X10-`.  The layout skips such a face.
 *
 * @param font  the face
 * @param s     UTF-8 text
 * @return false when a character other than the space draws no dot
 */
bool label_face_draws(const gfx_font_t *font, const char *s);

/**
 * @brief Draw UTF-8 text in a panel face at scale 1, black on white.
 *
 * A 1-bit face is drawn as it is; an 8-bit face is black where a glyph
 * pixel's coverage is 128 or above.  A code point the face does not hold is
 * drawn as '?'.  Only whole cells left of @p x_end are drawn.
 *
 * @param b      the bitmap
 * @param x      left edge of the first cell, dots
 * @param y      top edge of the cells, dots
 * @param font   the face
 * @param s      the text
 * @param x_end  first x no cell may reach
 * @return the cells drawn
 */
int label_text(label_bmp_t *b, int x, int y, const gfx_font_t *font,
               const char *s, int x_end);

#ifdef __cplusplus
}
#endif
