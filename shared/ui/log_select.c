/*
 * Keeping the entries a bounded list can show, and ordering them for it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_select.h"

#include <stdlib.h>

#include "log_name.h"

int log_select_keep(log_viewer_file_t *set, int count, int capacity,
                    const log_viewer_file_t *cand)
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
        if (log_name_rank(cand->name, set[n - 1].name) >= 0) {
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
    while (i > 0 && log_name_rank(cand->name, set[i - 1].name) < 0) {
        set[i] = set[i - 1];
        --i;
    }
    set[i] = *cand;
    return n;
}

static int by_display_order(const void *a, const void *b)
{
    const log_viewer_file_t *x = (const log_viewer_file_t *)a;
    const log_viewer_file_t *y = (const log_viewer_file_t *)b;
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    /* Names on one volume are unique, so this order is total and the sort
     * needs no tie-break to be repeatable. */
    return log_name_case_cmp(x->name, y->name);
}

void log_select_sort(log_viewer_file_t *set, int count)
{
    if (set == NULL || count <= 1) {
        return;
    }
    qsort(set, (size_t)count, sizeof(set[0]), by_display_order);
}
