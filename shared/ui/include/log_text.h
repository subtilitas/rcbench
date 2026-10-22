/**
 * @file log_text.h
 * @brief A text file as LOGS shows it: read whole, broken into rows.
 *
 * LOGS opens a report standing alone, LIMITnnn.TXT or SERVOnnn.TXT
 * (log_select.h), as text.  The file is read once into memory, up to
 * LOG_TEXT_MAX bytes, and its lines are broken into rows of at most a given
 * number of cells (code points, gfx_text_cells()): a line wider than a row
 * breaks at its last space inside the row, and a word wider than a row is
 * cut at the row's end.  A line ends at a line feed (LF), at a carriage
 * return (CR), and at a CR LF pair, which is one end; a tab and a NUL byte
 * are a space.  A file written on a computer reads as one written by the
 * panel.
 *
 * The load notes where every LOG_TEXT_MARK'th row starts.  A row is found
 * from the last row found when it is up to LOG_TEXT_MARK rows on, and from
 * the mark at or before it otherwise, so a frame of rows reads those rows
 * and at most LOG_TEXT_MARK - 1 rows before them, wherever the text is
 * scrolled to.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log_csv.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The most of a file the view holds: 32 KiB. */
#define LOG_TEXT_MAX ((size_t)32u * 1024u)

/** Rows from one mark to the next.  A row takes 1 byte at least, so a text
 *  holds LOG_TEXT_MAX rows at most and LOG_TEXT_MAX / LOG_TEXT_MARK + 1
 *  marks, 2,049 of 2 bytes, allocated beside the text. */
#define LOG_TEXT_MARK 16

/** One file's text and its rows.  The fields are log_text.c's. */
typedef struct {
    char  *buf;      /**< the text, terminated; NULL before a load       */
    size_t len;      /**< bytes in @c buf                                */
    bool   cut;      /**< the file holds more than LOG_TEXT_MAX bytes    */
    int    cols;     /**< cells a row holds                              */
    int    rows;     /**< rows of the whole text at @c cols              */
    int    at_row;   /**< the last row found ...                         */
    size_t at_off;   /**< ... where it starts in @c buf,                 */
    size_t at_end;   /**< ... where it ends                              */
    size_t at_next;  /**< ... and where the row after it starts          */
    bool   at_held;  /**< the four above hold a row                      */
    uint16_t *marks; /**< where row k * LOG_TEXT_MARK starts            */
    size_t walked;   /**< bytes the row lookups have read since the load,
                          what a frame costs                             */
} log_text_t;

/**
 * @brief Read @p src from its top into @p t, and count its rows.
 *
 * What @p t held before is freed first.
 *
 * @param t     the text; nothing happens for NULL
 * @param src   the file, read to its end or to LOG_TEXT_MAX bytes
 * @param cols  cells a row holds, 1 or more
 * @return LOG_OK; LOG_ERR_ARG for no source or @p cols under 1;
 *         LOG_ERR_MEMORY when the buffers are not given; LOG_ERR_READ when a
 *         read fails.  @p t holds nothing after a failure.
 */
log_err_t log_text_load(log_text_t *t, log_source_t *src, int cols);

/**
 * @brief Free what @p t holds and empty it.
 * @param t  the text; nothing happens for NULL
 */
void log_text_free(log_text_t *t);

/**
 * @brief Row @p i of the text into @p out.
 * @param t    the text
 * @param i    the row, 0 to @c rows - 1
 * @param out  takes the row, terminated; cut at @p n - 1 bytes, between
 *             two characters
 * @param n    bytes of @p out
 * @return false for a row outside the text, with @p out empty
 */
bool log_text_row(log_text_t *t, int i, char *out, size_t n);

/**
 * @brief Set the allocator the buffer comes from, for a test that refuses
 *        it.
 * @param alloc_fn  NULL for malloc
 * @param free_fn   NULL for free
 */
void log_text_set_allocator(void *(*alloc_fn)(size_t),
                            void (*free_fn)(void *));

#ifdef __cplusplus
}
#endif
