/*
 * A card in memory for the Wi-Fi page's walks and transfers.
 *
 * Folders and files in the order they are added, which is the order a
 * folder lists them in, as FAT (file allocation table) folders list theirs.
 * A file's bytes are a pattern of its index, or a buffer given; a read can
 * be made to fail, or to end early, at a chosen byte; and the whole card
 * can be pulled.  Open handles are counted so a test can hold a walk to the
 * one folder handle it promises.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_WA_FAKEFS_H
#define RCBENCH_WA_FAKEFS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "card_tree.h"
#include "wa_walk.h"

typedef struct {
    char           *path;      /* relative to the root; "" for the root */
    char           *name;
    bool            dir;
    uint64_t        size;
    int64_t         mtime;
    const uint8_t  *data;      /* NULL: the pattern                     */
    int64_t         fail_at;   /* a read reaching this byte fails; -1   */
    int64_t         end_at;    /* the file ends here early; -1          */
    int             first;     /* a folder's first entry, or -1         */
    int             last;
    int             next;      /* the next entry of the same folder     */
} fake_node_t;

typedef struct {
    fake_node_t *n;
    int          count;
    int          cap;
    bool         pulled;       /* every call fails                      */
    int          dirs_open;
    int          dirs_open_max;
    int          files_open;
    long         readdirs;
} fake_fs_t;

typedef struct {
    int at;                    /* the next node to hand out, or -1      */
} fake_dir_t;

typedef struct {
    int      node;
    uint64_t pos;
} fake_file_t;

static inline uint8_t fake_byte(int node, uint64_t i)
{
    return (uint8_t)(i * 31u + (uint64_t)node * 7u);
}

static inline int fake_find(const fake_fs_t *fs, const char *path)
{
    for (int i = 0; i < fs->count; ++i) {
        if (strcmp(fs->n[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static inline void fake_init(fake_fs_t *fs)
{
    memset(fs, 0, sizeof(*fs));
    fs->cap = 64;
    fs->n = calloc((size_t)fs->cap, sizeof(fake_node_t));
    fake_node_t *root = &fs->n[fs->count++];
    root->path = strdup("");
    root->name = strdup("");
    root->dir = true;
    root->first = root->last = root->next = -1;
}

static inline void fake_free(fake_fs_t *fs)
{
    for (int i = 0; i < fs->count; ++i) {
        free(fs->n[i].path);
        free(fs->n[i].name);
    }
    free(fs->n);
    memset(fs, 0, sizeof(*fs));
}

/* Add @p name in the folder at node @p p; returns its node. */
static inline int fake_add_in(fake_fs_t *fs, int p, const char *name,
                              bool dir, uint64_t size, int64_t mtime)
{
    if (fs->count == fs->cap) {
        fs->cap *= 2;
        fake_node_t *grown = realloc(fs->n,
                                     (size_t)fs->cap * sizeof(fake_node_t));
        if (grown == NULL) {
            abort();
        }
        fs->n = grown;
    }
    const int i = fs->count++;
    fake_node_t *n = &fs->n[i];
    memset(n, 0, sizeof(*n));
    const char *pp = fs->n[p].path;
    const size_t pl = strlen(pp), nl = strlen(name);
    n->path = malloc(pl + nl + 2u);
    if (n->path == NULL) {
        abort();
    }
    if (pl > 0u) {
        memcpy(n->path, pp, pl);
        n->path[pl] = '/';
        memcpy(n->path + pl + 1u, name, nl + 1u);
    } else {
        memcpy(n->path, name, nl + 1u);
    }
    n->name = strdup(name);
    n->dir = dir;
    n->size = dir ? 0u : size;
    n->mtime = mtime;
    n->fail_at = -1;
    n->end_at = -1;
    n->first = n->last = n->next = -1;
    if (fs->n[p].last < 0) {
        fs->n[p].first = i;
    } else {
        fs->n[fs->n[p].last].next = i;
    }
    fs->n[p].last = i;
    return i;
}

/* Add @p path, its folder already added; returns its node. */
static inline int fake_add(fake_fs_t *fs, const char *path, bool dir,
                           uint64_t size, int64_t mtime)
{
    const char *slash = strrchr(path, '/');
    char parent[WA_PATH_MAX * 4];
    if (slash == NULL) {
        parent[0] = '\0';
    } else {
        memcpy(parent, path, (size_t)(slash - path));
        parent[slash - path] = '\0';
    }
    const int p = fake_find(fs, parent);
    return fake_add_in(fs, (p < 0) ? 0 : p, (slash != NULL) ? slash + 1 : path,
                       dir, size, mtime);
}

static inline void *fake_opendir(void *ctx, const char *path)
{
    fake_fs_t *fs = ctx;
    if (fs->pulled) {
        return NULL;
    }
    const int i = fake_find(fs, path);
    if (i < 0 || !fs->n[i].dir) {
        return NULL;
    }
    fake_dir_t *d = malloc(sizeof(*d));
    if (d == NULL) {
        return NULL;
    }
    d->at = fs->n[i].first;
    if (++fs->dirs_open > fs->dirs_open_max) {
        fs->dirs_open_max = fs->dirs_open;
    }
    return d;
}

static inline int fake_readdir(void *ctx, void *dir, wa_dirent_t *out)
{
    fake_fs_t *fs = ctx;
    fake_dir_t *d = dir;
    ++fs->readdirs;
    if (fs->pulled) {
        return -1;
    }
    if (d->at < 0) {
        return 0;
    }
    const fake_node_t *n = &fs->n[d->at];
    memset(out, 0, sizeof(*out));
    strncpy(out->name, n->name, WA_NAME_BYTES - 1u);
    out->dir = n->dir;
    out->size = n->size;
    out->mtime = n->mtime;
    d->at = n->next;
    return 1;
}

static inline void fake_closedir(void *ctx, void *dir)
{
    fake_fs_t *fs = ctx;
    --fs->dirs_open;
    free(dir);
}

static inline void *fake_open(void *ctx, const char *path)
{
    fake_fs_t *fs = ctx;
    if (fs->pulled) {
        return NULL;
    }
    const int i = fake_find(fs, path);
    if (i < 0 || fs->n[i].dir) {
        return NULL;
    }
    fake_file_t *f = malloc(sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    f->node = i;
    f->pos = 0u;
    ++fs->files_open;
    return f;
}

static inline int32_t fake_read(void *ctx, void *file, uint8_t *dst,
                                uint32_t len)
{
    fake_fs_t *fs = ctx;
    fake_file_t *f = file;
    const fake_node_t *n = &fs->n[f->node];
    if (fs->pulled) {
        return -1;
    }
    if (n->fail_at >= 0 && f->pos + len > (uint64_t)n->fail_at) {
        if (f->pos < (uint64_t)n->fail_at) {
            len = (uint32_t)((uint64_t)n->fail_at - f->pos);  /* up to it */
        } else {
            return -1;
        }
    }
    uint64_t end = n->size;
    if (n->end_at >= 0 && (uint64_t)n->end_at < end) {
        end = (uint64_t)n->end_at;
    }
    if (f->pos >= end) {
        return 0;
    }
    if (len > end - f->pos) {
        len = (uint32_t)(end - f->pos);
    }
    for (uint32_t k = 0; k < len; ++k) {
        dst[k] = (n->data != NULL) ? n->data[f->pos + k]
                                   : fake_byte(f->node, f->pos + k);
    }
    f->pos += len;
    return (int32_t)len;
}

static inline void fake_close(void *ctx, void *file)
{
    fake_fs_t *fs = ctx;
    --fs->files_open;
    free(file);
}

static inline wa_fs_t fake_wa(fake_fs_t *fs)
{
    wa_fs_t w = { fake_opendir, fake_readdir, fake_closedir, fake_open,
                  fake_read, fake_close, fs };
    return w;
}

/*
 * A card as the panel leaves it (card_tree.h): the bench's folders under
 * BENCH, the user's under USER, and in the root the files of firmware 0.21
 * and older, its ESC and KST folders included.  BENCH/HITEC/CONF is empty.
 * 16 files in 13 folders, the root counted, and 1,121,451 bytes.
 */
#define FAKE_TREE_FILES 16
#define FAKE_TREE_BYTES 1121451u

static inline void fake_bench_tree(fake_fs_t *fs)
{
    static const struct {
        const char *path;
        bool        dir;
        uint64_t    size;
    } k[] = {
        { "BENCH041.CSV", false, 18422u },
        { "SERVO040.TXT", false, 1890u },
        { "RCBENCH.LOG", false, 2080u },
        { "ESC", true, 0u },
        { "ESC/hobbywing-flyfun-8item.json", false, 3104u },
        { "KST", true, 0u },
        { "KST/KST001.TXT", false, 1310u },
        { CARD_BENCH, true, 0u },
        { CARD_FAULT_LOG, false, 6240u },
        { CARD_FAULT_OLD, false, 1048576u },
        { CARD_AUTO_LOGS, true, 0u },
        { CARD_AUTO_LOGS "/SERVO001.CSV", false, 4410u },
        { CARD_AUTO_LOGS "/MOTOR002.CSV", false, 9120u },
        { CARD_AUTO_LOGS "/SUPPL003.CSV", false, 2205u },
        { "BENCH/ESC", true, 0u },
        { CARD_ESC_CONF, true, 0u },
        { CARD_ESC_CONF "/hobbywing-flyfun-8item.json", false, 3104u },
        { "BENCH/KST", true, 0u },
        { CARD_KST_CONF, true, 0u },
        { CARD_KST_CONF "/KST.TXT", false, 396u },
        { CARD_KST_CONF "/Wing left.TXT", false, 1310u },
        { "BENCH/HITEC", true, 0u },
        { CARD_HITEC_CONF, true, 0u },
        { CARD_USER, true, 0u },
        { CARD_USER_LOGS, true, 0u },
        { CARD_USER_LOGS "/ATEST001.CSV", false, 15104u },
        { CARD_USER_LOGS "/ATEST001.TXT", false, 2240u },
        { CARD_USER_LOGS "/LIMIT002.TXT", false, 1940u },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        fake_add(fs, k[i].path, k[i].dir, k[i].size, 315532800);
    }
}

#endif /* RCBENCH_WA_FAKEFS_H */
