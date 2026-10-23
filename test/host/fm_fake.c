/**
 * @file fm_fake.c
 * @brief A card in memory for the file manager's core (fm_fake.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "fm_fake.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Whether two paths are one, case folded.
 * @param a  a path
 * @param b  a path
 * @return true for one
 */
static bool same(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        if (*a == '\0') {
            return true;
        }
    }
}

/**
 * @brief realloc() that ends the process when the block cannot grow, so
 *        the old block is never lost behind a NULL.
 * @param p      the block
 * @param bytes  its new size
 * @return the grown block
 */
static void *grow(void *p, size_t bytes)
{
    void *q = realloc(p, bytes);
    if (q == NULL) {
        abort();
    }
    return q;
}

/**
 * @brief Whether the first @p k characters of @p a and @p b are one, case
 *        folded.
 * @param a  a path of @p k characters or more
 * @param b  a path of @p k characters or more
 * @param k  how many
 * @return true for one
 */
static bool same_head(const char *a, const char *b, size_t k)
{
    for (size_t i = 0u; i < k; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Write the folder of @p path into @p out: "" for one in the root.
 * @param out   room for the path
 * @param n     its bytes
 * @param path  a path
 */
static void parent_of(char *out, size_t n, const char *path)
{
    const char *cut = strrchr(path, '/');
    const size_t k = (cut != NULL) ? (size_t)(cut - path) : 0u;
    const size_t m = (k < n) ? k : n - 1u;
    memcpy(out, path, m);
    out[m] = '\0';
}

/**
 * @brief The index of the entry at @p path, or -1.
 * @param c     the card
 * @param path  the path
 * @return its index
 */
static int find(const fk_card_t *c, const char *path)
{
    for (int i = 0; i < c->count; ++i) {
        if (same(c->n[i].path, path)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Whether folder @p dir is there: the root always.
 * @param c    the card
 * @param dir  the folder
 * @return true for a folder
 */
static bool is_folder(const fk_card_t *c, const char *dir)
{
    if (dir[0] == '\0') {
        return true;
    }
    const int i = find(c, dir);
    return i >= 0 && c->n[i].dir;
}

/**
 * @brief The entries folder @p dir holds.
 * @param c    the card
 * @param dir  the folder
 * @return the count
 */
static int entries_in(const fk_card_t *c, const char *dir)
{
    int k = 0;
    char up[FK_PATH];
    for (int i = 0; i < c->count; ++i) {
        parent_of(up, sizeof(up), c->n[i].path);
        if (same(up, dir)) {
            ++k;
        }
    }
    return k;
}

/**
 * @brief Add an entry.
 * @param c     the card
 * @param path  its path
 * @param dir   a folder
 * @return its index
 */
static int add(fk_card_t *c, const char *path, bool dir)
{
    if (c->count == c->cap) {
        c->cap = (c->cap > 0) ? c->cap * 2 : 64;
        c->n = grow(c->n, (size_t)c->cap * sizeof(c->n[0]));
    }
    fk_node_t *e = &c->n[c->count];
    memset(e, 0, sizeof(*e));
    e->path = malloc(strlen(path) + 1u);
    if (e->path == NULL) {
        abort();
    }
    memcpy(e->path, path, strlen(path) + 1u);
    e->dir = dir;
    return c->count++;
}

/**
 * @brief Remove entry @p i, keeping the others in order and the open
 *        files on theirs.
 * @param c  the card
 * @param i  its index
 */
static void drop(fk_card_t *c, int i)
{
    free(c->n[i].path);
    free(c->n[i].data);
    memmove(&c->n[i], &c->n[i + 1], (size_t)(c->count - i - 1)
            * sizeof(c->n[0]));
    --c->count;
    for (int f = 0; f < FK_FILES; ++f) {
        if (c->files[f].used && c->files[f].node > i) {
            --c->files[f].node;
        }
    }
}

/**
 * @brief Count a call, and answer what was asked to fail.
 * @param c     the card
 * @param call  its kind
 * @return FM_OK to go on, or the answer
 */
static fm_err_t enter(fk_card_t *c, fk_call_t call)
{
    ++c->calls[call];
    ++c->calls[FK_ANY];
    if (c->fail_nth > 0
        && ((c->fail_call == call && c->calls[call] == c->fail_nth)
            || (c->fail_call == FK_ANY && c->calls[FK_ANY] == c->fail_nth))) {
        c->fail_nth = 0;
        if (c->fail_err == FM_ERR_CARD_GONE) {
            c->in = false;
        }
        return c->fail_err;
    }
    return c->in ? FM_OK : FM_ERR_CARD_GONE;
}

static bool k_mounted(void *ctx)
{
    return ((fk_card_t *)ctx)->in;
}

static unsigned k_mounts(void *ctx)
{
    return ((fk_card_t *)ctx)->mount;
}

static bool k_hold(void *ctx, unsigned mount)
{
    fk_card_t *c = (fk_card_t *)ctx;
    if (enter(c, FK_HOLD) != FM_OK || mount != c->mount) {
        return false;
    }
    ++c->holds;
    return true;
}

static void k_release(void *ctx)
{
    --((fk_card_t *)ctx)->holds;
}

static fm_err_t k_walk(void *ctx, const char *dir, fm_visit_fn visit,
                       void *vctx, unsigned *unlisted)
{
    fk_card_t *c = (fk_card_t *)ctx;
    if (unlisted != NULL) {
        *unlisted = 0u;
    }
    fm_err_t err = enter(c, FK_WALK);
    if (err != FM_OK) {
        return err;
    }
    if (!is_folder(c, dir)) {
        return FM_ERR_NOT_FOUND;
    }
    int handed = 0;
    char up[FK_PATH];
    for (int i = 0; i < c->count; ++i) {
        parent_of(up, sizeof(up), c->n[i].path);
        if (!same(up, dir)) {
            continue;
        }
        if ((c->gone_at_entry >= 0 && handed == c->gone_at_entry)
            || (c->gone_walk > 0 && c->calls[FK_WALK] == c->gone_walk)) {
            c->gone_walk = 0;
            /* Found gone part way and mounted again: the old card's. */
            c->gone_at_entry = -1;
            ++c->mount;
            return FM_ERR_CARD_GONE;
        }
        const char *slash = strrchr(c->n[i].path, '/');
        const char *name = (slash != NULL) ? slash + 1 : c->n[i].path;
        if ((strlen(name) > FM_LIST_NAME_MAX && !c->hand_long)
            || (name[0] == '.' && !c->show_dots)) {
            if (unlisted != NULL) {
                ++*unlisted;
            }
            continue;
        }
        const fm_dirent_t e = { name, c->n[i].size, c->n[i].dir };
        ++handed;
        if (visit != NULL) {
            visit(&e, vctx);
        }
    }
    return FM_OK;
}

static fm_err_t k_stat(void *ctx, const char *path, fm_stat_t *out)
{
    fk_card_t *c = (fk_card_t *)ctx;
    const fm_err_t err = enter(c, FK_STAT);
    if (err != FM_OK) {
        return err;
    }
    const int i = find(c, path);
    if (i < 0) {
        return FM_ERR_NOT_FOUND;
    }
    out->size = c->n[i].size;
    out->is_dir = c->n[i].dir;
    return FM_OK;
}

/**
 * @brief Whether a new entry may be made at @p path.
 * @param c     the card
 * @param path  the path
 * @return FM_OK, FM_ERR_EXISTS, FM_ERR_NOT_FOUND for no folder, or
 *         FM_ERR_FULL for a full one
 */
static fm_err_t room_for(const fk_card_t *c, const char *path)
{
    if (find(c, path) >= 0) {
        return FM_ERR_EXISTS;
    }
    char up[FK_PATH];
    parent_of(up, sizeof(up), path);
    if (!is_folder(c, up)) {
        return FM_ERR_NOT_FOUND;
    }
    if (c->dir_cap > 0 && entries_in(c, up) >= c->dir_cap) {
        return FM_ERR_FULL;
    }
    return FM_OK;
}

static fm_err_t k_mkdir(void *ctx, const char *path)
{
    fk_card_t *c = (fk_card_t *)ctx;
    fm_err_t err = enter(c, FK_MKDIR);
    if (err == FM_OK) {
        err = room_for(c, path);
    }
    if (err == FM_OK) {
        (void)add(c, path, true);
    }
    return err;
}

/**
 * @brief Whether entry @p i is open.
 * @param c  the card
 * @param i  its index
 * @return true while a handle holds it
 */
static bool is_open(const fk_card_t *c, int i)
{
    for (int f = 0; f < FK_FILES; ++f) {
        if (c->files[f].used && c->files[f].node == i) {
            return true;
        }
    }
    return false;
}

static fm_err_t k_unlink(void *ctx, const char *path)
{
    fk_card_t *c = (fk_card_t *)ctx;
    const fm_err_t err = enter(c, FK_UNLINK);
    if (err != FM_OK) {
        return err;
    }
    const int i = find(c, path);
    if (i < 0) {
        return FM_ERR_NOT_FOUND;
    }
    if (c->n[i].dir) {
        return FM_ERR_ARG;
    }
    if (is_open(c, i)) {
        return FM_ERR_DENIED;          /* FAT deletes no open file */
    }
    c->free_bytes += c->n[i].size;
    drop(c, i);
    return FM_OK;
}

static fm_err_t k_rmdir(void *ctx, const char *path)
{
    fk_card_t *c = (fk_card_t *)ctx;
    const fm_err_t err = enter(c, FK_RMDIR);
    if (err != FM_OK) {
        return err;
    }
    const int i = find(c, path);
    if (i < 0) {
        return FM_ERR_NOT_FOUND;
    }
    if (!c->n[i].dir) {
        return FM_ERR_ARG;
    }
    if (entries_in(c, path) > 0) {
        return FM_ERR_NOT_EMPTY;
    }
    drop(c, i);
    return FM_OK;
}

static fm_err_t k_rename(void *ctx, const char *from, const char *to)
{
    fk_card_t *c = (fk_card_t *)ctx;
    fm_err_t err = enter(c, FK_RENAME);
    if (err != FM_OK) {
        return err;
    }
    const int i = find(c, from);
    if (i < 0) {
        return FM_ERR_NOT_FOUND;
    }
    if (!same(from, to)) {
        err = room_for(c, to);
        if (err != FM_OK) {
            return err;
        }
    }
    /* The entry and, for a folder, every path below it. */
    const size_t k = strlen(from);
    for (int j = 0; j < c->count; ++j) {
        char *p = c->n[j].path;
        const bool below = j != i && strlen(p) > k && p[k] == '/'
                           && same_head(p, from, k);
        if (j != i && !below) {
            continue;
        }
        const size_t rest = strlen(p) - k;
        char *q = malloc(strlen(to) + rest + 1u);
        if (q == NULL) {
            abort();
        }
        memcpy(q, to, strlen(to));
        memcpy(q + strlen(to), p + k, rest + 1u);
        free(p);
        c->n[j].path = q;
    }
    return FM_OK;
}

static fm_err_t k_open(void *ctx, const char *path, fm_mode_t mode,
                       void **file)
{
    fk_card_t *c = (fk_card_t *)ctx;
    fm_err_t err = enter(c, FK_OPEN);
    if (err != FM_OK) {
        return err;
    }
    int f = 0;
    while (f < FK_FILES && c->files[f].used) {
        ++f;
    }
    if (f == FK_FILES) {
        return FM_ERR_IO;
    }
    int i = find(c, path);
    if (mode == FM_OPEN_NEW) {
        err = room_for(c, path);
        if (err != FM_OK) {
            return err;
        }
        i = add(c, path, false);
    } else if (i < 0) {
        return FM_ERR_NOT_FOUND;
    } else if (c->n[i].dir) {
        return FM_ERR_ARG;
    }
    c->files[f].used = true;
    c->files[f].node = i;
    c->files[f].pos = 0u;
    c->files[f].write = (mode == FM_OPEN_NEW);
    *file = &c->files[f];
    return FM_OK;
}

static fm_err_t k_read(void *ctx, void *file, uint8_t *buf, size_t cap,
                       size_t *got)
{
    fk_card_t *c = (fk_card_t *)ctx;
    fk_file_t *h = (fk_file_t *)file;
    *got = 0u;
    const fm_err_t err = enter(c, FK_READ);
    if (err != FM_OK) {
        return err;
    }
    const fk_node_t *e = &c->n[h->node];
    const size_t left = e->size - h->pos;
    const size_t n = (left < cap) ? left : cap;
    memcpy(buf, e->data + h->pos, n);
    h->pos += n;
    *got = n;
    return FM_OK;
}

static fm_err_t k_write(void *ctx, void *file, const uint8_t *buf,
                        size_t len)
{
    fk_card_t *c = (fk_card_t *)ctx;
    fk_file_t *h = (fk_file_t *)file;
    const fm_err_t err = enter(c, FK_WRITE);
    if (err != FM_OK) {
        return err;
    }
    if ((uint64_t)len > c->free_bytes) {
        return FM_ERR_FULL;
    }
    fk_node_t *e = &c->n[h->node];
    if (e->size + len > e->cap) {
        e->cap = (e->size + len) * 2u;
        e->data = grow(e->data, e->cap);
    }
    memcpy(e->data + e->size, buf, len);
    e->size += len;
    c->free_bytes -= len;
    return FM_OK;
}

static fm_err_t k_sync(void *ctx, void *file)
{
    (void)file;
    return enter((fk_card_t *)ctx, FK_SYNC);
}

static fm_err_t k_close(void *ctx, void *file)
{
    fk_card_t *c = (fk_card_t *)ctx;
    ((fk_file_t *)file)->used = false;    /* freed whatever the answer */
    return enter(c, FK_CLOSE);
}

void fk_init(fk_card_t *c)
{
    memset(c, 0, sizeof(*c));
    c->in = true;
    c->mount = 1u;
    c->free_bytes = UINT64_MAX / 2u;
    c->gone_at_entry = -1;
}

void fk_free(fk_card_t *c)
{
    for (int i = 0; i < c->count; ++i) {
        free(c->n[i].path);
        free(c->n[i].data);
    }
    free(c->n);
    memset(c, 0, sizeof(*c));
}

fm_io_t fk_io(fk_card_t *c)
{
    const fm_io_t io = {
        .mounted = k_mounted, .mounts = k_mounts, .hold = k_hold,
        .release = k_release, .walk = k_walk, .stat = k_stat,
        .mkdir = k_mkdir, .unlink = k_unlink, .rmdir = k_rmdir,
        .rename = k_rename, .open = k_open, .read = k_read,
        .write = k_write, .sync = k_sync, .close = k_close, .ctx = c,
    };
    return io;
}

void fk_dir(fk_card_t *c, const char *path)
{
    char p[FK_PATH];
    const size_t n = strlen(path);
    if (n >= sizeof(p)) {
        abort();
    }
    for (size_t i = 1u; i <= n; ++i) {
        if (path[i] == '/' || path[i] == '\0') {
            memcpy(p, path, i);
            p[i] = '\0';
            if (find(c, p) < 0) {
                (void)add(c, p, true);
            }
        }
    }
}

uint8_t fk_pattern(const char *path, size_t i)
{
    return (uint8_t)((i * 31u + strlen(path) * 7u + (i >> 8)) & 0xFFu);
}

void fk_file(fk_card_t *c, const char *path, const uint8_t *data,
             size_t size)
{
    char up[FK_PATH];
    parent_of(up, sizeof(up), path);
    if (up[0] != '\0') {
        fk_dir(c, up);
    }
    const int i = add(c, path, false);
    fk_node_t *e = &c->n[i];
    e->data = malloc(size + 1u);
    if (e->data == NULL) {
        abort();
    }
    for (size_t k = 0u; k < size; ++k) {
        e->data[k] = (data != NULL) ? data[k] : fk_pattern(path, k);
    }
    e->size = size;
    e->cap = size + 1u;
}

const fk_node_t *fk_get(const fk_card_t *c, const char *path)
{
    const int i = find(c, path);
    return (i >= 0) ? &c->n[i] : NULL;
}

bool fk_holds_pattern(const fk_card_t *c, const char *path, const char *of,
                      size_t size)
{
    const fk_node_t *e = fk_get(c, path);
    if (e == NULL || e->dir || e->size != size) {
        return false;
    }
    for (size_t k = 0u; k < size; ++k) {
        if (e->data[k] != fk_pattern(of, k)) {
            return false;
        }
    }
    return true;
}

void fk_fail(fk_card_t *c, fk_call_t call, long nth, fm_err_t err)
{
    c->fail_call = call;
    c->fail_nth = c->calls[call] + nth;
    c->fail_err = err;
}

void fk_pull(fk_card_t *c)
{
    c->in = false;
}

void fk_insert(fk_card_t *c)
{
    c->in = true;
    ++c->mount;
}

int fk_open_files(const fk_card_t *c)
{
    int k = 0;
    for (int f = 0; f < FK_FILES; ++f) {
        k += c->files[f].used ? 1 : 0;
    }
    return k;
}
