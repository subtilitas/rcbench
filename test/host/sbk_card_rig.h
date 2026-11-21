/*
 * A card's folder BENCH/SETUP in memory, behind settings_backup.h's sb_io_t:
 * up to FAKE_FILES files of FAKE_BYTES, a room for the whole card, a card
 * that takes no file, a rename or a read that fails.  For the suites of the
 * settings backup and for the renderer's views of BACKUP.
 *
 * Functions are `static inline` and the folder is `unused`, so a file that
 * includes this and calls only some compiles clean under
 * -Werror=unused-function.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "settings_backup.h"

#define FAKE_FILES 8
#define FAKE_BYTES (SF_FILE_MAX + 1u)

typedef struct {
    bool   used;
    char   name[SB_NAME_MAX];
    char   data[FAKE_BYTES];
    size_t len;
} fake_file_t;

typedef struct {
    bool        mounted;
    fake_file_t f[FAKE_FILES];
    size_t      room;       /* bytes the card takes in all              */
    bool        deny;       /* the card takes no file                   */
    int         fail_rename; /* the nth rename from now fails; 0 none  */
    bool        fail_read;
    unsigned    writes;
} fake_t;

static fake_t g_fake __attribute__((unused));

static inline fake_file_t *find(const char *name)
{
    for (int i = 0; i < FAKE_FILES; ++i) {
        if (g_fake.f[i].used && strcmp(g_fake.f[i].name, name) == 0) {
            return &g_fake.f[i];
        }
    }
    return NULL;
}

static inline size_t used_bytes(void)
{
    size_t n = 0u;
    for (int i = 0; i < FAKE_FILES; ++i) {
        n += g_fake.f[i].used ? g_fake.f[i].len : 0u;
    }
    return n;
}

static inline bool f_mounted(void *ctx)
{
    (void)ctx;
    return g_fake.mounted;
}

static inline int32_t f_size(void *ctx, const char *name)
{
    (void)ctx;
    const fake_file_t *f = find(name);
    return (f != NULL) ? (int32_t)f->len : -1;
}

static inline int32_t f_read(void *ctx, const char *name, char *buf, size_t cap)
{
    (void)ctx;
    const fake_file_t *f = find(name);
    if (f == NULL || g_fake.fail_read) {
        return -1;
    }
    const size_t n = (f->len < cap) ? f->len : cap;
    memcpy(buf, f->data, n);
    return (int32_t)n;
}

static inline sb_io_result_t f_write(void *ctx, const char *name, const char *data,
                              size_t len)
{
    (void)ctx;
    ++g_fake.writes;
    if (g_fake.deny) {
        return SB_IO_DENIED;
    }
    fake_file_t *f = find(name);
    const size_t had = (f != NULL) ? f->len : 0u;
    if (used_bytes() - had + len > g_fake.room || len > FAKE_BYTES) {
        if (f != NULL) {
            f->used = false;           /* the open emptied it */
        }
        return SB_IO_FULL;
    }
    if (f == NULL) {
        for (int i = 0; i < FAKE_FILES && f == NULL; ++i) {
            if (!g_fake.f[i].used) {
                f = &g_fake.f[i];
            }
        }
        if (f == NULL) {
            return SB_IO_ERROR;
        }
    }
    f->used = true;
    snprintf(f->name, sizeof(f->name), "%s", name);
    memcpy(f->data, data, len);
    f->data[(len < FAKE_BYTES) ? len : FAKE_BYTES - 1u] = '\0';
    f->len = len;
    return SB_IO_OK;
}

static inline sb_io_result_t f_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    if (g_fake.fail_rename > 0 && --g_fake.fail_rename == 0) {
        return SB_IO_ERROR;
    }
    fake_file_t *f = find(from);
    if (f == NULL) {
        return SB_IO_MISSING;
    }
    if (find(to) != NULL) {
        return SB_IO_ERROR;            /* FAT renames onto no file */
    }
    snprintf(f->name, sizeof(f->name), "%s", to);
    return SB_IO_OK;
}

static inline sb_io_result_t f_remove(void *ctx, const char *name)
{
    (void)ctx;
    fake_file_t *f = find(name);
    if (f == NULL) {
        return SB_IO_MISSING;
    }
    f->used = false;
    return SB_IO_OK;
}

static const sb_io_t k_io __attribute__((unused)) = {
    NULL, f_mounted, f_size, f_read, f_write, f_rename, f_remove,
};

/** The folder empty, a card mounted, 100000 bytes of room. */
static inline void fake_fresh(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.mounted = true;
    g_fake.room = 100000u;
}

