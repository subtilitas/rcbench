/**
 * @file
 * @brief The file manager's names and paths (fm_name.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "fm_name.h"

#include <string.h>

/**
 * @brief @p c in lower case, for ASCII.
 *
 * @param c A character.
 * @return Its lower case; other characters as they are.
 */
static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/**
 * @brief Whether the @p n bytes at @p s, in any case, are the 3 of @p word.
 *
 * @param s    The bytes.
 * @param n    Their count.
 * @param word 3 lower-case letters.
 * @return true when they are the same.
 */
static bool same3(const char *s, size_t n, const char *word)
{
    return n >= 3u && lower(s[0]) == word[0] && lower(s[1]) == word[1] &&
           lower(s[2]) == word[2];
}

/**
 * @brief Whether the stem of a name, the @p n bytes before its first full
 *        stop, is a device name.
 *
 * @param s The name.
 * @param n Bytes of its stem.
 * @return true for CON, PRN, AUX, NUL, COM1 to COM9 and LPT1 to LPT9.
 */
static bool device(const char *s, size_t n)
{
    if (n == 3u) {
        return same3(s, n, "con") || same3(s, n, "prn") ||
               same3(s, n, "aux") || same3(s, n, "nul");
    }
    return n == 4u && s[3] >= '1' && s[3] <= '9' &&
           (same3(s, n, "com") || same3(s, n, "lpt"));
}

/**
 * @brief Bytes of the character at @p s a name may hold: printable ASCII
 *        but the marks a FAT name cannot hold, or a German letter.
 *
 * @param s The name at the character.
 * @return 1 or 2; 0 for a character no name holds.
 */
static size_t name_char(const unsigned char *s)
{
    if (s[0] >= 0x20u && s[0] <= 0x7Eu) {
        return (strchr("\"*/:<>?\\|", s[0]) != NULL) ? 0u : 1u;
    }
    if (s[0] == 0xC3u) {
        switch (s[1]) {
        case 0x84u: case 0x96u: case 0x9Cu:   /* Ä Ö Ü */
        case 0xA4u: case 0xB6u: case 0xBCu:   /* ä ö ü */
        case 0x9Fu:                           /* ß */
            return 2u;
        default:
            break;
        }
    }
    return 0u;
}

fm_name_t fm_name_check(const char *name)
{
    size_t stem = 0u;

    if (name == NULL || name[0] == '\0') {
        return FM_NAME_EMPTY;
    }
    const size_t n = strlen(name);
    if (n > FM_NAME_MAX) {
        return FM_NAME_LONG;
    }
    for (size_t i = 0u; i < n;) {
        const size_t k = name_char((const unsigned char *)name + i);
        if (k == 0u) {
            return FM_NAME_CHAR;
        }
        i += k;
    }
    if (name[0] == '.' || name[0] == ' ' || name[n - 1u] == '.' ||
        name[n - 1u] == ' ') {
        return FM_NAME_EDGE;
    }
    while (stem < n && name[stem] != '.') {
        stem++;
    }
    return device(name, stem) ? FM_NAME_RESERVED : FM_NAME_OK;
}

bool fm_name_new(const char *name, char *out, size_t n)
{
    if (name == NULL || out == NULL || name[0] == '\0') {
        return false;
    }
    const size_t len = strlen(name);
    if (len > FM_NAME_MAX || len + FM_NEW_SUFFIX_LEN + 1u > n) {
        return false;
    }
    memmove(out, name, len + 1u);
    memcpy(out + len, FM_NEW_SUFFIX, FM_NEW_SUFFIX_LEN + 1u);
    return true;
}

/**
 * @brief Whether the @p n bytes at @p s are a name a path may hold: not
 *        empty, not "." or "..", and without "/".
 *
 * @param s The name.
 * @param n Its bytes.
 * @return true for a name.
 */
static bool path_name(const char *s, size_t n)
{
    if (n == 0u || (n == 1u && s[0] == '.') ||
        (n == 2u && s[0] == '.' && s[1] == '.')) {
        return false;
    }
    return memchr(s, '/', n) == NULL;
}

fm_path_t fm_path_check(const char *path)
{
    size_t depth = 0u;
    size_t from = 0u;

    if (path == NULL) {
        return FM_PATH_ARG;
    }
    const size_t n = strlen(path);
    if (n > FM_PATH_MAX) {
        return FM_PATH_LONG;
    }
    if (n == 0u) {
        return FM_PATH_OK;
    }
    for (size_t i = 0u; i <= n; ++i) {
        if (i < n && path[i] != '/') {
            continue;
        }
        if (!path_name(path + from, i - from)) {
            return FM_PATH_BAD;
        }
        depth++;
        from = i + 1u;
    }
    return (depth > FM_DEPTH_MAX) ? FM_PATH_DEEP : FM_PATH_OK;
}

/**
 * @brief Whether one name of a path names its entry one way only.
 * @param s  the name's first byte
 * @param n  its bytes
 * @return true when it is not an alias (fm_path_plain())
 */
static bool plain_name(const char *s, size_t n)
{
    if (n == 0u || s[0] == ' ' || s[n - 1u] == ' ' || s[n - 1u] == '.') {
        return false;
    }
    for (size_t i = 0u; i < n; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20u || c >= 0x7Fu || strchr("\"*:<>?\\|", c) != NULL) {
            return false;
        }
        if (c == '~' && i + 1u < n && s[i + 1u] >= '0' && s[i + 1u] <= '9') {
            return false;
        }
    }
    return true;
}

bool fm_path_plain(const char *path)
{
    if (path == NULL) {
        return false;
    }
    size_t from = 0u;
    const size_t n = strlen(path);
    for (size_t i = 0u; i <= n && n > 0u; ++i) {
        if (i < n && path[i] != '/') {
            continue;
        }
        if (!plain_name(path + from, i - from)) {
            return false;
        }
        from = i + 1u;
    }
    return true;
}

size_t fm_path_depth(const char *path)
{
    size_t depth = 0u;

    if (path == NULL || path[0] == '\0') {
        return 0u;
    }
    for (const char *p = path; *p != '\0'; ++p) {
        depth += (*p == '/') ? 1u : 0u;
    }
    return depth + 1u;
}

fm_path_t fm_path_join(const char *dir, const char *name, bool is_dir,
                       char *out, size_t n)
{
    if (dir == NULL || name == NULL || out == NULL) {
        return FM_PATH_ARG;
    }
    const fm_path_t d = fm_path_check(dir);
    if (d != FM_PATH_OK) {
        return d;
    }
    const size_t dl = strlen(dir);
    const size_t nl = strlen(name);
    if (!path_name(name, nl)) {
        return FM_PATH_BAD;
    }
    const size_t total = (dl > 0u) ? dl + 1u + nl : nl;
    if (total > FM_PATH_MAX) {
        return FM_PATH_LONG;
    }
    if (is_dir && fm_path_depth(dir) + 1u > FM_DEPTH_MAX) {
        return FM_PATH_DEEP;
    }
    if (total + 1u > n) {
        return FM_PATH_ARG;
    }
    /* out may be dir: the folder stays where it is and the name goes
     * behind it. */
    if (out != dir) {
        memmove(out, dir, dl);
    }
    size_t at = dl;
    if (dl > 0u) {
        out[at++] = '/';
    }
    memmove(out + at, name, nl);
    out[total] = '\0';
    return FM_PATH_OK;
}

bool fm_path_up(char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    char *slash = strrchr(path, '/');
    if (slash == NULL) {
        path[0] = '\0';
    } else {
        *slash = '\0';
    }
    return true;
}

const char *fm_path_base(const char *path)
{
    if (path == NULL) {
        return "";
    }
    const char *slash = strrchr(path, '/');
    return (slash == NULL) ? path : slash + 1u;
}
