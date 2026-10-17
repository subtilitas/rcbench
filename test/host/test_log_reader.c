/*
 * Host unit tests for the CSV reader's byte layer (shared/logfile/log_reader.c):
 * the row splitter with its quoting, line ends and store limits, the
 * delimiter sniffer, a source that fails part way, and the tail read that
 * finds a cut last row past the row cap.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>
#include <string.h>

#include "log_csv.h"
#include "log_fields.h"
#include "log_numbers.h"

#include "logfile_rig.h"

/* ----------------------------------------------------------------- rows --- */

static void split_check(const char *text, char delim, int row_index,
                        const char *const *want, int want_n)
{
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_reader_t r;
    log_reader_init(&r, &src);

    char store[LOG_ROW_MAX];
    char *fields[LOG_MAX_COLUMNS];
    int total = 0;
    for (int i = 0; i <= row_index; ++i) {
        int n = log_reader_row(&r, delim, store, sizeof(store), fields,
                               LOG_MAX_COLUMNS, &total);
        if (n < 0) {
            T_FAIL("row %d missing in \"%s\"", row_index, text);
            return;
        }
        if (i != row_index) {
            continue;
        }
        if (n != want_n) {
            T_FAIL("row %d has %d fields, want %d", row_index, n, want_n);
            return;
        }
        for (int k = 0; k < n; ++k) {
            if (strcmp(fields[k], want[k]) != 0) {
                T_FAIL("row %d field %d: got \"%s\", want \"%s\"", row_index, k,
                       fields[k], want[k]);
            }
        }
    }
}

TEST_CASE(rows_honour_quotes_and_doubled_quotes)
{
    static const char *const r0[] = { "a", "b,c", "d" };
    static const char *const r1[] = { "1", "2", "3" };
    split_check("a,\"b,c\",d\n1,2,3\n", ',', 0, r0, 3);
    split_check("a,\"b,c\",d\n1,2,3\n", ',', 1, r1, 3);

    static const char *const esc[] = { "a", "say \"hi\"", "c" };
    split_check("a,\"say \"\"hi\"\"\",c\n", ',', 0, esc, 3);

    /* CRLF (carriage return, line feed) line ends. */
    static const char *const crlf[] = { "x", "y" };
    split_check("x,y\r\nz,w\r\n", ',', 0, crlf, 2);
    static const char *const tail[] = { "z", "w" };
    split_check("x,y\r\nz,w\r\n", ',', 1, tail, 2);
}

/* How many rows the reader returns from @p text, and whether it says the
 * input ended inside one. */
static int rows_in(const char *text, bool *cut)
{
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));
    log_reader_t r;
    log_reader_init(&r, &src);
    char store[LOG_ROW_MAX];
    char *fields[LOG_MAX_COLUMNS];
    int total = 0;
    int rows = 0;
    while (log_reader_row(&r, ',', store, sizeof(store), fields,
                          LOG_MAX_COLUMNS, &total) >= 0) {
        ++rows;
    }
    /* Asked again past the end, it stays at the end and keeps saying so. */
    CHECK_EQ(log_reader_row(&r, ',', store, sizeof(store), fields,
                            LOG_MAX_COLUMNS, &total), -1);
    *cut = r.cut;
    return rows;
}

/*
 * A row is complete at its line feed and at nothing else.  A last row
 * without one is left out and the reader says so, whatever it holds: all of
 * its fields, the start of a quoted field, a line cut between CR and LF.  A
 * tail of blanks after the last line feed is no row and nothing is cut.
 */
TEST_CASE(a_last_row_without_its_line_feed_is_not_a_row)
{
    bool cut = false;
    CHECK_EQ(rows_in("a,b\n1,2\n", &cut), 2);
    CHECK(!cut);
    CHECK_EQ(rows_in("a,b\n1,2", &cut), 1);
    CHECK(cut);
    CHECK_EQ(rows_in("a,b\n1,", &cut), 1);
    CHECK(cut);
    CHECK_EQ(rows_in("a,b\n1,2\r", &cut), 1);
    CHECK(cut);
    CHECK_EQ(rows_in("a,b\n\"1\n2", &cut), 1);
    CHECK(cut);
    CHECK_EQ(rows_in("a,b", &cut), 0);
    CHECK(cut);
    CHECK_EQ(rows_in("", &cut), 0);
    CHECK(!cut);
    CHECK_EQ(rows_in("a,b\n1,2\n \t\r", &cut), 2);
    CHECK(!cut);
}

/* The row cut off at the end of the file is left out of the sniff sample as
 * it is left out of the rows: with it, the third line gives ';' two columns
 * and the file reads as semicolon-delimited. */
TEST_CASE(a_cut_last_row_does_not_vote_on_the_delimiter)
{
    static const char text[] =
        "time,note;x,value\n0,a;b,10\n1,c;d,20\n2,e;f";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, sizeof(text) - 1u);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.delimiter, ',');
    CHECK_EQ(a.n_columns, 3);
    CHECK_EQ(a.row_count, 2);
    CHECK(a.cut_row);

    /* The same file with the cut row padded to 2047, 2048 and 2049 bytes in
     * all: whether the 2048-byte sample holds the whole file, exactly fills
     * or ends inside the cut row, that row does not vote. */
    static char big[2100];
    for (size_t total = 2047u; total <= 2049u; ++total) {
        size_t n = sizeof(text) - 1u;
        memcpy(big, text, n);
        while (n < total) {
            big[n++] = 'f';
        }
        log_source_memory(&src, &ctx, big, total);
        CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
        CHECK_EQ(a.delimiter, ',');
        CHECK_EQ(a.n_columns, 3);
        CHECK_EQ(a.row_count, 2);
        CHECK(a.cut_row);
    }
}

/* A sample with no line feed at all is sniffed whole, as before: the single
 * unterminated line is the sample.  The reader then finds no complete row,
 * and the analysis is refused as a file without a header. */
TEST_CASE(a_sample_without_a_line_feed_is_sniffed_whole)
{
    static const char text[] = "a;b;c";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, sizeof(text) - 1u);
    CHECK_EQ(log_csv_sniff(text, sizeof(text) - 1u), ';');
    log_analysis_t a;
    CHECK(log_csv_analyse(&src, NULL, &a) != LOG_OK);
}

/* Blank is space, tab and CR.  VT and FF after the last line feed are
 * content: a row cut off. */
TEST_CASE(only_space_tab_and_cr_are_blank_after_the_last_line_feed)
{
    bool cut = false;
    CHECK_EQ(rows_in("a,b\n1,2\n \t\r", &cut), 2);
    CHECK(!cut);
    CHECK_EQ(rows_in("a,b\n1,2\n\v", &cut), 2);
    CHECK(cut);
    CHECK_EQ(rows_in("a,b\n1,2\n\f", &cut), 2);
    CHECK(cut);
}

TEST_CASE(a_quoted_field_may_straddle_a_read_boundary)
{
    /* The reader refills in 256-byte chunks.  A field that crosses a chunk
     * boundary, and a doubled quote whose halves land in different chunks,
     * must read the same as inside one chunk. */
    char text[900];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "a,b\n\"");
    for (int i = 0; i < 250; ++i) {
        text[n++] = 'x';
    }
    n += (size_t)snprintf(text + n, sizeof(text) - n, "\"\"end\",2\n");
    text[n] = '\0';

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);
    log_reader_t r;
    log_reader_init(&r, &src);

    char store[LOG_ROW_MAX];
    char *fields[LOG_MAX_COLUMNS];
    int total = 0;
    (void)log_reader_row(&r, ',', store, sizeof(store), fields, LOG_MAX_COLUMNS,
                         &total);
    int got = log_reader_row(&r, ',', store, sizeof(store), fields,
                             LOG_MAX_COLUMNS, &total);
    CHECK_EQ(got, 2);
    if (got == 2) {
        CHECK_EQ((long)strlen(fields[0]), 254);
        CHECK_STR_EQ(fields[0] + 250, "\"end");
        CHECK_STR_EQ(fields[1], "2");
    }
}

TEST_CASE(delimiter_is_sniffed_from_a_sample)
{
    if (!load_fixture("en.csv")) {
        return;
    }
    CHECK_EQ(log_csv_sniff(g_text, g_len), ',');
    if (!load_fixture("de.csv")) {
        return;
    }
    CHECK_EQ(log_csv_sniff(g_text, g_len), ';');
}

/* The cut row is at the end of the file.  Past the row cap the flag holds
 * at exactly the cap and with complete rows past it, and stays false when
 * the file ends on a line feed. */
static log_analysis_t capped(int complete_rows, const char *tail, int cap)
{
    static char text[4096];
    size_t n = (size_t)snprintf(text, sizeof(text), "t,v\n");
    for (int i = 0; i < complete_rows; ++i) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%d,%d\n", i, i);
    }
    n += (size_t)snprintf(text + n, sizeof(text) - n, "%s", tail);
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.max_rows = cap;
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    return a;
}

TEST_CASE(a_cut_row_is_found_past_the_row_cap)
{
    /* Exactly the cap, then the cut row: nothing is truncated. */
    log_analysis_t a = capped(10, "10,2", 10);
    CHECK_EQ(a.row_count, 10);
    CHECK(!a.truncated);
    CHECK(a.cut_row);

    /* The cap, N complete rows past it, then the cut row. */
    for (int extra = 1; extra <= 30; extra += 29) {
        a = capped(10 + extra, "99,2", 10);
        CHECK_EQ(a.row_count, 10);
        CHECK(a.truncated);
        CHECK(a.cut_row);
    }

    /* The same files ending on a line feed: no cut row. */
    a = capped(10, "", 10);
    CHECK(!a.truncated);
    CHECK(!a.cut_row);
    a = capped(40, "", 10);
    CHECK_EQ(a.row_count, 10);
    CHECK(a.truncated);
    CHECK(!a.cut_row);
}

/*
 * A source over memory that counts the bytes read from it, with or without
 * the size and seek operations.  Without them it is a source that can only
 * be read from the top, like a stream.
 */
typedef struct {
    log_source_t inner;
    log_mem_ctx_t mem;
    size_t bytes;
} counted_ctx_t;

static size_t counted_read(void *v, char *buf, size_t max)
{
    counted_ctx_t *c = (counted_ctx_t *)v;
    size_t n = c->inner.read(c->inner.ctx, buf, max);
    c->bytes += n;
    return n;
}

static bool counted_rewind(void *v)
{
    counted_ctx_t *c = (counted_ctx_t *)v;
    return c->inner.rewind(c->inner.ctx);
}

static bool counted_size(void *v, size_t *bytes)
{
    counted_ctx_t *c = (counted_ctx_t *)v;
    return c->inner.size(c->inner.ctx, bytes);
}

static bool counted_seek(void *v, size_t offset)
{
    counted_ctx_t *c = (counted_ctx_t *)v;
    return c->inner.seek(c->inner.ctx, offset);
}

static void counted_source(log_source_t *src, counted_ctx_t *c,
                           const char *text, size_t len, bool seekable)
{
    log_source_memory(&c->inner, &c->mem, text, len);
    c->bytes = 0;
    memset(src, 0, sizeof(*src));
    src->read = counted_read;
    src->rewind = counted_rewind;
    src->ctx = c;
    if (seekable) {
        src->size = counted_size;
        src->seek = counted_seek;
    }
}

/* 20000 rows of about 12 bytes, then @p tail. */
static size_t long_log(char *text, size_t cap, const char *tail)
{
    size_t n = (size_t)snprintf(text, cap, "t,v\n");
    for (int i = 0; i < 20000; ++i) {
        n += (size_t)snprintf(text + n, cap - n, "%d,%d\n", i, i * 3);
    }
    n += (size_t)snprintf(text + n, cap - n, "%s", tail);
    return n;
}

/*
 * Past the row cap, a source that can seek is not read to its end for the
 * cut row: the analysis stops at the first row past the cap and reads the
 * last bytes of the file.  20000 rows and a cap of 100 read under 16 KiB of
 * a file over 200 KiB, and the flags match the source that can only be read
 * on to its end.
 */
TEST_CASE(a_capped_file_is_not_read_to_its_end_for_the_cut_row)
{
    static char text[300000];
    static const char *const tails[] = { "", "99,2", "\n \t\r", "\v",
                                         "7,\"1\n2" };
    static const bool cut[] = { false, true, false, true, true };
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.max_rows = 100;

    for (size_t i = 0; i < sizeof(tails) / sizeof(tails[0]); ++i) {
        size_t n = long_log(text, sizeof(text), tails[i]);
        CHECK(n > 200000u);
        for (int seekable = 1; seekable >= 0; --seekable) {
            log_source_t src;
            counted_ctx_t c;
            counted_source(&src, &c, text, n, seekable != 0);
            log_analysis_t a;
            CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
            CHECK_EQ(a.row_count, 100);
            CHECK(a.truncated);
            CHECK_EQ(a.cut_row, cut[i]);
            if (seekable) {
                CHECK(c.bytes < 16384u);
            } else {
                CHECK(c.bytes >= n);   /* the fallback reads on to the end */
            }
        }
    }

    /* The one case the two answers differ: a quoted field left open with a
     * line feed as the last byte.  The reader counts it as a cut row; the
     * tail sees a line feed. */
    size_t n = long_log(text, sizeof(text), "7,\"1\n");
    for (int seekable = 1; seekable >= 0; --seekable) {
        log_source_t src;
        counted_ctx_t c;
        counted_source(&src, &c, text, n, seekable != 0);
        log_analysis_t a;
        CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
        CHECK(a.truncated);
        CHECK_EQ(a.cut_row, seekable == 0);
    }
}

/* More than 4096 bytes of blanks after the last line feed: the tail is not
 * scanned further back, the file is read on to its end instead, and the
 * answer is the same. */
TEST_CASE(a_long_blank_tail_falls_back_to_reading_on)
{
    static char text[300000];
    static char blanks[5001];
    memset(blanks, ' ', sizeof(blanks) - 1u);
    blanks[sizeof(blanks) - 1u] = '\0';
    size_t n = long_log(text, sizeof(text), blanks);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.max_rows = 100;
    log_source_t src;
    counted_ctx_t c;
    counted_source(&src, &c, text, n, true);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK(a.truncated);
    CHECK(!a.cut_row);
    CHECK(c.bytes >= n);

    /* Under the limit the tail decides it. */
    blanks[4000] = '\0';
    n = long_log(text, sizeof(text), blanks);
    counted_source(&src, &c, text, n, true);
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK(!a.cut_row);
    CHECK(c.bytes < 16384u);
}

/* The card's source is a stdio stream; it seeks too. */
TEST_CASE(a_stdio_source_finds_the_cut_row_from_the_tail)
{
    static char text[300000];
    size_t n = long_log(text, sizeof(text), "99,2");
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    CHECK_EQ(fwrite(text, 1, n, f), n);
    log_source_t src;
    log_source_stdio(&src, f);
    size_t bytes = 0;
    CHECK(src.size(src.ctx, &bytes));
    CHECK_EQ(bytes, n);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.max_rows = 100;
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.row_count, 100);
    CHECK(a.truncated);
    CHECK(a.cut_row);
    CHECK(!src.seek(src.ctx, (size_t)-1));   /* past what fseek takes */
    fclose(f);
}

TEST_CASE(an_over_long_row_is_truncated_without_running_off_the_end)
{
    /* A file whose rows are longer than the row buffer is malformed for this
     * viewer, but it must not be a buffer overrun. */
    static char text[LOG_ROW_MAX * 3];
    size_t n = (size_t)snprintf(text, sizeof(text), "a,b\n");
    for (int i = 0; i < LOG_ROW_MAX + 200 && n + 4u < sizeof(text); ++i) {
        text[n++] = (char)('0' + (i % 10));
    }
    n += (size_t)snprintf(text + n, sizeof(text) - n, ",7\n1,2\n");
    text[n] = '\0';

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);

    log_analysis_t a;
    log_err_t err = log_csv_analyse(&src, NULL, &a);
    CHECK(err == LOG_OK);
    CHECK_EQ(a.n_columns, 2);
}

/* ---------------------------------------------------- a card that fails --- */

/*
 * A source that stops part way, the way a bad sector or a pulled card does.
 * Without an error channel this is indistinguishable from a clean end of file,
 * and a truncated log is then presented as a complete short one, with a row
 * count, duration and sample rate computed from the readable part.
 */
typedef struct {
    const char *text;
    size_t len;
    size_t pos;
    int    pass;         /* rewinds seen, so which pass is reading */
    int    fail_pass;    /* the pass that fails, PASS_NONE for none */
    size_t fail_after;   /* bytes into that pass before it fails */
    bool   failed;
} failing_ctx_t;

/*
 * A pass is named by the rewind it starts with, because every pass starts
 * with one and reads from byte 0.  log_csv_analyse sniffs the delimiter,
 * measures the geometry, then scans the columns; log_csv_build rewinds once
 * for its own read of the rows.  failing_arm puts the counter back before the
 * first pass, so the numbers are per call.  Failing at an absolute byte
 * offset instead would trip the earliest pass every time and leave the later
 * ones unreachable.
 */
#define PASS_SNIFF     0
#define PASS_GEOMETRY  1
#define PASS_COLUMNS   2
#define PASS_ROWS      0
#define PASS_NONE      (-1)

static size_t failing_read(void *vctx, char *buf, size_t max)
{
    failing_ctx_t *c = (failing_ctx_t *)vctx;
    if (c->pos >= c->len) {
        return 0;               /* a genuine end of file */
    }
    if (c->pass == c->fail_pass) {
        if (c->pos >= c->fail_after) {
            c->failed = true;   /* data left, but the card stopped answering */
            return 0;
        }
        size_t room = c->fail_after - c->pos;
        if (max > room) { max = room; }
    }
    size_t left = c->len - c->pos;
    if (max > left) { max = left; }
    memcpy(buf, c->text + c->pos, max);
    c->pos += max;
    return max;
}

static bool failing_error(void *vctx)
{
    return ((failing_ctx_t *)vctx)->failed;
}

static bool failing_rewind(void *vctx)
{
    failing_ctx_t *c = (failing_ctx_t *)vctx;
    c->pos = 0;
    c->failed = false;
    c->pass++;
    return true;
}

/* Back to the top, with the failure placed on one pass of the call that
 * follows.  fail_after is counted within that pass. */
static void failing_arm(failing_ctx_t *c, int fail_pass, size_t fail_after)
{
    c->pos = 0;
    c->pass = -1;
    c->failed = false;
    c->fail_pass = fail_pass;
    c->fail_after = fail_after;
}

TEST_CASE(a_read_error_is_not_a_clean_end_of_file)
{
    static char text[8000];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "time,rpm\n");
    for (int i = 0; i < 400; ++i) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%d.%03d,%d\n",
                              i / 100, (i % 100) * 10, 1000 + i);
    }

    failing_ctx_t ctx = { text, n, 0, 0, PASS_NONE, 0, false };
    log_source_t src = { failing_read, failing_rewind, failing_error, &ctx,
                         NULL, NULL };

    /* Healthy first: the whole file is there. */
    log_analysis_t a;
    failing_arm(&ctx, PASS_NONE, 0);
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.row_count, 400);

    /* Then the card gives up a quarter of the way into the geometry pass. */
    failing_arm(&ctx, PASS_GEOMETRY, 1000);
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_ERR_READ);

    /* And a failure during the build pass is refused too, rather than
     * returning a short array the screen would plot as the whole run. */
    failing_arm(&ctx, PASS_NONE, 0);
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    const int pick[1] = { 1 };
    log_data_t d;
    failing_arm(&ctx, PASS_ROWS, 1000);
    CHECK_EQ(log_csv_build(&src, &a, pick, 1, &d), LOG_ERR_READ);
}

/*
 * The column pass is where the unit, the range and the monotonic flag are
 * settled, so a source that stops during it is refused for the same reason
 * the other passes refuse one: the statistics would describe the readable
 * part and nothing would say so.
 *
 * The fixture is a 40 s run logged at 10 Hz, its time column in milliseconds
 * under a bare header.  With no unit to read, the unit is guessed from the
 * span: 39,900 ms over the whole file reads as "ms", while the 3,000 ms of
 * the 31 rows a failure 300 bytes in leaves reads as "s".  Accepting that
 * hands the plot a time axis 1000x too long -- 39,900 s instead of 39.9 s --
 * and nothing on the screen is drawn in UI_WARN.
 */
TEST_CASE(a_column_pass_that_fails_is_refused_like_the_others)
{
    static char text[8000];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "time,rpm\n");
    for (int i = 0; i < 400; ++i) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%d,%d\n",
                              i * 100, 1000 + i);
    }

    failing_ctx_t ctx = { text, n, 0, 0, PASS_NONE, 0, false };
    log_source_t src = { failing_read, failing_rewind, failing_error, &ctx,
                         NULL, NULL };

    /* Intact, the span settles the unit at milliseconds. */
    log_analysis_t a;
    failing_arm(&ctx, PASS_NONE, 0);
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.row_count, 400);
    CHECK(strcmp(a.time_unit, "ms") == 0);

    /* The geometry pass reads the file whole; the column pass stops 300
     * bytes in, at row 31 of 400. */
    failing_arm(&ctx, PASS_COLUMNS, 300);
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_ERR_READ);
}

/*
 * A row too long for the field store loses every field after the overflow --
 * they are emitted as empty cells, which downstream look exactly like blanks
 * a file legitimately contains.  The count is what lets the screen say so.
 */
TEST_CASE(a_row_too_long_for_the_store_is_counted_not_swallowed)
{
    static char text[LOG_ROW_MAX * 3];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "time,note,rpm\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n, "0,short,1000\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n, "1,");
    for (size_t i = 0; i < LOG_ROW_MAX + 40u; ++i) {
        text[n++] = 'x';
    }
    n += (size_t)snprintf(text + n, sizeof(text) - n, ",1100\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n, "2,short,1200\n");

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.row_count, 3);
    CHECK_EQ(a.long_rows, 1);
}

int main(void)
{
    RUN(rows_honour_quotes_and_doubled_quotes);
    RUN(a_last_row_without_its_line_feed_is_not_a_row);
    RUN(a_quoted_field_may_straddle_a_read_boundary);
    RUN(delimiter_is_sniffed_from_a_sample);
    RUN(a_cut_row_is_found_past_the_row_cap);
    RUN(a_capped_file_is_not_read_to_its_end_for_the_cut_row);
    RUN(a_long_blank_tail_falls_back_to_reading_on);
    RUN(a_stdio_source_finds_the_cut_row_from_the_tail);
    RUN(an_over_long_row_is_truncated_without_running_off_the_end);
    RUN(a_cut_last_row_does_not_vote_on_the_delimiter);
    RUN(a_sample_without_a_line_feed_is_sniffed_whole);
    RUN(only_space_tab_and_cr_are_blank_after_the_last_line_feed);
    RUN(a_read_error_is_not_a_clean_end_of_file);
    RUN(a_column_pass_that_fails_is_refused_like_the_others);
    RUN(a_row_too_long_for_the_store_is_counted_not_swallowed);
    return test_summary("log_reader");
}
