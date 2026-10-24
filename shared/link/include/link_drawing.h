/**
 * @file
 * @brief Reading a board drawing (LINK_ART_DRAWING): the layout link_pages.h
 * gives under LINK_DRAW_*.
 *
 * A drawing arrives over the link or out of the panel's flash, and either
 * way it is bytes somebody else wrote.  It is checked whole before any of it
 * is used: every count against the length, every pad and every shape inside
 * the outline, pads in ascending order, and only the kinds the format names.
 * A drawing with one record out of place is refused, not drawn without it --
 * a board that is drawn is a board an operator wires by.
 *
 * Nothing is copied.  A parsed drawing points into the bytes it was parsed
 * from, which must outlive it.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_LINK_DRAWING_H
#define RCBENCH_LINK_DRAWING_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One pad record. */
typedef struct {
    uint8_t  pad;      /**< 1 to 63                                         */
    uint8_t  kind;     /**< LINK_PAD_* or LINK_DRAW_PAD_GPIO                */
    uint8_t  value;    /**< GPIO, or a POWER pad's tenths of a volt         */
    uint8_t  flags;    /**< LINK_DRAW_PAD_SQUARE                            */
    uint16_t x;        /**< pad centre, 0.01mm from the outline's left     */
    uint16_t y;        /**< pad centre, 0.01mm from the outline's top      */
} link_draw_pad_t;

/** @brief One shape record; see link_pages.h for what a to d mean per kind. */
typedef struct {
    uint8_t  kind;     /**< a link_draw_shape_kind_t                        */
    uint8_t  param;    /**< RRECT corner radius, LINE width, else 0         */
    uint16_t colour;   /**< RGB565                                          */
    uint16_t a, b, c, d;
} link_draw_shape_t;

/** @brief A drawing that passed link_drawing_parse(). */
typedef struct {
    const uint8_t *data;
    uint32_t bytes;
    uint8_t  pads;
    uint16_t shapes;
    uint16_t width;    /**< outline, 0.01mm                                */
    uint16_t height;
    uint16_t colour;   /**< the board, RGB565                               */
    uint16_t ring;     /**< the pad rings, RGB565                           */
    uint8_t  corner;   /**< outline corner radius, 0.01mm                  */
    uint8_t  ring_r;   /**< pad ring radius, 0.01mm                        */
    uint8_t  hole_r;   /**< pad hole radius, 0.01mm                        */
} link_drawing_t;

/**
 * @brief Check @p bytes of @p data whole and describe them in @p out.
 *
 * False, with @p out zeroed, when anything is out of place; @p why (may be
 * NULL) then names the first thing found, for a log line, and is never left
 * pointing at nothing.
 *
 * @param out Receives the description; zeroed on failure.
 * @param data The drawing bytes.
 * @param bytes Length of data in bytes.
 * @param why Receives the reason text on failure; may be NULL.
 * @return true when the drawing is whole and in order, else false.
 */
bool link_drawing_parse(link_drawing_t *out, const uint8_t *data,
                        uint32_t bytes, const char **why);

/**
 * @brief Pad record @p i, 0 to pads - 1, in the order the drawing lists them.
 *
 * @param d The parsed drawing.
 * @param i Index of the record, 0 to pads - 1.
 * @param out Receives the record.
 * @return true when i is in range, else false.
 */
bool link_drawing_pad_at(const link_drawing_t *d, uint8_t i,
                         link_draw_pad_t *out);

/**
 * @brief The record of pad number @p pad.
 *
 * False when the drawing has none.
 *
 * @param d The parsed drawing.
 * @param pad The pad number, 1 to 63.
 * @param out Receives the record; may be NULL.
 * @return true when the drawing has the pad, else false.
 */
bool link_drawing_find_pad(const link_drawing_t *d, uint8_t pad,
                           link_draw_pad_t *out);

/**
 * @brief Shape record @p i, 0 to shapes - 1, in drawing order.
 *
 * @param d The parsed drawing.
 * @param i Index of the record, 0 to shapes - 1.
 * @param out Receives the record.
 * @return true when i is in range, else false.
 */
bool link_drawing_shape_at(const link_drawing_t *d, uint16_t i,
                           link_draw_shape_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_DRAWING_H */
