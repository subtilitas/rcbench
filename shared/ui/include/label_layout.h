/**
 * @file label_layout.h
 * @brief The servo label: the QR (Quick Response) code of the servo id and,
 *        beside or under it, the id's prefix, its serial large and the DUT
 *        NAME, laid out for the loaded roll; DUT NAME alone where both do
 *        not fit.
 *
 * The QR code holds the id's text and nothing else, at error correction
 * level M (raised where the version stays the same), versions 1 to 3, the
 * mask chosen by qrcodegen, with a quiet zone of 2 modules.  An id of up to
 * 20 characters without `_`, or up to 14 with `_`, is version 1 (21 x 21
 * modules); a longer one up to 23 characters is version 2 (25 x 25).
 *
 * Sizes in dots (8 dots/mm), W across the label and H its image columns
 * (label_bitmap.h); n modules a side, `n + 4` with the quiet zone:
 *
 * - Side by side, when `W >= H + 64` and the text column is 64 dots or
 *   wider: module `m = min(8, (H - 8) / (n + 4), 55 * W / 100 / (n + 4))`;
 *   the QR block at the left edge, vertically centered; the text column
 *   from 8 dots right of the block to 8 dots inside the right edge.  40 x
 *   30mm: version 1 with a module of 7 dots (175-dot block, 129-dot
 *   column), version 2 with 6 (174, 130).
 * - Stacked otherwise (a roll narrower than that):
 *   `m = min(8, W / (n + 4), (H - 28) / (n + 4))`, the quiet zone its side
 *   margin; the block 4 dots from the top, centered; the text under it
 *   from 8 dots inside each edge.
 * - A module under 4 dots is LABEL_TOO_SMALL.
 *
 * The lines, left-aligned, 4 dots apart; side by side the block of them is
 * vertically centered beside the code, stacked it starts 4 dots under it:
 *
 * | Line | Text | Face |
 * | --- | --- | --- |
 * | 1 | the prefix and its `-`: `KST_X10-` | the largest of 16x28, 12x24, 10x20, 8x16, 6x12 that holds it on one line and draws each of its characters (label_face_draws()) |
 * | 2 | the serial: `0000A` | 20x36, else the largest of the list above that holds it |
 * | 3, 4 | DUT NAME, broken at a space or at the column's end, the rest cut | 8x16; 6x12 where 8x16 needs a third line |
 *
 * Lines that do not fit the height go from the bottom: DUT NAME's second
 * line, then its first.  Where lines 1 and 2 still do not fit, they are one
 * line in the largest face that holds both.  A line that no face holds is
 * cut to the 6x12 cells that fit.  The id splits at its last `-`; an id
 * without one is line 2 alone.
 *
 * Where the height drops a line of DUT NAME, the label holds DUT NAME
 * alone and the id is in the code only: lines 1 and 2 are not drawn, and
 * DUT NAME takes the largest of 16x28, 12x24, 10x20, 8x16, 6x12 that holds
 * it in 1 or 2 lines, broken as above, and draws each of its characters;
 * else 6x12 in the lines that fit, the rest cut.  An empty DUT NAME keeps
 * the id lines.  The test label keeps its lines 1 and 2.  Per roll, for
 * `KST_X10-0000A` and `WING LEFT`:
 *
 * | Roll | Dots | Code | Lines |
 * | --- | --- | --- | --- |
 * | 40 x 30mm, 48 x 30mm, 30 x 20mm | 320 x 224, 384 x 224, 240 x 144 | side by side | prefix, serial, DUT NAME |
 * | 15 x 30mm, 25 x 100mm | 120 x 224, 200 x 784 | stacked | prefix, serial, DUT NAME |
 * | 25 x 25mm | 200 x 184 | stacked, module 6, 22 dots under it | DUT NAME in 10x20; without it the id in 10x20 |
 * | 15 x 20mm | 120 x 144 | stacked, module 4, 32 dots under it | DUT NAME in 10x20; without it the id in 8x16 |
 *
 * A 23-character id is version 2: on 25 x 25mm at module 5, with 26 dots
 * under the code; a 23-character DUT NAME is one 184-dot line of 8x16.
 *
 * Pure C: no heap, no clock; the QR matrix and qrcodegen's work buffer are
 * in label_info_t, which the caller places.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "label_bitmap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name The code and the lines
 *  @{ */
#define LABEL_QR_VERSION_MAX 3u    /**< highest QR version taken          */
#define LABEL_QR_BUF         107u  /**< bytes of a version 3 matrix:
                                        qrcodegen_BUFFER_LEN_FOR_VERSION(3) */
#define LABEL_QUIET          2u    /**< quiet zone, modules a side        */
#define LABEL_MODULE_MAX     8u    /**< largest module, dots: 1mm         */
#define LABEL_MODULE_MIN     4u    /**< smallest module, dots: 0.5mm      */
#define LABEL_LINES          4u    /**< text lines at most                */
#define LABEL_LINE_MAX       64u   /**< bytes of a line's text with NUL   */
#define LABEL_GAP            4u    /**< dots between lines, and between the
                                        code and the stacked text          */
#define LABEL_SIDE_MIN_COL   64u   /**< narrowest text column side by side */
/** @} */

/** @brief The texts of a servo label. */
typedef struct {
    const char *id;   /**< the servo id, `<prefix>-<serial>`; the QR's text */
    const char *dut;  /**< DUT NAME, "" or NULL for none                    */
} label_text_t;

/** @brief One drawn line. */
typedef struct {
    const gfx_font_t *face;        /**< NULL: the line is not drawn       */
    int16_t  x;                    /**< left edge, reading, dots          */
    int16_t  y;                    /**< top edge, reading, dots           */
    uint16_t w;                    /**< width of its cells, dots          */
    uint16_t h;                    /**< the face's height, dots           */
    char     text[LABEL_LINE_MAX]; /**< the text drawn                    */
} label_line_t;

/** @brief What the layout chose, and the code's matrix. */
typedef struct {
    uint8_t  qr[LABEL_QR_BUF];     /**< the matrix in qrcodegen's form    */
    uint8_t  work[LABEL_QR_BUF];   /**< qrcodegen's work buffer           */
    uint8_t  version;              /**< QR version, 1 to 3                */
    uint8_t  modules;              /**< modules a side: 21, 25 or 29      */
    uint8_t  module;               /**< dots a module                     */
    bool     stacked;              /**< code over text, else side by side */
    int16_t  qr_x;                 /**< the block's left edge, with the
                                        quiet zone, dots                   */
    int16_t  qr_y;                 /**< its top edge, dots                */
    uint16_t qr_side;              /**< its side, dots                    */
    label_line_t line[LABEL_LINES]; /**< prefix, serial, DUT NAME 1 and 2 */
} label_info_t;

/**
 * @brief Draw a servo label for @p roll into @p out.
 * @param roll  the loaded roll
 * @param t     the id and DUT NAME
 * @param out   the bitmap; @c bits and @c cap set by the caller
 * @param info  out: what was drawn, and the room for the QR matrix
 * @return LABEL_OK; a refusal of label_bmp_init(); LABEL_QR for an empty id
 *         or one past version 3; LABEL_TOO_SMALL for a module under 4 dots
 */
label_err_t label_layout(const label_roll_t *roll, const label_text_t *t,
                         label_bmp_t *out, label_info_t *info);

/**
 * @brief Draw the test label: the code of `RCBENCH TEST`, the lines
 *        `RCBENCH`, @p version and the roll as `40x30mm`.
 * @param roll     the loaded roll
 * @param version  the firmware version, as `0.23.17`
 * @param out      the bitmap; @c bits and @c cap set by the caller
 * @param info     out: what was drawn
 * @return as label_layout()
 */
label_err_t label_layout_test(const label_roll_t *roll, const char *version,
                              label_bmp_t *out, label_info_t *info);

/**
 * @brief Whether module (mx, my) of the drawn code is dark.
 * @param info  the layout's result
 * @param mx    column, 0 left, without the quiet zone
 * @param my    row, 0 top
 * @return false outside the matrix
 */
bool label_qr_module(const label_info_t *info, int mx, int my);

#ifdef __cplusplus
}
#endif
