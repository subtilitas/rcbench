/**
 * @file
 * @brief Walking the card. See wa_walk.h.
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
    for (unsigned k = 0; k <= WA_WALK_DEPTH; ++k) {
        if (w->lv[k].dir != NULL && w->fs != NULL
            && w->fs->closedir != NULL) {
            w->fs->closedir(w->fs->ctx, w->lv[k].dir);
        }
        w->lv[k].dir = NULL;
    }
}

/**
 * @brief End the walk with @p ev, every handle closed.
 * @param w   the walk
 * @param ev  END, NO_CARD or ERROR
 * @return @p ev
 */
static wa_walk_ev_t finish(wa_walk_t *w, wa_walk_ev_t ev)
{
    wa_walk_end(w);
    w->over = true;
    w->last = ev;
    return ev;
}

/**
 * @brief Whether a name read from the card can be served by its path.
 * @param s  the name
 * @param n  its bytes
 * @return true for UTF-8 of 1 to WA_NAME_UNITS units, no separator, no
 *         control byte
 */
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

/**
 * @brief The path of @p name in the folder whose path is @p base bytes,
 *        into @c w->path.
 * @param w     the walk
 * @param base  bytes of the folder's path
 * @param name  the name
 * @param n     its bytes
 * @return the path's bytes, or 0 when it is over WA_PATH_MAX
 */
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
        w->lv[0].dir = (fs != NULL) ? fs->opendir(fs->ctx, "") : NULL;
        if (w->lv[0].dir == NULL) {
            return finish(w, WA_WALK_NO_CARD);
        }
        return WA_WALK_DIR;
    }
    for (;;) {
        if (fs->stopped != NULL && fs->stopped(fs->ctx)) {
            return finish(w, WA_WALK_ERROR);
        }
        wa_level_t *lv = &w->lv[w->depth];
        if (lv->dir == NULL) {
            /* The folders' pass: the folder again from its start. */
            w->path[lv->len] = '\0';
            lv->dir = fs->opendir(fs->ctx, w->path);
            if (lv->dir == NULL) {
                return finish(w, WA_WALK_ERROR);
            }
        }
        const int r = fs->readdir(fs->ctx, lv->dir, &w->ent);
        if (r < 0) {
            return finish(w, WA_WALK_ERROR);
        }
        if (r == 0) {
            fs->closedir(fs->ctx, lv->dir);
            lv->dir = NULL;
            if (!lv->dirs) {
                lv->dirs = true;     /* the files are done: the folders */
                continue;
            }
            if (w->depth == 0u) {
                return finish(w, WA_WALK_END);
            }
            --w->depth;              /* the parent's handle reads on */
            continue;
        }
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
        ++w->depth;
        w->lv[w->depth] = (wa_level_t){ NULL, len, false };
        w->lv[w->depth].dir = fs->opendir(fs->ctx, w->path);
        if (w->lv[w->depth].dir == NULL) {
            return finish(w, WA_WALK_ERROR);
        }
        w->ent.size = 0u;
        return WA_WALK_DIR;
    }
}
