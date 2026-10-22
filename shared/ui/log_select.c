/*
 * Keeping the entries a bounded list can show, and ordering them for it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_select.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "log_name.h"

/*
 * The insertion both selections share: @p sign 1 keeps the entries that rank
 * first, -1 the entries that rank last.
 */
static int keep_ranked(log_viewer_file_t *set, int count, int capacity,
                       const log_viewer_file_t *cand, int sign)
{
    if (set == NULL || cand == NULL || capacity <= 0) {
        return count;
    }

    int n = count;
    int i;
    if (n >= capacity) {
        /* Full: the candidate has to beat the entry at the bottom to get in,
         * and that entry is the one that leaves. */
        n = capacity;
        if (sign * log_name_rank(cand->name, set[n - 1].name) >= 0) {
            return n;
        }
        i = n - 1;
    } else {
        i = n;
        ++n;
    }

    /* Insertion into a set already in rank order: walk the candidate up past
     * everything it outranks.  At most LOG_VIEWER_MAX_FILES moves per entry,
     * against one card read per entry to find its size. */
    while (i > 0 && sign * log_name_rank(cand->name, set[i - 1].name) < 0) {
        set[i] = set[i - 1];
        --i;
    }
    set[i] = *cand;
    return n;
}

int log_select_keep(log_viewer_file_t *set, int count, int capacity,
                    const log_viewer_file_t *cand)
{
    return keep_ranked(set, count, capacity, cand, 1);
}

void log_select_page_begin(log_select_page_t *sel, log_viewer_file_t *set,
                           int capacity, const log_viewer_page_t *page)
{
    if (sel == NULL) {
        return;
    }
    sel->set = set;
    sel->capacity = (set != NULL && capacity > 0) ? capacity : 0;
    sel->dir = (page != NULL) ? page->dir : LOG_PAGE_FROM;
    sel->key = (page != NULL) ? page->key : "";
    sel->held = 0;
    sel->inside = 0;
    sel->outside = 0;
    /* AFTER nothing is from the top, as FROM nothing is. */
    if (sel->key[0] == '\0' && sel->dir == LOG_PAGE_AFTER) {
        sel->dir = LOG_PAGE_FROM;
    }
}

void log_select_page_offer(log_select_page_t *sel,
                           const log_viewer_file_t *cand)
{
    if (sel == NULL || cand == NULL) {
        return;
    }
    const bool none = (sel->key[0] == '\0');
    const int cmp = none ? 0 : log_name_rank(cand->name, sel->key);
    switch (sel->dir) {
    case LOG_PAGE_BEFORE:
        /* The page before the key is the last of what ranks ahead of it,
         * so the walk keeps the bottom of that side.  What ranks with the
         * key or behind it is no part of the count it needs. */
        if (none || cmp < 0) {
            ++sel->inside;
            sel->held = keep_ranked(sel->set, sel->held, sel->capacity, cand,
                                    -1);
        }
        return;
    case LOG_PAGE_AFTER:
        if (cmp <= 0) {
            ++sel->outside;
            return;
        }
        break;
    case LOG_PAGE_FROM:
    default:
        if (!none && cmp < 0) {
            ++sel->outside;
            return;
        }
        break;
    }
    ++sel->inside;
    sel->held = keep_ranked(sel->set, sel->held, sel->capacity, cand, 1);
}

int log_select_page_end(log_select_page_t *sel, log_viewer_page_t *page)
{
    if (sel == NULL) {
        return 0;
    }
    /* Forward, the page starts behind everything ahead of the key.  Back,
     * it ends at the key, so what stands ahead of it is the rest of that
     * side. */
    const int ahead = (sel->dir == LOG_PAGE_BEFORE) ? sel->inside - sel->held
                                                    : sel->outside;
    log_select_sort(sel->set, sel->held);
    if (page != NULL) {
        page->ahead = ahead;
        page->held = sel->held;
    }
    return sel->held;
}

static int by_display_order(const void *a, const void *b)
{
    const log_viewer_file_t *x = (const log_viewer_file_t *)a;
    const log_viewer_file_t *y = (const log_viewer_file_t *)b;
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    /* The pages' own order, so a page reads on from the one before it:
     * the newest run at the top.  Names on one volume are unique and the
     * rank is zero only for one name, so the order is total and the sort
     * needs no tie-break to be repeatable. */
    return log_name_rank(x->name, y->name);
}

void log_select_sort(log_viewer_file_t *set, int count)
{
    if (set == NULL || count <= 1) {
        return;
    }
    qsort(set, (size_t)count, sizeof(set[0]), by_display_order);
}

/* ------------------------------------------------- reports standing alone */

/** @brief Whether bit @p n of @p bits is set.
 *  @param bits  a prefix's bits
 *  @param n     a run number, LOG_RUN_FIRST to LOG_RUN_LAST
 *  @return the bit */
static bool bit(const uint8_t *bits, int n)
{
    return (bits[n / 8] & (uint8_t)(1u << (n % 8))) != 0u;
}

/** @brief Set bit @p n of @p bits.
 *  @param bits  a prefix's bits
 *  @param n     a run number, LOG_RUN_FIRST to LOG_RUN_LAST */
static void set_bit(uint8_t *bits, int n)
{
    bits[n / 8] = (uint8_t)(bits[n / 8] | (1u << (n % 8)));
}

/** @brief Whether @p name ends in ".csv", case folded, after a base name.
 *  @param name  a file's name
 *  @return true for "RUN.CSV" and "sweep.csv", false for ".csv" alone */
static bool is_csv(const char *name)
{
    static const char k_csv[] = ".csv";
    const size_t n = strlen(name);
    const size_t k = sizeof(k_csv) - 1u;
    if (n <= k) {
        return false;
    }
    for (size_t i = 0u; i < k; ++i) {
        if (tolower((unsigned char)name[n - k + i]) != k_csv[i]) {
            return false;
        }
    }
    return true;
}

void log_lone_begin(log_lone_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof(*l));
    }
}

bool log_lone_first(log_lone_t *l, const char *name)
{
    if (name == NULL) {
        return false;
    }
    log_prefix_t k = LOG_PREFIX_ATEST;
    bool report = false;
    const int n = log_name_parse(name, &k, &report);
    if (n < 0) {
        return is_csv(name);
    }
    if (!report) {
        if (l != NULL) {
            set_bit(l->csv[k], n);
        }
        return true;
    }
    if (k == LOG_PREFIX_LIMIT) {
        return true;        /* a limit run writes no CSV */
    }
    if (l != NULL) {
        set_bit(l->txt[k], n);
    }
    return false;
}

bool log_lone_again(const log_lone_t *l)
{
    if (l == NULL) {
        return false;
    }
    for (int k = 0; k < (int)LOG_PREFIX_COUNT; ++k) {
        for (int i = 0; i < LOG_LONE_BYTES; ++i) {
            if ((l->txt[k][i] & (uint8_t)~l->csv[k][i]) != 0u) {
                return true;
            }
        }
    }
    return false;
}

bool log_lone_second(const log_lone_t *l, const char *name)
{
    log_prefix_t k = LOG_PREFIX_ATEST;
    bool report = false;
    const int n = (l != NULL) ? log_name_parse(name, &k, &report) : -1;
    return n >= 0 && report && k != LOG_PREFIX_LIMIT && bit(l->txt[k], n)
           && !bit(l->csv[k], n);
}
