/**
 * @file
 * @brief The `info` cell of a run's CSV file: written, read back, and the
 *        date it carries.  See log_info.h.
 *
 * SPDX-License-Identifier: MIT
 */

/** @brief localtime_r() and strnlen() are POSIX: newlib and glibc declare
 *         them with this, and its name is the standard's, not ours. */
/* NOLINTNEXTLINE(bugprone-reserved-identifier) */
#define _POSIX_C_SOURCE 200809L

#include "log_info.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "wall_clock.h"

/** @brief The clock log_info_now() reads; NULL for none. */
static log_info_clock_fn s_clock;

/** @brief One key and where its value lives in a log_info_t. */
typedef struct {
    const char *key;     /**< the key as written */
    size_t      offset;  /**< the field's offset in log_info_t */
    size_t      size;    /**< the field's size, bytes */
} info_key_t;

/** @brief A log_info_t field's entry: its key, offset and size. */
#define INFO_KEY(name) \
    { #name, offsetof(log_info_t, name), sizeof(((log_info_t *)0)->name) }

/** @brief The keys in the order a cell carries them. */
static const info_key_t k_keys[] = {
    INFO_KEY(dut), INFO_KEY(note), INFO_KEY(date), INFO_KEY(fw),
    INFO_KEY(servo), INFO_KEY(meter), INFO_KEY(angle), INFO_KEY(id),
};

/** @brief The number of keys. */
#define INFO_KEYS (sizeof(k_keys) / sizeof(k_keys[0]))

/**
 * @brief Whether the escape rule replaces byte @p c.
 * @param c The byte.
 * @return true for `&`, `=`, `;`, `"` and a control character.
 */
static bool escaped(unsigned char c)
{
    return c < 0x20u || c == 0x7Fu || c == '&' || c == '=' || c == ';'
           || c == '"';
}

void log_info_clean(char *out, size_t n, const char *in)
{
    if (out == NULL || n == 0u) {
        return;
    }
    size_t k = 0u;
    if (in != NULL) {
        for (; in[k] != '\0' && k + 1u < n; ++k) {
            out[k] = escaped((unsigned char)in[k]) ? '_' : in[k];
        }
    }
    out[k] = '\0';
}

size_t log_info_format(const log_info_t *in, char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (in == NULL) {
        return 0u;
    }
    size_t len = 0u;
    for (size_t i = 0u; i < INFO_KEYS; ++i) {
        const char *v = (const char *)in + k_keys[i].offset;
        /* A field filled to its last byte is still cut at it. */
        const size_t vlen = strnlen(v, k_keys[i].size - 1u);
        if (vlen == 0u) {
            continue;
        }
        const size_t klen = strlen(k_keys[i].key);
        const size_t need = (len > 0u ? 1u : 0u) + klen + 1u + vlen;
        if (len + need >= n) {
            out[0] = '\0';
            return 0u;
        }
        if (len > 0u) {
            out[len++] = '&';
        }
        memcpy(out + len, k_keys[i].key, klen);
        len += klen;
        out[len++] = '=';
        for (size_t c = 0u; c < vlen; ++c) {
            out[len++] = escaped((unsigned char)v[c]) ? '_' : v[c];
        }
        out[len] = '\0';
    }
    return len;
}

/**
 * @brief The field of key @p key, of length @p klen.
 * @param key  The key, not terminated.
 * @param klen Its length, bytes.
 * @return Its entry; NULL for a key the reader does not know.
 */
static const info_key_t *key_of(const char *key, size_t klen)
{
    for (size_t i = 0u; i < INFO_KEYS; ++i) {
        if (strlen(k_keys[i].key) == klen
            && memcmp(k_keys[i].key, key, klen) == 0) {
            return &k_keys[i];
        }
    }
    return NULL;
}

bool log_info_parse(const char *cell, log_info_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (cell == NULL) {
        return false;
    }
    bool any = false;
    const char *p = cell;
    while (*p != '\0') {
        const char *end = strchr(p, '&');
        if (end == NULL) {
            end = p + strlen(p);
        }
        const char *eq = memchr(p, '=', (size_t)(end - p));
        const info_key_t *k = (eq != NULL) ? key_of(p, (size_t)(eq - p))
                                           : NULL;
        if (k != NULL) {
            char *field = (char *)out + k->offset;
            size_t vlen = (size_t)(end - eq - 1);
            if (vlen > k->size - 1u) {
                vlen = k->size - 1u;
            }
            memcpy(field, eq + 1, vlen);
            field[vlen] = '\0';
            any = true;
        }
        p = (*end == '&') ? end + 1 : end;
    }
    return any;
}

/**
 * @brief A header cell without the blanks around it.
 * @param s The cell; changed in place.
 * @return The trimmed text.
 */
static char *trim_cell(char *s)
{
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    size_t n = strlen(s);
    while (n > 0u && (s[n - 1u] == ' ' || s[n - 1u] == '\t'
                      || s[n - 1u] == '\r')) {
        s[--n] = '\0';
    }
    return s;
}

bool log_info_read(log_source_t *src, char delimiter, log_info_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (src == NULL || out == NULL || src->rewind == NULL
        || !src->rewind(src->ctx)) {
        return false;
    }
    log_reader_t reader;
    log_reader_init(&reader, src);
    char store[LOG_ROW_MAX];
    char *fields[LOG_MAX_COLUMNS];
    int total = 0;
    const int n = log_reader_row(&reader, delimiter, store, sizeof(store),
                                 fields, LOG_MAX_COLUMNS, &total);
    int col = -1;
    for (int i = 0; i < n; ++i) {
        if (strcmp(trim_cell(fields[i]), LOG_INFO_HEAD) == 0) {
            col = i;
        }
    }
    if (col < 0) {
        return false;
    }
    /* The first row with a cell in it: a blank line is no data row. */
    for (;;) {
        const int got = log_reader_row(&reader, delimiter, store,
                                       sizeof(store), fields,
                                       LOG_MAX_COLUMNS, &total);
        if (got < 0) {
            return false;
        }
        bool blank = true;
        for (int i = 0; i < got && blank; ++i) {
            blank = trim_cell(fields[i])[0] == '\0';
        }
        if (!blank) {
            return col < got && log_info_parse(fields[col], out);
        }
    }
}

void log_info_date(char *out, size_t n, int64_t unix_s)
{
    if (out == NULL || n == 0u) {
        return;
    }
    out[0] = '\0';
    const time_t t = (time_t)unix_s;
    struct tm tm;
    if (!wall_valid(unix_s) || (int64_t)t != unix_s
        || localtime_r(&t, &tm) == NULL) {
        return;
    }
    char text[32];
    const int k = snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d",
                           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                           tm.tm_hour, tm.tm_min);
    if (k > 0 && (size_t)k < n) {
        memcpy(out, text, (size_t)k + 1u);
    }
}

void log_info_set_clock(log_info_clock_fn fn)
{
    s_clock = fn;
}

int64_t log_info_now(void)
{
    return (s_clock != NULL) ? s_clock() : 0;
}
