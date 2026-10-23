/**
 * @file fm_select.c
 * @brief Keeping the page a bounded list shows while a folder is read, in
 *        the caller's order (fm_select.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "fm_select.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Entry @p i of @p set.
 * @param set   the entries
 * @param size  bytes of one entry
 * @param i     the index
 * @return its address
 */
static void *at(void *set, size_t size, int i)
{
    return (char *)set + (size_t)i * size;
}

/**
 * @brief @p rank's answer as -1, 0 or 1, so that a sign can turn it round
 *        whatever its size.
 * @param rank  the order
 * @param a     an entry
 * @param b     an entry
 * @return -1, 0 or 1
 */
static int order(fm_rank_fn rank, const void *a, const void *b)
{
    const int c = rank(a, b);
    return (c > 0) - (c < 0);
}

/**
 * @brief The insertion both kinds of page share: @p sign 1 keeps the
 *        entries that rank first, -1 the entries that rank last.
 * @param set       the entries, in @p sign times @p rank order
 * @param size      bytes of one entry
 * @param count     entries in @p set
 * @param capacity  room in @p set
 * @param cand      the entry offered
 * @param rank      the order
 * @param sign      1 or -1
 * @return the count after the offer
 */
static int keep_ranked(void *set, size_t size, int count, int capacity,
                       const void *cand, fm_rank_fn rank, int sign)
{
    if (set == NULL || cand == NULL || capacity <= 0 || rank == NULL
        || size == 0u) {
        return count;
    }

    int n = count;
    int i;
    if (n >= capacity) {
        /* Full: the candidate has to beat the entry at the bottom to get in,
         * and that entry is the one that leaves. */
        n = capacity;
        if (sign * order(rank, cand, at(set, size, n - 1)) >= 0) {
            return n;
        }
        i = n - 1;
    } else {
        i = n;
        ++n;
    }

    /* Insertion into a set already in rank order: walk the candidate up past
     * everything it outranks.  At most a page's worth of moves per entry,
     * against one card read per entry. */
    while (i > 0 && sign * order(rank, cand, at(set, size, i - 1)) < 0) {
        memcpy(at(set, size, i), at(set, size, i - 1), size);
        --i;
    }
    memcpy(at(set, size, i), cand, size);
    return n;
}

int fm_select_keep(void *set, size_t size, int count, int capacity,
                   const void *cand, fm_rank_fn rank)
{
    return keep_ranked(set, size, count, capacity, cand, rank, 1);
}

void fm_select_begin(fm_select_t *sel, void *set, size_t size, int capacity,
                     fm_rank_fn rank, fm_page_dir_t dir, const void *key)
{
    if (sel == NULL) {
        return;
    }
    sel->set = set;
    sel->size = size;
    sel->capacity = (set != NULL && capacity > 0 && size > 0u
                     && rank != NULL) ? capacity : 0;
    sel->rank = rank;
    sel->dir = dir;
    sel->key = key;
    sel->held = 0;
    sel->inside = 0;
    sel->outside = 0;
    /* AFTER nothing is from the top, as FROM nothing is.  Without an order
     * there is no key to stand on either side of. */
    if (rank == NULL) {
        sel->key = NULL;
    }
    if (sel->key == NULL && sel->dir == FM_PAGE_AFTER) {
        sel->dir = FM_PAGE_FROM;
    }
}

void fm_select_offer(fm_select_t *sel, const void *cand)
{
    if (sel == NULL || cand == NULL) {
        return;
    }
    const bool none = (sel->key == NULL);
    const int cmp = none ? 0 : order(sel->rank, cand, sel->key);
    switch (sel->dir) {
    case FM_PAGE_BEFORE:
        /* The page before the key is the last of what ranks ahead of it,
         * so the walk keeps the bottom of that side.  What ranks with the
         * key or behind it is no part of the count it needs. */
        if (none || cmp < 0) {
            ++sel->inside;
            sel->held = keep_ranked(sel->set, sel->size, sel->held,
                                    sel->capacity, cand, sel->rank, -1);
        }
        return;
    case FM_PAGE_AFTER:
        if (cmp <= 0) {
            ++sel->outside;
            return;
        }
        break;
    case FM_PAGE_FROM:
    default:
        if (!none && cmp < 0) {
            ++sel->outside;
            return;
        }
        break;
    }
    ++sel->inside;
    sel->held = keep_ranked(sel->set, sel->size, sel->held, sel->capacity,
                            cand, sel->rank, 1);
}

int fm_select_end(fm_select_t *sel, fm_rank_fn show, int *ahead)
{
    if (sel == NULL) {
        if (ahead != NULL) {
            *ahead = 0;
        }
        return 0;
    }
    /* Forward, the page starts behind everything ahead of the key.  Back,
     * it ends at the key, so what stands ahead of it is the rest of that
     * side. */
    if (ahead != NULL) {
        *ahead = (sel->dir == FM_PAGE_BEFORE) ? sel->inside - sel->held
                                              : sel->outside;
    }
    const fm_rank_fn by = (show != NULL) ? show : sel->rank;
    if (sel->set != NULL && sel->held > 1 && by != NULL) {
        qsort(sel->set, (size_t)sel->held, sel->size, by);
    }
    return sel->held;
}
