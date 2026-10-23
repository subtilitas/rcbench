/**
 * @file log_select.c
 * @brief Keeping the entries a bounded list can show, and ordering them for
 *        it: LOGS' order over the file manager's page keeping
 *        (fm_select.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_select.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log_name.h"

/**
 * @brief The pages' order: log_name_rank() of two entries' names.
 * @param a  a log_viewer_file_t
 * @param b  a log_viewer_file_t
 * @return negative when @p a ranks ahead, positive when @p b does
 */
static int by_rank(const void *a, const void *b)
{
    const log_viewer_file_t *x = (const log_viewer_file_t *)a;
    const log_viewer_file_t *y = (const log_viewer_file_t *)b;
    return log_name_rank(x->name, y->name);
}

/**
 * @brief The order the browse list draws: directories first, then the
 *        pages' own order.
 * @param a  a log_viewer_file_t
 * @param b  a log_viewer_file_t
 * @return negative when @p a is drawn above @p b
 */
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

int log_select_keep(log_viewer_file_t *set, int count, int capacity,
                    const log_viewer_file_t *cand)
{
    return fm_select_keep(set, sizeof(set[0]), count, capacity, cand,
                          by_rank);
}

void log_select_page_begin(log_select_page_t *sel, log_viewer_file_t *set,
                           int capacity, const log_viewer_page_t *page)
{
    if (sel == NULL) {
        return;
    }
    /* The key is held here, so the page may change while the walk runs;
     * "" is no key. */
    const log_viewer_page_t first = { .dir = LOG_PAGE_FROM, .key = "" };
    const log_viewer_page_t *p = (page != NULL) ? page : &first;
    snprintf(sel->key.name, sizeof(sel->key.name), "%s", p->key);
    sel->key.size = 0u;
    sel->key.is_dir = false;
    fm_page_dir_t dir = FM_PAGE_FROM;
    if (p->dir == LOG_PAGE_AFTER) {
        dir = FM_PAGE_AFTER;
    } else if (p->dir == LOG_PAGE_BEFORE) {
        dir = FM_PAGE_BEFORE;
    }
    fm_select_begin(&sel->sel, set, sizeof(set[0]), capacity, by_rank, dir,
                    (sel->key.name[0] != '\0') ? &sel->key : NULL);
}

void log_select_page_offer(log_select_page_t *sel,
                           const log_viewer_file_t *cand)
{
    if (sel != NULL) {
        fm_select_offer(&sel->sel, cand);
    }
}

int log_select_page_end(log_select_page_t *sel, log_viewer_page_t *page)
{
    if (sel == NULL) {
        return 0;
    }
    int ahead = 0;
    const int held = fm_select_end(&sel->sel, by_display_order, &ahead);
    if (page != NULL) {
        page->ahead = ahead;
        page->held = held;
    }
    return held;
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
