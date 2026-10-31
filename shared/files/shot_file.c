/**
 * @file
 * @brief A screenshot on the card (shot_file.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "shot_file.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "card_tree.h"

/** @brief Room for SHOT_DIR, "/", a name and ".NEW". */
#define SHOT_PATH (sizeof(SHOT_DIR) + FM_NAME_BUF + FM_NEW_SUFFIX_LEN)

/** @brief A save's write buffer and how it is doing. */
typedef struct {
    const shot_save_t *s;   /**< the save                         */
    void    *file;          /**< the open file                    */
    size_t   used;          /**< bytes in the chunk               */
    fm_err_t err;           /**< the first failure                */
} shot_sink_t;

/**
 * @brief Write out what the chunk holds.
 * @param k  the writer
 * @return true when it went
 */
static bool flush_chunk(shot_sink_t *k)
{
    if (k->used == 0u) {
        return k->err == FM_OK;
    }
    if (k->err == FM_OK) {
        k->err = k->s->io->write(k->s->io->ctx, k->file, k->s->chunk, k->used);
    }
    k->used = 0u;
    return k->err == FM_OK;
}

/**
 * @brief The encoder's sink: the bytes through the chunk.
 * @param data  the bytes
 * @param n     how many
 * @param ctx   the writer
 * @return false once a write failed
 */
static bool sink_write(const uint8_t *data, size_t n, void *ctx)
{
    shot_sink_t *k = (shot_sink_t *)ctx;
    while (n != 0u) {
        const size_t room = k->s->chunk_size - k->used;
        const size_t take = (n < room) ? n : room;
        memcpy(k->s->chunk + k->used, data, take);
        k->used += take;
        data += take;
        n -= take;
        if (k->used == k->s->chunk_size && !flush_chunk(k)) {
            return false;
        }
    }
    return k->err == FM_OK;
}

/** @brief card_make_dirs()'s context: the card and its last answer. */
typedef struct {
    const fm_io_t *io;  /**< the card                         */
    fm_err_t err;       /**< what the failing level answered  */
} make_t;

/**
 * @brief card_make_dirs()'s make through the card's table.
 * @param ctx    the make_t
 * @param level  the folder
 * @return CARD_DIR_OK when it is there
 */
static card_dir_t make_level(void *ctx, const char *level)
{
    make_t *m = (make_t *)ctx;
    const fm_err_t e = m->io->mkdir(m->io->ctx, level);
    if (e == FM_OK) {
        return CARD_DIR_OK;
    }
    if (e != FM_ERR_EXISTS) {
        m->err = e;
        return CARD_DIR_FAILED;
    }
    fm_stat_t st;
    if (m->io->stat(m->io->ctx, level, &st) == FM_OK && st.is_dir) {
        return CARD_DIR_OK;
    }
    m->err = FM_ERR_NAME;       /* a file holds the folder's name */
    return CARD_DIR_FILE;
}

int shot_name_check(const char *base)
{
    char name[FM_NAME_BUF];
    const size_t len = (base != NULL) ? strlen(base) : 0u;
    if (len > SHOT_BASE_MAX) {
        return (int)FM_NAME_LONG;
    }
    (void)snprintf(name, sizeof(name), "%s%s", (base != NULL) ? base : "",
                   SHOT_EXT);
    /* An empty name is empty, not one of just the extension. */
    return (len == 0u) ? (int)FM_NAME_EMPTY : (int)fm_name_check(name);
}

/**
 * @brief A walk's visit: the highest number of a SHOT<digits>.PNG so far.
 * @param entry  the entry
 * @param vctx   the highest, an unsigned
 */
static void next_visit(const fm_dirent_t *entry, void *vctx)
{
    unsigned *high = (unsigned *)vctx;
    const size_t prefix = sizeof(SHOT_PREFIX) - 1u;
    const size_t len = strlen(entry->name);
    if (entry->is_dir || len <= prefix + SHOT_EXT_LEN) {
        return;
    }
    for (size_t i = 0u; i < prefix; ++i) {
        if (toupper((unsigned char)entry->name[i]) != SHOT_PREFIX[i]) {
            return;
        }
    }
    for (size_t i = 0u; i < SHOT_EXT_LEN; ++i) {
        if (toupper((unsigned char)entry->name[len - SHOT_EXT_LEN + i])
            != SHOT_EXT[i]) {
            return;
        }
    }
    unsigned v = 0u;
    for (size_t i = prefix; i < len - SHOT_EXT_LEN; ++i) {
        const unsigned char c = (unsigned char)entry->name[i];
        if (!isdigit(c)) {
            return;
        }
        if (v <= SHOT_NUMBER_MAX) {
            v = v * 10u + (unsigned)(c - '0');
        }
    }
    if (v > *high) {
        *high = v;
    }
}

fm_err_t shot_file_next(const fm_io_t *io, unsigned *number)
{
    if (io == NULL || number == NULL) {
        return FM_ERR_ARG;
    }
    *number = 1u;
    if (!io->mounted(io->ctx)) {
        return FM_ERR_NO_CARD;
    }
    const unsigned mount = io->mounts(io->ctx);
    if (!io->hold(io->ctx, mount)) {
        return FM_ERR_CARD_GONE;
    }
    unsigned high = 0u;
    fm_err_t e = io->walk(io->ctx, SHOT_DIR, next_visit, &high, NULL);
    io->release(io->ctx);
    if (e == FM_ERR_NOT_FOUND) {
        e = FM_OK;
    }
    if (e == FM_OK) {
        *number = (high >= SHOT_NUMBER_MAX) ? SHOT_NUMBER_MAX : high + 1u;
    }
    return e;
}

/**
 * @brief The picture into the ".NEW".
 * @param s     the save
 * @param temp  the ".NEW"'s path
 * @return FM_OK with the ".NEW" whole on the card
 */
static fm_err_t write_new(const shot_save_t *s, const char *temp)
{
    shot_sink_t k = { .s = s, .file = NULL, .used = 0u, .err = FM_OK };
    fm_err_t e = s->io->open(s->io->ctx, temp, FM_OPEN_NEW, &k.file);
    if (e != FM_OK) {
        return e;
    }
    bool go = png_enc_start(s->enc, s->w, s->h, sink_write, &k);
    for (int y = 0; go && y < s->h; ++y) {
        if (s->cancelled != NULL && s->cancelled(s->ctx)) {
            k.err = FM_ERR_CANCELLED;
            go = false;
        }
        if (!go) {
            break;
        }
        go = png_enc_row(s->enc, s->pixels + (size_t)y * (size_t)s->w);
        if (s->progress != NULL) {
            s->progress(y + 1, s->h, s->ctx);
        }
    }
    go = go && png_enc_end(s->enc) && flush_chunk(&k);
    if (go) {
        k.err = s->io->sync(s->io->ctx, k.file);
    }
    e = s->io->close(s->io->ctx, k.file);
    if (k.err != FM_OK) {
        return k.err;
    }
    if (!go) {
        return FM_ERR_IO;
    }
    return e;
}

fm_err_t shot_file_save(const shot_save_t *s, char *path, size_t n)
{
    if (s == NULL || s->io == NULL || s->pixels == NULL || s->enc == NULL
        || s->chunk == NULL || s->chunk_size == 0u || s->base == NULL) {
        return FM_ERR_ARG;
    }
    if (shot_name_check(s->base) != (int)FM_NAME_OK) {
        return FM_ERR_NAME;
    }
    const fm_io_t *io = s->io;
    if (!io->mounted(io->ctx)) {
        return FM_ERR_NO_CARD;
    }
    const unsigned mount = io->mounts(io->ctx);
    if (!io->hold(io->ctx, mount)) {
        return FM_ERR_CARD_GONE;
    }
    char final_[SHOT_PATH];
    char temp[SHOT_PATH + FM_NEW_SUFFIX_LEN];
    (void)snprintf(final_, sizeof(final_), "%s/%s%s", SHOT_DIR, s->base,
                   SHOT_EXT);
    (void)snprintf(temp, sizeof(temp), "%s%s", final_, FM_NEW_SUFFIX);

    fm_err_t e = FM_OK;
    make_t made = { .io = io, .err = FM_ERR_IO };
    if (card_make_dirs(SHOT_DIR, make_level, &made) != CARD_DIR_OK) {
        e = made.err;
    }
    fm_stat_t st;
    if (e == FM_OK) {
        const fm_err_t there = io->stat(io->ctx, final_, &st);
        if (there == FM_OK) {
            e = FM_ERR_EXISTS;
        } else if (there != FM_ERR_NOT_FOUND) {
            e = there;
        }
    }
    if (e == FM_OK) {
        /* A ".NEW" a power cut left. */
        (void)io->unlink(io->ctx, temp);
        e = write_new(s, temp);
        if (e == FM_OK) {
            e = io->rename(io->ctx, temp, final_);
        }
        if (e != FM_OK) {
            (void)io->unlink(io->ctx, temp);
        }
    }
    io->release(io->ctx);
    if (e == FM_OK && path != NULL && n != 0u) {
        (void)snprintf(path, n, "%s", final_);
    }
    return e;
}
