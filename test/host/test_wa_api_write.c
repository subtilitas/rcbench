/**
 * @file test_wa_api_write.c
 * @brief The writes of the Wi-Fi page's file API (wa_api.h) over a card in
 *        memory (fm_fake.h): PUT of a file new and replaced, its limits at
 *        their edges (1MiB, 65,536 line ends, a NUL at byte 4,095 and
 *        4,096), If-Match with the ETag a read gave and with a stale one,
 *        If-None-Match: *, a card pulled and the bench switched to ARMED
 *        part way; MKDIR, RENAME and its move, DELETE with their
 *        refusals; and the answer's JSON (JavaScript Object Notation).
 *
 * Cases:
 * - put_makes_a_new_file_and_answers_its_etag
 * - put_of_1mib_is_saved_and_one_byte_more_refused
 * - put_holds_the_viewers_limits_before_any_card_call
 * - put_with_the_read_etag_replaces_and_a_stale_one_is_412
 * - put_refused_changed_answers_the_etag_it_found
 * - put_if_none_match_refuses_a_taken_name
 * - put_headers_and_names_not_taken_are_refused
 * - put_into_a_locked_busy_or_missing_place_is_refused
 * - a_card_pulled_mid_put_leaves_the_old_file
 * - armed_or_the_radio_going_off_stop_a_put
 * - a_put_without_its_body_room_is_memory
 * - mkdir_makes_a_folder_once
 * - rename_renames_in_place_and_moves_by_folder
 * - rename_refusals
 * - delete_takes_a_file_a_run_with_its_report_and_an_empty_folder
 * - delete_refusals
 * - a_write_from_another_card_is_card_gone
 * - a_saved_esc_profile_carries_its_note
 *
 * SPDX-License-Identifier: MIT
 */
#include <inttypes.h>
#include <stdio.h>

#include "wa_api_rig.h"

#include "card_tree.h"
#include "text_buf.h"
#include "wa_zip.h"

/** @brief The file room: the viewer's 1MiB. */
static uint8_t *g_text;
/** @brief The line index: the viewer's 65,536 entries. */
static uint32_t *g_ends;

/** @brief A fresh card and the API with the viewer's room. */
static void fresh(void)
{
    rig_fresh();
    if (g_text == NULL) {
        g_text = malloc(TEXT_BUF_FILE_MAX);
        g_ends = malloc(TEXT_BUF_ENDS_MAX * sizeof(g_ends[0]));
        REQUIRE_ALLOC(g_text);
        REQUIRE_ALLOC(g_ends);
    }
    wa_api_set_text(&g_api, g_text, TEXT_BUF_FILE_MAX, g_ends,
                    TEXT_BUF_ENDS_MAX);
}

/**
 * @brief PUT @p n bytes of @p data with the query and headers given.
 * @param query  the query string
 * @param im     If-Match, or NULL
 * @param inm    If-None-Match, or NULL
 * @param data   the body
 * @param n      its bytes
 * @return the API's answer
 */
static wa_api_err_t put(const char *query, const char *im, const char *inm,
                        const void *data, size_t n)
{
    size_t cap = 0u;
    uint8_t *body = wa_api_body(&g_api, &cap);
    CHECK(body != NULL);
    CHECK(n <= cap || n == cap + 1u);
    if (n > 0u) {
        memcpy(body, data, (n <= cap) ? n : cap);
    }
    return wa_api_put(&g_api, query, im, inm, n);
}

/**
 * @brief PUT a short text.
 * @param query  the query string
 * @param im     If-Match, or NULL
 * @param inm    If-None-Match, or NULL
 * @param text   the body, a C string
 * @return the API's answer
 */
static wa_api_err_t put_s(const char *query, const char *im, const char *inm,
                          const char *text)
{
    return put(query, im, inm, text, strlen(text));
}

/**
 * @brief Whether file @p path holds the C string @p want.
 * @param path  the file
 * @param want  the bytes
 * @return true when it does
 */
static bool holds(const char *path, const char *want)
{
    const fk_node_t *n = fk_get(&g_card, path);
    return n != NULL && !n->dir && n->size == strlen(want)
           && (n->size == 0u || memcmp(n->data, want, n->size) == 0);
}

/**
 * @brief The answer's JSON.
 * @return the text
 */
static const char *answer(void)
{
    json_begin();
    wa_api_write_json(&g_api, &g_out);
    return json_end();
}

/**
 * @brief The ETag a GET of @p path gives, as If-Match carries it.
 * @param path  the file
 * @param out   takes "\"xxxxxxxx\"", 12 bytes
 */
static void etag_of_get(const char *path, char *out)
{
    char q[160];
    (void)snprintf(q, sizeof(q), "path=%s", path);
    CHECK_EQ(wa_api_file(&g_api, q), WA_API_OK);
    (void)snprintf(out, 12, "\"%08" PRIx32 "\"", g_api.etag);
}

/**
 * @brief Another writer's new bytes for file @p path, there already, as
 *        FILES on the panel would leave them.
 * @param path  the file
 * @param data  the bytes
 * @param n     their count
 */
static void rewrite(const char *path, const void *data, size_t n)
{
    fk_node_t *e = (fk_node_t *)fk_get(&g_card, path);
    REQUIRE_ALLOC(e);
    uint8_t *mem = malloc(n + 1u);
    REQUIRE_ALLOC(mem);
    memcpy(mem, data, n);
    free(e->data);
    e->data = mem;
    e->size = n;
    e->cap = n + 1u;
}

/** @brief Whether nothing is left open or held. @return true for clean */
static bool clean(void)
{
    return g_card.holds == 0 && fk_open_files(&g_card) == 0;
}

TEST_CASE(put_makes_a_new_file_and_answers_its_etag)
{
    fresh();
    fk_dir(&g_card, CARD_USER_LOGS);
    CHECK_EQ(put_s("path=USER/LOGS/N%201.TXT", NULL, "*", "a\r\nb\n"),
             WA_API_OK);
    CHECK(holds("USER/LOGS/N 1.TXT", "a\r\nb\n"));
    CHECK(g_api.created);
    CHECK_STR_EQ(wa_api_write_status(&g_api), "201 Created");
    char want[96];
    (void)snprintf(want, sizeof(want),
                   "{\"path\":\"USER/LOGS/N 1.TXT\",\"created\":true,"
                   "\"etag\":\"%08" PRIx32 "\",\"note\":\"\"}",
                   wa_crc32(0u, "a\r\nb\n", 5u));
    CHECK_STR_EQ(answer(), want);
    /* The ETag header carries the same CRC-32, quoted as a GET gives it. */
    char tag[WA_API_ETAG_SIZE];
    CHECK(wa_api_write_etag(&g_api, tag));
    (void)snprintf(want, sizeof(want), "\"%08" PRIx32 "\"",
                   wa_crc32(0u, "a\r\nb\n", 5u));
    CHECK_STR_EQ(tag, want);
    CHECK(fk_get(&g_card, "USER/LOGS/N 1.TXT.NEW") == NULL);
    CHECK(clean());
    /* An empty file: made, its ETag the CRC of nothing. */
    CHECK_EQ(put_s("path=EMPTY.TXT", NULL, "*", ""), WA_API_OK);
    CHECK(holds("EMPTY.TXT", ""));
    CHECK(has(answer(), "\"etag\":\"00000000\""));
    /* No header replaces a file there: 200, not created. */
    CHECK_EQ(put_s("path=EMPTY.TXT", NULL, NULL, "x"), WA_API_OK);
    CHECK(holds("EMPTY.TXT", "x"));
    CHECK(!g_api.created);
    CHECK_STR_EQ(wa_api_write_status(&g_api), "200 OK");
    CHECK(wa_api_write_etag(&g_api, tag));
    (void)snprintf(want, sizeof(want), "\"%08" PRIx32 "\"",
                   wa_crc32(0u, "x", 1u));
    CHECK_STR_EQ(tag, want);
    /* A write that is not a PUT carries no ETag. */
    CHECK_EQ(wa_api_mkdir(&g_api, "path=D"), WA_API_OK);
    CHECK(!wa_api_write_etag(&g_api, tag));
    CHECK_STR_EQ(tag, "");
}

/* FatFs reads "CONF." as "CONF", "\\" as "/", "0:" as a drive and
 * "KST~1.TXT" as a short name: each would reach an entry the rules judge
 * by another name.  Every write refuses them before a card call. */
TEST_CASE(a_path_alias_is_refused_by_every_write)
{
    fresh();
    fk_file(&g_card, "BENCH/KST/CONF/KST.TXT", (const uint8_t *)"j", 1u);
    fk_file(&g_card, "NOTE.TXT", (const uint8_t *)"n", 1u);
    char im[WA_API_ETAG_SIZE];
    (void)snprintf(im, sizeof(im), "\"%08" PRIx32 "\"",
                   wa_crc32(0u, "j", 1u));
    static const char *const alias[] = {
        "BENCH/KST/CONF./KST.TXT",   "BENCH/KST/CONF%20/KST.TXT",
        "%20BENCH/KST/CONF/KST.TXT", "BENCH/KST/CONF/KST.TXT.",
        "BENCH/KST/CONF/KST.TXT%20", "BENCH%5CKST%5CCONF%5CKST.TXT",
        "0%3A/BENCH/KST/CONF/KST.TXT", "BENCH/KST/CONF/KST~1.TXT",
        "BENCH/KST/CONF/LOG000~1.BFL", "BENCH/KST/CONF/K%01ST.TXT",
        "BENCH/./KST/CONF/KST.TXT",  "BENCH/KST/../KST/CONF/KST.TXT",
        "BENCH//KST/CONF/KST.TXT",   "BENCH/KST/CONF/KST.TXT/",
        "/BENCH/KST/CONF/KST.TXT",
        /* FatFs folds U+017F LONG S to "S", U+0131 DOTLESS I to "I". */
        "BENCH/K%C5%BFT/CONF/KST.TXT", "BENCH/KST/CONF/K%C5%BFT.TXT",
        "BENCH/H%C4%B1TEC/X.TXT",      "BENCH/KST/CONF/N%C3%84.TXT",
    };
    const long writes = g_card.calls[FK_WRITE];
    for (size_t i = 0u; i < sizeof(alias) / sizeof(alias[0]); ++i) {
        char q[160];
        (void)snprintf(q, sizeof(q), "path=%s", alias[i]);
        CHECK_EQ(put_s(q, im, NULL, "x"), WA_API_NAME);
        CHECK_EQ(put_s(q, NULL, NULL, "x"), WA_API_NAME);
        CHECK_EQ(wa_api_mkdir(&g_api, q), WA_API_NAME);
        (void)snprintf(q, sizeof(q), "path=%s&dir=0", alias[i]);
        CHECK_EQ(wa_api_delete(&g_api, q), WA_API_NAME);
        (void)snprintf(q, sizeof(q), "from=%s&to=BENCH/KST/CONF/K2.TXT",
                       alias[i]);
        CHECK_EQ(wa_api_rename(&g_api, q), WA_API_NAME);
        (void)snprintf(q, sizeof(q), "from=NOTE.TXT&to=%s", alias[i]);
        CHECK_EQ(wa_api_rename(&g_api, q), WA_API_NAME);
    }
    CHECK_EQ(g_card.calls[FK_WRITE], writes);
    CHECK(holds("BENCH/KST/CONF/KST.TXT", "j"));
    CHECK(holds("NOTE.TXT", "n"));
    CHECK(fk_get(&g_card, "BENCH/KST/CONF/K2.TXT") == NULL);
    /* Case is no alias: the rules fold it, so the record stays LOCKED. */
    CHECK_EQ(put_s("path=bench/kst/conf/kst.txt", im, NULL, "x"),
             WA_API_LOCKED);
    CHECK_EQ(wa_api_delete(&g_api, "path=Bench/Kst/Conf/Kst.Txt&dir=0"),
             WA_API_LOCKED);
    CHECK(holds("BENCH/KST/CONF/KST.TXT", "j"));
    /* A name with a "~" and no digit after it is a name. */
    CHECK_EQ(put_s("path=A~B.TXT", NULL, "*", "x"), WA_API_OK);
    CHECK(clean());
}

TEST_CASE(put_of_1mib_is_saved_and_one_byte_more_refused)
{
    fresh();
    uint8_t *big = malloc(TEXT_BUF_FILE_MAX);
    REQUIRE_ALLOC(big);
    for (size_t i = 0u; i < TEXT_BUF_FILE_MAX; ++i) {
        big[i] = (i % 64u == 63u) ? '\n' : (uint8_t)('A' + i % 26u);
    }
    CHECK_EQ(put("path=BIG.TXT", NULL, "*", big, TEXT_BUF_FILE_MAX),
             WA_API_OK);
    const fk_node_t *n = fk_get(&g_card, "BIG.TXT");
    CHECK(n != NULL && n->size == TEXT_BUF_FILE_MAX
          && memcmp(n->data, big, TEXT_BUF_FILE_MAX) == 0);
    CHECK_EQ(g_api.etag, wa_crc32(0u, big, TEXT_BUF_FILE_MAX));
    /* One byte more than the room: refused before any card call. */
    const long opens = g_card.calls[FK_OPEN];
    CHECK_EQ(put("path=BIG2.TXT", NULL, "*", big, TEXT_BUF_FILE_MAX + 1u),
             WA_API_TOO_BIG);
    CHECK_EQ(g_card.calls[FK_OPEN], opens);
    CHECK(fk_get(&g_card, "BIG2.TXT") == NULL);
    free(big);
}

TEST_CASE(put_holds_the_viewers_limits_before_any_card_call)
{
    fresh();
    static uint8_t buf[TEXT_BUF_PROBE + 8u];
    memset(buf, 'x', sizeof(buf));
    buf[TEXT_BUF_PROBE - 1u] = 0u;           /* a NUL at byte 4,095 */
    CHECK_EQ(put("path=A.TXT", NULL, NULL, buf, sizeof(buf)),
             WA_API_NOT_TEXT);
    CHECK_EQ(g_card.calls[FK_OPEN], 0);
    CHECK_EQ(g_card.calls[FK_STAT], 0);
    buf[TEXT_BUF_PROBE - 1u] = 'x';
    buf[TEXT_BUF_PROBE] = 0u;                /* at 4,096: text */
    CHECK_EQ(put("path=A.TXT", NULL, NULL, buf, sizeof(buf)), WA_API_OK);
    CHECK_EQ(fk_get(&g_card, "A.TXT")->size, sizeof(buf));
    /* 65,536 line ends are taken, 65,537 are not. */
    uint8_t *lines = malloc(TEXT_BUF_ENDS_MAX + 1u);
    REQUIRE_ALLOC(lines);
    memset(lines, '\n', TEXT_BUF_ENDS_MAX + 1u);
    CHECK_EQ(put("path=L.TXT", NULL, NULL, lines, TEXT_BUF_ENDS_MAX),
             WA_API_OK);
    const long opens = g_card.calls[FK_OPEN];
    CHECK_EQ(put("path=L2.TXT", NULL, NULL, lines, TEXT_BUF_ENDS_MAX + 1u),
             WA_API_TOO_MANY_LINES);
    CHECK_EQ(g_card.calls[FK_OPEN], opens);
    CHECK(fk_get(&g_card, "L2.TXT") == NULL);
    free(lines);
    CHECK(clean());
}

TEST_CASE(put_with_the_read_etag_replaces_and_a_stale_one_is_412)
{
    fresh();
    fk_dir(&g_card, CARD_KST_CONF);
    fk_file(&g_card, "BENCH/KST/CONF/MG90S.TXT", (const uint8_t *)"v1\n",
            3u);
    char tag[12];
    etag_of_get("BENCH/KST/CONF/MG90S.TXT", tag);
    /* FILES on the panel saves the file between the read and the PUT. */
    rewrite("BENCH/KST/CONF/MG90S.TXT", "v2\n", 3u);
    const long writes = g_card.calls[FK_WRITE];
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", tag, NULL, "web\n"),
             WA_API_CHANGED);
    CHECK_EQ(wa_api_http(WA_API_CHANGED), 412u);
    CHECK(holds("BENCH/KST/CONF/MG90S.TXT", "v2\n"));
    CHECK_EQ(g_card.calls[FK_WRITE], writes);
    CHECK(fk_get(&g_card, "BENCH/KST/CONF/MG90S.TXT.NEW") == NULL);
    CHECK(clean());
    /* Read again: the new ETag replaces it, and the answer's ETag is the
     * file's now. */
    etag_of_get("BENCH/KST/CONF/MG90S.TXT", tag);
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", tag, NULL, "web\n"),
             WA_API_OK);
    CHECK(holds("BENCH/KST/CONF/MG90S.TXT", "web\n"));
    CHECK(!g_api.created);
    char now[12];
    etag_of_get("BENCH/KST/CONF/MG90S.TXT", now);
    char want[32];
    (void)snprintf(want, sizeof(want), "\"etag\":%s", now);
    CHECK(has(answer(), want));
    /* The upper-case form of the same ETag is the same ETag. */
    for (char *c = now; *c != '\0'; ++c) {
        if (*c >= 'a' && *c <= 'f') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", now, NULL, "w2\n"),
             WA_API_OK);
    /* The file gone since the read: CHANGED, nothing made. */
    etag_of_get("BENCH/KST/CONF/MG90S.TXT", tag);
    fk_free(&g_card);
    fk_init(&g_card);
    fk_dir(&g_card, CARD_KST_CONF);
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", tag, NULL, "w3\n"),
             WA_API_CHANGED);
    CHECK(fk_get(&g_card, "BENCH/KST/CONF/MG90S.TXT") == NULL);
}

/**
 * @brief The refusal's JSON for @p e.
 * @param api  the API, or NULL
 * @param e    why it is refused
 * @return the text
 */
static const char *refusal(const wa_api_t *api, wa_api_err_t e)
{
    json_begin();
    wa_api_refused_json(api, &g_out, e, false);
    return json_end();
}

/*
 * A 412 carries the ETag of the file it found, the one a GET gives, and a
 * PUT with it replaces that file; one more change refuses it, with the new
 * ETag.  A file that is gone, and any other refusal, carry none.
 */
TEST_CASE(put_refused_changed_answers_the_etag_it_found)
{
    fresh();
    fk_dir(&g_card, CARD_KST_CONF);
    fk_file(&g_card, "BENCH/KST/CONF/MG90S.TXT", (const uint8_t *)"v1\n",
            3u);
    char tag[12];
    etag_of_get("BENCH/KST/CONF/MG90S.TXT", tag);
    rewrite("BENCH/KST/CONF/MG90S.TXT", "v2\n", 3u);
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", tag, NULL, "web\n"),
             WA_API_CHANGED);
    char now[12];
    char want[48];
    /* The answer's JSON first: the read below starts a request of its own. */
    char body[256];
    (void)snprintf(body, sizeof(body), "%s",
                   refusal(&g_api, WA_API_CHANGED));
    CHECK(has(body, "\"error\":\"CHANGED\""));
    CHECK(has(refusal(&g_api, WA_API_PANEL_BUSY), "\"error\":\"PANEL_BUSY\""));
    CHECK(!has(refusal(&g_api, WA_API_PANEL_BUSY), "etag"));
    CHECK(!has(refusal(NULL, WA_API_CHANGED), "etag"));
    etag_of_get("BENCH/KST/CONF/MG90S.TXT", now);
    (void)snprintf(want, sizeof(want), "\"etag\":%s", now);
    CHECK(has(body, want));
    /* The ETag found replaces the file. */
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", now, NULL, "web\n"),
             WA_API_OK);
    CHECK(holds("BENCH/KST/CONF/MG90S.TXT", "web\n"));
    CHECK(!has(refusal(&g_api, WA_API_CHANGED), "etag"));
    /* A third writer between the refusal and the PUT: refused again. */
    rewrite("BENCH/KST/CONF/MG90S.TXT", "v3\n", 3u);
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", now, NULL, "w\n"),
             WA_API_CHANGED);
    CHECK(holds("BENCH/KST/CONF/MG90S.TXT", "v3\n"));
    CHECK(has(refusal(&g_api, WA_API_CHANGED), "\"etag\":\""));
    /* The file gone: CHANGED, without an ETag. */
    etag_of_get("BENCH/KST/CONF/MG90S.TXT", tag);
    fk_free(&g_card);
    fk_init(&g_card);
    fk_dir(&g_card, CARD_KST_CONF);
    CHECK_EQ(put_s("path=BENCH/KST/CONF/MG90S.TXT", tag, NULL, "w3\n"),
             WA_API_CHANGED);
    CHECK(!has(refusal(&g_api, WA_API_CHANGED), "etag"));
    CHECK(fk_get(&g_card, "BENCH/KST/CONF/MG90S.TXT") == NULL);
}

TEST_CASE(put_if_none_match_refuses_a_taken_name)
{
    fresh();
    fk_file(&g_card, "A.TXT", (const uint8_t *)"old", 3u);
    CHECK_EQ(put_s("path=A.TXT", NULL, "*", "new"), WA_API_EXISTS);
    CHECK_EQ(wa_api_http(WA_API_EXISTS), 409u);
    CHECK(holds("A.TXT", "old"));
    /* Case folded, as FAT compares. */
    CHECK_EQ(put_s("path=a.txt", NULL, "*", "new"), WA_API_EXISTS);
    CHECK(holds("A.TXT", "old"));
    fk_dir(&g_card, "F");
    CHECK_EQ(put_s("path=F", NULL, "*", "new"), WA_API_EXISTS);
    /* A folder of the name without the header: not a file to replace. */
    CHECK_EQ(put_s("path=F", NULL, NULL, "new"), WA_API_ARG);
    CHECK(fk_get(&g_card, "F")->dir);
    CHECK(clean());
}

TEST_CASE(put_headers_and_names_not_taken_are_refused)
{
    fresh();
    fk_file(&g_card, "A.TXT", (const uint8_t *)"old", 3u);
    static const char *const bad_tags[] = {
        "*", "W/\"00000000\"", "00000000", "\"0000000\"", "\"000000000\"",
        "\"0000000g\"", "\"00000000\", \"00000001\"", "",
    };
    for (size_t i = 0u; i < sizeof(bad_tags) / sizeof(bad_tags[0]); ++i) {
        CHECK_EQ(put_s("path=A.TXT", bad_tags[i], NULL, "x"),
                 WA_API_REQUEST);
    }
    CHECK_EQ(put_s("path=A.TXT", NULL, "\"00000000\"", "x"), WA_API_REQUEST);
    CHECK_EQ(put_s("path=A.TXT", "\"00000000\"", "*", "x"), WA_API_REQUEST);
    CHECK(holds("A.TXT", "old"));
    /* Names: the root, a path the decoder refuses, and a new name the
     * panel's keyboard could not type. */
    CHECK_EQ(put_s("path=", NULL, "*", "x"), WA_API_REQUEST);
    CHECK_EQ(put_s("", NULL, "*", "x"), WA_API_REQUEST);
    CHECK_EQ(put_s("path=..%2Fx", NULL, "*", "x"), WA_API_NAME);
    CHECK_EQ(put_s("path=A%2FB.TXT", NULL, "*", "x"), WA_API_NAME);
    CHECK_EQ(put_s("path=A%3FB.TXT", NULL, "*", "x"), WA_API_NAME);
    CHECK_EQ(put_s("path=%20A.TXT", NULL, "*", "x"), WA_API_NAME);
    CHECK_EQ(put_s("path=AUX.TXT", NULL, "*", "x"), WA_API_NAME);
    CHECK_EQ(put_s("path=A.TXT&mount=x", NULL, NULL, "x"), WA_API_REQUEST);
    char longq[160];
    (void)snprintf(longq, sizeof(longq), "path=%060d.TX", 0);
    CHECK_EQ(put_s(longq, NULL, "*", "x"), WA_API_NAME);
    (void)snprintf(longq, sizeof(longq), "path=%055d.TX", 0);
    CHECK_EQ(put_s(longq, NULL, "*", "x"), WA_API_OK);
    CHECK_EQ(g_card.calls[FK_WRITE], 1);
}

TEST_CASE(put_into_a_locked_busy_or_missing_place_is_refused)
{
    fresh();
    fk_dir(&g_card, CARD_KST_CONF);
    fk_file(&g_card, "BENCH/KST/CONF/KST.TXT", (const uint8_t *)"j", 1u);
    CHECK_EQ(put_s("path=BENCH/KST/CONF/KST.TXT", NULL, NULL, "x"),
             WA_API_LOCKED);
    CHECK_EQ(put_s("path=BENCH/KST/CONF/KST.TXT", NULL, "*", "x"),
             WA_API_LOCKED);
    CHECK(holds("BENCH/KST/CONF/KST.TXT", "j"));
    CHECK_EQ(put_s("path=BENCH/RCBENCH.LOG", NULL, NULL, "x"),
             WA_API_FIXED);
    /* A file the logger has open. */
    fk_dir(&g_card, CARD_AUTO_LOGS);
    fk_file(&g_card, "BENCH/LOGS/RUN004.CSV", (const uint8_t *)"t", 1u);
    g_marks.run_dir = CARD_AUTO_LOGS;
    g_marks.run = 4u;
    CHECK_EQ(put_s("path=BENCH/LOGS/RUN004.CSV", NULL, NULL, "x"),
             WA_API_LOGGING);
    memset(&g_marks, 0, sizeof(g_marks));
    /* A folder that is not there. */
    CHECK_EQ(put_s("path=NONE/A.TXT", NULL, "*", "x"), WA_API_NOT_FOUND);
    CHECK(fk_get(&g_card, "NONE") == NULL);
    /* A full card: nothing left but a ".NEW" removed. */
    g_card.free_bytes = 2u;
    CHECK_EQ(put_s("path=FULL.TXT", NULL, "*", "xyz"), WA_API_FULL);
    CHECK(fk_get(&g_card, "FULL.TXT") == NULL);
    CHECK(fk_get(&g_card, "FULL.TXT.NEW") == NULL);
    CHECK(clean());
}

TEST_CASE(a_card_pulled_mid_put_leaves_the_old_file)
{
    fresh();
    uint8_t *big = malloc(3u * FM_CHUNK);
    REQUIRE_ALLOC(big);
    memset(big, 'b', 3u * FM_CHUNK);
    fk_file(&g_card, "B.TXT", (const uint8_t *)"old", 3u);
    fk_fail(&g_card, FK_WRITE, 2, FM_ERR_CARD_GONE);
    CHECK_EQ(put("path=B.TXT", NULL, NULL, big, 3u * FM_CHUNK),
             WA_API_CARD_GONE);
    CHECK_EQ(wa_api_http(WA_API_CARD_GONE), 503u);
    CHECK(holds("B.TXT", "old"));
    CHECK_EQ(fk_open_files(&g_card), 0);
    /* The card back: the next PUT names its mount and is refused, one
     * without a mount writes to it. */
    fk_insert(&g_card);
    CHECK_EQ(put("path=B.TXT&mount=1", NULL, NULL, big, 10u),
             WA_API_CARD_GONE);
    CHECK(holds("B.TXT", "old"));
    free(big);
}

TEST_CASE(armed_or_the_radio_going_off_stop_a_put)
{
    fresh();
    uint8_t *big = malloc(3u * FM_CHUNK);
    REQUIRE_ALLOC(big);
    memset(big, 'c', 3u * FM_CHUNK);
    fk_file(&g_card, "C.TXT", (const uint8_t *)"old", 3u);
    /* ARMED at the start: refused, nothing opened. */
    g_armed = true;
    CHECK_EQ(put("path=C.TXT", NULL, NULL, big, 3u * FM_CHUNK),
             WA_API_ARMED);
    CHECK_EQ(g_card.calls[FK_OPEN], 0);
    g_armed = false;
    /* ARMED after the first chunk: stopped, the ".NEW" removed. */
    g_benches = 0;
    g_armed_after = 3;
    CHECK_EQ(put("path=C.TXT", NULL, NULL, big, 3u * FM_CHUNK),
             WA_API_ARMED);
    CHECK(holds("C.TXT", "old"));
    CHECK(fk_get(&g_card, "C.TXT.NEW") == NULL);
    CHECK(clean());
    g_armed_after = -1;
    /* The radio going off: CANCELLED, the same. */
    g_benches = 0;
    g_cancel_after = 2;
    CHECK_EQ(put("path=C.TXT", NULL, NULL, big, 3u * FM_CHUNK),
             WA_API_CANCELLED);
    CHECK(holds("C.TXT", "old"));
    CHECK(fk_get(&g_card, "C.TXT.NEW") == NULL);
    CHECK(clean());
    /* ARMED while the old file is read for If-Match. */
    g_cancel_after = -1;
    char tag[12];
    rewrite("C.TXT", big, 3u * FM_CHUNK);
    etag_of_get("C.TXT", tag);
    g_benches = 0;
    g_armed_after = 2;
    CHECK_EQ(put("path=C.TXT", tag, NULL, "new", 3u), WA_API_ARMED);
    CHECK_EQ(fk_get(&g_card, "C.TXT")->size, 3u * FM_CHUNK);
    CHECK(fk_get(&g_card, "C.TXT.NEW") == NULL);
    CHECK(clean());
    free(big);
}

TEST_CASE(a_put_without_its_body_room_is_memory)
{
    rig_fresh();
    size_t cap = 1u;
    CHECK(wa_api_body(&g_api, &cap) == NULL);
    CHECK_EQ(cap, 0u);
    CHECK_EQ(wa_api_put(&g_api, "path=A.TXT", NULL, NULL, 0u),
             WA_API_MEMORY);
    /* Room, but the body not asked for: the same. */
    fresh();
    CHECK_EQ(wa_api_put(&g_api, "path=A.TXT", NULL, NULL, 0u),
             WA_API_MEMORY);
    /* One body, one PUT. */
    CHECK_EQ(put_s("path=A.TXT", NULL, NULL, "a"), WA_API_OK);
    CHECK_EQ(wa_api_put(&g_api, "path=A.TXT", NULL, NULL, 1u),
             WA_API_MEMORY);
    CHECK_EQ(g_card.calls[FK_WRITE], 1);
}

TEST_CASE(mkdir_makes_a_folder_once)
{
    fresh();
    fk_dir(&g_card, CARD_USER_LOGS);
    CHECK_EQ(wa_api_mkdir(&g_api, "path=USER/LOGS/RIG%201"), WA_API_OK);
    const fk_node_t *n = fk_get(&g_card, "USER/LOGS/RIG 1");
    CHECK(n != NULL && n->dir);
    CHECK_STR_EQ(wa_api_write_status(&g_api), "201 Created");
    CHECK_STR_EQ(answer(), "{\"path\":\"USER/LOGS/RIG 1\",\"created\":true,"
                           "\"note\":\"\"}");
    CHECK_EQ(wa_api_mkdir(&g_api, "path=USER/LOGS/rig%201"), WA_API_EXISTS);
    CHECK_EQ(wa_api_mkdir(&g_api, "path=USER/LOGS/A%3CB"), WA_API_NAME);
    CHECK_EQ(wa_api_mkdir(&g_api, "path=USER/LOGS/A."), WA_API_NAME);
    CHECK_EQ(wa_api_mkdir(&g_api, "path="), WA_API_REQUEST);
    CHECK_EQ(wa_api_mkdir(&g_api, NULL), WA_API_REQUEST);
    CHECK_EQ(wa_api_mkdir(&g_api, "path=NONE/A"), WA_API_NOT_FOUND);
    fk_dir(&g_card, CARD_KST_CONF);
    CHECK_EQ(wa_api_mkdir(&g_api, "path=BENCH/KST/CONF/KST.TXT"),
             WA_API_LOCKED);
    g_armed = true;
    CHECK_EQ(wa_api_mkdir(&g_api, "path=USER/B"), WA_API_ARMED);
    CHECK(fk_get(&g_card, "USER/B") == NULL);
    CHECK(clean());
}

TEST_CASE(rename_renames_in_place_and_moves_by_folder)
{
    fresh();
    fk_dir(&g_card, CARD_USER_LOGS);
    fk_dir(&g_card, "USER/OLD");
    fk_file(&g_card, "USER/LOGS/A.TXT", (const uint8_t *)"a", 1u);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/A.TXT&to=USER/LOGS/B.TXT"),
             WA_API_OK);
    CHECK(holds("USER/LOGS/B.TXT", "a"));
    CHECK(fk_get(&g_card, "USER/LOGS/A.TXT") == NULL);
    CHECK_STR_EQ(answer(), "{\"path\":\"USER/LOGS/B.TXT\",\"created\":false,"
                           "\"note\":\"\"}");
    CHECK_STR_EQ(wa_api_write_status(&g_api), "200 OK");
    /* A change of case alone is a rename. */
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/B.TXT&to=USER/LOGS/b.txt"),
             WA_API_OK);
    CHECK(fk_get(&g_card, "USER/LOGS/b.txt") != NULL);
    /* Into another folder under the same name: a move. */
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/b.txt&to=USER/OLD/b.txt"),
             WA_API_OK);
    CHECK(holds("USER/OLD/b.txt", "a"));
    /* An empty folder moves; "dir=1" says it is one. */
    fk_dir(&g_card, "USER/E");
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/E&to=USER/OLD/E&dir=1"),
             WA_API_OK);
    CHECK(fk_get(&g_card, "USER/OLD/E")->dir);
    CHECK(clean());
}

TEST_CASE(rename_refusals)
{
    fresh();
    fk_dir(&g_card, CARD_USER_LOGS);
    fk_dir(&g_card, "USER/OLD");
    fk_file(&g_card, "USER/LOGS/A.TXT", (const uint8_t *)"a", 1u);
    fk_file(&g_card, "USER/LOGS/B.TXT", (const uint8_t *)"b", 1u);
    fk_file(&g_card, "USER/OLD/A.TXT", (const uint8_t *)"o", 1u);
    /* Onto a name that is taken, in place and by a move. */
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/A.TXT&to=USER/LOGS/B.TXT"),
             WA_API_EXISTS);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/A.TXT&to=USER/OLD/A.TXT"),
             WA_API_EXISTS);
    CHECK(holds("USER/LOGS/A.TXT", "a"));
    CHECK(holds("USER/LOGS/B.TXT", "b"));
    CHECK(holds("USER/OLD/A.TXT", "o"));
    /* Another folder and another name at once. */
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/A.TXT&to=USER/OLD/C.TXT"),
             WA_API_REQUEST);
    /* A folder of the bench, the journal, a name not typeable. */
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS&to=USER/L2&dir=1"),
             WA_API_FIXED);
    fk_dir(&g_card, CARD_KST_CONF);
    fk_file(&g_card, "BENCH/KST/CONF/KST.TXT", (const uint8_t *)"j", 1u);
    CHECK_EQ(wa_api_rename(&g_api, "from=BENCH/KST/CONF/KST.TXT&"
                                   "to=BENCH/KST/CONF/K.TXT"),
             WA_API_LOCKED);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/A.TXT&to=USER/LOGS/A*"),
             WA_API_NAME);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/A.TXT&"
                                   "to=USER/LOGS/KST.TXT"), WA_API_OK);
    /* A non-empty folder does not move; a file named as a folder is
     * not one. */
    fk_file(&g_card, "USER/F/X.TXT", NULL, 1u);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/F&to=USER/OLD/F&dir=1"),
             WA_API_NOT_EMPTY);
    CHECK(fk_get(&g_card, "USER/F/X.TXT") != NULL);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/F&to=USER/G"), WA_API_ARG);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/F&to=USER/G&dir=2"),
             WA_API_REQUEST);
    /* The same name, a missing source, a missing target. */
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/B.TXT&"
                                   "to=USER/LOGS/B.TXT"), WA_API_ARG);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/Q.TXT&"
                                   "to=USER/LOGS/R.TXT"), WA_API_NOT_FOUND);
    CHECK_EQ(wa_api_rename(&g_api, "from=USER/LOGS/B.TXT"), WA_API_REQUEST);
    CHECK_EQ(wa_api_rename(&g_api, NULL), WA_API_REQUEST);
    CHECK(clean());
}

TEST_CASE(delete_takes_a_file_a_run_with_its_report_and_an_empty_folder)
{
    fresh();
    fk_dir(&g_card, CARD_USER_LOGS);
    fk_file(&g_card, "USER/LOGS/NOTE.TXT", NULL, 4u);
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/LOGS/NOTE.TXT"), WA_API_OK);
    CHECK(fk_get(&g_card, "USER/LOGS/NOTE.TXT") == NULL);
    CHECK_STR_EQ(answer(), "{\"path\":\"USER/LOGS\",\"created\":false,"
                           "\"note\":\"\"}");
    fk_file(&g_card, "USER/LOGS/ATEST042.CSV", NULL, 4u);
    fk_file(&g_card, "USER/LOGS/ATEST042.TXT", NULL, 4u);
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/LOGS/ATEST042.CSV"),
             WA_API_OK);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST042.CSV") == NULL);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST042.TXT") == NULL);
    fk_dir(&g_card, "USER/EMPTY");
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/EMPTY&dir=1"), WA_API_OK);
    CHECK(fk_get(&g_card, "USER/EMPTY") == NULL);
    CHECK(clean());
}

TEST_CASE(delete_refusals)
{
    fresh();
    fk_file(&g_card, "USER/FULL/X.TXT", NULL, 1u);
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/FULL&dir=1"),
             WA_API_NOT_EMPTY);
    CHECK_EQ(wa_api_http(WA_API_NOT_EMPTY), 409u);
    CHECK(fk_get(&g_card, "USER/FULL/X.TXT") != NULL);
    /* Only a hidden entry: not empty either. */
    fk_file(&g_card, "USER/HID/.x", NULL, 1u);
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/HID&dir=1"),
             WA_API_NOT_EMPTY);
    CHECK_EQ(wa_api_delete(&g_api, "path=USER&dir=1"), WA_API_FIXED);
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/NONE.TXT"),
             WA_API_NOT_FOUND);
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/FULL"), WA_API_ARG);
    fk_dir(&g_card, CARD_KST_CONF);
    fk_file(&g_card, "BENCH/KST/CONF/KST.TXT", NULL, 1u);
    CHECK_EQ(wa_api_delete(&g_api, "path=BENCH/KST/CONF/KST.TXT"),
             WA_API_LOCKED);
    fk_dir(&g_card, CARD_AUTO_LOGS);
    fk_file(&g_card, "BENCH/LOGS/RUN004.CSV", NULL, 1u);
    g_marks.run_dir = CARD_AUTO_LOGS;
    g_marks.run = 4u;
    CHECK_EQ(wa_api_delete(&g_api, "path=BENCH/LOGS/RUN004.CSV"),
             WA_API_LOGGING);
    CHECK(fk_get(&g_card, "BENCH/LOGS/RUN004.CSV") != NULL);
    memset(&g_marks, 0, sizeof(g_marks));
    g_armed = true;
    CHECK_EQ(wa_api_delete(&g_api, "path=USER/FULL/X.TXT"), WA_API_ARMED);
    CHECK(fk_get(&g_card, "USER/FULL/X.TXT") != NULL);
    CHECK_EQ(wa_api_delete(&g_api, "path="), WA_API_REQUEST);
    CHECK_EQ(wa_api_delete(&g_api, "path=A%5CB"), WA_API_NAME);
    CHECK(clean());
}

TEST_CASE(a_write_from_another_card_is_card_gone)
{
    fresh();
    fk_file(&g_card, "A.TXT", NULL, 1u);
    fk_pull(&g_card);
    fk_insert(&g_card);                      /* mount 2 now */
    CHECK_EQ(wa_api_delete(&g_api, "path=A.TXT&mount=1"), WA_API_CARD_GONE);
    CHECK_EQ(wa_api_rename(&g_api, "from=A.TXT&to=B.TXT&mount=1"),
             WA_API_CARD_GONE);
    CHECK_EQ(wa_api_mkdir(&g_api, "path=D&mount=1"), WA_API_CARD_GONE);
    CHECK(fk_get(&g_card, "A.TXT") != NULL);
    CHECK(fk_get(&g_card, "D") == NULL);
    CHECK_EQ(wa_api_delete(&g_api, "path=A.TXT&mount=2"), WA_API_OK);
    /* No card at all. */
    fk_pull(&g_card);
    CHECK_EQ(wa_api_mkdir(&g_api, "path=D"), WA_API_NO_CARD);
}

TEST_CASE(a_saved_esc_profile_carries_its_note)
{
    fresh();
    fk_dir(&g_card, CARD_ESC_CONF);
    CHECK_EQ(put_s("path=BENCH/ESC/CONF/MINE.json", NULL, "*", "{}\n"),
             WA_API_OK);
    CHECK(has(answer(), "\"note\":\"ESC_NEXT_START\"}"));
    CHECK_EQ(put_s("path=BENCH/ESC/CONF/MINE.TXT", NULL, "*", "x"),
             WA_API_OK);
    CHECK(has(answer(), "\"note\":\"\"}"));
    /* A write after it that is no PUT carries no ETag and no note. */
    CHECK_EQ(wa_api_mkdir(&g_api, "path=BENCH/ESC/CONF/OLD"), WA_API_OK);
    CHECK(!has(answer(), "etag"));
}

int main(void)
{
    RUN(put_makes_a_new_file_and_answers_its_etag);
    RUN(put_of_1mib_is_saved_and_one_byte_more_refused);
    RUN(put_holds_the_viewers_limits_before_any_card_call);
    RUN(put_with_the_read_etag_replaces_and_a_stale_one_is_412);
    RUN(put_refused_changed_answers_the_etag_it_found);
    RUN(put_if_none_match_refuses_a_taken_name);
    RUN(put_headers_and_names_not_taken_are_refused);
    RUN(a_path_alias_is_refused_by_every_write);
    RUN(put_into_a_locked_busy_or_missing_place_is_refused);
    RUN(a_card_pulled_mid_put_leaves_the_old_file);
    RUN(armed_or_the_radio_going_off_stop_a_put);
    RUN(a_put_without_its_body_room_is_memory);
    RUN(mkdir_makes_a_folder_once);
    RUN(rename_renames_in_place_and_moves_by_folder);
    RUN(rename_refusals);
    RUN(delete_takes_a_file_a_run_with_its_report_and_an_empty_folder);
    RUN(delete_refusals);
    RUN(a_write_from_another_card_is_card_gone);
    RUN(a_saved_esc_profile_carries_its_note);
    fk_free(&g_card);
    free(g_text);
    free(g_ends);
    free(g_json);
    return test_summary("wa_api_write");
}
