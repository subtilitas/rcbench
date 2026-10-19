/*
 * Making a folder of the card one level at a time.  See card_tree.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "card_tree.h"

#include <string.h>

card_dir_t card_make_dirs(const char *dir, card_make_fn make, void *ctx)
{
    if (dir == NULL || make == NULL) {
        return CARD_DIR_ARG;
    }
    const size_t len = strlen(dir);
    if (len >= CARD_DIR_MAX) {
        return CARD_DIR_ARG;
    }
    char level[CARD_DIR_MAX];
    for (size_t i = 1u; i <= len; ++i) {
        if (dir[i] != '/' && dir[i] != '\0') {
            continue;
        }
        memcpy(level, dir, i);
        level[i] = '\0';
        const card_dir_t r = make(ctx, level);
        if (r != CARD_DIR_OK) {
            return r;
        }
    }
    return CARD_DIR_OK;
}
