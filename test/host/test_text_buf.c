/*
 * The text buffer of the viewer and the editor: loading at the limits and
 * refusing past them, line ends of every kind, the BOM, tabs and wrapping,
 * UTF-8 units against gfx_utf8_next(), the cursor, typing through the gap
 * to the cap, the edits that join or split line ends, the index after
 * random edits against a fresh load, and serialising what was loaded.
 *
 * Every buffer is on the heap at its exact size, so a write past its end
 * stops the sanitizer build.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "text_buf.h"

/* The panel's sizes: the file cap and the index of 65,536 entries. */
#define CAP   TEXT_BUF_FILE_MAX
#define LINES TEXT_BUF_ENDS_MAX

static text_buf_t g_tb;
static uint8_t *g_mem;
static uint32_t *g_ends;
static uint8_t *g_out;

/* A buffer of @p cap bytes and @p lines index entries. */
static void fresh(size_t cap, size_t lines)
{
    free(g_mem);
    free(g_ends);
    g_mem = malloc(cap > 0u ? cap : 1u);
    g_ends = malloc((lines > 0u ? lines : 1u) * sizeof(uint32_t));
    REQUIRE_ALLOC(g_mem);
    REQUIRE_ALLOC(g_ends);
    CHECK_EQ(text_buf_init(&g_tb, g_mem, cap, g_ends, lines), TEXT_BUF_OK);
}

/* The panel's buffer with @p len bytes of @p s loaded. */
static text_buf_err_t load(const char *s, size_t len)
{
    fresh(CAP, LINES);
    return text_buf_load(&g_tb, s, len);
}

/* load() of a C string. */
static text_buf_err_t loads(const char *s)
{
    return load(s, strlen(s));
}

/* The serialised file into g_out; its length. */
static size_t serial(void)
{
    const size_t n = text_buf_size(&g_tb);
    free(g_out);
    g_out = malloc(n + 1u);
    REQUIRE_ALLOC(g_out);
    CHECK_EQ(text_buf_read(&g_tb, 0u, g_out, n + 1u), n);
    g_out[n] = '\0';
    return n;
}

/* Whether the file serialises to the @p len bytes of @p want, read whole
 * and in pieces of 7 bytes. */
static int same_file(const char *want, size_t len)
{
    if (serial() != len || memcmp(g_out, want, len) != 0) {
        return 0;
    }
    uint8_t piece[7];
    for (size_t at = 0u; at < len; at += sizeof(piece)) {
        const size_t got = text_buf_read(&g_tb, at, piece, sizeof(piece));
        const size_t want_n = (len - at < sizeof(piece)) ? len - at
                                                          : sizeof(piece);
        if (got != want_n || memcmp(piece, want + at, got) != 0) {
            return 0;
        }
    }
    return text_buf_read(&g_tb, len, piece, sizeof(piece)) == 0u;
}

/* same_file() of a C string. */
static int same(const char *want)
{
    return same_file(want, strlen(want));
}

/* Line @p line of the text as a C string. */
static const char *line_text(size_t line)
{
    static char buf[512];
    size_t len = 0u;
    const char *p = text_buf_line(&g_tb, line, &len);
    if (p == NULL || len >= sizeof(buf)) {
        return "<none>";
    }
    memcpy(buf, p, len);
    buf[len] = '\0';
    return buf;
}

/* --- empty, one byte, the caps --------------------------------------- */

TEST_CASE(an_empty_file_is_one_empty_line)
{
    CHECK_EQ(load(NULL, 0u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_len(&g_tb), 0u);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);
    CHECK_EQ(text_buf_line_len(&g_tb, 0u), 0u);
    CHECK_EQ(text_buf_rows(&g_tb, 0u, 98u), 1u);
    CHECK_EQ(text_buf_eol(&g_tb), TEXT_EOL_LF);
    CHECK(!text_buf_mixed(&g_tb));
    CHECK(!text_buf_bom(&g_tb));
    CHECK(!text_buf_dirty(&g_tb));
    CHECK(same(""));
    CHECK(!text_buf_left(&g_tb));
    CHECK(!text_buf_right(&g_tb));
    CHECK(!text_buf_delete_before(&g_tb));
    CHECK(!text_buf_delete_after(&g_tb));
    CHECK(!text_buf_dirty(&g_tb));
    size_t len = 99u;
    CHECK(text_buf_line(&g_tb, 0u, &len) != NULL);
    CHECK_EQ(len, 0u);
    CHECK(text_buf_line(&g_tb, 1u, &len) == NULL);
    /* ENTER in an empty file is LF. */
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK(same("\n"));
    CHECK_EQ(text_buf_lines(&g_tb), 2u);
}

TEST_CASE(one_byte_and_no_final_line_end)
{
    CHECK_EQ(loads("x"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);
    CHECK_EQ(text_buf_line_len(&g_tb, 0u), 1u);
    CHECK(same("x"));
    CHECK_EQ(loads("a\nb"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 2u);
    CHECK_STR_EQ(line_text(1u), "b");
    /* With a final line end the last line is empty. */
    CHECK_EQ(loads("a\nb\n"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 3u);
    CHECK_EQ(text_buf_line_len(&g_tb, 2u), 0u);
    CHECK_EQ(text_buf_line_start(&g_tb, 2u), 4u);
    CHECK_EQ(text_buf_line_start(&g_tb, 9u), 4u);
    CHECK_EQ(text_buf_line_len(&g_tb, 9u), 0u);
    CHECK_EQ(text_buf_line_of(&g_tb, 4u), 2u);
    CHECK_EQ(text_buf_line_of(&g_tb, 99u), 2u);
}

/* Exactly 1 MiB is taken, 1 MiB and a byte are refused before a byte is
 * copied, and a refused load leaves an empty text. */
TEST_CASE(a_file_of_the_cap_loads_and_one_byte_more_does_not)
{
    char *big = malloc(CAP + 1u);
    REQUIRE_ALLOC(big);
    memset(big, 'a', CAP + 1u);
    for (size_t k = 99u; k < CAP; k += 100u) {
        big[k] = '\n';
    }
    CHECK_EQ(load(big, CAP), TEXT_BUF_OK);
    CHECK_EQ(text_buf_size(&g_tb), CAP);
    CHECK_EQ(text_buf_lines(&g_tb), CAP / 100u + 1u);
    CHECK(same_file(big, CAP));
    /* At the cap nothing more goes in. */
    text_buf_set_cursor(&g_tb, 50u);
    CHECK_EQ(text_buf_insert(&g_tb, 'b'), TEXT_BUF_FULL);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_FULL);
    CHECK(!text_buf_dirty(&g_tb));
    CHECK(text_buf_delete_after(&g_tb));
    CHECK_EQ(text_buf_insert(&g_tb, 'b'), TEXT_BUF_OK);
    CHECK_EQ(text_buf_insert(&g_tb, 'b'), TEXT_BUF_FULL);
    /* One byte of room takes an LF, not a CR LF. */
    CHECK(text_buf_delete_after(&g_tb));
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK_EQ(text_buf_size(&g_tb), CAP);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_FULL);
    CHECK_EQ(load("a\r\n", 3u), TEXT_BUF_OK);
    fresh(4u, 4u);
    CHECK_EQ(text_buf_load(&g_tb, "a\r\n", 3u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_FULL);

    CHECK_EQ(load(big, CAP + 1u), TEXT_BUF_TOO_LARGE);
    CHECK_EQ(text_buf_len(&g_tb), 0u);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);
    CHECK(same(""));
    /* A smaller buffer has a smaller room. */
    fresh(16u, 4u);
    CHECK_EQ(text_buf_room(&g_tb), 16u);
    CHECK_EQ(text_buf_load(&g_tb, big, 17u), TEXT_BUF_TOO_LARGE);
    CHECK_EQ(text_buf_load(&g_tb, big, 16u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_insert(&g_tb, 'x'), TEXT_BUF_FULL);
    /* A buffer larger than the cap still takes 1 MiB at most. */
    fresh(CAP + 64u, 4u);
    CHECK_EQ(text_buf_room(&g_tb), CAP);
    free(big);
}

/* A BOM counts in the file's 1 MiB: with one, the text holds 3 bytes
 * fewer, so what a save writes always opens again. */
TEST_CASE(the_bom_counts_in_the_cap)
{
    char *big = malloc(CAP);
    REQUIRE_ALLOC(big);
    memset(big, 'z', CAP);
    memcpy(big, "\xEF\xBB\xBF", 3u);
    CHECK_EQ(load(big, CAP), TEXT_BUF_OK);
    CHECK(text_buf_bom(&g_tb));
    CHECK_EQ(text_buf_len(&g_tb), CAP - 3u);
    CHECK_EQ(text_buf_size(&g_tb), CAP);
    CHECK_EQ(text_buf_insert(&g_tb, 'y'), TEXT_BUF_FULL);
    CHECK(same_file(big, CAP));
    free(big);
}

/* A file 65,536 bytes under the cap takes 65,536 typed code points through
 * the gap and refuses the next; the file it gives loads again. */
TEST_CASE(typing_fills_the_gap_to_the_cap)
{
    const size_t start = CAP - 65536u;
    char *base = malloc(start);
    REQUIRE_ALLOC(base);
    memset(base, 'q', start);
    CHECK_EQ(load(base, start), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, start / 2u);
    for (size_t k = 0u; k < 65536u; ++k) {
        if (text_buf_insert(&g_tb, 'a' + (uint32_t)(k % 26u)) != TEXT_BUF_OK) {
            T_FAIL("code point %zu refused", k);
            break;
        }
    }
    CHECK_EQ(text_buf_size(&g_tb), CAP);
    CHECK_EQ(text_buf_insert(&g_tb, 'x'), TEXT_BUF_FULL);
    CHECK_EQ(text_buf_cursor(&g_tb), start / 2u + 65536u);
    CHECK_EQ(text_buf_byte(&g_tb, start / 2u), 'a');
    CHECK_EQ(text_buf_byte(&g_tb, start / 2u + 25u), 'z');
    CHECK_EQ(text_buf_byte(&g_tb, start / 2u + 65536u), 'q');
    CHECK_EQ(text_buf_byte(&g_tb, CAP), 0u);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);
    CHECK_EQ(text_buf_line_len(&g_tb, 0u), CAP);
    serial();
    uint8_t *copy = malloc(CAP);
    REQUIRE_ALLOC(copy);
    memcpy(copy, g_out, CAP);
    CHECK_EQ(load((const char *)copy, CAP), TEXT_BUF_OK);
    CHECK(same_file((const char *)copy, CAP));
    /* A 4-byte code point does not fit in 3 bytes of room; 2 bytes do. */
    CHECK_EQ(load((const char *)copy, CAP - 3u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_insert(&g_tb, 0x1F600u), TEXT_BUF_FULL);
    CHECK_EQ(text_buf_insert(&g_tb, 0xE4u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_insert(&g_tb, 0xE4u), TEXT_BUF_FULL);
    CHECK_EQ(text_buf_insert(&g_tb, 'a'), TEXT_BUF_OK);
    CHECK_EQ(text_buf_size(&g_tb), CAP);
    free(copy);
    free(base);
}

/* 65,536 line ends load, 65,537 do not; ENTER on a full index is refused
 * and changes nothing. */
TEST_CASE(sixty_five_thousand_lines_are_the_most)
{
    const size_t n = 65537u * 2u;
    char *t = malloc(n);
    REQUIRE_ALLOC(t);
    for (size_t k = 0u; k < n; k += 2u) {
        t[k] = 'x';
        t[k + 1u] = '\n';
    }
    CHECK_EQ(load(t, 65536u * 2u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 65537u);
    CHECK_STR_EQ(line_text(65535u), "x");
    CHECK_EQ(text_buf_line_len(&g_tb, 65536u), 0u);
    text_buf_set_cursor(&g_tb, 1u);
    const uint32_t rev = text_buf_revision(&g_tb);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_TOO_MANY);
    CHECK_EQ(text_buf_revision(&g_tb), rev);
    CHECK(!text_buf_dirty(&g_tb));
    /* Typing on a full index is fine; joining two lines frees an entry. */
    CHECK_EQ(text_buf_insert(&g_tb, 'y'), TEXT_BUF_OK);
    CHECK(text_buf_delete_after(&g_tb));
    CHECK_EQ(text_buf_lines(&g_tb), 65536u);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 65537u);

    CHECK_EQ(load(t, n), TEXT_BUF_TOO_MANY);
    CHECK_EQ(text_buf_len(&g_tb), 0u);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);
    /* A caller's smaller index is its limit; a larger one is 65,536. */
    fresh(64u, 2u);
    CHECK_EQ(text_buf_load(&g_tb, "a\nb\nc", 5u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_load(&g_tb, "a\nb\nc\n", 6u), TEXT_BUF_TOO_MANY);
    fresh(64u, LINES + 10u);
    CHECK_EQ(g_tb.ends_cap, LINES);
    free(t);
}

/* A line of 100 kB: one line, wrapped at 98 cells into 1,021 rows. */
TEST_CASE(a_long_line_wraps)
{
    const size_t n = 100000u;
    char *t = malloc(n);
    REQUIRE_ALLOC(t);
    memset(t, 'w', n);
    CHECK_EQ(load(t, n), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);
    CHECK_EQ(text_buf_rows(&g_tb, 0u, 98u), 1021u);
    CHECK_EQ(text_buf_rows(&g_tb, 0u, 0u), 1u);
    size_t line = 9u, cell = 0u;
    text_buf_cell_of(&g_tb, n, 98u, &line, &cell);
    CHECK_EQ(line, 0u);
    CHECK_EQ(cell, n);
    CHECK_EQ(text_buf_pos_at(&g_tb, 0u, 1020u * 98u, 98u), 1020u * 98u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 0u, 2u * n, 98u), n);
    /* Typing in its middle: the line reads whole across the gap. */
    text_buf_set_cursor(&g_tb, n / 2u);
    CHECK_EQ(text_buf_insert(&g_tb, 'M'), TEXT_BUF_OK);
    size_t len = 0u;
    const char *p = text_buf_line(&g_tb, 0u, &len);
    CHECK_EQ(len, n + 1u);
    CHECK(p != NULL && p[n / 2u] == 'M' && p[0] == 'w' && p[n] == 'w');
    CHECK_EQ(text_buf_insert(&g_tb, 'N'), TEXT_BUF_OK);
    p = text_buf_line(&g_tb, 0u, &len);
    CHECK(p != NULL && p[n / 2u + 1u] == 'N' && len == n + 2u);
    free(t);
}

/* --- line ends ---------------------------------------------------------- */

TEST_CASE(each_kind_of_line_end_is_found_and_kept)
{
    CHECK_EQ(loads("a\nb\nc"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_eol(&g_tb), TEXT_EOL_LF);
    CHECK(!text_buf_mixed(&g_tb));

    CHECK_EQ(loads("a\r\nbb\r\nc\r\n"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_eol(&g_tb), TEXT_EOL_CRLF);
    CHECK(!text_buf_mixed(&g_tb));
    CHECK_EQ(text_buf_lines(&g_tb), 4u);
    CHECK_STR_EQ(line_text(1u), "bb");
    CHECK_EQ(text_buf_line_start(&g_tb, 2u), 7u);
    CHECK(same("a\r\nbb\r\nc\r\n"));

    /* A lone CR ends a line too. */
    CHECK_EQ(loads("a\rb\rc"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_eol(&g_tb), TEXT_EOL_CR);
    CHECK_EQ(text_buf_lines(&g_tb), 3u);
    CHECK_STR_EQ(line_text(2u), "c");
    CHECK_EQ(loads("a\r"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 2u);
    CHECK_EQ(text_buf_eol(&g_tb), TEXT_EOL_CR);

    /* Mixed: kept as it is, and the file's kind is its first one. */
    CHECK_EQ(loads("a\r\nb\nc\rd"), TEXT_BUF_OK);
    CHECK(text_buf_mixed(&g_tb));
    CHECK_EQ(text_buf_eol(&g_tb), TEXT_EOL_CRLF);
    CHECK_EQ(text_buf_lines(&g_tb), 4u);
    CHECK_STR_EQ(line_text(0u), "a");
    CHECK_STR_EQ(line_text(1u), "b");
    CHECK_STR_EQ(line_text(2u), "c");
    CHECK_STR_EQ(line_text(3u), "d");
    CHECK(same("a\r\nb\nc\rd"));
    /* CR CR LF is a lone CR and a CR LF; LF CR is an LF and a lone CR. */
    CHECK_EQ(loads("a\r\r\nb\n\rc"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 5u);
    CHECK(text_buf_mixed(&g_tb));
    CHECK_STR_EQ(line_text(1u), "");
    CHECK_STR_EQ(line_text(2u), "b");
    CHECK_STR_EQ(line_text(4u), "c");
}

/* ENTER inserts the file's kind, and CR LF where the kind would join a
 * lone CR or LF beside it. */
TEST_CASE(enter_inserts_the_files_line_end_and_always_adds_a_line)
{
    CHECK_EQ(loads("ab"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 1u);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK(same("a\nb"));
    CHECK_EQ(text_buf_cursor(&g_tb), 2u);

    CHECK_EQ(loads("x\r\nab"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 4u);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK(same("x\r\na\r\nb"));
    CHECK_EQ(text_buf_cursor(&g_tb), 6u);
    CHECK(!text_buf_mixed(&g_tb));

    CHECK_EQ(loads("x\rab"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 3u);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK(same("x\ra\rb"));

    /* An LF file with a lone CR: ENTER right after the CR. */
    CHECK_EQ(loads("x\ny\rz"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 4u);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 4u);
    CHECK(same("x\ny\r\r\nz"));
    /* A CR file with an LF: ENTER right before the LF. */
    CHECK_EQ(loads("a\rb\nc"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 3u);
    CHECK_EQ(text_buf_insert_eol(&g_tb), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 4u);
    CHECK(same("a\rb\r\n\nc"));
}

/* A CR LF is one unit; a lone CR and an LF that a delete brings together
 * become one, and the cursor goes in front of it. */
TEST_CASE(deletes_join_line_ends)
{
    CHECK_EQ(loads("a\r\nb"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 3u);
    CHECK(text_buf_delete_before(&g_tb));
    CHECK(same("ab"));
    CHECK_EQ(text_buf_cursor(&g_tb), 1u);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);

    CHECK_EQ(loads("a\r\nb"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 1u);
    CHECK(text_buf_delete_after(&g_tb));
    CHECK(same("ab"));

    CHECK_EQ(loads("a\rX\nb"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_lines(&g_tb), 3u);
    CHECK(text_buf_mixed(&g_tb));
    text_buf_set_cursor(&g_tb, 3u);
    CHECK(text_buf_delete_before(&g_tb));
    CHECK(same("a\r\nb"));
    CHECK_EQ(text_buf_lines(&g_tb), 2u);
    CHECK_EQ(text_buf_cursor(&g_tb), 1u);
    CHECK(!text_buf_mixed(&g_tb));
    CHECK_STR_EQ(line_text(1u), "b");

    CHECK_EQ(loads("a\rX\nb"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 2u);
    CHECK(text_buf_delete_after(&g_tb));
    CHECK(same("a\r\nb"));
    CHECK_EQ(text_buf_cursor(&g_tb), 1u);
    /* The cursor never stands between CR and LF. */
    text_buf_set_cursor(&g_tb, 2u);
    CHECK_EQ(text_buf_cursor(&g_tb), 1u);
    CHECK(text_buf_right(&g_tb));
    CHECK_EQ(text_buf_cursor(&g_tb), 3u);
    CHECK(text_buf_left(&g_tb));
    CHECK_EQ(text_buf_cursor(&g_tb), 1u);
    CHECK_EQ(text_buf_cp(&g_tb, 1u, NULL), '\n');
}

/* --- the BOM, NUL, units ------------------------------------------------ */

TEST_CASE(the_bom_is_kept_and_takes_no_cell)
{
    CHECK_EQ(loads("\xEF\xBB\xBF" "abc\n"), TEXT_BUF_OK);
    CHECK(text_buf_bom(&g_tb));
    CHECK_EQ(text_buf_len(&g_tb), 4u);
    CHECK_EQ(text_buf_size(&g_tb), 7u);
    CHECK_EQ(text_buf_cp(&g_tb, 0u, NULL), 'a');
    size_t line = 9u, cell = 9u;
    text_buf_cell_of(&g_tb, 0u, 98u, &line, &cell);
    CHECK_EQ(cell, 0u);
    CHECK(!text_buf_left(&g_tb));
    CHECK(same("\xEF\xBB\xBF" "abc\n"));
    /* A read that starts inside the BOM, and one too short for it. */
    uint8_t b[4];
    CHECK_EQ(text_buf_read(&g_tb, 1u, b, 3u), 3u);
    CHECK(memcmp(b, "\xBB\xBF" "a", 3u) == 0);
    CHECK_EQ(text_buf_read(&g_tb, 0u, b, 2u), 2u);
    CHECK(memcmp(b, "\xEF\xBB", 2u) == 0);
    CHECK_EQ(text_buf_read(&g_tb, 7u, b, 2u), 0u);
    /* Deleting everything keeps the BOM. */
    text_buf_set_cursor(&g_tb, 0u);
    while (text_buf_delete_after(&g_tb)) {
    }
    CHECK(same("\xEF\xBB\xBF"));
    /* A BOM alone; one not at the start is a code point of one cell. */
    CHECK_EQ(loads("\xEF\xBB\xBF"), TEXT_BUF_OK);
    CHECK(text_buf_bom(&g_tb));
    CHECK_EQ(text_buf_len(&g_tb), 0u);
    CHECK_EQ(loads("a\xEF\xBB\xBF"), TEXT_BUF_OK);
    CHECK(!text_buf_bom(&g_tb));
    CHECK_EQ(text_buf_cp(&g_tb, 1u, NULL), 0xFEFFu);
    CHECK_EQ(text_buf_rows(&g_tb, 0u, 2u), 1u);
    CHECK_EQ(loads("\xEF\xBB"), TEXT_BUF_OK);
    CHECK(!text_buf_bom(&g_tb));
}

/* A NUL in the first 4 KiB is no text; one after it stays. */
TEST_CASE(a_nul_in_the_first_4_kib_is_no_text)
{
    char t[TEXT_BUF_PROBE + 8u];
    memset(t, 'a', sizeof(t));
    t[0] = '\0';
    CHECK_EQ(load(t, sizeof(t)), TEXT_BUF_NOT_TEXT);
    CHECK_EQ(text_buf_len(&g_tb), 0u);
    t[0] = 'a';
    t[TEXT_BUF_PROBE - 1u] = '\0';
    CHECK_EQ(load(t, sizeof(t)), TEXT_BUF_NOT_TEXT);
    t[TEXT_BUF_PROBE - 1u] = 'a';
    t[TEXT_BUF_PROBE] = '\0';
    CHECK_EQ(load(t, sizeof(t)), TEXT_BUF_OK);
    size_t n = 0u;
    CHECK_EQ(text_buf_cp(&g_tb, TEXT_BUF_PROBE, &n), 0u);
    CHECK_EQ(n, 1u);
    CHECK_EQ(text_buf_next(&g_tb, TEXT_BUF_PROBE), TEXT_BUF_PROBE + 1u);
    CHECK(same_file(t, sizeof(t)));
    CHECK_EQ(load("\0", 1u), TEXT_BUF_NOT_TEXT);
}

/* 1-, 2-, 3- and 4-byte code points are one unit and one cell each. */
TEST_CASE(a_code_point_is_stepped_and_deleted_whole)
{
    static const char k[] = "a\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80" "b";
    static const size_t stops[] = {0u, 1u, 3u, 6u, 10u, 11u};
    CHECK_EQ(loads(k), TEXT_BUF_OK);
    for (size_t i = 0u; i + 1u < 6u; ++i) {
        CHECK_EQ(text_buf_cursor(&g_tb), stops[i]);
        CHECK(text_buf_right(&g_tb));
    }
    CHECK_EQ(text_buf_cursor(&g_tb), 11u);
    CHECK(!text_buf_right(&g_tb));
    for (size_t i = 5u; i > 0u; --i) {
        CHECK(text_buf_left(&g_tb));
        CHECK_EQ(text_buf_cursor(&g_tb), stops[i - 1u]);
    }
    CHECK_EQ(text_buf_cp(&g_tb, 1u, NULL), 0xE4u);
    CHECK_EQ(text_buf_cp(&g_tb, 3u, NULL), 0x20ACu);
    CHECK_EQ(text_buf_cp(&g_tb, 6u, NULL), 0x1F600u);
    size_t line = 0u, cell = 0u;
    text_buf_cell_of(&g_tb, 10u, 0u, &line, &cell);
    CHECK_EQ(cell, 4u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 0u, 3u, 0u), 6u);
    /* Inside a sequence the cursor goes to its start. */
    text_buf_set_cursor(&g_tb, 8u);
    CHECK_EQ(text_buf_cursor(&g_tb), 6u);
    text_buf_set_cursor(&g_tb, 10u);
    CHECK(text_buf_delete_before(&g_tb));
    CHECK(same("a\xC3\xA4\xE2\x82\xAC" "b"));
    text_buf_set_cursor(&g_tb, 3u);
    CHECK(text_buf_delete_before(&g_tb));
    CHECK(same("a\xE2\x82\xAC" "b"));
    CHECK(text_buf_delete_after(&g_tb));
    CHECK(same("ab"));
}

/* The 7 German letters typed are their UTF-8 and one cell each. */
TEST_CASE(the_german_letters_are_typed)
{
    static const uint32_t k[7] = {0xC4u, 0xD6u, 0xDCu, 0xE4u,
                                  0xF6u, 0xFCu, 0xDFu};
    CHECK_EQ(load(NULL, 0u), TEXT_BUF_OK);
    for (size_t i = 0u; i < 7u; ++i) {
        CHECK_EQ(text_buf_insert(&g_tb, k[i]), TEXT_BUF_OK);
    }
    CHECK(same("\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F"));
    size_t cell = 0u;
    text_buf_cell_of(&g_tb, text_buf_len(&g_tb), 0u, NULL, &cell);
    CHECK_EQ(cell, 7u);
    CHECK_EQ(gfx_text_cells((const char *)g_out), 7);
    /* The edges of each encoded length. */
    CHECK_EQ(load(NULL, 0u), TEXT_BUF_OK);
    static const uint32_t k_edge[] = {0x7Eu, 0xA0u, 0x7FFu, 0x800u, 0xFFFFu,
                                      0x10000u, 0x10FFFFu, '\t'};
    for (size_t i = 0u; i < sizeof(k_edge) / sizeof(k_edge[0]); ++i) {
        CHECK_EQ(text_buf_insert(&g_tb, k_edge[i]), TEXT_BUF_OK);
    }
    CHECK(same("\x7E\xC2\xA0\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF"
               "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\t"));
}

TEST_CASE(a_code_point_no_text_holds_is_refused)
{
    static const uint32_t k[] = {0u, 0x0Au, 0x0Du, 0x1Fu, 0x7Fu, 0x80u,
                                 0x9Fu, 0xD800u, 0xDFFFu, 0x110000u,
                                 0xFFFFFFFFu};
    CHECK_EQ(loads("ab"), TEXT_BUF_OK);
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (text_buf_insert(&g_tb, k[i]) != TEXT_BUF_ARG) {
            T_FAIL("U+%04X taken", (unsigned)k[i]);
        }
    }
    CHECK(same("ab"));
    CHECK(!text_buf_dirty(&g_tb));
    CHECK_EQ(text_buf_insert(&g_tb, 0x20u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_insert(&g_tb, 0xA0u), TEXT_BUF_OK);
}

/* A byte that starts no well-formed sequence is one cell, as
 * gfx_utf8_next() draws it, and is deleted alone. */
TEST_CASE(a_malformed_byte_is_one_cell)
{
    static const char *const k[] = {
        "\xFF", "\x80", "\xC3", "\xC0\x80", "\xED\xA0\x80", "\xF5\x80\x80\x80",
        "\xE2\x82", "\xF0\x9F\x98", "\xC3\xC3\xA4", "a\x80\x80\xC3\xA4\xBF",
        "\xF4\x90\x80\x80", "\xE0\x80\xAF", "\xF0\x80\x80\x80",
        "\xF0\x8F\xBF\xBF",
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(loads(k[i]), TEXT_BUF_OK);
        size_t cell = 0u;
        text_buf_cell_of(&g_tb, text_buf_len(&g_tb), 0u, NULL, &cell);
        if ((int)cell != gfx_text_cells(k[i])) {
            T_FAIL("case %zu: %zu cells, gfx draws %d", i, cell,
                   gfx_text_cells(k[i]));
        }
        CHECK(same(k[i]));
    }
    /* An overlong form reads as '?', one byte at a time. */
    size_t n = 0u;
    CHECK_EQ(loads("\xE0\x80\xAF"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_cp(&g_tb, 0u, &n), '?');
    CHECK_EQ(n, 1u);
    CHECK_EQ(loads("\xF0\x80\x80\x80"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_cp(&g_tb, 0u, &n), '?');
    CHECK_EQ(n, 1u);
    CHECK_EQ(loads("\xC3\xC3\xA4"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_cp(&g_tb, 0u, NULL), '?');
    text_buf_set_cursor(&g_tb, 1u);
    CHECK(text_buf_delete_before(&g_tb));
    CHECK(same("\xC3\xA4"));
}

/* On random bytes without NUL and line ends, the units are the cells
 * gfx_utf8_next() draws, stepping forward and back. */
TEST_CASE(the_units_are_the_cells_gfx_draws)
{
    uint32_t seed = 12345u;
    char t[64];
    for (int round = 0; round < 3000; ++round) {
        const size_t n = 1u + (size_t)(round % 40);
        for (size_t i = 0u; i < n; ++i) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            uint8_t b = (uint8_t)(seed >> 8);
            if (b == 0u || b == '\n' || b == '\r' || b == '\t') {
                b = 0xA4u;
            }
            t[i] = (char)b;
        }
        t[n] = '\0';
        CHECK_EQ(load(t, n), TEXT_BUF_OK);
        size_t units = 0u;
        const char *s = t;
        for (size_t p = 0u; p < n; p = text_buf_next(&g_tb, p)) {
            const char *before = s;
            (void)gfx_utf8_next(&s);
            if ((size_t)(s - before) != text_buf_next(&g_tb, p) - p) {
                T_FAIL("round %d: unit at %zu differs", round, p);
                return;
            }
            units++;
        }
        CHECK_EQ((int)units, gfx_text_cells(t));
        size_t back = 0u;
        for (size_t p = n; p > 0u; p = text_buf_prev(&g_tb, p)) {
            back++;
        }
        CHECK_EQ(back, units);
    }
}

/* --- cells, tabs, wrapping ---------------------------------------------- */

TEST_CASE(a_tab_reaches_the_next_stop)
{
    size_t cell = 0u;
    CHECK_EQ(loads("\tx"), TEXT_BUF_OK);
    text_buf_cell_of(&g_tb, 1u, 0u, NULL, &cell);
    CHECK_EQ(cell, 8u);
    CHECK_EQ(loads("ab\tc\t\td"), TEXT_BUF_OK);
    text_buf_cell_of(&g_tb, 3u, 0u, NULL, &cell);
    CHECK_EQ(cell, 8u);
    text_buf_cell_of(&g_tb, 6u, 0u, NULL, &cell);
    CHECK_EQ(cell, 24u);
    /* A tap inside a tab's cells lands on the tab. */
    CHECK_EQ(text_buf_pos_at(&g_tb, 0u, 5u, 0u), 2u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 0u, 8u, 0u), 3u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 0u, 200u, 0u), 7u);
    /* A tab that does not fit the row ends at its last cell. */
    CHECK_EQ(loads("12345678\tX"), TEXT_BUF_OK);
    text_buf_cell_of(&g_tb, 9u, 10u, NULL, &cell);
    CHECK_EQ(cell, 10u);
    CHECK_EQ(text_buf_rows(&g_tb, 0u, 10u), 2u);
    /* Rows narrower than a tab stop. */
    CHECK_EQ(loads("\tx"), TEXT_BUF_OK);
    text_buf_cell_of(&g_tb, 1u, 5u, NULL, &cell);
    CHECK_EQ(cell, 5u);
    CHECK_EQ(text_buf_rows(&g_tb, 0u, 5u), 2u);
    CHECK_EQ(loads("a\t\tb"), TEXT_BUF_OK);
    text_buf_cell_of(&g_tb, 3u, 4u, NULL, &cell);
    CHECK_EQ(cell, 8u);
    CHECK(same("a\t\tb"));
}

/* A full row adds no empty row; the end of a full row is its column
 * width. */
TEST_CASE(a_line_wraps_at_the_width)
{
    size_t cell = 0u, line = 0u;
    CHECK_EQ(loads("abcd\nabcdefghij\n"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_rows(&g_tb, 0u, 4u), 1u);
    text_buf_cell_of(&g_tb, 4u, 4u, &line, &cell);
    CHECK_EQ(line, 0u);
    CHECK_EQ(cell, 4u);
    CHECK_EQ(text_buf_rows(&g_tb, 1u, 4u), 3u);
    text_buf_cell_of(&g_tb, 9u, 4u, &line, &cell);
    CHECK_EQ(line, 1u);
    CHECK_EQ(cell, 4u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 1u, 4u, 4u), 9u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 1u, 9u, 4u), 14u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 1u, 11u, 4u), 15u);
    CHECK_EQ(text_buf_pos_at(&g_tb, 9u, 0u, 4u), 16u);
    CHECK_EQ(text_buf_rows(&g_tb, 2u, 4u), 1u);
    CHECK_EQ(text_buf_rows(&g_tb, 3u, 4u), 0u);
    /* Every position of a line maps to its cell and back. */
    for (size_t pos = 5u; pos < 15u; ++pos) {
        text_buf_cell_of(&g_tb, pos, 4u, &line, &cell);
        CHECK_EQ(text_buf_pos_at(&g_tb, line, cell, 4u), pos);
    }
    text_buf_cell_of(&g_tb, 999u, 4u, &line, &cell);
    CHECK_EQ(line, 2u);
    CHECK_EQ(cell, 0u);
}

/* --- the index after edits ---------------------------------------------- */

/* A random edit at a random place, many times: the index, the kinds and
 * the lines equal a fresh load of what the buffer serialises. */
TEST_CASE(the_index_after_random_edits_is_a_fresh_loads)
{
    static const uint32_t k_cps[] = {'a', 'b', ' ', '\t', 0xE4u, 0x20ACu,
                                     0x1F600u};
    uint32_t seed = 0xC0FFEEu;
    text_buf_t ref;
    uint8_t *ref_mem = malloc(4096u);
    uint32_t *ref_ends = malloc(4096u * sizeof(uint32_t));
    REQUIRE_ALLOC(ref_mem);
    REQUIRE_ALLOC(ref_ends);
    /* One file of each kind of line end, each with the other two in it,
     * so ENTER puts in LF, CR LF and CR beside lone CRs and LFs. */
    static const char *const k_files[3] = {
        "a\r\nb\nc\rd\r\r\n\n\re\xC3\xA4\x80" "f\n",
        "a\nb\r\rc\r\nd\n\r\n\r",
        "a\rb\n\nc\r\n\r\rd\xF0\x9F\x98\x80",
    };
    for (int op = 0; op < 12000; ++op) {
        if (op % 4000 == 0) {
            CHECK_EQ(loads(k_files[op / 4000]), TEXT_BUF_OK);
        }
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        text_buf_set_cursor(&g_tb, seed % (text_buf_len(&g_tb) + 1u));
        switch ((seed >> 12) % 6u) {
        case 0:
            (void)text_buf_insert_eol(&g_tb);
            break;
        case 1:
            (void)text_buf_delete_before(&g_tb);
            break;
        case 2:
            (void)text_buf_delete_after(&g_tb);
            break;
        default:
            (void)text_buf_insert(&g_tb, k_cps[(seed >> 16) % 7u]);
            break;
        }
        if (text_buf_len(&g_tb) > 3000u) {
            text_buf_set_cursor(&g_tb, 0u);
            for (int k = 0; k < 1500; ++k) {
                (void)text_buf_delete_after(&g_tb);
            }
        }
        const size_t n = serial();
        CHECK_EQ(text_buf_init(&ref, ref_mem, 4096u, ref_ends, 4096u),
                 TEXT_BUF_OK);
        CHECK_EQ(text_buf_load(&ref, g_out, n), TEXT_BUF_OK);
        int bad = text_buf_lines(&ref) != text_buf_lines(&g_tb) ||
                  text_buf_mixed(&ref) != text_buf_mixed(&g_tb) ||
                  !text_buf_is_unit(&g_tb, text_buf_cursor(&g_tb));
        for (size_t k = 0u; k < TEXT_EOL_KINDS; ++k) {
            bad |= ref.kinds[k] != g_tb.kinds[k];
        }
        for (size_t l = 0u; !bad && l < text_buf_lines(&ref); ++l) {
            bad |= text_buf_line_start(&ref, l) !=
                       text_buf_line_start(&g_tb, l) ||
                   text_buf_line_len(&ref, l) != text_buf_line_len(&g_tb, l);
        }
        if (bad) {
            T_FAIL("op %d: the index differs from a fresh load", op);
            break;
        }
    }
    free(ref_mem);
    free(ref_ends);
}

/* --- state, serialising, refusals --------------------------------------- */

TEST_CASE(dirty_and_the_revision)
{
    CHECK_EQ(loads("abc"), TEXT_BUF_OK);
    const uint32_t r0 = text_buf_revision(&g_tb);
    CHECK(!text_buf_dirty(&g_tb));
    CHECK(text_buf_right(&g_tb));
    CHECK_EQ(text_buf_revision(&g_tb), r0);
    CHECK_EQ(text_buf_insert(&g_tb, 'x'), TEXT_BUF_OK);
    CHECK(text_buf_dirty(&g_tb));
    CHECK(text_buf_revision(&g_tb) != r0);
    text_buf_saved(&g_tb);
    CHECK(!text_buf_dirty(&g_tb));
    /* Reading a line moves the gap and nothing else. */
    const uint32_t r1 = text_buf_revision(&g_tb);
    size_t len = 0u;
    (void)text_buf_line(&g_tb, 0u, &len);
    CHECK_EQ(text_buf_revision(&g_tb), r1);
    CHECK(!text_buf_dirty(&g_tb));
    /* The revision wraps and still changes. */
    g_tb.revision = UINT32_MAX;
    CHECK(text_buf_delete_before(&g_tb));
    CHECK_EQ(text_buf_revision(&g_tb), 0u);
    CHECK(same("abc"));
    /* A load, taken or refused, moves it on and clears dirty. */
    const uint32_t r2 = text_buf_revision(&g_tb);
    CHECK_EQ(text_buf_load(&g_tb, "\0", 1u), TEXT_BUF_NOT_TEXT);
    CHECK(text_buf_revision(&g_tb) != r2);
    CHECK(!text_buf_dirty(&g_tb));
}

/* What every fixture of this suite loads, it serialises unchanged. */
TEST_CASE(serialise_equals_load)
{
    static const char *const k[] = {
        "", "x", "a\nb\n", "a\r\nb\r\n", "a\rb", "a\r\nb\nc\rd",
        "\xEF\xBB\xBFx\r\n", "\xEF\xBB\xBF", "\t\tx\t", "\xFF\xC3\x80",
        "\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F\n",
        "\r\r\n\n\r",
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(loads(k[i]), TEXT_BUF_OK);
        if (!same(k[i])) {
            T_FAIL("fixture %zu does not serialise as loaded", i);
        }
    }
    /* After edits that undo each other. */
    CHECK_EQ(loads("a\r\nb"), TEXT_BUF_OK);
    text_buf_set_cursor(&g_tb, 3u);
    CHECK_EQ(text_buf_insert(&g_tb, 0xDFu), TEXT_BUF_OK);
    CHECK(text_buf_delete_before(&g_tb));
    CHECK(same("a\r\nb"));
}

/* The in-place load: a loader writes into the area, a failed one leaves
 * an empty text. */
TEST_CASE(a_load_in_place_and_its_abort)
{
    fresh(32u, 4u);
    CHECK_EQ(text_buf_load(&g_tb, "old", 3u), TEXT_BUF_OK);
    uint8_t *area = text_buf_load_area(&g_tb);
    CHECK(area == g_mem);
    CHECK_EQ(text_buf_len(&g_tb), 0u);
    CHECK_EQ(text_buf_lines(&g_tb), 1u);
    memcpy(area, "new\nfile", 8u);
    CHECK_EQ(text_buf_load_done(&g_tb, 8u), TEXT_BUF_OK);
    CHECK(same("new\nfile"));
    (void)text_buf_load_area(&g_tb);
    CHECK_EQ(text_buf_load_done(&g_tb, 33u), TEXT_BUF_TOO_LARGE);
    CHECK(same(""));
}

/* Every call takes NULL without a crash. */
TEST_CASE(null_arguments_are_refused)
{
    uint8_t m[4];
    uint32_t e[2];
    size_t len = 0u;
    CHECK_EQ(text_buf_init(NULL, m, 4u, e, 2u), TEXT_BUF_ARG);
    CHECK_EQ(text_buf_init(&g_tb, NULL, 4u, e, 2u), TEXT_BUF_ARG);
    CHECK_EQ(text_buf_init(&g_tb, m, 4u, NULL, 2u), TEXT_BUF_ARG);
    CHECK_EQ(text_buf_load(NULL, "a", 1u), TEXT_BUF_ARG);
    CHECK_EQ(text_buf_load_done(NULL, 1u), TEXT_BUF_ARG);
    CHECK(text_buf_load_area(NULL) == NULL);
    CHECK_EQ(text_buf_room(NULL), 0u);
    CHECK_EQ(text_buf_size(NULL), 0u);
    CHECK_EQ(text_buf_len(NULL), 0u);
    CHECK_EQ(text_buf_lines(NULL), 0u);
    CHECK_EQ(text_buf_byte(NULL, 0u), 0u);
    CHECK_EQ(text_buf_read(NULL, 0u, m, 4u), 0u);
    CHECK_EQ(text_buf_line_start(NULL, 1u), 0u);
    CHECK_EQ(text_buf_line_len(NULL, 0u), 0u);
    CHECK(text_buf_line(NULL, 0u, &len) == NULL);
    CHECK_EQ(text_buf_line_of(NULL, 3u), 0u);
    CHECK_EQ(text_buf_cp(NULL, 0u, &len), 0u);
    CHECK_EQ(len, 0u);
    CHECK_EQ(text_buf_next(NULL, 3u), 0u);
    CHECK_EQ(text_buf_prev(NULL, 3u), 0u);
    CHECK(!text_buf_is_unit(NULL, 0u));
    CHECK_EQ(text_buf_rows(NULL, 0u, 8u), 0u);
    CHECK_EQ(text_buf_pos_at(NULL, 0u, 0u, 8u), 0u);
    size_t line = 7u, cell = 7u;
    text_buf_cell_of(NULL, 0u, 8u, &line, &cell);
    CHECK_EQ(line, 0u);
    CHECK_EQ(cell, 0u);
    CHECK_EQ(text_buf_cursor(NULL), 0u);
    text_buf_set_cursor(NULL, 1u);
    CHECK(!text_buf_left(NULL));
    CHECK(!text_buf_right(NULL));
    CHECK_EQ(text_buf_insert(NULL, 'a'), TEXT_BUF_ARG);
    CHECK_EQ(text_buf_insert_eol(NULL), TEXT_BUF_ARG);
    CHECK(!text_buf_delete_before(NULL));
    CHECK(!text_buf_delete_after(NULL));
    CHECK_EQ(text_buf_eol(NULL), TEXT_EOL_LF);
    CHECK(!text_buf_mixed(NULL));
    CHECK(!text_buf_bom(NULL));
    CHECK(!text_buf_dirty(NULL));
    text_buf_saved(NULL);
    CHECK_EQ(text_buf_revision(NULL), 0u);
    CHECK_EQ(loads("ab"), TEXT_BUF_OK);
    CHECK_EQ(text_buf_load(&g_tb, NULL, 1u), TEXT_BUF_ARG);
    CHECK_EQ(text_buf_read(&g_tb, 0u, NULL, 1u), 0u);
    CHECK(text_buf_line(&g_tb, 0u, NULL) == NULL);
    CHECK(!text_buf_is_unit(&g_tb, 3u));
    text_buf_set_cursor(&g_tb, 99u);
    CHECK_EQ(text_buf_cursor(&g_tb), 2u);
    CHECK_EQ(text_buf_next(&g_tb, 99u), 2u);
    CHECK_EQ(text_buf_prev(&g_tb, 99u), 1u);
    CHECK_EQ(text_buf_cp(&g_tb, 2u, NULL), 0u);
}

int main(void)
{
    RUN(an_empty_file_is_one_empty_line);
    RUN(one_byte_and_no_final_line_end);
    RUN(a_file_of_the_cap_loads_and_one_byte_more_does_not);
    RUN(the_bom_counts_in_the_cap);
    RUN(typing_fills_the_gap_to_the_cap);
    RUN(sixty_five_thousand_lines_are_the_most);
    RUN(a_long_line_wraps);
    RUN(each_kind_of_line_end_is_found_and_kept);
    RUN(enter_inserts_the_files_line_end_and_always_adds_a_line);
    RUN(deletes_join_line_ends);
    RUN(the_bom_is_kept_and_takes_no_cell);
    RUN(a_nul_in_the_first_4_kib_is_no_text);
    RUN(a_code_point_is_stepped_and_deleted_whole);
    RUN(the_german_letters_are_typed);
    RUN(a_code_point_no_text_holds_is_refused);
    RUN(a_malformed_byte_is_one_cell);
    RUN(the_units_are_the_cells_gfx_draws);
    RUN(a_tab_reaches_the_next_stop);
    RUN(a_line_wraps_at_the_width);
    RUN(the_index_after_random_edits_is_a_fresh_loads);
    RUN(dirty_and_the_revision);
    RUN(serialise_equals_load);
    RUN(a_load_in_place_and_its_abort);
    RUN(null_arguments_are_refused);
    free(g_mem);
    free(g_ends);
    free(g_out);
    return test_summary("text_buf");
}
