/*
 * Walking the card. See wa_walk.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_walk.h"

#include <string.h>

void wa_walk_begin(wa_walk_t *w, const wa_fs_t *fs)
{
    memset(w, 0, sizeof(*w));
    w->fs = fs;
}

void wa_walk_end(wa_walk_t *w)
{
    if (w->dir != NULL && w->fs != NULL && w->fs->closedir != NULL) {
        w->fs->closedir(w->fs->ctx, w->dir);
    }
    w->dir = NULL;
}

static wa_walk_ev_t finish(wa_walk_t *w, wa_walk_ev_t ev)
{
    wa_walk_end(w);
    w->over = true;
    w->last = ev;
    return ev;
}

/* Whether a name read from the card can be served by its path. */
static bool name_ok(const char *s, size_t n)
{
    if (n == 0u || memchr(s, '/', n) != NULL || memchr(s, '\\', n) != NULL) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if ((uint8_t)s[i] < 0x20u || s[i] == 0x7F) {
            return false;
        }
    }
    const long units = wa_utf8_units(s, n);
    return units > 0 && (unsigned long)units <= WA_NAME_UNITS;
}

/* The path of @p name in the folder at @p base bytes, or 0 when too long. */
static uint32_t join(wa_walk_t *w, uint32_t base, const char *name, size_t n)
{
    const size_t len = base + ((base > 0u) ? 1u : 0u) + n;
    if (base >= WA_PATH_MAX || len > WA_PATH_MAX) {
        return 0u;
    }
    if (base > 0u) {
        w->path[base] = '/';
        memcpy(w->path + base + 1u, name, n);
    } else {
        memcpy(w->path, name, n);
    }
    w->path[len] = '\0';
    return (uint32_t)len;
}

wa_walk_ev_t wa_walk_next(wa_walk_t *w)
{
    if (w->over) {
        return w->last;
    }
    const wa_fs_t *fs = w->fs;
    if (!w->started) {
        w->started = true;
        w->path[0] = '\0';
        w->dir = (fs != NULL) ? fs->opendir(fs->ctx, "") : NULL;
        if (w->dir == NULL) {
            return finish(w, WA_WALK_NO_CARD);
        }
        return WA_WALK_DIR;
    }
    for (;;) {
        wa_level_t *lv = &w->lv[w->depth];
        if (w->dir == NULL) {
            /* Back from a folder: open this one again where it was left. */
            w->path[lv->len] = '\0';
            w->dir = fs->opendir(fs->ctx, w->path);
            if (w->dir == NULL) {
                return finish(w, WA_WALK_ERROR);
            }
            for (uint32_t k = 0; k < lv->done; ++k) {
                if (fs->readdir(fs->ctx, w->dir, &w->ent) != 1) {
                    return finish(w, WA_WALK_ERROR);
                }
            }
        }
        const int r = fs->readdir(fs->ctx, w->dir, &w->ent);
        if (r < 0) {
            return finish(w, WA_WALK_ERROR);
        }
        if (r == 0) {
            fs->closedir(fs->ctx, w->dir);
            w->dir = NULL;
            if (!lv->dirs) {
                lv->dirs = true;     /* the files are done: the folders */
                lv->done = 0u;
                continue;
            }
            if (w->depth == 0u) {
                return finish(w, WA_WALK_END);
            }
            --w->depth;
            continue;
        }
        ++lv->done;
        w->ent.name[WA_NAME_BYTES - 1u] = '\0';
        const size_t n = strlen(w->ent.name);
        if ((n == 1u && w->ent.name[0] == '.')
            || (n == 2u && w->ent.name[0] == '.' && w->ent.name[1] == '.')) {
            continue;
        }
        if (w->ent.dir != lv->dirs) {
            continue;                /* the other pass's */
        }
        if (!name_ok(w->ent.name, n)) {
            ++w->left_out;
            continue;
        }
        if (!lv->dirs) {
            if (join(w, lv->len, w->ent.name, n) == 0u) {
                ++w->left_out;
                continue;
            }
            return WA_WALK_FILE;
        }
        if (w->depth == WA_WALK_DEPTH) {
            ++w->left_out;
            continue;
        }
        const uint32_t len = join(w, lv->len, w->ent.name, n);
        if (len == 0u) {
            ++w->left_out;
            continue;
        }
        fs->closedir(fs->ctx, w->dir);
        ++w->depth;
        w->lv[w->depth] = (wa_level_t){ 0u, len, false };
        w->dir = fs->opendir(fs->ctx, w->path);
        if (w->dir == NULL) {
            return finish(w, WA_WALK_ERROR);
        }
        w->ent.size = 0u;
        return WA_WALK_DIR;
    }
}
