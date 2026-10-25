/**
 * @file
 * @brief The fault log, BENCH/RCBENCH.LOG: one line every 5s while the link to
 *        the IOmcu is down, written whatever SETUP > SD CARD says.
 *
 * The file is held to FAULT_LOG_CAP, 1MiB (1,048,576 bytes).  A line that
 * would take it past the cap first renames it to BENCH/RCBENCH.OLD,
 * replacing the old one, and starts a new BENCH/RCBENCH.LOG with that line.
 * The card holds at most 2MiB of fault log; at 208 bytes a line every 5s,
 * one file is about 7 hours of link loss.
 *
 * A file that ends at the cap exactly is kept: the cap is the most bytes the
 * file holds.  A line that does not fit an empty file is refused.  When the
 * rename fails the line is not written, so the file never passes the cap,
 * and a file whose size does not read takes no line either.
 *
 * The old RCBENCH.OLD gives way only once the full file has its name.  It
 * is first renamed to BENCH/RCBENCH.TMP; a rename of the full file that
 * fails gives it its name back.  A TMP that stands at a line, left by a
 * power cut or by a removal the card refused, is RCBENCH.OLD again when no
 * OLD stands and is removed when one does; until it is gone no line is
 * written.
 *
 * Each line is appended on its own, opened and closed around it, so a
 * line is on the card before the next fault can happen.  BENCH is made the
 * first time (card_make_dirs()).
 *
 * The card is reached through fault_log_io_t, functions the caller supplies,
 * so the rules run on a host without a card.  Pure C, no ESP-IDF
 * (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "card_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief size()'s answers that are no size.
 */
#define FAULT_SIZE_NONE   (-1)
/** @brief size() answer: the size does not read, or a folder has the name. */
#define FAULT_SIZE_UNREAD (-2)

/**
 * @brief The most bytes BENCH/RCBENCH.LOG holds.
 */
#define FAULT_LOG_CAP 1048576u     /* 1MiB */

/**
 * @brief The card.
 *
 * A path is from the root (card_tree.h).  All 6 functions are set.
 */
typedef struct {
    void *ctx;                  /**< passed to every function               */
    /**
     * @brief Whether a card is mounted.
     */
    bool (*mounted)(void *ctx);
    /**
     * @brief Make one folder level (card_make_fn).
     */
    card_dir_t (*make_dir)(void *ctx, const char *level);
    /**
     * @brief The size of file @p path in bytes, INT32_MAX for a larger one;
     *        FAULT_SIZE_NONE when nothing has the name; FAULT_SIZE_UNREAD when
     *        the size does not read, or a folder has the name.
     */
    int32_t (*size)(void *ctx, const char *path);
    /**
     * @brief Remove file @p path.
     *
     * True when no file has the name afterwards.
     */
    bool (*remove)(void *ctx, const char *path);
    /**
     * @brief Give file @p from the name @p to, which no file has.
     */
    bool (*rename)(void *ctx, const char *from, const char *to);
    /**
     * @brief Append @p line and a line feed to @p path, creating the file.
     */
    bool (*append)(void *ctx, const char *path, const char *line);
} fault_log_io_t;

/**
 * @brief What a line came to.
 */
typedef enum {
    FAULT_LOG_OK = 0,
    FAULT_LOG_ARG,        /**< a NULL, or a line longer than the cap     */
    FAULT_LOG_NO_CARD,    /**< no card is mounted                       */
    FAULT_LOG_NO_FOLDER,  /**< BENCH cannot be made                     */
    FAULT_LOG_NO_ROOM,    /**< the full file could not be renamed, or the
                               old one not moved aside                   */
    FAULT_LOG_UNREAD,     /**< the size of a file does not read         */
    FAULT_LOG_WRITE,      /**< the append failed                        */
} fault_log_t;

/**
 * @brief Append @p line to the fault log, renamed to RCBENCH.OLD first when the
 *        line and its line feed would take it past FAULT_LOG_CAP.  @p rotated,
 *        when not NULL, says whether that rename happened.
 *
 * @param io       The card functions.
 * @param line     The line to append, without its line feed.
 * @param rotated  Receives true when the file was renamed, or NULL.
 * @return FAULT_LOG_OK, or the reason the line was not written.
 */
fault_log_t fault_log_note(const fault_log_io_t *io, const char *line,
                           bool *rotated);

#ifdef __cplusplus
}
#endif
