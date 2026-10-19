/*
 * Taking a run's number in a folder of the card.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_take.h"

#include <stdio.h>
#include <string.h>

void log_series_init(log_series_set_t *set)
{
    if (set != NULL) {
        memset(set, 0, sizeof(*set));
    }
}

/* Whether @p a and @p b name one folder: FAT compares without case. */
static bool same_dir(const char *a, const char *b)
{
    return log_name_case_cmp((a != NULL) ? a : "", (b != NULL) ? b : "")
           == 0;
}

log_series_t *log_series_of(log_series_set_t *set, const char *dir)
{
    if (set == NULL || dir == NULL || strlen(dir) >= CARD_DIR_MAX) {
        return NULL;
    }
    log_series_t *free_slot = NULL;
    for (int i = 0; i < LOG_SERIES_SLOTS; ++i) {
        log_series_t *sl = &set->slot[i];
        if (sl->numbered || sl->dir[0] != '\0') {
            if (same_dir(sl->dir, dir)) {
                return sl;
            }
        } else if (free_slot == NULL) {
            free_slot = sl;
        }
    }
    if (free_slot == NULL) {
        /* A third folder: one series is given up, and walked again when
         * its folder is next written. */
        free_slot = &set->slot[set->evict % LOG_SERIES_SLOTS];
        set->evict = (set->evict + 1u) % LOG_SERIES_SLOTS;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    snprintf(free_slot->dir, sizeof(free_slot->dir), "%s", dir);
    return free_slot;
}

/* The highest number a walk sees, under any prefix, a folder's name too:
 * a folder called MOTOR003.CSV takes that number as a file does, because
 * no file of that name can be created beside it. */
static void take_highest(const char *name, void *ctx)
{
    int *highest = (int *)ctx;
    const int n = log_name_number(name);
    if (n > *highest) {
        *highest = n;
    }
}

/* One level of the folder, through the caller's make_dir().  A
 * card_make_fn, so its context stays a plain pointer. */
// cppcheck-suppress constParameterCallback
static card_dir_t take_level(void *ctx, const char *level)
{
    const log_take_io_t *io = (const log_take_io_t *)ctx;
    return io->make_dir(io->ctx, level) ? CARD_DIR_OK : CARD_DIR_FAILED;
}

/* None of number @p n's LOG_NAME_FORMS names is in @p dir. */
static bool number_free(const log_take_io_t *io, const char *dir, int n)
{
    for (int form = 0; form < LOG_NAME_FORMS; ++form) {
        char name[LOG_RUN_NAME_MAX];
        log_name_form(name, sizeof(name), n, form);
        if (io->exists(io->ctx, dir, name)) {
            return false;
        }
    }
    return true;
}

log_take_t log_take(log_series_set_t *set, const log_take_io_t *io,
                    const char *dir, log_prefix_t prefix, bool report,
                    int *number)
{
    if (number != NULL) {
        *number = -1;
    }
    if (set == NULL || io == NULL || dir == NULL || number == NULL
        || io->mounted == NULL || io->make_dir == NULL || io->walk == NULL
        || io->exists == NULL || io->create == NULL
        || (int)prefix < 0 || (int)prefix >= LOG_PREFIX_WRITTEN
        || strlen(dir) >= CARD_DIR_MAX) {
        return LOG_TAKE_ARG;
    }
    if (!io->mounted(io->ctx)) {
        return LOG_TAKE_NO_CARD;
    }
    if (card_make_dirs(dir, take_level, (void *)io) != CARD_DIR_OK) {
        return LOG_TAKE_NO_FOLDER;
    }
    log_series_t *sr = log_series_of(set, dir);
    if (sr == NULL) {
        return LOG_TAKE_ARG;
    }
    /*
     * Above every number the folder carries, not in the first gap: a run
     * written into a gap is the newest run wearing an old number, and a
     * full list would rank it last and hide it.  A folder that does not
     * read is a run not recorded, rather than one numbered from 1.
     */
    if (!sr->numbered) {
        int highest = LOG_RUN_FIRST - 1;
        if (!io->walk(io->ctx, dir, take_highest, &highest)) {
            return LOG_TAKE_UNREADABLE;
        }
        sr->next = log_name_next(highest);
        sr->numbered = true;
    }
    unsigned refused = 0u;
    for (int i = sr->next; i <= LOG_RUN_LAST; ++i) {
        if (!number_free(io, dir, i)) {
            continue;
        }
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name), prefix, i, report);
        if (io->create(io->ctx, dir, name, i)) {
            sr->next = i + 1;
            *number = i;
            return LOG_TAKE_OK;
        }
        if (++refused >= LOG_TAKE_TRIES) {
            break;      /* the volume, not this name */
        }
    }
    /* Walked again next time: a file deleted in between frees a number. */
    sr->numbered = false;
    return LOG_TAKE_FULL;
}

bool log_marks_busy(const log_marks_t *marks, const char *dir,
                    const char *name)
{
    if (marks == NULL || name == NULL) {
        return false;
    }
    const bool run_here = marks->run_dir != NULL
                          && same_dir(marks->run_dir, dir);
    const bool test_here = marks->test_dir != NULL
                           && same_dir(marks->test_dir, dir);
    return log_name_busy(name, run_here ? marks->run : 0u,
                         test_here ? marks->test : 0u,
                         test_here ? marks->txt : 0u);
}
