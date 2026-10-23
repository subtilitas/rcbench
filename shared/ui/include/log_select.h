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

#include "fm_select.h"
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

#ifdef __cplusplus
}
#endif
