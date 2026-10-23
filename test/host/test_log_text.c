/**
 * @file test_log_text.c
 * @brief Host tests for a report's rows as LOGS' TEXT view shows them.
 *
 * The failures under test: a line broken inside a word that fits, a word
 * wider than a row lost or doubled, a CR (carriage return) or a CR LF pair
 * read as text, a row counted in bytes rather than characters, and a row
 * cut inside a character.  The view on the text is held by
 * test_logview_report.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>

#include "gfx.h"
#include "log_text.h"

/** @brief Load @p text into @p t at @p cols cells a row.
 *  @param t     the text
 *  @param text  the file
 *  @param cols  cells a row
 *  @return the load's result */
static log_err_t load(log_text_t *t, const char *text, int cols)
{
    log_mem_ctx_t ctx;
    log_source_t src;
    log_source_memory(&src, &ctx, text, strlen(text));
    return log_text_load(t, &src, cols);
}

/** @brief Every row of @p t joined by '|' into @p out.
 *  @param t    the text
 *  @param out  takes the rows
 *  @param n    bytes of @p out */
static void joined(log_text_t *t, char *out, size_t n)
{
    size_t at = 0u;
    out[0] = '\0';
    for (int i = 0; i < t->rows && at < n; ++i) {
        char row[256];
        CHECK(log_text_row(t, i, row, sizeof(row)));
        at += (size_t)snprintf(out + at, n - at, "%s%s", (i > 0) ? "|" : "",
                               row);
    }
}

TEST_CASE(rows_break_at_spaces_and_cut_a_word_wider_than_a_row)
{
    log_text_t t;
    memset(&t, 0, sizeof(t));
    char got[512];
    CHECK_EQ(load(&t, "one two three four\n", 9), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, "one two|three|four");
    /* A row exactly full, the space after it the break. */
    CHECK_EQ(load(&t, "abcdefghi jk", 9), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, "abcdefghi|jk");
    /* A word wider than a row is cut at the row's end. */
    CHECK_EQ(load(&t, "abcdefghijklmnopqrstu", 9), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, "abcdefghi|jklmnopqr|stu");
    /* A leading space is no break. */
    CHECK_EQ(load(&t, " abcdefghijk", 9), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, " abcdefgh|ijk");
    /* CRLF, a tab, blank lines; no row after the last line feed. */
    CHECK_EQ(load(&t, "a\r\n\r\n\tb\nc", 9), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, "a|| b|c");
    CHECK_EQ(t.rows, 4);
    /* A lone CR ends a line as LF does; LF CR is two ends. */
    CHECK_EQ(load(&t, "a\rb\r\rc\n\rd\r", 9), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, "a|b||c||d");
    /* A CR LF pair split across two reads of 256 bytes is one end. */
    {
        static char split[300];
        memset(split, 'x', 255u);
        memcpy(split + 255, "\r\ny", 4u);
        CHECK_EQ(load(&t, split, 300), LOG_OK);
        CHECK_EQ(t.rows, 2);
        char row[8];
        CHECK(log_text_row(&t, 1, row, sizeof(row)));
        CHECK_STR_EQ(row, "y");
    }
    /* A NUL byte is a space, so the rows after it are not lost. */
    CHECK_EQ(load(&t, "", 9), LOG_OK);
    CHECK_EQ(t.rows, 0);
    CHECK(!t.cut);
    {
        static const char k_nul[] = "a\0b\nc";
        log_mem_ctx_t ctx;
        log_source_t src;
        log_source_memory(&src, &ctx, k_nul, sizeof(k_nul) - 1u);
        CHECK_EQ(log_text_load(&t, &src, 9), LOG_OK);
        joined(&t, got, sizeof(got));
        CHECK_STR_EQ(got, "a b|c");
    }
    /* One cell a row. */
    CHECK_EQ(load(&t, "ab c", 1), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, "a|b|c");
    log_text_free(&t);
    log_text_free(&t);
    log_text_free(NULL);
}

TEST_CASE(a_row_counts_characters_and_is_never_cut_inside_one)
{
    log_text_t t;
    memset(&t, 0, sizeof(t));
    char got[512];
    /* "Größe" is 5 cells in 7 bytes; "µs" 2 cells in 3. */
    CHECK_EQ(load(&t, "Größe 12 µs Länge", 8), LOG_OK);
    joined(&t, got, sizeof(got));
    CHECK_STR_EQ(got, "Größe 12|µs Länge");
    for (int i = 0; i < t.rows; ++i) {
        char row[64];
        CHECK(log_text_row(&t, i, row, sizeof(row)));
        CHECK(gfx_text_cells(row) <= 8);
    }
    /* A buffer too small for a row: cut between two characters. */
    char small[4];
    CHECK(log_text_row(&t, 0, small, sizeof(small)));
    CHECK_STR_EQ(small, "Gr");
    CHECK(log_text_row(&t, 1, small, sizeof(small)));
    CHECK_STR_EQ(small, "µs");
    /* Rows asked out of order, and outside the text. */
    char row[64];
    CHECK(log_text_row(&t, 1, row, sizeof(row)));
    CHECK(log_text_row(&t, 0, row, sizeof(row)));
    CHECK_STR_EQ(row, "Größe 12");
    CHECK(!log_text_row(&t, 2, row, sizeof(row)));
    CHECK_STR_EQ(row, "");
    CHECK(!log_text_row(&t, -1, row, sizeof(row)));
    CHECK(!log_text_row(&t, 0, row, 0u));
    CHECK(!log_text_row(NULL, 0, row, sizeof(row)));
    log_text_free(&t);
    CHECK(!log_text_row(&t, 0, row, sizeof(row)));
}

/* The allocation that fails: 1 the first, 2 the second; 0 none. */

int main(void)
{
    RUN(rows_break_at_spaces_and_cut_a_word_wider_than_a_row);
    RUN(a_row_counts_characters_and_is_never_cut_inside_one);
    return test_summary("log_text");
}
