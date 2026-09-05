/*
 * Reading a CSV log without knowing, in advance, what wrote it.
 *
 * A bench log can come from a transmitter, a charger, a datalogger or a
 * spreadsheet, and they disagree about nearly everything: comma, semicolon or
 * tab between fields, comma or dot for the decimal point, CRLF or LF between
 * rows, a byte-order mark or not, quoted fields with doubled quotes inside
 * them.  So nothing here is assumed: the delimiter and the decimal convention
 * are found by reading the file (see log_numbers.c for the evidence they are
 * found from), and the row splitter handles the quoting and line-ending cases
 * rather than trusting the file to be simple.
 *
 * The source is behind read/rewind function pointers so the same reader runs
 * against a file on the card and against a string in a test, and the
 * allocator is injectable for the same reason: the parser can be held to an
 * exact allocation count without a heap under it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_csv.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log_fields.h"

/* ----------------------------------------------------------- allocator --- */

static void *(*s_alloc)(size_t) = malloc;
static void (*s_free)(void *) = free;

void log_set_allocator(void *(*alloc_fn)(size_t), void (*free_fn)(void *))
{
    s_alloc = (alloc_fn != NULL) ? alloc_fn : malloc;
    s_free = (free_fn != NULL) ? free_fn : free;
}

const char *log_err_str(log_err_t err)
{
    switch (err) {
    case LOG_OK:                   return "ok";
    case LOG_ERR_READ:             return "could not read the file";
    case LOG_ERR_NO_HEADER:        return "no header line";
    case LOG_ERR_NO_ROWS:          return "no data rows below the header";
    case LOG_ERR_NO_NUMERIC:       return "no numeric columns -- check the delimiter";
    case LOG_ERR_TOO_MANY_COLUMNS: return "too many columns";
    case LOG_ERR_MEMORY:           return "out of memory";
    case LOG_ERR_ARG:              return "bad argument";
    default:                       return "unknown error";
    }
}

/* -------------------------------------------------------------- source --- */

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

static bool mem_rewind(void *ctx)
{
    ((log_mem_ctx_t *)ctx)->pos = 0;
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
}

static size_t file_read(void *ctx, char *buf, size_t max)
{
    return fread(buf, 1, max, (FILE *)ctx);
}

static bool file_error(void *ctx)
{
    return ferror((FILE *)ctx) != 0;
}

static bool file_rewind(void *ctx)
{
    clearerr((FILE *)ctx);
    return fseek((FILE *)ctx, 0, SEEK_SET) == 0;
}

void log_source_stdio(log_source_t *src, void *file)
{
    src->read = file_read;
    src->error = file_error;
    src->rewind = file_rewind;
    src->ctx = file;
}

/* ---------------------------------------------------------------- rows --- */

void log_reader_init(log_reader_t *r, log_source_t *src)
{
    memset(r, 0, sizeof(*r));
    r->src = src;
}

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

static int reader_peek(log_reader_t *r)
{
    if (reader_fill(r) == 0) {
        return -1;
    }
    return (unsigned char)r->buf[r->pos];
}

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
    bool saw_anything = false;
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
        saw_anything = true;

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
        /* End of input.  A trailing partial row still counts, but a file that
         * ended cleanly on a newline must not produce a phantom empty one. */
        if (!saw_anything || (sp == field_start && tf == 0)) {
            return -1;
        }
        if (!full && sp + 1u <= store_size) {
            store[sp] = '\0';
            if (nf < max_fields) {
                fields[nf++] = store + field_start;
            }
        } else if (nf < max_fields) {
            fields[nf++] = s_empty;
        }
        tf++;
    }

    if (total_fields != NULL) {
        *total_fields = tf;
    }
    r->overflow = full;
    return nf;
}

/* --------------------------------------------------------------- utils --- */

static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    size_t n = strlen(s);
    while (n > 0u && isspace((unsigned char)s[n - 1u])) {
        s[--n] = '\0';
    }
    return s;
}

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t n = strlen(src);
    if (n >= dst_size) {
        n = dst_size - 1u;
        while (n > 0u && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            --n;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool ci_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool ci_contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = hay; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < n && p[i] != '\0' &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == n) {
            return true;
        }
    }
    return false;
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

/* ------------------------------------------------------------- analysis -- */

void log_csv_opts_default(log_csv_opts_t *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->convention = LOG_CONV_AUTO;
    opts->ambiguous_as = LOG_AMBIG_THOUSANDS;
    opts->time_index = LOG_TIME_AUTO;
    opts->max_rows = LOG_DEFAULT_MAX_ROWS;
}

typedef struct {
    char store[LOG_ROW_MAX];
    char *fields[LOG_MAX_COLUMNS];
    int n;
    int total;
} row_buf_t;

/*
 * Scratch, file-static rather than on the stack: a row buffer and a unit
 * tally per column come to about 3 kB, which is too much for a UI task's
 * stack on this chip.  Analysis and build are called from one task, one file
 * at a time; they are not reentrant, and neither is the screen that drives
 * them.
 */
static row_buf_t s_row;
static log_unit_tally_t s_tally[LOG_MAX_COLUMNS];

/** Read the next row that is not a blank line.  false at end of input. */
static bool next_row(log_reader_t *r, char delimiter, row_buf_t *row)
{
    for (;;) {
        row->n = log_reader_row(r, delimiter, row->store, sizeof(row->store),
                                row->fields, LOG_MAX_COLUMNS, &row->total);
        if (row->n < 0) {
            return false;
        }
        if (row->total > 1) {
            return true;
        }
        if (row->n == 1 && trim(row->fields[0])[0] != '\0') {
            return true;
        }
    }
}

/** Does this row have anything in it at all? */
static bool row_has_content(row_buf_t *row)
{
    for (int i = 0; i < row->n; ++i) {
        if (trim(row->fields[i])[0] != '\0') {
            return true;
        }
    }
    return false;
}

static const char *const k_time_names[] = {
    "time", "timestamp", "zeit", "zeitstempel", "t", "elapsed", "clock",
    "sekunden", "seconds", "millis", "micros",
};

static void guess_time_unit(const log_column_t *col, char *out, size_t out_size)
{
    char u[LOG_UNIT_MAX];
    size_t n = 0;
    for (const char *p = col->unit; *p != '\0' && n + 1u < sizeof(u); ++p) {
        if (*p == '(' || *p == ')' || *p == '[' || *p == ']') {
            continue;
        }
        u[n++] = (char)tolower((unsigned char)*p);
    }
    u[n] = '\0';

    if (log_time_unit_scale(u) > 0.0) {
        copy_str(out, out_size, u);
        return;
    }
    if (ci_contains(col->raw_header, "millis")) {
        copy_str(out, out_size, "ms");
        return;
    }
    if (ci_contains(col->raw_header, "micros")) {
        copy_str(out, out_size, "us");
        return;
    }

    /* Fall back on magnitude: a bench run is minutes long, so a span in the
     * millions is microseconds and one over a few thousand is milliseconds.
     *
     * Limitation: a run longer than 83 minutes whose time column is plain
     * seconds under a bare header is read as milliseconds and divided by a
     * thousand.  Magnitude alone cannot separate it from an 8-second log in
     * milliseconds.  A unit in the header always takes precedence over this
     * guess. */
    if (col->parsed < 2) {
        copy_str(out, out_size, "s");
        return;
    }
    double span = col->last - col->first;
    copy_str(out, out_size, (span > 5e6) ? "us" : (span > 5e3) ? "ms" : "s");
}

/*
 * Sniffing the delimiter, which has to happen before anything can be split
 * into fields.  A source that cannot be rewound is a read error rather than
 * a guess: every pass below starts from the top.
 */
static log_err_t sniff_delimiter(log_source_t *src, const log_csv_opts_t *opts,
                                 char *out)
{
    char delimiter = opts->delimiter;
    if (delimiter == '\0') {
        static char sniff_buf[2048];
        if (!src->rewind(src->ctx)) {
            return LOG_ERR_READ;
        }
        size_t got = 0;
        while (got < sizeof(sniff_buf)) {
            size_t n = src->read(src->ctx, sniff_buf + got,
                                 sizeof(sniff_buf) - got);
            if (n == 0u) {
                break;
            }
            got += n;
        }
        const char *s = sniff_buf;
        size_t slen = got;
        if (slen >= 3u && (unsigned char)s[0] == 0xEFu &&
            (unsigned char)s[1] == 0xBBu && (unsigned char)s[2] == 0xBFu) {
            s += 3;
            slen -= 3u;
        }
        delimiter = log_csv_sniff(s, slen);
    }
    *out = delimiter;
    return LOG_OK;
}

/*
 * Pass one: how many columns there are, how many rows, and what the file
 * looks like as evidence for a decimal convention.
 *
 * Two passes rather than one because the convention decides how a cell
 * parses, and it is not known until every cell has voted.  The alternative is
 * holding the file, and the file is the one thing that does not fit.
 */
static log_err_t scan_geometry(log_source_t *src, char delimiter,
                               const log_csv_opts_t *opts, int max_rows,
                               log_analysis_t *out)
{
    log_reader_t reader;


    if (!src->rewind(src->ctx)) {
        return LOG_ERR_READ;
    }
    log_reader_init(&reader, src);

    if (!next_row(&reader, delimiter, &s_row)) {
        return LOG_ERR_NO_HEADER;
    }
    if (s_row.total > LOG_MAX_COLUMNS) {
        return LOG_ERR_TOO_MANY_COLUMNS;
    }
    out->n_columns = s_row.total;
    out->max_fields = s_row.total;
    for (int c = 0; c < s_row.n; ++c) {
        const char *h = trim(s_row.fields[c]);
        copy_str(out->columns[c].raw_header, LOG_NAME_MAX, h);
    }

    log_votes_t votes;
    log_votes_reset(&votes);

    while (out->row_count < max_rows && next_row(&reader, delimiter, &s_row)) {
        if (!row_has_content(&s_row)) {
            continue;
        }
        if (s_row.total != out->n_columns) {
            out->ragged_rows++;
        }
        if (reader.overflow) {
            /* The row did not fit the field store, so everything after the
             * overflow is emitted as an empty cell.  Those look like
             * legitimately blank cells downstream; counted here so the screen
             * can say so instead of plotting invented samples. */
            out->long_rows++;
        }
        if (s_row.total > out->max_fields) {
            out->max_fields = s_row.total;
        }
        for (int c = 0; c < s_row.n && c < out->n_columns; ++c) {
            const char *cell = trim(s_row.fields[c]);
            if (cell[0] != '\0') {
                log_votes_add(&votes, cell);
            }
        }
        out->row_count++;
    }
    if (reader.failed) {
        return LOG_ERR_READ;
    }
    if (out->row_count >= max_rows && next_row(&reader, delimiter, &s_row)) {
        out->truncated = true;
    }
    if (out->row_count == 0) {
        return LOG_ERR_NO_ROWS;
    }

    out->votes = votes;
    log_conv_t inferred = log_votes_result(&votes, opts->ambiguous_as,
                                           &out->convention_confident,
                                           &out->convention_conflict);
    out->convention_forced = (opts->convention != LOG_CONV_AUTO);
    out->convention = out->convention_forced ? opts->convention : inferred;
    return LOG_OK;
}

/*
 * Pass two: what is in each column, now that the convention is settled.
 *
 * Bounded by the row count pass one arrived at, so a file that grew between
 * the two passes is read to the length that was measured rather than to a
 * different one.
 */
static log_err_t scan_columns(log_source_t *src, char delimiter,
                              log_analysis_t *out)
{
    log_reader_t reader;

    for (int c = 0; c < out->n_columns; ++c) {
        out->columns[c].min = 0.0;
        out->columns[c].max = 0.0;
        out->columns[c].monotonic = true;
    }

    if (!src->rewind(src->ctx)) {
        return LOG_ERR_READ;
    }
    log_reader_init(&reader, src);
    if (!next_row(&reader, delimiter, &s_row)) {
        return LOG_ERR_NO_HEADER;
    }

    for (int c = 0; c < out->n_columns; ++c) {
        log_unit_tally_reset(&s_tally[c]);
    }

    int seen = 0;
    while (seen < out->row_count && next_row(&reader, delimiter, &s_row)) {
        if (!row_has_content(&s_row)) {
            continue;
        }
        for (int c = 0; c < out->n_columns; ++c) {
            log_column_t *col = &out->columns[c];
            const char *cell = (c < s_row.n) ? trim(s_row.fields[c]) : "";
            if (seen < LOG_SAMPLES) {
                copy_str(col->sample[seen], LOG_SAMPLE_MAX, cell);
            }
            if (cell[0] == '\0') {
                continue;
            }
            col->non_empty++;

            log_value_t parts;
            if (log_split_value(cell, &parts)) {
                log_unit_tally_add(&s_tally[c], parts.unit);
            }

            double v;
            if (log_parse_with(cell, out->convention, &v, NULL, 0)) {
                if (col->parsed == 0) {
                    col->first = v;
                    col->min = v;
                    col->max = v;
                } else {
                    if (v < col->min) {
                        col->min = v;
                    }
                    if (v > col->max) {
                        col->max = v;
                    }
                    if (v < col->last) {
                        col->monotonic = false;
                    }
                }
                col->last = v;
                col->parsed++;
            }
        }
        ++seen;
    }

    for (int c = 0; c < out->n_columns; ++c) {
        log_column_t *col = &out->columns[c];

        char name[LOG_NAME_MAX];
        char header_unit[LOG_UNIT_MAX];
        log_unit_from_header(col->raw_header, name, sizeof(name), header_unit,
                             sizeof(header_unit));
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "column %d", c + 1);
        }
        copy_str(col->name, LOG_NAME_MAX, name);

        char value_unit[LOG_UNIT_MAX];
        bool mixed = false;
        log_unit_dominant(&s_tally[c], value_unit, sizeof(value_unit), &mixed);
        col->mixed_units = mixed;
        col->unit_from_values = (value_unit[0] != '\0');
        copy_str(col->unit, LOG_UNIT_MAX,
                 col->unit_from_values ? value_unit : header_unit);

        col->numeric = (col->non_empty > 0) &&
                       (col->parsed * 5 >= col->non_empty * 4);
        col->monotonic = col->monotonic && (col->parsed > 1);
    }
    return LOG_OK;
}

/*
 * Which column is the time axis.
 *
 * Named first, then headed like one, then a first column that only ever
 * increases -- in that order, because a column called "time" is a statement
 * and a column that happens to rise is a guess.
 */
static void pick_time_column(const log_csv_opts_t *opts, log_analysis_t *out)
{

    if (opts->time_index != LOG_TIME_AUTO) {
        out->time_index = (opts->time_index < out->n_columns) ? opts->time_index
                                                              : -1;
    } else {
        for (int c = 0; c < out->n_columns && out->time_index < 0; ++c) {
            if (!out->columns[c].numeric) {
                continue;
            }
            for (size_t k = 0;
                 k < sizeof(k_time_names) / sizeof(k_time_names[0]); ++k) {
                if (ci_equal(out->columns[c].name, k_time_names[k])) {
                    out->time_index = c;
                    break;
                }
            }
        }
        for (int c = 0; c < out->n_columns && out->time_index < 0; ++c) {
            const char *h = out->columns[c].raw_header;
            if (out->columns[c].numeric &&
                (ci_contains(h, "time") || ci_contains(h, "zeit") ||
                 ci_contains(h, "elapsed") || ci_contains(h, "timestamp"))) {
                out->time_index = c;
            }
        }
        /* A first column that only ever increases is very likely a time axis. */
        if (out->time_index < 0 && out->n_columns > 0 &&
            out->columns[0].numeric && out->columns[0].monotonic) {
            out->time_index = 0;
        }
    }

    if (opts->time_unit != NULL) {
        copy_str(out->time_unit, LOG_UNIT_MAX, opts->time_unit);
    } else if (out->time_index >= 0) {
        guess_time_unit(&out->columns[out->time_index], out->time_unit,
                        LOG_UNIT_MAX);
    } else {
        copy_str(out->time_unit, LOG_UNIT_MAX, "s");
    }
}

log_err_t log_csv_analyse(log_source_t *src, const log_csv_opts_t *opts,
                          log_analysis_t *out)
{
    log_csv_opts_t defaults;
    if (opts == NULL) {
        log_csv_opts_default(&defaults);
        opts = &defaults;
    }
    if (src == NULL || out == NULL) {
        return LOG_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->ambiguous_as = opts->ambiguous_as;
    out->time_index = -1;

    const int max_rows = (opts->max_rows > 0) ? opts->max_rows
                                              : LOG_DEFAULT_MAX_ROWS;

    char delimiter;
    log_err_t err = sniff_delimiter(src, opts, &delimiter);
    if (err != LOG_OK) {
        return err;
    }
    out->delimiter = delimiter;

    err = scan_geometry(src, delimiter, opts, max_rows, out);
    if (err != LOG_OK) {
        return err;
    }
    err = scan_columns(src, delimiter, out);
    if (err != LOG_OK) {
        return err;
    }
    pick_time_column(opts, out);
    return LOG_OK;
}

/* ---------------------------------------------------------------- build -- */

void log_data_free(log_data_t *data)
{
    if (data == NULL) {
        return;
    }
    if (data->time != NULL) {
        s_free(data->time);
    }
    for (int i = 0; i < LOG_MAX_SERIES; ++i) {
        if (data->value[i] != NULL) {
            s_free(data->value[i]);
        }
    }
    memset(data, 0, sizeof(*data));
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a;
    float y = *(const float *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/*
 * Which columns the plot gets: the ones asked for, or every numeric one that
 * is not the time axis.
 *
 * A column that is not numeric is dropped rather than refused, because a file
 * with a text column beside its numbers is an ordinary file and not a broken
 * one.
 */
static int pick_columns(const log_analysis_t *a, const int *columns,
                        int n_columns, int *pick)
{
    int n_pick = 0;

    if (columns != NULL) {
        for (int i = 0; i < n_columns && n_pick < LOG_MAX_SERIES; ++i) {
            int c = columns[i];
            if (c >= 0 && c < a->n_columns && c != a->time_index &&
                a->columns[c].numeric) {
                pick[n_pick++] = c;
            }
        }
    } else {
        for (int c = 0; c < a->n_columns && n_pick < LOG_MAX_SERIES; ++c) {
            if (c != a->time_index && a->columns[c].numeric) {
                pick[n_pick++] = c;
            }
        }
    }
    return n_pick;
}

/*
 * What each series is called, what it is in, and what it grouped with.
 *
 * The blackbox grouping when the names match, and otherwise by unit, which
 * keeps related channels together on a screen that has to draw them
 * somewhere.
 */
static void name_fields(const log_analysis_t *a, const int *pick, int n_pick,
                        const double *vmin, const double *vmax,
                        log_data_t *out)
{
    for (int k = 0; k < n_pick; ++k) {
        int c = pick[k];
        log_field_t *f = &out->field[k];
        copy_str(f->name, LOG_NAME_MAX, a->columns[c].name);
        copy_str(f->unit, LOG_UNIT_MAX, a->columns[c].unit);
        f->column = c;
        f->min = (float)vmin[k];
        f->max = (float)vmax[k];
        f->constant = (vmin[k] == vmax[k]);
        f->mixed_units = a->columns[c].mixed_units;

        log_field_meta_t meta = log_field_meta(f->name);
        if (strcmp(meta.group, "Other") != 0) {
            copy_str(f->group, LOG_NAME_MAX, meta.group);
        } else if (f->unit[0] != '\0') {
            char unit[LOG_UNIT_MAX];
            copy_str(unit, sizeof(unit), f->unit);
            snprintf(f->group, LOG_NAME_MAX, "Unit %s", unit);
        } else {
            copy_str(f->group, LOG_NAME_MAX, "Data");
        }
    }
}

/*
 * A time axis that runs backwards means the wrong column was picked, so the
 * samples are numbered instead and the axis loses its name -- a plot against
 * sample number is honest, and one against a column that is not time is not.
 */
static void undo_a_backwards_time_axis(const log_analysis_t *a,
                                       log_data_t *out, int count)
{
    if (a->time_index < 0) {
        return;
    }
    for (int k = 1; k < count; ++k) {
        if (out->time[k] < out->time[k - 1]) {
            for (int j = 0; j < count; ++j) {
                out->time[j] = (float)j;
            }
            out->time_name[0] = '\0';
            out->time_unit[0] = '\0';
            return;
        }
    }
}

/*
 * The time axis and the series, read together.
 *
 * One pass: a row's time and its values are the same row, and reading them
 * separately would need the file twice or the rows held.
 */
static log_err_t read_series(log_source_t *src, const log_analysis_t *a,
                             const int *pick, int n_pick, log_data_t *out,
                             int *count_out)
{
    /*
     * The row count pass one arrived at, which is what the arrays are sized
     * to: a file that grew between the passes is read to the length that was
     * measured, not to a different one.
     */
    int count = a->row_count;
    out->count = count;
    out->n_fields = n_pick;
    out->convention = a->convention;
    out->delimiter = a->delimiter;

    out->time = (float *)s_alloc(sizeof(float) * (size_t)count);
    if (out->time == NULL) {
        log_data_free(out);
        return LOG_ERR_MEMORY;
    }
    for (int i = 0; i < n_pick; ++i) {
        out->value[i] = (float *)s_alloc(sizeof(float) * (size_t)count);
        if (out->value[i] == NULL) {
            log_data_free(out);
            return LOG_ERR_MEMORY;
        }
    }

    double time_scale = 1.0;
    if (a->time_index >= 0) {
        double s = log_time_unit_scale(a->time_unit);
        time_scale = (s > 0.0) ? s : 1.0;
        copy_str(out->time_name, LOG_NAME_MAX, a->columns[a->time_index].name);
        copy_str(out->time_unit, LOG_UNIT_MAX, a->time_unit);
    }

    double vmin[LOG_MAX_SERIES];
    double vmax[LOG_MAX_SERIES];
    bool any[LOG_MAX_SERIES];
    for (int i = 0; i < n_pick; ++i) {
        vmin[i] = 0.0;
        vmax[i] = 0.0;
        any[i] = false;
    }

    log_reader_t reader;
    if (!src->rewind(src->ctx)) {
        log_data_free(out);
        return LOG_ERR_READ;
    }
    log_reader_init(&reader, src);
    if (!next_row(&reader, a->delimiter, &s_row)) {
        log_data_free(out);
        return LOG_ERR_NO_HEADER;
    }

    double t0 = 0.0;
    bool have_t0 = false;
    double t_last = 0.0;
    int i = 0;

    while (i < count && next_row(&reader, a->delimiter, &s_row)) {
        if (!row_has_content(&s_row)) {
            continue;
        }

        for (int k = 0; k < n_pick; ++k) {
            int c = pick[k];
            const char *cell = (c < s_row.n) ? trim(s_row.fields[c]) : "";
            double v;
            if (cell[0] != '\0' && log_parse_with(cell, a->convention, &v, NULL, 0)) {
                out->value[k][i] = (float)v;
                if (!any[k]) {
                    vmin[k] = v;
                    vmax[k] = v;
                    any[k] = true;
                } else {
                    if (v < vmin[k]) {
                        vmin[k] = v;
                    }
                    if (v > vmax[k]) {
                        vmax[k] = v;
                    }
                }
            } else {
                /* Hold the previous value across a gap so one unreadable cell
                 * does not punch a hole in the trace; count it so the screen
                 * can say how many there were. */
                out->value[k][i] = (i > 0) ? out->value[k][i - 1] : NAN;
                if (cell[0] != '\0') {
                    out->unparsed_cells++;
                }
            }
        }

        if (a->time_index >= 0) {
            const char *cell = (a->time_index < s_row.n) ? trim(s_row.fields[a->time_index])
                                                       : "";
            double v;
            if (cell[0] != '\0' && log_parse_with(cell, a->convention, &v, NULL, 0)) {
                v *= time_scale;
            } else {
                v = t_last;
            }
            if (!have_t0) {
                t0 = v;
                have_t0 = true;
            }
            t_last = v;
            out->time[i] = (float)(v - t0);
        } else {
            out->time[i] = (float)i;
        }

        ++i;
    }
    if (reader.failed) {
        /* A read that failed part way leaves a short array that looks like a
         * complete short log.  Refuse it rather than plot it. */
        log_data_free(out);
        return LOG_ERR_READ;
    }
    if (i == 0) {
        /* Nothing was built.  Returning LOG_OK here hands the screen empty
         * arrays it then indexes at [count - 1]. */
        log_data_free(out);
        return LOG_ERR_NO_ROWS;
    }
    out->count = i;
    count = i;

    name_fields(a, pick, n_pick, vmin, vmax, out);
    undo_a_backwards_time_axis(a, out, count);

    /* The rows actually read, which is what the arrays hold: a file with
     * blank rows in it ends shorter than pass one measured. */
    *count_out = count;
    return LOG_OK;
}

/*
 * What the samples add up to: how long, how fast, and the widest gap.
 *
 * The median interval rather than the mean, because one long gap in a log
 * that is otherwise steady should not move the rate the screen reports. When
 * there is no room to sort for a median, the mean is reported and the screen
 * says which it is showing.
 */
static void series_stats(log_data_t *out, int count)
{

    if (count > 1) {
        out->duration_s = (double)out->time[count - 1] - (double)out->time[0];
        float *dts = (float *)s_alloc(sizeof(float) * (size_t)(count - 1));
        double sum = 0.0;
        for (int k = 1; k < count; ++k) {
            double dt = (double)out->time[k] - (double)out->time[k - 1];
            if (dt > out->max_gap_s) {
                out->max_gap_s = dt;
            }
            sum += dt;
            if (dts != NULL) {
                dts[k - 1] = (float)dt;
            }
        }
        if (dts != NULL) {
            qsort(dts, (size_t)(count - 1), sizeof(float), cmp_float);
            out->median_dt_s = (double)dts[(count - 1) >> 1];
            s_free(dts);
        } else {
            /* Without room to sort, the mean is reported, and the screen says
             * which statistic it shows. */
            out->median_dt_s = sum / (double)(count - 1);
        }
        out->rate_hz = (out->median_dt_s > 0.0) ? 1.0 / out->median_dt_s : 0.0;
    }
}

log_err_t log_csv_build(log_source_t *src, const log_analysis_t *a,
                        const int *columns, int n_columns, log_data_t *out)
{
    if (src == NULL || a == NULL || out == NULL) {
        return LOG_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));

    int pick[LOG_MAX_SERIES];
    const int n_pick = pick_columns(a, columns, n_columns, pick);
    if (n_pick == 0) {
        return LOG_ERR_NO_NUMERIC;
    }

    int count = 0;
    const log_err_t err = read_series(src, a, pick, n_pick, out, &count);
    if (err != LOG_OK) {
        return err;
    }
    series_stats(out, count);
    return LOG_OK;
}
