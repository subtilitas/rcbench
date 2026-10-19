/*
 * The fault log under its cap.  See fault_log.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fault_log.h"

#include <string.h>

/* One level of BENCH, through the caller's make_dir().  A card_make_fn,
 * so its context stays a plain pointer. */
// cppcheck-suppress constParameterCallback
static card_dir_t fault_level(void *ctx, const char *dir)
{
    const fault_log_io_t *io = (const fault_log_io_t *)ctx;
    return io->make_dir(io->ctx, dir);
}

/*
 * The full file renamed to RCBENCH.OLD.  FAT renames onto no file, so the
 * old OLD is moved to RCBENCH.TMP first and removed only after the rename
 * has gone through; a rename that fails gives it its name back.
 */
static fault_log_t rotate(const fault_log_io_t *io)
{
    /* A TMP a power cut left: the OLD of its rotation, or one older. */
    const int32_t tmp = io->size(io->ctx, CARD_FAULT_TMP);
    const int32_t old = io->size(io->ctx, CARD_FAULT_OLD);
    if (tmp < FAULT_SIZE_NONE || old < FAULT_SIZE_NONE) {
        return FAULT_LOG_UNREAD;
    }
    if (tmp >= 0) {
        const bool cleared =
            (old >= 0) ? io->remove(io->ctx, CARD_FAULT_TMP)
                       : io->rename(io->ctx, CARD_FAULT_TMP, CARD_FAULT_OLD);
        if (!cleared) {
            return FAULT_LOG_NO_ROOM;
        }
    }
    /* An OLD stands now: the one there, or the TMP given its name. */
    const bool aside = (tmp >= 0) || (old >= 0);
    if (aside && !io->rename(io->ctx, CARD_FAULT_OLD, CARD_FAULT_TMP)) {
        return FAULT_LOG_NO_ROOM;
    }
    if (!io->rename(io->ctx, CARD_FAULT_LOG, CARD_FAULT_OLD)) {
        if (aside) {
            (void)io->rename(io->ctx, CARD_FAULT_TMP, CARD_FAULT_OLD);
        }
        return FAULT_LOG_NO_ROOM;
    }
    if (aside) {
        (void)io->remove(io->ctx, CARD_FAULT_TMP);
    }
    return FAULT_LOG_OK;
}

fault_log_t fault_log_note(const fault_log_io_t *io, const char *line,
                           bool *rotated)
{
    if (rotated != NULL) {
        *rotated = false;
    }
    if (io == NULL || line == NULL || io->mounted == NULL
        || io->make_dir == NULL || io->size == NULL || io->remove == NULL
        || io->rename == NULL || io->append == NULL) {
        return FAULT_LOG_ARG;
    }
    const size_t need = strlen(line) + 1u;      /* and its line feed */
    if (need > FAULT_LOG_CAP) {
        return FAULT_LOG_ARG;
    }
    if (!io->mounted(io->ctx)) {
        return FAULT_LOG_NO_CARD;
    }
    if (card_make_dirs(CARD_BENCH, fault_level, (void *)io) != CARD_DIR_OK) {
        return FAULT_LOG_NO_FOLDER;
    }
    const int32_t size = io->size(io->ctx, CARD_FAULT_LOG);
    if (size < FAULT_SIZE_NONE) {
        return FAULT_LOG_UNREAD;
    }
    if (size >= 0 && (uint32_t)size > FAULT_LOG_CAP - need) {
        const fault_log_t r = rotate(io);
        if (r != FAULT_LOG_OK) {
            return r;
        }
        if (rotated != NULL) {
            *rotated = true;
        }
    }
    return io->append(io->ctx, CARD_FAULT_LOG, line) ? FAULT_LOG_OK
                                                     : FAULT_LOG_WRITE;
}
