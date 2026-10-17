/*
 * Which of a card's entries a bounded list keeps, and the order it draws them
 * in.
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
 * read to its end.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "log_viewer_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Offer @p cand to a set that holds @p count entries of at most @p capacity,
 * in log_name_rank() order, and return the count after the offer.
 *
 * The count is the same one that came back from the last call, so a set never
 * drifts past its capacity.  A candidate that ranks below a full set is
 * dropped and the count does not move.
 */
int log_select_keep(log_viewer_file_t *set, int count, int capacity,
                    const log_viewer_file_t *cand);

/**
 * One page being chosen while a directory is read.  Filled by
 * log_select_page_begin(); the fields are the walk's, not the caller's.
 */
typedef struct {
    log_viewer_file_t *set;
    int capacity;
    log_page_dir_t dir;
    const char *key;     /* borrowed from the page, "" for none */
    int held;            /* entries in @p set                   */
    int inside;          /* offered entries on @p key's page side */
    int outside;         /* offered entries ahead of @p key (FROM, AFTER) */
} log_select_page_t;

/**
 * Start choosing @p page into @p set, which has room for @p capacity.  A
 * @p page of NULL is the first page.  @p page has to outlive the walk.
 */
void log_select_page_begin(log_select_page_t *sel, log_viewer_file_t *set,
                           int capacity, const log_viewer_page_t *page);

/** Offer one directory entry; it is copied if it is kept. */
void log_select_page_offer(log_select_page_t *sel,
                           const log_viewer_file_t *cand);

/**
 * End the walk: put the set into the order the browse list draws
 * (log_select_sort()), write @p page's ahead and held, and return held.
 * @p page may be NULL.
 */
int log_select_page_end(log_select_page_t *sel, log_viewer_page_t *page);

/**
 * Put @p count entries into the order the browse list draws them in:
 * directories first, then names, case-insensitively.
 */
void log_select_sort(log_viewer_file_t *set, int count);

#ifdef __cplusplus
}
#endif
