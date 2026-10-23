/**
 * @file fm_select.h
 * @brief One page of a folder held while the folder is read: which entries
 *        a bounded list keeps, in an order the caller names.
 *
 * A folder holds more entries than a page has room for, and a walk hands
 * them over in the order the volume holds them, which is creation order
 * and not the list's.  So every entry is offered as it arrives, and the
 * page keeps the ones on its side of a key: the first entries from the key
 * on (FROM), the first after it (AFTER, the next page), or the last ahead
 * of it (BEFORE, the page before).  A key is an entry, the one at a page's
 * edge, and not a page number: nothing about the other pages is held, and
 * a key whose file has gone still names a place in the order.  Memory is
 * the page and nothing more: one walk per page, whatever the folder holds.
 *
 * Entries are the caller's type, of any size, compared by a function of
 * the caller's, as qsort() takes them.  The order has to be strict, zero
 * only for one entry: two entries that tie fall on the key's side of every
 * page edge, and one of them is on no page.  LOGS keeps its runs newest
 * first (log_select.h, log_name_rank()); the file manager keeps folders
 * first and names in order (fm_tree.h).
 *
 * The offers may come in any order and from more than one walk of a
 * folder: what a page holds depends on what was offered, not on when.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Which side of the key a page holds.
 */
typedef enum {
    /** The first entries that rank with the key or behind it.  With no key,
     *  the first page. */
    FM_PAGE_FROM = 0,
    /** The first entries that rank behind the key: the next page.  With no
     *  key, the first page. */
    FM_PAGE_AFTER,
    /** The last entries that rank ahead of the key: the page before.  With
     *  no key, the last entries of all: the last page. */
    FM_PAGE_BEFORE
} fm_page_dir_t;

/**
 * @brief The order of two entries, as qsort() takes it.
 * @param a  an entry
 * @param b  an entry
 * @return negative when @p a ranks ahead, positive when @p b does, 0 for
 *         one entry
 */
typedef int (*fm_rank_fn)(const void *a, const void *b);

/**
 * @brief One page being chosen while a folder is read.  Filled by
 *        fm_select_begin(); the fields are the walk's, not the caller's.
 */
typedef struct {
    void *set;          /**< the page's room, @p capacity entries      */
    size_t size;        /**< bytes of one entry                        */
    int capacity;       /**< entries @p set takes                      */
    fm_rank_fn rank;    /**< the order the pages run in                */
    fm_page_dir_t dir;  /**< the side of @p key the page holds         */
    const void *key;    /**< borrowed; NULL for none                   */
    int held;           /**< entries in @p set                         */
    int inside;         /**< offered entries on the page's side of the key */
    int outside;        /**< offered entries ahead of the key (FROM, AFTER) */
} fm_select_t;

/**
 * @brief Offer @p cand to a set of @p count entries in @p rank order, which
 *        keeps the @p capacity entries that rank first.
 *
 * A candidate that ranks behind a full set is dropped; one that ranks
 * ahead of its last entry takes that entry's place.
 *
 * @param set       the entries, in @p rank order
 * @param size      bytes of one entry
 * @param count     entries in @p set: the last call's answer
 * @param capacity  room in @p set
 * @param cand      the entry offered; copied when it is kept
 * @param rank      the order
 * @return the count after the offer; never above @p capacity
 */
int fm_select_keep(void *set, size_t size, int count, int capacity,
                   const void *cand, fm_rank_fn rank);

/**
 * @brief Start choosing one page into @p set.
 * @param sel       the walk's state
 * @param set       room for @p capacity entries of @p size bytes
 * @param size      bytes of one entry
 * @param capacity  entries @p set takes
 * @param rank      the order the pages run in; strict
 * @param dir       the side of @p key the page holds
 * @param key       the entry at the page's edge, or NULL for none; it has
 *                  to outlive the walk
 */
void fm_select_begin(fm_select_t *sel, void *set, size_t size, int capacity,
                     fm_rank_fn rank, fm_page_dir_t dir, const void *key);

/**
 * @brief Offer one entry of the folder.
 * @param sel   the walk's state
 * @param cand  the entry; copied when it is kept
 */
void fm_select_offer(fm_select_t *sel, const void *cand);

/**
 * @brief End the walk: sort the page for display and say where it stands.
 * @param sel    the walk's state
 * @param show   the order the page is drawn in, or NULL for the walk's
 *               own order
 * @param ahead  takes the count of offered entries that rank ahead of the
 *               page's first; may be NULL
 * @return the entries the page holds
 */
int fm_select_end(fm_select_t *sel, fm_rank_fn show, int *ahead);

#ifdef __cplusplus
}
#endif
