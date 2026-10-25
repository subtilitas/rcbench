/**
 * @file
 * @brief The CSV reader's byte layer: the sources behind the read/rewind
 *        function pointers, the row splitter that handles quoting and line
 *        endings, and the delimiter sniffer.
 *
 * A source is a string in memory (tests, the sniff sample) or a stdio file
 * (the card); the row splitter reads either through log_source_t and never
 * holds more than one buffer of it.  The splitter and the sniffer take no
 * allocation; the allocator and the analysis built on them are in log_csv.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "log_csv.h"
#include "log_csv_int.h"
/* -------------------------------------------------------------- source --- */


/* -------------------------------------------------------------- source */


/**
 * @brief Read from a memory source.
 *
 * @param ctx  The log_mem_ctx_t.
 * @param buf  Receives the bytes.
 * @param max  Capacity of @p buf, bytes.
 * @return Bytes copied; 0 at the end of the text.
 */
static size_t mem_read(void *ctx, char *buf, size_t max)
{
    log_mem_ctx_t *m = (log_mem_ctx_t *)ctx;
    size_t n = m->len - m->pos;
    if (n > max) {
        n = max;
    }
    memcpy(buf, m->text + m->pos, n);
    m->pos += n;
    return n;
}

/**
 * @brief Rewind a memory source to its start.
 *
 * @param ctx  The log_mem_ctx_t.
 * @return true, always.
 */
static bool mem_rewind(void *ctx)
{
    ((log_mem_ctx_t *)ctx)->pos = 0;
    return true;
}

/**
 * @brief Length of a memory source.
 *
 * @param ctx    The log_mem_ctx_t.
 * @param bytes  Receives the length, bytes.
 * @return true, always.
 */
static bool mem_size(void *ctx, size_t *bytes)
{
    *bytes = ((log_mem_ctx_t *)ctx)->len;
    return true;
}

/**
 * @brief Move a memory source's read position.
 *
 * @param ctx     The log_mem_ctx_t.
 * @param offset  New position, bytes from the start.
 * @return false when @p offset is past the end.
 */
static bool mem_seek(void *ctx, size_t offset)
{
    log_mem_ctx_t *m = (log_mem_ctx_t *)ctx;
    if (offset > m->len) {
        return false;
    }
    m->pos = offset;
    return true;
}

void log_source_memory(log_source_t *src, log_mem_ctx_t *ctx, const char *text,
                       size_t len)
{
    ctx->text = text;
    ctx->len = len;
    ctx->pos = 0;
    src->read = mem_read;
    src->error = NULL;      /* memory does not fail */
    src->rewind = mem_rewind;
    src->ctx = ctx;
    src->size = mem_size;
    src->seek = mem_seek;
}

/**
 * @brief Read from a stdio file.
 *
 * @param ctx  The FILE *.
 * @param buf  Receives the bytes.
 * @param max  Capacity of @p buf, bytes.
 * @return Bytes read; 0 at the end of the file or on an error.
 */
static size_t file_read(void *ctx, char *buf, size_t max)
{
    return fread(buf, 1, max, (FILE *)ctx);
}

/**
 * @brief Whether the last read of a stdio file failed.
 *
 * @param ctx  The FILE *.
 * @return true when the stream has its error flag set.
 */
static bool file_error(void *ctx)
{
    return ferror((FILE *)ctx) != 0;
}

/**
 * @brief Rewind a stdio file to its start.
 *
 * @param ctx  The FILE *.
 * @return true when the seek succeeded.
 */
static bool file_rewind(void *ctx)
{
    clearerr((FILE *)ctx);
    return fseek((FILE *)ctx, 0, SEEK_SET) == 0;
}

/**
 * @brief fseek and ftell take a long: 2GiB less 1 byte on the panel.
 *
 * A file longer than that answers false, and the analysis reads it on instead.
 *
 * @param ctx    The FILE *.
 * @param bytes  Receives the length, bytes.
 * @return true when the length was read.
 */
static bool file_size(void *ctx, size_t *bytes)
{
    FILE *f = (FILE *)ctx;
    clearerr(f);
    if (fseek(f, 0, SEEK_END) != 0) {
        return false;
    }
    long end = ftell(f);
    if (end < 0) {
        return false;
    }
    *bytes = (size_t)end;
    return true;
}

/**
 * @brief Move a stdio file's read position.
 *
 * @param ctx     The FILE *.
 * @param offset  New position, bytes from the start.
 * @return true when the seek succeeded; false when @p offset exceeds LONG_MAX.
 */
static bool file_seek(void *ctx, size_t offset)
{
    if (offset > (size_t)LONG_MAX) {
        return false;
    }
    clearerr((FILE *)ctx);
    return fseek((FILE *)ctx, (long)offset, SEEK_SET) == 0;
}

void log_source_stdio(log_source_t *src, void *file)
{
    src->read = file_read;
    src->error = file_error;
    src->rewind = file_rewind;
    src->ctx = file;
    src->size = file_size;
    src->seek = file_seek;
}

/* ---------------------------------------------------------------- rows --- */

void log_reader_init(log_reader_t *r, log_source_t *src)
{
    memset(r, 0, sizeof(*r));
    r->src = src;
}

/**
 * @brief Refill the read-ahead buffer when it is empty.
 *
 * Sets failed when the source reports an error at its end.
 *
 * @param r  The row splitter.
 * @return 1 when bytes are buffered, 0 at the end of input.
 */
static int reader_fill(log_reader_t *r)
{
    if (r->pos < r->len) {
        return 1;
    }
    if (r->eof) {
        return 0;
    }
    r->len = r->src->read(r->src->ctx, r->buf, sizeof(r->buf));
    r->pos = 0;
    if (r->len == 0u) {
        /* Distinguish "nothing left" from "the card stopped answering".  Both
         * look like a short read; only one of them means the data is real. */
        if (r->src->error != NULL && r->src->error(r->src->ctx)) {
            r->failed = true;
        }
        r->eof = true;
        return 0;
    }
    return 1;
}

/**
 * @brief Read one byte, skipping a leading byte order mark.
 *
 * @param r  The row splitter.
 * @return The byte, 0 to 255, or -1 at the end of input.
 */
static int reader_getc(log_reader_t *r)
{
    if (reader_fill(r) == 0) {
        return -1;
    }
    if (!r->started) {
        /* A byte order mark belongs to the file, not to the first field: strip
         * it here so nothing downstream ever sees it glued to a column name. */
        r->started = true;
        if (r->len - r->pos >= 3u && (unsigned char)r->buf[r->pos] == 0xEFu &&
            (unsigned char)r->buf[r->pos + 1u] == 0xBBu &&
            (unsigned char)r->buf[r->pos + 2u] == 0xBFu) {
            r->pos += 3u;
        }
        if (reader_fill(r) == 0) {
            return -1;
        }
    }
    return (unsigned char)r->buf[r->pos++];
}

/**
 * @brief Look at the next byte without taking it.
 *
 * @param r  The row splitter.
 * @return The byte, 0 to 255, or -1 at the end of input.
 */
static int reader_peek(log_reader_t *r)
{
    if (reader_fill(r) == 0) {
        return -1;
    }
    return (unsigned char)r->buf[r->pos];
}

/** @brief Empty field text, for fields that did not fit the store. */
static char s_empty[1] = "";

int log_reader_row(log_reader_t *r, char delimiter, char *store,
                   size_t store_size, char **fields, int max_fields,
                   int *total_fields)
{
    size_t sp = 0;
    size_t field_start = 0;
    int nf = 0;
    int tf = 0;
    bool in_quotes = false;
    bool saw_content = false; /* a byte that is not a blank */
    bool full = false;
    bool done = false;

    if (total_fields != NULL) {
        *total_fields = 0;
    }
    r->overflow = false;
    if (store == NULL || store_size < 2u) {
        return -1;
    }

    while (!done) {
        int c = reader_getc(r);
        if (c < 0) {
            break;
        }
        if (!is_blank(c)) {
            saw_content = true;
        }

        if (in_quotes) {
            if (c == '"') {
                if (reader_peek(r) == '"') {
                    (void)reader_getc(r);
                } else {
                    in_quotes = false;
                    continue;
                }
            }
            if (sp + 1u < store_size) {
                store[sp++] = (char)c;
            } else {
                full = true;
            }
            continue;
        }

        if (c == '"') {
            in_quotes = true;
        } else if (c == (int)(unsigned char)delimiter || c == '\n') {
            if (!full && sp + 1u <= store_size) {
                store[sp] = '\0';
                if (nf < max_fields) {
                    fields[nf++] = store + field_start;
                }
                sp++;
                field_start = sp;
            } else if (nf < max_fields) {
                fields[nf++] = s_empty;
            }
            tf++;
            if (c == '\n') {
                done = true;
            }
        } else if (c == '\r') {
            /* handled by the '\n' branch */
        } else if (sp + 1u < store_size) {
            store[sp++] = (char)c;
        } else {
            full = true;
        }
    }

    if (!done) {
        /*
         * End of input inside a row.  A row is complete at its line feed and
         * nothing else proves it: a write cut off after "95" of "9554" leaves
         * a row with every field present and a wrong number in the last one,
         * and its field count matches the header.  So a row without its line
         * feed is not a row.  It is dropped and the reader says so, unless
         * it holds nothing but blanks: a file that ends on its newline, or on
         * a stray carriage return or space after it, has no row left to lose.
         */
        if (saw_content) {
            r->cut = true;
        }
        return -1;
    }

    if (total_fields != NULL) {
        *total_fields = tf;
    }
    r->overflow = full;
    return nf;
}

/* ---------------------------------------------------------------- sniff -- */

const char log_csv_delimiters[4] = { ';', ',', '\t', '|' };

const char *log_csv_delimiter_label(char delimiter)
{
    switch (delimiter) {
    case ';':  return "SEMICOLON";
    case ',':  return "COMMA";
    case '\t': return "TAB";
    case '|':  return "PIPE";
    default:   return "?";
    }
}

/**
 * @brief Count the fields a delimiter makes in one line.
 *
 * Delimiters inside a quoted field are not counted.
 *
 * @param s          The line.
 * @param len        Length of the line, bytes.
 * @param delimiter  The candidate delimiter.
 * @return The number of fields, at least 1.
 */
static int count_fields(const char *s, size_t len, char delimiter)
{
    int n = 1;
    bool q = false;
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (q) {
            if (c == '"') {
                if (i + 1u < len && s[i + 1u] == '"') {
                    ++i;
                } else {
                    q = false;
                }
            }
        } else if (c == '"') {
            q = true;
        } else if (c == delimiter) {
            ++n;
        }
    }
    return n;
}

/** @brief Most non-blank lines the delimiter sniffer looks at. */
#define SNIFF_LINES 25

char log_csv_sniff(const char *sample, size_t len)
{
    const char *lines[SNIFF_LINES];
    size_t lens[SNIFF_LINES];
    int n_lines = 0;

    size_t i = 0;
    while (i < len && n_lines < SNIFF_LINES) {
        size_t start = i;
        while (i < len && sample[i] != '\n') {
            ++i;
        }
        size_t end = i;
        if (end > start && sample[end - 1u] == '\r') {
            --end;
        }
        /* Skip blank lines the way a whole-file split would. */
        bool blank = true;
        for (size_t k = start; k < end; ++k) {
            if (!isspace((unsigned char)sample[k])) {
                blank = false;
                break;
            }
        }
        if (!blank) {
            lines[n_lines] = sample + start;
            lens[n_lines] = end - start;
            ++n_lines;
        }
        if (i < len) {
            ++i;
        }
    }

    if (n_lines == 0) {
        return ',';
    }

    char best = ',';
    double best_score = -1.0;
    for (size_t d = 0; d < sizeof(log_csv_delimiters); ++d) {
        char cand = log_csv_delimiters[d];
        int first = count_fields(lines[0], lens[0], cand);
        if (first < 2) {
            continue;
        }
        int same = 0;
        for (int k = 0; k < n_lines; ++k) {
            if (count_fields(lines[k], lens[k], cand) == first) {
                ++same;
            }
        }
        /* Consistency matters far more than width: a stray comma inside text
         * should not beat a clean semicolon layout. */
        double consistent = (double)same / (double)n_lines;
        double score = consistent * 100.0 + (double)((first < 40) ? first : 40);
        if (score > best_score) {
            best_score = score;
            best = cand;
        }
    }
    return best;
}
