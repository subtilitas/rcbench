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

_Static_assert(LOG_TEXT_MAX <= UINT16_MAX,
               "a row's start fits a mark of 16 bits");

/** @brief Marks a text of LOG_TEXT_MAX rows takes: rows 0, MARK, 2 MARK,
 *         ... */
#define MARKS ((LOG_TEXT_MAX / LOG_TEXT_MARK) + 1u)


/** @brief Takes a text's buffer and marks (log_text_set_allocator()). */
static void *(*s_alloc)(size_t) = malloc;
/** @brief Gives them back. */
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
    if (t->marks != NULL) {
        s_free(t->marks);
    }
    memset(t, 0, sizeof(*t));
}

/**
 * @brief The row that starts at @p off: where it ends and where the next
 *        one starts.  Counts the bytes it reads into @c walked.
 * @param t     the text, @p off inside it
 * @param off   the row's first byte
 * @param end   takes the byte after the row's last
 * @return the next row's first byte; @c len after the last row
 */
static size_t row_at(log_text_t *t, size_t off, size_t *end)
{
    const char *s = t->buf + off;
    const char *space = NULL;   /* the last space inside the row */
    size_t next;
    for (int cells = 0;; ++cells) {
        if (*s == '\0') {
            *end = (size_t)(s - t->buf);
            next = t->len;
            break;
        }
        if (*s == '\n') {
            *end = (size_t)(s - t->buf);
            next = *end + 1u;
            break;
        }
        if (cells == t->cols) {
            /* Full: at a space here, or back to the last one, or cut
             * here.  The space is the break and belongs to neither row. */
            if (*s == ' ') {
                *end = (size_t)(s - t->buf);
                next = *end + 1u;
            } else if (space != NULL) {
                *end = (size_t)(space - t->buf);
                next = *end + 1u;
            } else {
                *end = (size_t)(s - t->buf);
                next = *end;
            }
            break;
        }
        if (*s == ' ' && cells > 0) {
            space = s;
        }
        (void)gfx_utf8_next(&s);
    }
    t->walked += (size_t)(s - t->buf) - off + 1u;
    return next;
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

/**
 * @brief Read @p src into @p buf, LOG_TEXT_MAX bytes at most, with the line
 *        ends made LF and a tab or a NUL made a space.
 * @param src  the file, read from where it stands
 * @param buf  takes the text; LOG_TEXT_MAX bytes and a terminator
 * @param cut  takes whether the file holds more
 * @return the bytes in @p buf
 */
static size_t read_text(log_source_t *src, char *buf, bool *cut)
{
    size_t len = 0u;
    bool cr = false;            /* the last byte read was a CR */
    char chunk[256];
    *cut = false;
    for (;;) {
        const size_t got = src->read(src->ctx, chunk, sizeof(chunk));
        if (got == 0u) {
            return len;
        }
        for (size_t i = 0u; i < got; ++i) {
            char ch = chunk[i];
            if (ch == '\n' && cr) {
                cr = false;
                continue;       /* CR LF is one end, written at the CR */
            }
            cr = (ch == '\r');
            if (cr) {
                ch = '\n';
            } else if (ch == '\t' || ch == '\0') {
                ch = ' ';       /* a NUL too: the buffer is a string */
            }
            if (len == LOG_TEXT_MAX) {
                *cut = true;
                return len;
            }
            buf[len++] = ch;
        }
    }
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
    uint16_t *marks = (buf != NULL) ? s_alloc(MARKS * sizeof(uint16_t))
                                    : NULL;
    if (marks == NULL) {
        if (buf != NULL) {
            s_free(buf);
        }
        return LOG_ERR_MEMORY;
    }
    bool cut = false;
    size_t len = read_text(src, buf, &cut);
    if (src->error != NULL && src->error(src->ctx)) {
        s_free(buf);
        s_free(marks);
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
    t->marks = marks;
    int rows = 0;
    for (size_t off = 0u; off < len; ++rows) {
        if (rows % LOG_TEXT_MARK == 0) {
            t->marks[rows / LOG_TEXT_MARK] = (uint16_t)off;
        }
        size_t end;
        off = row_at(t, off, &end);
    }
    t->rows = rows;
    t->at_row = 0;
    t->at_off = 0u;
    t->at_held = false;
    t->walked = 0u;
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
    /* From the last row found while it is ahead of @p i by less than a
     * mark's span, and from the mark at or before @p i otherwise.
     * The row found last is held with its end, so the next row on costs
     * one row's read. */
    if (i < t->at_row || i - t->at_row >= LOG_TEXT_MARK || !t->at_held) {
        t->at_row = i - (i % LOG_TEXT_MARK);
        t->at_off = t->marks[i / LOG_TEXT_MARK];
        t->at_next = row_at(t, t->at_off, &t->at_end);
        t->at_held = true;
    }
    while (t->at_row < i) {
        t->at_off = t->at_next;
        ++t->at_row;
        t->at_next = row_at(t, t->at_off, &t->at_end);
    }
    size_t k = t->at_end - t->at_off;
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
