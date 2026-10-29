/**
 * @file test_wa_api_file.c
 * @brief /api/file of the Wi-Fi page's file API (wa_api.h) over a card in
 *        memory (fm_fake.h): a text file sent byte for byte with its ETag,
 *        line ends, BOM and line count; the viewer's limits at their edges
 *        (1MiB, 65,536 line ends, a NUL at byte 4,095 and 4,096); the
 *        rules' refusals; and a card that fails, goes or is swapped, and a
 *        radio that goes off, part way through a load.
 *
 * Cases:
 * - a_file_is_sent_byte_for_byte_with_its_etag_and_head
 * - line_ends_and_a_bom_are_named_and_kept
 * - one_mib_is_sent_and_one_byte_more_is_refused
 * - line_ends_65536_are_sent_and_65537_refused
 * - a_nul_in_the_first_4096_bytes_is_not_text
 * - locked_busy_folders_and_missing_names_are_refused
 * - no_room_is_refused_before_any_card_call
 * - a_card_that_fails_or_goes_mid_load_leaves_nothing_open
 * - the_radio_going_off_stops_a_load_at_its_next_chunk
 * - a_head_that_does_not_fit_is_not_written
 *
 * SPDX-License-Identifier: MIT
 */
#include <inttypes.h>
#include <stdio.h>

#include "wa_api_rig.h"

#include "card_tree.h"
#include "text_buf.h"
#include "wa_zip.h"

/** @brief The file room: the viewer's 1MiB, and one byte to spare. */
static uint8_t *g_text;
/** @brief The line index: the viewer's 65,536 entries. */
static uint32_t *g_ends;
/** @brief A file's bytes as the case makes them. */
static uint8_t *g_bytes;
/** @brief What wa_api_file_read() gives back. */
static uint8_t *g_back;

/** @brief A fresh card and the API with the viewer's room. */
static void fresh(void)
{
    rig_fresh();
    if (g_text == NULL) {
        g_text = malloc(TEXT_BUF_FILE_MAX);
        g_ends = malloc(TEXT_BUF_ENDS_MAX * sizeof(g_ends[0]));
        g_bytes = malloc(TEXT_BUF_FILE_MAX + 1u);
        g_back = malloc(TEXT_BUF_FILE_MAX + 1u);
        REQUIRE_ALLOC(g_text);
        REQUIRE_ALLOC(g_ends);
        REQUIRE_ALLOC(g_bytes);
        REQUIRE_ALLOC(g_back);
    }
    wa_api_set_text(&g_api, g_text, TEXT_BUF_FILE_MAX, g_ends,
                    TEXT_BUF_ENDS_MAX);
}

/**
 * @brief Read the loaded file back through wa_api_file_read(), in pieces
 *        of @p piece bytes.
 * @param piece  bytes per call
 * @return the bytes read, in g_back
 */
static size_t read_back(size_t piece)
{
    size_t at = 0u;
    for (;;) {
        const size_t got = wa_api_file_read(&g_api, at, g_back + at, piece);
        if (got == 0u) {
            return at;
        }
        at += got;
    }
}

/**
 * @brief Whether the loaded file reads back as @p n bytes of @p want.
 * @param want  the bytes
 * @param n     how many
 * @return true when equal
 */
static bool sends(const uint8_t *want, size_t n)
{
    return wa_api_file_size(&g_api) == n && read_back(4096u) == n
           && memcmp(g_back, want, n) == 0;
}

/**
 * @brief The file answer's head.
 * @return the text, in a static buffer
 */
static const char *head(void)
{
    static char h[WA_API_HEAD_MAX];
    CHECK(wa_api_file_head(&g_api, h, sizeof(h)) > 0u);
    return h;
}

TEST_CASE(a_file_is_sent_byte_for_byte_with_its_etag_and_head)
{
    fresh();
    static const char k_kst[] = "# MG90S\nPULSE MIN=900\nPULSE MAX=2100\n";
    fk_file(&g_card, "BENCH/KST/CONF/MG90S.TXT", (const uint8_t *)k_kst,
            sizeof(k_kst) - 1u);
    CHECK_EQ(wa_api_file(&g_api, "path=BENCH/KST/CONF/MG90S.TXT&mount=1"),
             WA_API_OK);
    CHECK(sends((const uint8_t *)k_kst, sizeof(k_kst) - 1u));
    const uint32_t crc = wa_crc32(0u, k_kst, sizeof(k_kst) - 1u);
    CHECK_EQ(g_api.etag, crc);
    char want[WA_API_HEAD_MAX];
    snprintf(want, sizeof(want),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain; charset=utf-8\r\n"
             "Content-Length: 37\r\n"
             "ETag: \"%08" PRIx32 "\"\r\n"
             "X-Rcbench-Eol: lf\r\n"
             "X-Rcbench-Bom: 0\r\n"
             "X-Rcbench-Lines: 4\r\n"
             "Cache-Control: no-store\r\n\r\n", crc);
    CHECK_STR_EQ(head(), want);
    /* A piece of 1 byte reads the same bytes. */
    CHECK_EQ(read_back(1u), sizeof(k_kst) - 1u);
    CHECK_EQ(memcmp(g_back, k_kst, sizeof(k_kst) - 1u), 0);
    /* The CRC is known: "123456789" is CBF43926. */
    fk_file(&g_card, "CHECK.TXT", (const uint8_t *)"123456789", 9u);
    CHECK_EQ(wa_api_file(&g_api, "path=CHECK.TXT"), WA_API_OK);
    CHECK(has(head(), "ETag: \"cbf43926\"\r\n"));
    /* An empty file: no bytes, one line. */
    fk_file(&g_card, "EMPTY.TXT", NULL, 0u);
    CHECK_EQ(wa_api_file(&g_api, "path=EMPTY.TXT"), WA_API_OK);
    CHECK_EQ(wa_api_file_size(&g_api), 0u);
    CHECK(has(head(), "Content-Length: 0\r\nETag: \"00000000\"\r\n"));
    CHECK(has(head(), "X-Rcbench-Lines: 1\r\n"));
    /* The fault log is read like any text. */
    fk_file(&g_card, CARD_FAULT_LOG, (const uint8_t *)"E1\n", 3u);
    CHECK_EQ(wa_api_file(&g_api, "path=BENCH/RCBENCH.LOG"), WA_API_OK);
    CHECK_EQ(g_card.holds, 0);
    CHECK_EQ(fk_open_files(&g_card), 0);
}

TEST_CASE(line_ends_and_a_bom_are_named_and_kept)
{
    fresh();
    static const struct {
        const char *bytes;
        const char *eol;
        const char *lines;
    } k_ends[] = {
        { "a\r\nb\r\n", "crlf", "3" },
        { "a\rb", "cr", "2" },
        { "a\nb\r\nc", "mixed", "3" },
        { "no end", "lf", "1" },
    };
    char h[96];
    char name[16];
    char q[32];
    for (size_t i = 0u; i < sizeof(k_ends) / sizeof(k_ends[0]); ++i) {
        const size_t n = strlen(k_ends[i].bytes);
        snprintf(name, sizeof(name), "E%u.TXT", (unsigned)(i % 10u));
        snprintf(q, sizeof(q), "path=%s", name);
        fk_file(&g_card, name, (const uint8_t *)k_ends[i].bytes, n);
        CHECK_EQ(wa_api_file(&g_api, q), WA_API_OK);
        CHECK(sends((const uint8_t *)k_ends[i].bytes, n));
        snprintf(h, sizeof(h), "X-Rcbench-Eol: %s\r\n", k_ends[i].eol);
        CHECK(has(head(), h));
        snprintf(h, sizeof(h), "X-Rcbench-Lines: %s\r\n", k_ends[i].lines);
        CHECK(has(head(), h));
    }
    /* A BOM is sent where it is and counted in the length and the CRC. */
    static const uint8_t k_bom[] = { 0xEF, 0xBB, 0xBF, 'x', '\r', '\n' };
    fk_file(&g_card, "B.TXT", k_bom, sizeof(k_bom));
    CHECK_EQ(wa_api_file(&g_api, "path=B.TXT"), WA_API_OK);
    CHECK(sends(k_bom, sizeof(k_bom)));
    CHECK(has(head(), "Content-Length: 6\r\n"));
    CHECK(has(head(), "X-Rcbench-Bom: 1\r\n"));
    CHECK_EQ(g_api.etag, wa_crc32(0u, k_bom, sizeof(k_bom)));
    /* A BOM read in pieces of 2 bytes comes back whole. */
    CHECK_EQ(read_back(2u), sizeof(k_bom));
    CHECK_EQ(memcmp(g_back, k_bom, sizeof(k_bom)), 0);
}

TEST_CASE(one_mib_is_sent_and_one_byte_more_is_refused)
{
    fresh();
    for (size_t i = 0u; i <= TEXT_BUF_FILE_MAX; ++i) {
        g_bytes[i] = (uint8_t)('a' + (i % 26u));
    }
    fk_file(&g_card, "BIG.TXT", g_bytes, TEXT_BUF_FILE_MAX);
    CHECK_EQ(wa_api_file(&g_api, "path=BIG.TXT"), WA_API_OK);
    CHECK(sends(g_bytes, TEXT_BUF_FILE_MAX));
    CHECK(has(head(), "Content-Length: 1048576\r\n"));
    CHECK_EQ(g_api.etag, wa_crc32(0u, g_bytes, TEXT_BUF_FILE_MAX));
    fk_file(&g_card, "BIG2.TXT", g_bytes, TEXT_BUF_FILE_MAX + 1u);
    const long reads = g_card.calls[FK_READ];
    CHECK_EQ(wa_api_file(&g_api, "path=BIG2.TXT"), WA_API_TOO_BIG);
    /* Refused from its size, before a byte is read. */
    CHECK_EQ(g_card.calls[FK_READ], reads);
    CHECK_EQ(wa_api_file_size(&g_api), 0u);
    CHECK_EQ(fk_open_files(&g_card), 0);
    /* A room smaller than 1MiB is the limit then. */
    wa_api_set_text(&g_api, g_text, 100u, g_ends, TEXT_BUF_ENDS_MAX);
    fk_file(&g_card, "S.TXT", g_bytes, 100u);
    CHECK_EQ(wa_api_file(&g_api, "path=S.TXT"), WA_API_OK);
    fk_file(&g_card, "S2.TXT", g_bytes, 101u);
    CHECK_EQ(wa_api_file(&g_api, "path=S2.TXT"), WA_API_TOO_BIG);
}

TEST_CASE(line_ends_65536_are_sent_and_65537_refused)
{
    fresh();
    memset(g_bytes, '\n', TEXT_BUF_ENDS_MAX + 1u);
    fk_file(&g_card, "L.TXT", g_bytes, TEXT_BUF_ENDS_MAX);
    CHECK_EQ(wa_api_file(&g_api, "path=L.TXT"), WA_API_OK);
    CHECK(has(head(), "X-Rcbench-Lines: 65537\r\n"));
    CHECK(sends(g_bytes, TEXT_BUF_ENDS_MAX));
    fk_file(&g_card, "L2.TXT", g_bytes, TEXT_BUF_ENDS_MAX + 1u);
    CHECK_EQ(wa_api_file(&g_api, "path=L2.TXT"), WA_API_TOO_MANY_LINES);
    CHECK_EQ(wa_api_file_size(&g_api), 0u);
    /* CR LF is one line end. */
    for (size_t i = 0u; i < TEXT_BUF_ENDS_MAX; ++i) {
        g_bytes[2u * i] = '\r';
        g_bytes[2u * i + 1u] = '\n';
    }
    fk_file(&g_card, "L3.TXT", g_bytes, 2u * TEXT_BUF_ENDS_MAX);
    CHECK_EQ(wa_api_file(&g_api, "path=L3.TXT"), WA_API_OK);
    CHECK(has(head(), "X-Rcbench-Eol: crlf\r\n"));
}

TEST_CASE(a_nul_in_the_first_4096_bytes_is_not_text)
{
    fresh();
    memset(g_bytes, 'x', 8192u);
    g_bytes[TEXT_BUF_PROBE - 1u] = '\0';
    fk_file(&g_card, "N.TXT", g_bytes, 8192u);
    CHECK_EQ(wa_api_file(&g_api, "path=N.TXT"), WA_API_NOT_TEXT);
    CHECK_EQ(wa_api_file_size(&g_api), 0u);
    g_bytes[TEXT_BUF_PROBE - 1u] = 'x';
    g_bytes[TEXT_BUF_PROBE] = '\0';
    fk_file(&g_card, "N2.TXT", g_bytes, 8192u);
    CHECK_EQ(wa_api_file(&g_api, "path=N2.TXT"), WA_API_OK);
    CHECK(sends(g_bytes, 8192u));
    /* At the first byte too. */
    fk_file(&g_card, "N3.TXT", (const uint8_t *)"\0a", 2u);
    CHECK_EQ(wa_api_file(&g_api, "path=N3.TXT"), WA_API_NOT_TEXT);
}

TEST_CASE(locked_busy_folders_and_missing_names_are_refused)
{
    fresh();
    fk_file(&g_card, "BENCH/KST/CONF/KST.TXT", NULL, 40u);
    fk_file(&g_card, "BENCH/HITEC/CONF/HITEC.TXT", NULL, 40u);
    fk_file(&g_card, "BENCH/LOGS/SERVO005.CSV", NULL, 10u);
    const long opens = g_card.calls[FK_OPEN];
    CHECK_EQ(wa_api_file(&g_api, "path=BENCH/KST/CONF/KST.TXT"),
             WA_API_LOCKED);
    CHECK_EQ(wa_api_file(&g_api, "path=bench/hitec/conf/hitec.txt"),
             WA_API_LOCKED);
    g_marks.run_dir = CARD_AUTO_LOGS;
    g_marks.run = 5u;
    CHECK_EQ(wa_api_file(&g_api, "path=BENCH/LOGS/SERVO005.CSV"),
             WA_API_BUSY);
    CHECK_EQ(g_card.calls[FK_OPEN], opens);
    g_marks.run = 0u;
    CHECK_EQ(wa_api_file(&g_api, "path=BENCH/LOGS/SERVO005.CSV"),
             WA_API_OK);
    /* A folder is not opened as a file; a name not there and the root
     * are refused too. */
    CHECK_EQ(wa_api_file(&g_api, "path=BENCH/LOGS"), WA_API_ARG);
    CHECK_EQ(wa_api_file(&g_api, "path=NONE.TXT"), WA_API_NOT_FOUND);
    CHECK_EQ(wa_api_file(&g_api, "path="), WA_API_REQUEST);
    CHECK_EQ(wa_api_file(&g_api, "path=A.TXT&mount=x"), WA_API_REQUEST);
    /* ARMED does not stop a read. */
    g_armed = true;
    CHECK_EQ(wa_api_file(&g_api, "path=BENCH/LOGS/SERVO005.CSV"),
             WA_API_OK);
    CHECK_EQ(g_card.holds, 0);
    CHECK_EQ(fk_open_files(&g_card), 0);
}

TEST_CASE(no_room_is_refused_before_any_card_call)
{
    rig_fresh();
    fk_file(&g_card, "A.TXT", NULL, 3u);
    CHECK_EQ(wa_api_file(&g_api, "path=A.TXT"), WA_API_MEMORY);
    wa_api_set_text(&g_api, NULL, 100u, NULL, 100u);
    CHECK_EQ(wa_api_file(&g_api, "path=A.TXT"), WA_API_MEMORY);
    static uint8_t room[16];
    wa_api_set_text(&g_api, room, sizeof(room), NULL, 4u);
    CHECK_EQ(wa_api_file(&g_api, "path=A.TXT"), WA_API_MEMORY);
    CHECK_EQ(g_card.calls[FK_ANY], 0);
    CHECK_EQ(wa_api_file_size(&g_api), 0u);
    CHECK_EQ(wa_api_file_size(NULL), 0u);
    CHECK_EQ(wa_api_file_read(NULL, 0u, room, sizeof(room)), 0u);
}

TEST_CASE(a_card_that_fails_or_goes_mid_load_leaves_nothing_open)
{
    fresh();
    memset(g_bytes, 'y', 3u * FM_CHUNK);
    fk_file(&g_card, "R.TXT", g_bytes, 3u * FM_CHUNK);
    fk_fail(&g_card, FK_READ, 2, FM_ERR_IO);
    CHECK_EQ(wa_api_file(&g_api, "path=R.TXT"), WA_API_IO);
    CHECK_EQ(wa_api_file_size(&g_api), 0u);
    CHECK_EQ(fk_open_files(&g_card), 0);
    CHECK_EQ(g_card.holds, 0);
    fk_fail(&g_card, FK_READ, 2, FM_ERR_CARD_GONE);
    CHECK_EQ(wa_api_file(&g_api, "path=R.TXT"), WA_API_CARD_GONE);
    CHECK_EQ(fk_open_files(&g_card), 0);
    CHECK_EQ(g_card.holds, 0);
    /* Another card since the listing: refused, nothing read. */
    fk_insert(&g_card);
    const long reads = g_card.calls[FK_READ];
    CHECK_EQ(wa_api_file(&g_api, "path=R.TXT&mount=1"), WA_API_CARD_GONE);
    CHECK_EQ(g_card.calls[FK_READ], reads);
    CHECK_EQ(wa_api_file(&g_api, "path=R.TXT&mount=2"), WA_API_OK);
    fk_pull(&g_card);
    CHECK_EQ(wa_api_file(&g_api, "path=R.TXT"), WA_API_NO_CARD);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(the_radio_going_off_stops_a_load_at_its_next_chunk)
{
    fresh();
    memset(g_bytes, 'z', 3u * FM_CHUNK);
    fk_file(&g_card, "R.TXT", g_bytes, 3u * FM_CHUNK);
    /* The runner says CANCEL from its second bench() call on: the load
     * stops before its second chunk. */
    g_cancel_after = 2;
    const long reads = g_card.calls[FK_READ];
    CHECK_EQ(wa_api_file(&g_api, "path=R.TXT"), WA_API_CANCELLED);
    CHECK(g_card.calls[FK_READ] - reads <= 1);
    CHECK_EQ(wa_api_file_size(&g_api), 0u);
    CHECK_EQ(fk_open_files(&g_card), 0);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(a_head_that_does_not_fit_is_not_written)
{
    fresh();
    fk_file(&g_card, "A.TXT", (const uint8_t *)"abc", 3u);
    CHECK_EQ(wa_api_file(&g_api, "path=A.TXT"), WA_API_OK);
    char h[WA_API_HEAD_MAX];
    const size_t n = wa_api_file_head(&g_api, h, sizeof(h));
    CHECK(n > 0u && n < sizeof(h));
    CHECK_EQ(wa_api_file_head(&g_api, h, n), 0u);
    CHECK_EQ(wa_api_file_head(&g_api, h, n + 1u), n);
}

int main(void)
{
    RUN(a_file_is_sent_byte_for_byte_with_its_etag_and_head);
    RUN(line_ends_and_a_bom_are_named_and_kept);
    RUN(one_mib_is_sent_and_one_byte_more_is_refused);
    RUN(line_ends_65536_are_sent_and_65537_refused);
    RUN(a_nul_in_the_first_4096_bytes_is_not_text);
    RUN(locked_busy_folders_and_missing_names_are_refused);
    RUN(no_room_is_refused_before_any_card_call);
    RUN(a_card_that_fails_or_goes_mid_load_leaves_nothing_open);
    RUN(the_radio_going_off_stops_a_load_at_its_next_chunk);
    RUN(a_head_that_does_not_fit_is_not_written);
    fk_free(&g_card);
    free(g_json);
    free(g_text);
    free(g_ends);
    free(g_bytes);
    free(g_back);
    return test_summary("wa_api_file");
}
