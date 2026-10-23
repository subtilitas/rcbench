/**
 * @file log_select.h
 * @brief Which of a card's entries a bounded list keeps, and the order it
 *        draws them in.
 *
 * The browse list holds a page of LOG_VIEWER_MAX_FILES entries and a card
 * takes up to LOG_RUN_LAST runs, so a listing of a full card is a choice.
 * Making that choice by where the directory read stopped hides every run
 * written after the card passed that many files -- usually the newest
 * experiments.
 *
 * Entries are offered one at a time, in whatever order the volume hands them
 * over.  log_select_keep() holds the LOG_VIEWER_MAX_FILES that log_name_rank()
 * puts first; log_select_page_*() hold one page of that order, the one a
 * log_viewer_page_t names, and count where it starts.  Memory is the page and
 * nothing more: one walk of the directory per page, whatever the card holds.
 * Ordering for display is a separate pass, run once the directory has been
 * read to its end: directories first, then log_name_rank() order, so the
 * rows of a page run the same way as the pages, newest run at the top.
 *
 * The page keeping is the file manager's (fm_select.h) with log_name_rank()
 * as its order; the file manager's panes keep theirs in name order.
 *
 * Paging needs log_name_rank() to be a strict order: zero only for the same
 * file.  Two names that tie fall on the key's side of every page edge, and
 * one of them is shown on no page.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "fm_select.h"
#include "log_name.h"
#include "log_viewer_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Offer @p cand to a set in log_name_rank() order.
 *
 * The count is the same one that came back from the last call, so a set never
 * drifts past its capacity.  A candidate that ranks below a full set is
 * dropped and the count does not move.
 *
 * @param set       the entries, in log_name_rank() order
 * @param count     entries in @p set
 * @param capacity  room in @p set
 * @param cand      the entry offered; copied when it is kept
 * @return the count after the offer
 */
int log_select_keep(log_viewer_file_t *set, int count, int capacity,
                    const log_viewer_file_t *cand);

/**
 * @brief One page being chosen while a directory is read.  Filled by
 *        log_select_page_begin(); the fields are the walk's, not the
 *        caller's.  Not copied while a walk runs: its page keeping points
 *        at its own key.
 */
typedef struct {
    fm_select_t sel;          /**< the page keeping, in log_name_rank() order */
    log_viewer_file_t key;    /**< the page's key; "" for none */
} log_select_page_t;

/**
 * @brief Start choosing @p page into @p set.
 *
 * The key is copied, so @p page may change while the walk runs.
 *
 * @param sel       the walk's state
 * @param set       room for @p capacity entries
 * @param capacity  entries @p set takes
 * @param page      which page; NULL for the first
 */
void log_select_page_begin(log_select_page_t *sel, log_viewer_file_t *set,
                           int capacity, const log_viewer_page_t *page);

/**
 * @brief Offer one directory entry; it is copied if it is kept.
 * @param sel   the walk's state
 * @param cand  the entry
 */
void log_select_page_offer(log_select_page_t *sel,
                           const log_viewer_file_t *cand);

/**
 * @brief End the walk: put the set into the order the browse list draws
 *        (log_select_sort()) and write @p page's ahead and held.
 * @param sel   the walk's state
 * @param page  takes ahead and held; may be NULL
 * @return the entries held
 */
int log_select_page_end(log_select_page_t *sel, log_viewer_page_t *page);

/**
 * @brief Put @p count entries into the order the browse list draws them in:
 *        directories first, then log_name_rank() order -- runs newest
 *        first, then the other names, case-insensitively.
 * @param set    the entries
 * @param count  how many
 */
void log_select_sort(log_viewer_file_t *set, int count);

/*
 * Which names of a folder the list holds: every CSV file, and a report
 * standing alone.
 *
 * A run's report, ATESTnnn.TXT beside ATESTnnn.CSV, is not an entry of its
 * own: OPEN on the run reads its CSV, and DELETE on it takes the report
 * along.  A report with no CSV of its prefix and number beside it is the
 * only file of its run, and is listed: LIMITnnn.TXT always, a LIMIT RUN
 * writes no CSV, and SERVOnnn.TXT alone, the limit run of firmware 0.21.0
 * and older, in the root.  Any other ".TXT" is not listed.
 *
 * Whether a report stands alone is known once the whole folder is read, and
 * a page is chosen while it is read.  So a folder is walked once, and a
 * second time only when a report other than LIMITnnn.TXT was found without
 * its CSV: the first walk offers the CSV files and the LIMIT reports and
 * notes the rest, the second offers the reports noted alone.  The page
 * selection takes entries in any order, so the two walks choose the page
 * one walk of both would.  A folder written by this firmware, whose ATEST
 * reports stand beside their CSV files, is walked once.  The notes are two
 * bits per prefix and number: 1,750 bytes.
 */

/** @brief Bytes of one prefix's bit per number, LOG_RUN_FIRST to
 *         LOG_RUN_LAST. */
#define LOG_LONE_BYTES ((LOG_RUN_LAST + 8) / 8)

/** @brief The notes of one folder's walks.  The fields are the walks'. */
typedef struct {
    /** @brief A run log of the prefix and number is in the folder. */
    uint8_t csv[LOG_PREFIX_COUNT][LOG_LONE_BYTES];
    /** @brief A report of the prefix and number waits for the second walk. */
    uint8_t txt[LOG_PREFIX_COUNT][LOG_LONE_BYTES];
} log_lone_t;

/**
 * @brief Start the notes of a folder's walks.
 * @param l  the notes; nothing happens for NULL
 */
void log_lone_begin(log_lone_t *l);

/**
 * @brief The first walk's word on one name, which it notes.
 * @param l     the notes; NULL lists the CSV files and the LIMIT reports
 * @param name  a file's name in the folder
 * @return true when the list holds @p name and the first walk offers it: a
 *         name ending in ".csv", any case, or a LIMIT report; false for
 *         everything else, a report noted for the second walk included
 */
bool log_lone_first(log_lone_t *l, const char *name);

/**
 * @brief Whether the folder needs the second walk.
 * @param l  the notes after the first walk
 * @return true when a report was noted with no CSV of its prefix and
 *         number in the folder
 */
bool log_lone_again(const log_lone_t *l);

/**
 * @brief The second walk's word on one name.
 * @param l     the notes after the first walk
 * @param name  a file's name in the folder
 * @return true when @p name is a report the first walk noted and found no
 *         CSV for: the second walk offers it
 */
bool log_lone_second(const log_lone_t *l, const char *name);

#ifdef __cplusplus
}
#endif
