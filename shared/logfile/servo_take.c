/**
 * @file
 * @brief Taking a run's number in a labelled servo's folder.  See
 *        servo_take.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_take.h"

#include <stdio.h>
#include <string.h>

void servo_series_init(servo_series_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

void servo_bind_put(servo_bind_t *b, uint32_t run, const char *id)
{
    if (b == NULL || run == 0u) {
        return;
    }
    const unsigned i = b->next % SERVO_BIND_RUNS;
    b->run[i] = run;
    (void)snprintf(b->id[i], sizeof(b->id[i]), "%s", (id != NULL) ? id : "");
    b->next = (i + 1u) % SERVO_BIND_RUNS;
}

const char *servo_bind_get(const servo_bind_t *b, uint32_t run)
{
    if (b != NULL && run != 0u) {
        for (unsigned i = 0u; i < SERVO_BIND_RUNS; ++i) {
            if (b->run[i] == run) {
                return b->id[i];
            }
        }
    }
    return "";
}

unsigned servo_run_mark(servo_kind_t kind, int number)
{
    if ((int)kind < 0 || kind >= SERVO_KIND_COUNT || number < SERVO_RUN_FIRST
        || number > SERVO_RUN_LAST) {
        return 0u;
    }
    return SERVO_MARK_BASE + SERVO_MARK_BASE * (unsigned)kind
           + (unsigned)number;
}

/**
 * @brief A walk's visitor: the highest number each kind holds.
 * @param name An entry's name.
 * @param ctx  The servo_series_t's next[], holding the highest so far.
 */
static void servo_highest(const char *name, void *ctx)
{
    int *highest = (int *)ctx;
    servo_kind_t k;
    const int n = servo_run_parse(name, &k, NULL);
    if (n >= 0 && n > highest[k]) {
        highest[k] = n;
    }
}

/**
 * @brief Neither name of run @p n of kind @p k is in @p dir.
 * @param io  The card functions.
 * @param dir The folder.
 * @param k   The kind.
 * @param n   The number.
 * @return true when both are free.
 */
static bool servo_free(const log_take_io_t *io, const char *dir,
                       servo_kind_t k, int n)
{
    for (int form = 0; form < 2; ++form) {
        char name[SERVO_NAME_SIZE];
        servo_run_name(name, sizeof(name), k, n, form != 0);
        if (io->exists(io->ctx, dir, name)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief The folder's numbers, walked when they are not known for it.
 * @param s   The numbering.
 * @param io  The card functions.
 * @param p   The id.
 * @param dir Its folder.
 * @return false when the folder does not read to its end.
 */
static bool servo_numbered(servo_series_t *s, const log_take_io_t *io,
                           const servo_id_t *p, const char *dir)
{
    const unsigned edits = log_series_edits();
    if (s->walked && s->edits == edits && strcmp(s->id, p->text) == 0) {
        return true;
    }
    servo_series_init(s);
    int highest[SERVO_KIND_COUNT];
    for (int k = 0; k < (int)SERVO_KIND_COUNT; ++k) {
        highest[k] = SERVO_RUN_FIRST - 1;
    }
    if (!io->walk(io->ctx, dir, servo_highest, highest)) {
        return false;
    }
    for (int k = 0; k < (int)SERVO_KIND_COUNT; ++k) {
        s->next[k] = highest[k] + 1;
    }
    memcpy(s->id, p->text, sizeof(s->id));
    s->edits = edits;
    s->walked = true;
    return true;
}

log_take_t servo_take(servo_series_t *s, const log_take_io_t *io,
                      const char *id, servo_kind_t kind, bool report,
                      int *number)
{
    if (number != NULL) {
        *number = -1;
    }
    servo_id_t p;
    char dir[CARD_RUN_DIR_MAX];
    if (s == NULL || io == NULL || number == NULL || io->mounted == NULL
        || io->make_dir == NULL || io->walk == NULL || io->exists == NULL
        || io->create == NULL || (int)kind < 0 || kind >= SERVO_KIND_COUNT
        || !servo_id_parse(id, &p) || !servo_id_folder(dir, sizeof(dir), id)) {
        return LOG_TAKE_ARG;
    }
    if (!io->mounted(io->ctx)) {
        return LOG_TAKE_NO_CARD;
    }
    if (!io->make_dir(io->ctx, CARD_SERVOS) || !io->make_dir(io->ctx, dir)) {
        return LOG_TAKE_NO_FOLDER;
    }
    if (!servo_numbered(s, io, &p, dir)) {
        return LOG_TAKE_UNREADABLE;
    }
    unsigned refused = 0u;
    for (int i = s->next[kind]; i <= SERVO_RUN_LAST; ++i) {
        if (!servo_free(io, dir, kind, i)) {
            continue;
        }
        char name[SERVO_NAME_SIZE];
        servo_run_name(name, sizeof(name), kind, i, report);
        if (io->create(io->ctx, dir, name, i)) {
            s->next[kind] = i + 1;
            *number = i;
            return LOG_TAKE_OK;
        }
        if (++refused >= LOG_TAKE_TRIES) {
            break;      /* the volume, not this name */
        }
    }
    /* Walked again next time: a file moved off the card frees a number. */
    s->walked = false;
    return LOG_TAKE_FULL;
}
