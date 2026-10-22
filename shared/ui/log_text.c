/**
 * @file log_text.c
 * @brief A text file as LOGS shows it.  See log_text.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_text.h"

#include <stdlib.h>
#include <string.h>

#include "gfx.h"

static void *(*s_alloc)(size_t) = malloc;
static void (*s_free)(void *) = free;

void log_text_set_allocator(void *(*alloc_fn)(size_t),
                            void (*free_fn)(void *))
{
    s_alloc = (alloc_fn != NULL) ? alloc_fn : malloc;
    s_free = (free_fn != NULL) ? free_fn : free;
}

void log_text_free(log_text_t *t)
{
    if (t == NULL) {
        return;
    }
    if (t->buf != NULL) {
        s_free(t->buf);
    }
    memset(t, 0, sizeof(*t));
}

/**
 * @brief The row that starts at @p off: where it ends and where the next
 *        one starts.
 * @param t     the text, @p off inside it
 * @param off   the row's first byte
 * @param end   takes the byte after the row's last
 * @return the next row's first byte; @c len after the last row
 */
static size_t row_at(const log_text_t *t, size_t off, size_t *end)
{
    const char *s = t->buf + off;
    const char *space = NULL;   /* the last space inside the row */
    for (int cells = 0;; ++cells) {
        if (*s == '\0') {
            *end = (size_t)(s - t->buf);
            return t->len;
        }
        if (*s == '\n') {
            *end = (size_t)(s - t->buf);
            return *end + 1u;
        }
        if (cells == t->cols) {
            /* Full: at a space here, or back to the last one, or cut
             * here.  The space is the break and belongs to neither row. */
            if (*s == ' ') {
                *end = (size_t)(s - t->buf);
                return *end + 1u;
            }
            if (space != NULL) {
                *end = (size_t)(space - t->buf);
                return *end + 1u;
            }
            *end = (size_t)(s - t->buf);
            return *end;
        }
        if (*s == ' ' && cells > 0) {
            space = s;
        }
        (void)gfx_utf8_next(&s);
    }
}

/**
 * @brief The length of @p buf without a last character cut short.
 * @param buf  UTF-8 text
 * @param len  its bytes
 * @return @p len, or less by the bytes of a last character that misses
 *         some of its continuation bytes
 */
static size_t whole(const char *buf, size_t len)
{
    size_t lead = len;
    int back = 0;
    while (lead > 0u && back < 4
           && ((unsigned char)buf[lead - 1u] & 0xC0u) == 0x80u) {
        --lead;
        ++back;
    }
    if (lead == 0u) {
        return len;
    }
    const unsigned char b = (unsigned char)buf[lead - 1u];
    const size_t want = (b >= 0xF0u) ? 4u
                      : (b >= 0xE0u) ? 3u
                      : (b >= 0xC0u) ? 2u
                                     : 1u;
    return (len - (lead - 1u) < want) ? lead - 1u : len;
}

log_err_t log_text_load(log_text_t *t, log_source_t *src, int cols)
{
    if (t == NULL) {
        return LOG_ERR_ARG;
    }
    log_text_free(t);
    if (src == NULL || src->read == NULL || cols < 1) {
        return LOG_ERR_ARG;
    }
    if (src->rewind != NULL && !src->rewind(src->ctx)) {
        return LOG_ERR_READ;
    }
    char *buf = s_alloc(LOG_TEXT_MAX + 1u);
    if (buf == NULL) {
        return LOG_ERR_MEMORY;
    }
    /* Read to the end or to the cap; one byte past the cap says the file
     * goes on. */
    size_t len = 0u;
    bool cut = false;
    char chunk[256];
    for (;;) {
        const size_t got = src->read(src->ctx, chunk, sizeof(chunk));
        if (got == 0u) {
            break;
        }
        for (size_t i = 0u; i < got && !cut; ++i) {
            char ch = chunk[i];
            if (ch == '\r') {
                continue;
            }
            if (ch == '\t' || ch == '\0') {
                ch = ' ';           /* a NUL too: the buffer is a string */
            }
            if (len == LOG_TEXT_MAX) {
                cut = true;
                break;
            }
            buf[len++] = ch;
        }
        if (cut) {
            break;
        }
    }
    if (src->error != NULL && src->error(src->ctx)) {
        s_free(buf);
        return LOG_ERR_READ;
    }
    /* A cut inside a character would show as a '?': a character the cap
     * left short goes. */
    if (cut) {
        len = whole(buf, len);
    }
    buf[len] = '\0';
    t->buf = buf;
    t->len = len;
    t->cut = cut;
    t->cols = cols;
    int rows = 0;
    for (size_t off = 0u; off < len; ++rows) {
        size_t end;
        off = row_at(t, off, &end);
    }
    t->rows = rows;
    t->at_row = 0;
    t->at_off = 0u;
    return LOG_OK;
}

bool log_text_row(log_text_t *t, int i, char *out, size_t n)
{
    if (out != NULL && n > 0u) {
        out[0] = '\0';
    }
    if (t == NULL || t->buf == NULL || i < 0 || i >= t->rows || out == NULL
        || n == 0u) {
        return false;
    }
    if (i < t->at_row) {
        t->at_row = 0;
        t->at_off = 0u;
    }
    size_t end = 0u;
    size_t next = row_at(t, t->at_off, &end);
    while (t->at_row < i) {
        t->at_off = next;
        ++t->at_row;
        next = row_at(t, t->at_off, &end);
    }
    size_t k = end - t->at_off;
    if (k >= n) {
        /* Back to a character's first byte, so the cut is between two. */
        k = n - 1u;
        while (k > 0u
               && ((unsigned char)t->buf[t->at_off + k] & 0xC0u) == 0x80u) {
            --k;
        }
    }
    memcpy(out, t->buf + t->at_off, k);
    out[k] = '\0';
    return true;
}
