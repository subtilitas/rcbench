/**
 * @file
 * @brief Making a folder of the card one level at a time, and checking a log
 *        folder.
 *
 * See card_tree.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "card_tree.h"

#include <stdbool.h>
#include <stdio.h>
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

/**
 * @brief The folders the bench keeps its own files in: the parent of each
 *        programmer's CONF folder.
 *
 * A log folder there, or below, is refused.
 */
static const char *const k_bench_conf[] = {
    CARD_ESC_CONF,
    CARD_KST_CONF,
    CARD_HITEC_CONF,
};

/**
 * @brief The names FAT (File Allocation Table) volumes on Windows take for a
 *        device, with or without an extension.
 */
static const char *const k_devices[] = { "CON", "PRN", "AUX", "NUL" };

/**
 * @brief Whether a character is allowed in a folder name.
 *
 * @param c  The upper-case character.
 * @return true for A-Z, 0-9, "_" and "-".
 */
static bool folder_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
           || c == '-';
}

/**
 * @brief Whether a folder name is a device name.
 *
 * @param name  The name; not NUL-terminated.
 * @param len   Length of @p name, characters.
 * @return true for CON, PRN, AUX, NUL, COM1 to COM9 and LPT1 to LPT9.
 */
static bool is_device(const char *name, size_t len)
{
    for (size_t i = 0u; i < sizeof(k_devices) / sizeof(k_devices[0]); ++i) {
        if (len == 3u && memcmp(name, k_devices[i], 3u) == 0) {
            return true;
        }
    }
    return len == 4u
           && (memcmp(name, "COM", 3u) == 0 || memcmp(name, "LPT", 3u) == 0)
           && name[3] >= '1' && name[3] <= '9';
}

/**
 * @brief Whether @p path is a bench area folder or one below it.
 *
 * @param path  The upper-case path, folders separated by "/".
 * @return true for BENCH/ESC, BENCH/KST, BENCH/HITEC and below.
 */
static bool is_bench(const char *path)
{
    for (size_t i = 0u; i < sizeof(k_bench_conf) / sizeof(k_bench_conf[0]);
         ++i) {
        const char *conf = k_bench_conf[i];
        const char *slash = strrchr(conf, '/');
        const size_t area = (slash != NULL) ? (size_t)(slash - conf)
                                            : strlen(conf);
        if (strncmp(path, conf, area) == 0
            && (path[area] == '\0' || path[area] == '/')) {
            return true;
        }
    }
    return false;
}

card_folder_t card_folder_check(const char *in, char *out, size_t n)
{
    if (in == NULL) {
        return CARD_FOLDER_ARG;
    }
    /* Two folders of 8 and the "/" between them: 17 and the terminator. */
    char path[CARD_FOLDER_DEPTH_MAX * (CARD_FOLDER_NAME_MAX + 1)] = { 0 };
    size_t len = 0u;
    size_t seg = 0u;          /* characters of the folder being read */
    int depth = 1;
    for (const char *p = in;; ++p) {
        const char c = *p;
        if (c == '/' || c == '\0') {
            if (seg == 0u) {
                return CARD_FOLDER_EMPTY;
            }
            if (is_device(&path[len - seg], seg)) {
                return CARD_FOLDER_RESERVED;
            }
            if (c == '\0') {
                break;
            }
            if (++depth > CARD_FOLDER_DEPTH_MAX) {
                return CARD_FOLDER_DEEP;
            }
            path[len++] = '/';
            seg = 0u;
            continue;
        }
        const char up = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        if (!folder_char(up)) {
            return CARD_FOLDER_CHAR;
        }
        if (++seg > CARD_FOLDER_NAME_MAX) {
            return CARD_FOLDER_LONG;
        }
        path[len++] = up;
    }
    path[len] = '\0';
    if (is_bench(path)) {
        return CARD_FOLDER_BENCH;
    }
    if (out != NULL) {
        if (n <= len) {
            return CARD_FOLDER_ARG;
        }
        memcpy(out, path, len + 1u);
    }
    return CARD_FOLDER_OK;
}

const char *card_folder_use(const char *text, const char *fallback,
                            char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return fallback;
    }
    if (card_folder_check(text, out, n) != CARD_FOLDER_OK) {
        snprintf(out, n, "%s", (fallback != NULL) ? fallback : "");
    }
    return out;
}
