/**
 * @file
 * @brief The settings files on the card.  See settings_backup.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_backup.h"

#include <stdio.h>
#include <string.h>

/** @brief The automatic copies' names, newest first. */
static const char *const k_auto[SB_AUTO_COPIES] = {
    "AUTO1.TXT", "AUTO2.TXT", "AUTO3.TXT",
};

const char *sb_auto_name(int i)
{
    return (i >= 1 && i <= SB_AUTO_COPIES) ? k_auto[i - 1] : NULL;
}

/** @brief Whether @p io has every function and holds a mounted volume;
 *         SB_OK, SB_ARG or SB_NO_CARD. */
static sb_result_t ready(const sb_io_t *io)
{
    if (io == NULL || io->mounted == NULL || io->size == NULL
        || io->read == NULL || io->write == NULL || io->rename == NULL
        || io->remove == NULL) {
        return SB_ARG;
    }
    return io->mounted(io->ctx) ? SB_OK : SB_NO_CARD;
}

/** @brief A write's answer as a request's result. */
static sb_result_t from_io(sb_io_result_t r)
{
    switch (r) {
    case SB_IO_OK:      return SB_OK;
    case SB_IO_FULL:    return SB_FULL;
    case SB_IO_DENIED:  return SB_DENIED;
    case SB_IO_MISSING: return SB_MISSING;
    case SB_IO_ERROR:
    default:            return SB_WRITE;
    }
}

/** @brief Remove @p name; a missing file is no failure. */
static bool gone(const sb_io_t *io, const char *name)
{
    const sb_io_result_t r = io->remove(io->ctx, name);
    return r == SB_IO_OK || r == SB_IO_MISSING;
}

/** @brief Write @p text under @p tmp; on a failure the .TMP goes. */
static sb_result_t write_tmp(const sb_io_t *io, const char *tmp,
                             const char *text, size_t len)
{
    if (text == NULL || len == 0u || len > SF_FILE_MAX) {
        return SB_ARG;
    }
    /* A .TMP a cut write left behind would make the write a replace. */
    (void)gone(io, tmp);
    const sb_io_result_t r = io->write(io->ctx, tmp, text, len);
    if (r != SB_IO_OK) {
        (void)gone(io, tmp);
    }
    return from_io(r);
}

sb_result_t sb_write_saved(const sb_io_t *io, const char *text, size_t len)
{
    sb_result_t r = ready(io);
    if (r != SB_OK) {
        return r;
    }
    r = write_tmp(io, SB_SAVED_TMP, text, len);
    if (r != SB_OK) {
        return r;
    }
    if (!gone(io, SB_SAVED)
        || io->rename(io->ctx, SB_SAVED_TMP, SB_SAVED) != SB_IO_OK) {
        return SB_WRITE;
    }
    return SB_OK;
}

sb_result_t sb_write_auto(const sb_io_t *io, const char *text, size_t len)
{
    sb_result_t r = ready(io);
    if (r != SB_OK) {
        return r;
    }
    r = write_tmp(io, SB_AUTO_TMP, text, len);
    if (r != SB_OK) {
        return r;
    }
    /* The oldest goes, each copy moves one place on, the new one is the
     * first.  A copy that is not there is a gap that moves on with them. */
    if (!gone(io, k_auto[SB_AUTO_COPIES - 1])) {
        return SB_WRITE;
    }
    for (int i = SB_AUTO_COPIES - 1; i > 0; --i) {
        const sb_io_result_t m = io->rename(io->ctx, k_auto[i - 1], k_auto[i]);
        if (m != SB_IO_OK && m != SB_IO_MISSING) {
            return SB_WRITE;
        }
    }
    return (io->rename(io->ctx, SB_AUTO_TMP, k_auto[0]) == SB_IO_OK)
               ? SB_OK : SB_WRITE;
}

/** @brief List one file into @p e when it is there; whether it is. */
static bool list_one(const sb_io_t *io, const char *name, sb_entry_t *e)
{
    const int32_t size = io->size(io->ctx, name);
    if (size < 0) {
        return false;
    }
    memset(e, 0, sizeof(*e));
    (void)snprintf(e->name, sizeof(e->name), "%s", name);
    e->size = size;
    char head[SB_HEAD_BYTES];
    const int32_t n = io->read(io->ctx, name, head, sizeof(head));
    e->err = (uint8_t)((n < 0) ? SF_ERR_ARG
                               : sf_head(head, (size_t)n, &e->head));
    return true;
}

sb_result_t sb_list(const sb_io_t *io, sb_entry_t out[SB_FILES],
                    uint8_t *count)
{
    if (out == NULL || count == NULL) {
        return SB_ARG;
    }
    *count = 0u;
    const sb_result_t r = ready(io);
    if (r != SB_OK) {
        return r;
    }
    for (int i = 0; i < SB_AUTO_COPIES; ++i) {
        *count = (uint8_t)(*count + (list_one(io, k_auto[i], &out[*count])
                                     ? 1u : 0u));
    }
    *count = (uint8_t)(*count + (list_one(io, SB_SAVED, &out[*count])
                                 ? 1u : 0u));
    return SB_OK;
}

int sb_newest(const sb_entry_t *files, uint8_t count)
{
    int best = -1;
    for (int i = 0; files != NULL && i < (int)count; ++i) {
        if (files[i].err != SF_OK) {
            continue;
        }
        if (best < 0) {
            best = i;
            continue;
        }
        /* "YYYY-MM-DD HH:MM:SS UTC" sorts as it reads. */
        const char *a = files[i].head.written;
        const char *b = files[best].head.written;
        if (a[0] != '\0' && b[0] != '\0' && strcmp(a, b) > 0) {
            best = i;
        }
    }
    return best;
}

/** @brief Whether @p name is a file of this folder a read may name. */
static bool ours(const char *name)
{
    if (name == NULL) {
        return false;
    }
    if (strcmp(name, SB_SAVED) == 0) {
        return true;
    }
    for (int i = 0; i < SB_AUTO_COPIES; ++i) {
        if (strcmp(name, k_auto[i]) == 0) {
            return true;
        }
    }
    return false;
}

sb_result_t sb_read(const sb_io_t *io, const char *name, char *buf,
                    size_t cap, size_t *len)
{
    if (buf == NULL || len == NULL || cap < SF_FILE_MAX + 1u || !ours(name)) {
        return SB_ARG;
    }
    *len = 0u;
    buf[0] = '\0';
    const sb_result_t r = ready(io);
    if (r != SB_OK) {
        return r;
    }
    const int32_t size = io->size(io->ctx, name);
    if (size < 0) {
        return SB_MISSING;
    }
    if ((uint32_t)size > SF_FILE_MAX) {
        return SB_LARGE;
    }
    const int32_t n = io->read(io->ctx, name, buf, SF_FILE_MAX + 1u);
    if (n < 0) {
        return SB_READ;
    }
    if ((uint32_t)n > SF_FILE_MAX) {
        return SB_LARGE;          /* grew between the size and the read */
    }
    buf[n] = '\0';
    *len = (size_t)n;
    return SB_OK;
}

void sb_run(const sb_io_t *io, const sb_req_t *req, char *buf, size_t cap,
            sb_done_t *done)
{
    if (done == NULL) {
        return;
    }
    memset(done, 0, sizeof(*done));
    if (req == NULL || buf == NULL) {
        done->result = SB_ARG;
        return;
    }
    done->op = req->op;
    switch ((sb_req_op_t)req->op) {
    case SB_REQ_SAVE:
        done->result = (uint8_t)((req->len < cap)
                                     ? sb_write_saved(io, buf, req->len)
                                     : SB_ARG);
        break;
    case SB_REQ_AUTO:
        done->result = (uint8_t)((req->len < cap)
                                     ? sb_write_auto(io, buf, req->len)
                                     : SB_ARG);
        break;
    case SB_REQ_LIST:
        done->result = (uint8_t)sb_list(io, done->files, &done->count);
        break;
    case SB_REQ_READ:
        (void)snprintf(done->name, sizeof(done->name), "%s", req->name);
        done->result = (uint8_t)sb_read(io, req->name, buf, cap, &done->len);
        break;
    case SB_REQ_NONE:
    default:
        done->result = SB_ARG;
        break;
    }
}
