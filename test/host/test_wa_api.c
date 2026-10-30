/**
 * @file test_wa_api.c
 * @brief The Wi-Fi page's file API (wa_api.h) over a card in memory
 *        (fm_fake.h): every reason with its word, status and texts; the
 *        status answer; a folder's pages at 0, 1, 47, 48, 49 and 999
 *        entries, forward and back by the key at the page's edge; INFO of
 *        a file and a folder with its time as INFO shows it; and the query's refusals, before any card
 *        call.  The file answer is test_wa_api_file.c's.
 *
 * Cases:
 * - every_core_answer_has_a_reason_word_status_and_texts
 * - a_refusal_is_its_word_and_its_text_in_json
 * - the_status_says_the_card_and_whether_writes_are_open
 * - the_status_says_why_a_write_is_refused_by_mode_gate_and_hold
 * - a_write_is_allowed_by_mode_code_and_hold
 * - the_root_lists_folders_first_with_kind_place_and_busy
 * - pages_of_0_1_47_48_49_entries
 * - pages_of_999_entries_forward_and_back_by_key
 * - a_bad_path_is_refused_before_any_card_call
 * - a_bad_page_parameter_is_refused_before_any_card_call
 * - no_card_and_a_card_gone_at_both_walks_are_refused
 * - info_reads_a_file_and_a_folder
 * - info_of_another_card_kind_or_the_root_is_refused
 * - the_query_reads_first_values_and_ignores_the_rest
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "wa_api_rig.h"

#include "card_tree.h"
#include "wall_clock.h"
#include "fm_tree.h"

/**
 * @brief Make @p n files F000.TXT on in folder @p dir.
 * @param dir  the folder, made first
 * @param n    how many, up to 1000
 */
static void files(const char *dir, int n)
{
    fk_dir(&g_card, dir);
    char path[FK_PATH];
    for (int i = 0; i < n; ++i) {
        /* Made out of the page's order: 7 is prime to 1000. */
        snprintf(path, sizeof(path), "%s/F%03d.TXT", dir, (i * 7) % 1000);
        fk_file(&g_card, path, NULL, 10u);
    }
}

/**
 * @brief /api/list of @p query, with the answer's JSON caught.
 * @param query  the query string
 * @return the API's reason
 */
static wa_api_err_t list(const char *query)
{
    const wa_api_err_t e = wa_api_list(&g_api, query);
    json_begin();
    if (e == WA_API_OK) {
        wa_api_list_json(&g_api, &g_out);
    }
    (void)json_end();
    return e;
}

/**
 * @brief A query for the page after or before row @p row of the page on
 *        show.
 * @param go   "next" or "prev"
 * @param row  the row at the edge
 * @param out  takes the query
 * @param n    room at @p out
 */
static void key_query(const char *go, int row, char *out, size_t n)
{
    const fm_entry_t *e = &g_req.tree.page[row];
    snprintf(out, n, "path=D&go=%s&key=%s&key_dir=%d", go, e->name,
             e->is_dir ? 1 : 0);
}

TEST_CASE(every_core_answer_has_a_reason_word_status_and_texts)
{
    CHECK_EQ(wa_api_of(FM_OK), WA_API_OK);
    CHECK_STR_EQ(wa_api_word(WA_API_OK), "");
    CHECK_EQ(wa_api_http(WA_API_OK), 200u);
    CHECK_STR_EQ(wa_api_status_line(WA_API_OK), "200 OK");
    CHECK_STR_EQ(wa_api_text(WA_API_OK, false), "");
    /* Every answer of the core but FM_OK is a reason of its own. */
    bool seen[WA_API_COUNT] = { false };
    for (int e = FM_ERR_NO_CARD; e < FM_ERR_COUNT; ++e) {
        const wa_api_err_t a = wa_api_of((fm_err_t)e);
        CHECK(a != WA_API_OK);
        CHECK(!seen[a]);
        seen[a] = true;
    }
    /* Every reason: a word of its own, a refusing status, a text in both
     * languages, and the German not the English. */
    for (int a = WA_API_NO_CARD; a < WA_API_COUNT; ++a) {
        const wa_api_err_t r = (wa_api_err_t)a;
        CHECK(wa_api_word(r)[0] != '\0');
        for (int b = WA_API_NO_CARD; b < a; ++b) {
            CHECK(strcmp(wa_api_word(r), wa_api_word((wa_api_err_t)b))
                  != 0);
        }
        CHECK(wa_api_http(r) >= 400u);
        CHECK(strncmp(wa_api_status_line(r), "200", 3) != 0);
        CHECK(wa_api_text(r, false)[0] != '\0');
        CHECK(wa_api_text(r, true)[0] != '\0');
        CHECK(strcmp(wa_api_text(r, false), wa_api_text(r, true)) != 0);
    }
    CHECK_STR_EQ(wa_api_word(WA_API_NOT_FOUND), "NOT_FOUND");
    CHECK_STR_EQ(wa_api_status_line(WA_API_NOT_FOUND), "404 Not Found");
    CHECK_STR_EQ(wa_api_status_line(WA_API_BUSY), "423 Locked");
    CHECK_STR_EQ(wa_api_status_line(WA_API_TOO_BIG), "413 Content Too Large");
    CHECK_STR_EQ(wa_api_status_line(WA_API_NOT_TEXT),
                 "415 Unsupported Media Type");
    CHECK_STR_EQ(wa_api_status_line(WA_API_TOO_MANY_LINES),
                 "422 Unprocessable Content");
    CHECK_STR_EQ(wa_api_status_line(WA_API_FULL), "507 Insufficient Storage");
    CHECK_STR_EQ(wa_api_status_line(WA_API_IO), "500 Internal Server Error");
    CHECK_STR_EQ(wa_api_status_line(WA_API_EXISTS), "409 Conflict");
    CHECK_STR_EQ(wa_api_status_line(WA_API_CODE), "403 Forbidden");
    CHECK_STR_EQ(wa_api_status_line(WA_API_REQUEST), "400 Bad Request");
    CHECK_STR_EQ(wa_api_status_line(WA_API_NO_CARD),
                 "503 Service Unavailable");
    /* Outside the enums: the card failed. */
    CHECK_EQ(wa_api_of(FM_ERR_COUNT), WA_API_IO);
    CHECK_EQ(wa_api_of((fm_err_t)-1), WA_API_IO);
    CHECK_STR_EQ(wa_api_word(WA_API_COUNT), "IO");
    CHECK_EQ(wa_api_http((wa_api_err_t)-1), 500u);
}

/* The guard's refusals (wa_guard.h) are the API's own reasons, with the
 * guard's status, so the file API answers them in JSON: a read's and a
 * write's. */
TEST_CASE(a_guard_refusal_is_a_reason_with_the_guards_status)
{
    CHECK_EQ(wa_api_of_guard(WA_GUARD_OK), WA_API_OK);
    CHECK_EQ(wa_api_of_guard(WA_GUARD_BODY), WA_API_BODY);
    CHECK_EQ(wa_api_of_guard(WA_GUARD_HOST), WA_API_HOST);
    CHECK_STR_EQ(wa_api_status_line(WA_API_BODY),
                 wa_guard_status(WA_GUARD_BODY));
    CHECK_STR_EQ(wa_api_status_line(WA_API_HOST),
                 wa_guard_status(WA_GUARD_HOST));
    CHECK_STR_EQ(wa_api_status_line(WA_API_HOST), "421 Misdirected Request");
    /* A write's: a body over its limit, another page, no script header. */
    CHECK_EQ(wa_api_of_guard(WA_GUARD_BIG), WA_API_TOO_LARGE);
    CHECK_EQ(wa_api_of_guard(WA_GUARD_ORIGIN), WA_API_ORIGIN);
    CHECK_EQ(wa_api_of_guard(WA_GUARD_UPLOAD), WA_API_ORIGIN);
    CHECK_STR_EQ(wa_api_status_line(WA_API_TOO_LARGE),
                 wa_guard_status(WA_GUARD_BIG));
    CHECK_STR_EQ(wa_api_status_line(WA_API_ORIGIN),
                 wa_guard_status(WA_GUARD_ORIGIN));
    CHECK_STR_EQ(wa_api_status_line(WA_API_ORIGIN),
                 wa_guard_status(WA_GUARD_UPLOAD));
    CHECK_EQ(wa_api_of_guard((wa_guard_t)-1), WA_API_REQUEST);
    json_begin();
    wa_api_error_json(&g_out, wa_api_of_guard(WA_GUARD_BODY), false);
    CHECK_STR_EQ(json_end(), "{\"error\":\"BODY\",\"text\":"
                             "\"A read carries no body.\"}");
    json_begin();
    wa_api_error_json(&g_out, wa_api_of_guard(WA_GUARD_HOST), true);
    CHECK(has(json_end(), "{\"error\":\"HOST\",\"text\":\"Das Panel"));
}

TEST_CASE(a_refusal_is_its_word_and_its_text_in_json)
{
    json_begin();
    wa_api_error_json(&g_out, WA_API_NOT_FOUND, false);
    CHECK_STR_EQ(json_end(),
                 "{\"error\":\"NOT_FOUND\",\"text\":\"Not on the card.\"}");
    json_begin();
    wa_api_error_json(&g_out, WA_API_TOO_BIG, true);
    CHECK_STR_EQ(json_end(), "{\"error\":\"TOO_BIG\",\"text\":"
                             "\"\xc3\x9c" "ber 1MB: zu gro\xc3\x9f zum "
                             "\xc3\x96" "ffnen. Herunterladen.\"}");
    /* A text with an apostrophe stays one JSON string. */
    json_begin();
    wa_api_error_json(&g_out, WA_API_LOCKED, false);
    CHECK(has(json_end(), "\"text\":\"This is the servo programmer's "
                          "record. Download it to read it.\"}"));
}

TEST_CASE(the_status_says_the_card_and_whether_writes_are_open)
{
    wa_api_status_t st = {
        .name = "rcbench-1A2B", .version = "0.23.2", .de = false,
        .mounted = true, .mount = 3u, .card = "RC \"BENCH\"",
        .total = 31914983424u, .free = 1099511627776u, .gate = FM_OK,
    };
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK_STR_EQ(json_end(),
                 "{\"name\":\"rcbench-1A2B\",\"version\":\"0.23.2\","
                 "\"lang\":\"en\",\"writes\":\"open\","
                 "\"why\":\"\",\"why_text\":\"\",\"mode\":\"OPEN\","
                 "\"card\":{\"mounted\":true,"
                 "\"mount\":3,\"name\":\"RC \\\"BENCH\\\"\","
                 "\"total\":31914983424,\"free\":1099511627776},"
                 "\"page\":48,\"file_max\":1048576,\"lines_max\":65536,"
                 "\"user_logs\":\"USER/LOGS\"}");
    /* A User log folder SETUP names is where FILES' right pane opens. */
    st.user_logs = "MY LOGS";
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK(has(json_end(), "\"user_logs\":\"MY LOGS\"}"));
    st.user_logs = "";
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK(has(json_end(), "\"user_logs\":\"USER/LOGS\"}"));
    /* The gate shut says why; no card is no name and no bytes. */
    st.gate = FM_ERR_ARMED;
    st.de = true;
    st.mounted = false;
    st.card = NULL;
    st.total = st.free = 0u;
    json_begin();
    wa_api_status_json(&g_out, &st);
    const char *j = json_end();
    CHECK(has(j, "\"lang\":\"de\""));
    CHECK(has(j, "\"why\":\"ARMED\""));
    CHECK(has(j, "\"card\":{\"mounted\":false,\"mount\":3,\"name\":\"\","
                 "\"total\":0,\"free\":0}"));
    st.gate = FM_ERR_LOGGING;
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK(has(json_end(), "\"why\":\"LOGGING\""));
}

/*
 * The status says whether a write would pass now, and why not: OFF before
 * the gate, the gate before the hold, the hold once the gate is open; with
 * ACCESS CODE "open", since each write's cookie is checked.
 */
TEST_CASE(the_status_says_why_a_write_is_refused_by_mode_gate_and_hold)
{
    wa_api_status_t st = {
        .name = "rcbench-1A2B", .version = "0.23.2", .de = false,
        .mounted = true, .mount = 1u, .gate = FM_OK,
        .writes = WA_WRITES_HOLD, .window = false,
    };
    json_begin();
    wa_api_status_json(&g_out, &st);
    const char *j = json_end();
    CHECK(has(j, "\"writes\":\"closed\",\"why\":\"WRITES_HOLD\","));
    CHECK(has(j, "\"mode\":\"HOLD\""));
    CHECK(has(j, wa_api_text(WA_API_WRITES_HOLD, false)));
    st.window = true;
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK(has(json_end(), "\"writes\":\"open\",\"why\":\"\""));
    /* ARMED shuts an open window's writes too. */
    st.gate = FM_ERR_ARMED;
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK(has(json_end(), "\"writes\":\"closed\",\"why\":\"ARMED\""));
    st.window = false;
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK(has(json_end(), "\"why\":\"ARMED\""));
    /* OFF is said before the gate. */
    st.writes = WA_WRITES_OFF;
    st.de = true;
    json_begin();
    wa_api_status_json(&g_out, &st);
    j = json_end();
    CHECK(has(j, "\"why\":\"WRITES_OFF\""));
    CHECK(has(j, "\"mode\":\"OFF\""));
    CHECK(has(j, wa_api_text(WA_API_WRITES_OFF, true)));
    /* ACCESS CODE: open while the gate is. */
    st.writes = WA_WRITES_CODE;
    st.gate = FM_OK;
    json_begin();
    wa_api_status_json(&g_out, &st);
    j = json_end();
    CHECK(has(j, "\"writes\":\"open\""));
    CHECK(has(j, "\"mode\":\"CODE\""));
    /* A mode outside the enum is OFF. */
    st.writes = WA_WRITES_COUNT;
    json_begin();
    wa_api_status_json(&g_out, &st);
    CHECK(has(json_end(), "\"why\":\"WRITES_OFF\""));
}

/* What a write needs, by SETUP's mode: OPEN nothing, CODE the cookie,
 * HOLD the window, OFF never. */
TEST_CASE(a_write_is_allowed_by_mode_code_and_hold)
{
    for (int c = 0; c < 2; ++c) {
        for (int w = 0; w < 2; ++w) {
            CHECK_EQ(wa_api_may_write(WA_WRITES_OPEN, c, w), WA_API_OK);
            CHECK_EQ(wa_api_may_write(WA_WRITES_CODE, c, w),
                     c ? WA_API_OK : WA_API_WRITES_CODE);
            CHECK_EQ(wa_api_may_write(WA_WRITES_HOLD, c, w),
                     w ? WA_API_OK : WA_API_WRITES_HOLD);
            CHECK_EQ(wa_api_may_write(WA_WRITES_OFF, c, w),
                     WA_API_WRITES_OFF);
            CHECK_EQ(wa_api_may_write((wa_writes_t)-1, c, w),
                     WA_API_WRITES_OFF);
        }
    }
    CHECK_STR_EQ(wa_api_writes_word(WA_WRITES_OPEN), "OPEN");
    CHECK_STR_EQ(wa_api_writes_word(WA_WRITES_CODE), "CODE");
    CHECK_STR_EQ(wa_api_writes_word(WA_WRITES_HOLD), "HOLD");
    CHECK_STR_EQ(wa_api_writes_word(WA_WRITES_OFF), "OFF");
    CHECK_STR_EQ(wa_api_writes_word(WA_WRITES_COUNT), "OFF");
    CHECK_STR_EQ(wa_api_status_line(WA_API_CHANGED),
                 "412 Precondition Failed");
    CHECK_EQ(wa_api_of(FM_ERR_CHANGED), WA_API_CHANGED);
}

TEST_CASE(the_root_lists_folders_first_with_kind_place_and_busy)
{
    rig_fresh();
    fk_dir(&g_card, CARD_KST_CONF);
    fk_file(&g_card, "BENCH/KST/CONF/KST.TXT", NULL, 40u);
    fk_dir(&g_card, CARD_USER_LOGS);
    fk_file(&g_card, "zeta & \xc3\xa4.txt", NULL, 7u);
    fk_file(&g_card, "ATEST005.CSV", NULL, 4294967296u >> 20);
    CHECK_EQ(list(NULL), WA_API_OK);
    CHECK_STR_EQ(g_json,
                 "{\"path\":\"\",\"mount\":1,\"ahead\":0,\"total\":4,"
                 "\"unlisted\":0,\"prev\":false,\"next\":false,\"entries\":["
                 "{\"n\":\"BENCH\",\"d\":true,\"s\":0,\"k\":\"FOLDER\","
                 "\"p\":\"FIXED\",\"b\":false},"
                 "{\"n\":\"USER\",\"d\":true,\"s\":0,\"k\":\"FOLDER\","
                 "\"p\":\"FIXED\",\"b\":false},"
                 "{\"n\":\"ATEST005.CSV\",\"d\":false,\"s\":4096,"
                 "\"k\":\"RUN\",\"p\":\"FREE\",\"b\":false},"
                 "{\"n\":\"zeta & \xc3\xa4.txt\",\"d\":false,\"s\":7,"
                 "\"k\":\"TEXT\",\"p\":\"FREE\",\"b\":false}]}");
    /* The journal is LOCKED; a run the logger writes is busy. */
    CHECK_EQ(list("path=BENCH/KST/CONF"), WA_API_OK);
    CHECK(has(g_json, "{\"n\":\"KST.TXT\",\"d\":false,\"s\":40,"
                      "\"k\":\"KST\",\"p\":\"LOCKED\",\"b\":false}"));
    fk_file(&g_card, "USER/LOGS/ATEST005.CSV", NULL, 9u);
    g_marks.test_dir = CARD_USER_LOGS;
    g_marks.test = 5u;
    CHECK_EQ(list("path=USER/LOGS"), WA_API_OK);
    CHECK(has(g_json, "\"k\":\"RUN\",\"p\":\"FREE\",\"b\":true}"));
    /* The log folders in force are FIXED. */
    fk_dir(&g_card, "RUNS/A");
    wa_api_set_logs(&g_api, "RUNS/A", NULL);
    CHECK_EQ(list("path=RUNS"), WA_API_OK);
    CHECK(has(g_json, "{\"n\":\"A\",\"d\":true,\"s\":0,\"k\":\"FOLDER\","
                      "\"p\":\"FIXED\""));
    wa_api_set_logs(&g_api, NULL, "");
    CHECK_EQ(list("path=RUNS"), WA_API_OK);
    CHECK(has(g_json, "\"p\":\"FREE\""));
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(pages_of_0_1_47_48_49_entries)
{
    static const struct {
        int n;        /* entries                         */
        int held;     /* rows of the first page          */
        bool next;    /* a page after it                 */
    } k_rows[] = {
        { 0, 0, false }, { 1, 1, false }, { 47, 47, false },
        { 48, 48, false }, { 49, 48, true },
    };
    for (size_t i = 0u; i < sizeof(k_rows) / sizeof(k_rows[0]); ++i) {
        rig_fresh();
        files("D", k_rows[i].n);
        CHECK_EQ(list("path=D"), WA_API_OK);
        CHECK_EQ(g_req.tree.held, k_rows[i].held);
        CHECK(has(g_json, k_rows[i].next ? "\"next\":true"
                                         : "\"next\":false"));
        CHECK(has(g_json, "\"prev\":false"));
        CHECK(has(g_json, k_rows[i].n == 0 ? "\"entries\":[]}"
                                           : "\"entries\":[{"));
    }
    /* The 49th is the next page's one row; its prev is the first page. */
    char q[160];
    key_query("next", FM_PAGE_MAX - 1, q, sizeof(q));
    CHECK_EQ(list(q), WA_API_OK);
    CHECK_EQ(g_req.tree.held, 1);
    CHECK_STR_EQ(g_req.tree.page[0].name, "F336.TXT");
    CHECK(has(g_json, "\"ahead\":48,\"total\":49"));
    CHECK(has(g_json, "\"prev\":true,\"next\":false"));
    key_query("prev", 0, q, sizeof(q));
    CHECK_EQ(list(q), WA_API_OK);
    CHECK_EQ(g_req.tree.held, FM_PAGE_MAX);
    CHECK_STR_EQ(g_req.tree.page[0].name, "F000.TXT");
    /* last is the last page with no key. */
    CHECK_EQ(list("path=D&go=last"), WA_API_OK);
    CHECK_EQ(g_req.tree.held, FM_PAGE_MAX);
    CHECK_STR_EQ(g_req.tree.page[FM_PAGE_MAX - 1].name, "F336.TXT");
}

TEST_CASE(pages_of_999_entries_forward_and_back_by_key)
{
    rig_fresh();
    files("D", 999);
    CHECK_EQ(list("path=D"), WA_API_OK);
    char q[160];
    char last[FM_NAME_BUF] = "";
    int seen = 0;
    int pages = 1;
    for (;;) {
        for (int r = 0; r < g_req.tree.held; ++r) {
            /* Every row ranks after the one before, across pages too. */
            CHECK(strcmp(last, g_req.tree.page[r].name) < 0);
            snprintf(last, sizeof(last), "%s", g_req.tree.page[r].name);
        }
        seen += g_req.tree.held;
        if (!has(g_json, "\"next\":true")) {
            break;
        }
        key_query("next", g_req.tree.held - 1, q, sizeof(q));
        CHECK_EQ(list(q), WA_API_OK);
        ++pages;
    }
    CHECK_EQ(seen, 999);
    CHECK_EQ(pages, 21);                 /* 20 x 48 and 39 */
    CHECK_EQ(g_req.tree.held, 39);
    CHECK(has(g_json, "\"ahead\":960,\"total\":999"));
    /* And back to the first page. */
    seen = g_req.tree.held;
    while (has(g_json, "\"prev\":true")) {
        key_query("prev", 0, q, sizeof(q));
        CHECK_EQ(list(q), WA_API_OK);
        seen += g_req.tree.held;
    }
    CHECK_EQ(seen, 999);
    CHECK_STR_EQ(g_req.tree.page[0].name, "F000.TXT");
    /* same reads the page from the key on. */
    CHECK_EQ(list("path=D&go=same&key=F500.TXT"), WA_API_OK);
    CHECK_STR_EQ(g_req.tree.page[0].name, "F500.TXT");
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(a_bad_path_is_refused_before_any_card_call)
{
    rig_fresh();
    static const char *const k_bad[] = {
        "path=/BENCH", "path=..", "path=USER/..", "path=%2e%2e",
        "path=USER%2FLOGS", "path=USER\\LOGS", "path=USER%5CLOGS",
        "path=A%00B", "path=A%0aB", "path=%zz", "path=USER/", "path=A//B",
        "path=%c3", "path=.",
    };
    for (size_t i = 0u; i < sizeof(k_bad) / sizeof(k_bad[0]); ++i) {
        CHECK_EQ(list(k_bad[i]), WA_API_NAME);
        CHECK_EQ(wa_api_info(&g_api, k_bad[i]), WA_API_NAME);
        CHECK_EQ(wa_api_file(&g_api, k_bad[i]), WA_API_NAME);
    }
    /* 120 bytes is a path, 121 is not; 16 folders deep is, 17 is not. */
    char q[200];
    char path[130];
    memset(path, 'A', 120u);
    path[120] = '\0';
    snprintf(q, sizeof(q), "path=%s", path);
    CHECK_EQ(list(q), WA_API_NOT_FOUND);
    snprintf(q, sizeof(q), "path=%sA", path);
    CHECK_EQ(list(q), WA_API_NAME);
    snprintf(q, sizeof(q), "path=%s", "A/B/C/D/E/F/G/H/I/J/K/L/M/N/O/P");
    CHECK_EQ(list(q), WA_API_NOT_FOUND);
    snprintf(q, sizeof(q), "path=%s", "A/B/C/D/E/F/G/H/I/J/K/L/M/N/O/P/Q");
    CHECK_EQ(list(q), WA_API_NAME);
    /* Only the four answers that reached the card made calls. */
    CHECK_EQ(g_card.calls[FK_WALK], 2);
}

TEST_CASE(a_bad_page_parameter_is_refused_before_any_card_call)
{
    rig_fresh();
    files("D", 3);
    static const char *const k_bad[] = {
        "path=D&go=down", "path=D&go=", "path=D&go=NEXT",
        "path=D&go=next", "path=D&go=prev&key=", "path=D&go=same&key",
        "path=D&go=next&key=A/B", "path=D&go=next&key=..",
        "path=D&go=next&key=%zz", "path=D&go=next&key=A&key_dir=2",
        "path=D&key_dir=yes",
    };
    for (size_t i = 0u; i < sizeof(k_bad) / sizeof(k_bad[0]); ++i) {
        CHECK_EQ(list(k_bad[i]), WA_API_REQUEST);
    }
    /* A key of 64 bytes is no name the walk lists; 63 is. */
    char q[200];
    char key[70];
    memset(key, 'K', 63u);
    key[63] = '\0';
    snprintf(q, sizeof(q), "path=D&go=next&key=%sK", key);
    CHECK_EQ(list(q), WA_API_REQUEST);
    CHECK_EQ(g_card.calls[FK_ANY], 0);
    snprintf(q, sizeof(q), "path=D&go=prev&key=%s", key);
    CHECK_EQ(list(q), WA_API_OK);
    CHECK_EQ(g_req.tree.held, 3);
}

TEST_CASE(no_card_and_a_card_gone_at_both_walks_are_refused)
{
    rig_fresh();
    files("D", 3);
    CHECK_EQ(list("path=NONE"), WA_API_NOT_FOUND);
    fk_pull(&g_card);
    CHECK_EQ(list("path=D"), WA_API_NO_CARD);
    fk_insert(&g_card);
    /* Gone part way through the first walk and mounted again: the second
     * walk reads mount 3, and the answer says so. */
    g_card.gone_at_entry = 1;
    CHECK_EQ(list("path=D"), WA_API_OK);
    CHECK_EQ(g_req.tree.held, 3);
    CHECK(has(g_json, "\"mount\":3,"));
    /* Gone at both walks. */
    g_card.gone_at_entry = 0;
    fk_fail(&g_card, FK_HOLD, 2, FM_ERR_IO);
    CHECK_EQ(list("path=D"), WA_API_CARD_GONE);
    /* Found gone, and no card in the slot for the second walk. */
    fk_fail(&g_card, FK_WALK, 1, FM_ERR_CARD_GONE);
    CHECK_EQ(list("path=D"), WA_API_NO_CARD);
    CHECK_EQ(g_card.holds, 0);
    rig_fresh();
    files("D", 3);
    fk_fail(&g_card, FK_WALK, 1, FM_ERR_IO);
    CHECK_EQ(list("path=D"), WA_API_IO);
}

TEST_CASE(info_reads_a_file_and_a_folder)
{
    rig_fresh();
    g_card.mtime = 1798761600;       /* 2027-01-01 00:00 */
    fk_file(&g_card, "USER/LOGS/LIMIT006.TXT", NULL, 1873u);
    fk_file(&g_card, "USER/LOGS/ATEST005.CSV", NULL, 15104u);
    fk_file(&g_card, "USER/LOGS/.DS_Store", NULL, 6148u);
    CHECK_EQ(wa_api_info(&g_api, "path=USER/LOGS/LIMIT006.TXT"), WA_API_OK);
    json_begin();
    wa_api_info_json(&g_api, &g_out);
    CHECK_STR_EQ(json_end(),
                 "{\"path\":\"USER/LOGS/LIMIT006.TXT\",\"d\":false,"
                 "\"mount\":1,\"s\":1873,\"t\":1798761600,\"entries\":0,"
                 "\"unlisted\":0,\"time\":\"2027-01-01 00:00\"}");
    CHECK_EQ(wa_api_info(&g_api, "path=USER/LOGS&dir=1&mount=1"), WA_API_OK);
    json_begin();
    wa_api_info_json(&g_api, &g_out);
    CHECK_STR_EQ(json_end(),
                 "{\"path\":\"USER/LOGS\",\"d\":true,\"mount\":1,"
                 "\"s\":16977,\"t\":1798761600,\"entries\":2,"
                 "\"unlisted\":1,\"time\":\"2027-01-01 00:00\"}");
    /* No time on the card: -1. */
    g_card.mtime = -1;
    CHECK_EQ(wa_api_info(&g_api, "path=USER/LOGS/LIMIT006.TXT"), WA_API_OK);
    json_begin();
    wa_api_info_json(&g_api, &g_out);
    CHECK(has(json_end(), "\"t\":-1,"));
    CHECK(has(json_end(), "\"time\":\"\"}"));
    /* A 1980 stamp is an unset clock's (wall_clock.h): no date; the first
     * stamp taken as known, 1981-01-01 12:00 UTC, is one. */
    g_card.mtime = 315644040;        /* 1980-01-02 06:54 */
    CHECK_EQ(wa_api_info(&g_api, "path=USER/LOGS/LIMIT006.TXT"), WA_API_OK);
    json_begin();
    wa_api_info_json(&g_api, &g_out);
    CHECK(has(json_end(), "\"t\":315644040,"));
    CHECK(has(json_end(), "\"time\":\"\"}"));
    g_card.mtime = WALL_STAMP_FLOOR_S;
    CHECK_EQ(wa_api_info(&g_api, "path=USER/LOGS/LIMIT006.TXT"), WA_API_OK);
    json_begin();
    wa_api_info_json(&g_api, &g_out);
    CHECK(has(json_end(), "\"time\":\"1981-01-01 12:00\"}"));
    g_card.mtime = WALL_STAMP_FLOOR_S - 1;
    CHECK_EQ(wa_api_info(&g_api, "path=USER/LOGS/LIMIT006.TXT"), WA_API_OK);
    json_begin();
    wa_api_info_json(&g_api, &g_out);
    CHECK(has(json_end(), "\"time\":\"\"}"));
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(info_of_another_card_kind_or_the_root_is_refused)
{
    rig_fresh();
    fk_file(&g_card, "A.TXT", NULL, 3u);
    fk_dir(&g_card, "F");
    /* Listed from mount 1; card 2 is in the slot now. */
    fk_pull(&g_card);
    fk_insert(&g_card);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=1"), WA_API_CARD_GONE);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=2"), WA_API_OK);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT"), WA_API_OK);
    /* A folder asked as a file, and the other way round. */
    CHECK_EQ(wa_api_info(&g_api, "path=F"), WA_API_NOT_FOUND);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&dir=1"), WA_API_NOT_FOUND);
    CHECK_EQ(wa_api_info(&g_api, "path=NONE.TXT"), WA_API_NOT_FOUND);
    const long stats = g_card.calls[FK_STAT];
    CHECK_EQ(wa_api_info(&g_api, "path="), WA_API_REQUEST);
    CHECK_EQ(wa_api_info(&g_api, NULL), WA_API_REQUEST);
    /* A mount number of 1 to 10 digits up to 2^32 - 1. */
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=x"), WA_API_REQUEST);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount="), WA_API_REQUEST);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=-1"), WA_API_REQUEST);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=4294967296"),
             WA_API_REQUEST);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=00000000002"),
             WA_API_REQUEST);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&dir=2"), WA_API_REQUEST);
    CHECK_EQ(g_card.calls[FK_STAT], stats);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=4294967295"),
             WA_API_CARD_GONE);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT&mount=0000000002"), WA_API_OK);
    fk_pull(&g_card);
    CHECK_EQ(wa_api_info(&g_api, "path=A.TXT"), WA_API_NO_CARD);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(the_query_reads_first_values_and_ignores_the_rest)
{
    rig_fresh();
    files("D", 3);
    fk_dir(&g_card, "E");
    /* The first "path" counts; "pathx", "xpath" and a bare "&" do not. */
    CHECK_EQ(list("xpath=E&pathx=E&&path=D&path=E&t=123"), WA_API_OK);
    CHECK_STR_EQ(g_req.tree.dir, "D");
    /* "path" alone and "path=" are the root; "+" is a plus. */
    CHECK_EQ(list("path"), WA_API_OK);
    CHECK_STR_EQ(g_req.tree.dir, "");
    CHECK_EQ(list("path=&go=first"), WA_API_OK);
    CHECK_STR_EQ(g_req.tree.dir, "");
    fk_dir(&g_card, "A+B C");
    CHECK_EQ(list("path=A+B%20C"), WA_API_OK);
    CHECK_STR_EQ(g_req.tree.dir, "A+B C");
    CHECK_EQ(list(""), WA_API_OK);
    CHECK_STR_EQ(g_req.tree.dir, "");
}

int main(void)
{
    /* Times as UTC whatever the shell's zone: INFO's time is read in the C
     * library's zone (wall_local_s()). */
    (void)setenv("TZ", "UTC0", 1);
    tzset();
    RUN(every_core_answer_has_a_reason_word_status_and_texts);
    RUN(a_guard_refusal_is_a_reason_with_the_guards_status);
    RUN(a_refusal_is_its_word_and_its_text_in_json);
    RUN(the_status_says_the_card_and_whether_writes_are_open);
    RUN(the_status_says_why_a_write_is_refused_by_mode_gate_and_hold);
    RUN(a_write_is_allowed_by_mode_code_and_hold);
    RUN(the_root_lists_folders_first_with_kind_place_and_busy);
    RUN(pages_of_0_1_47_48_49_entries);
    RUN(pages_of_999_entries_forward_and_back_by_key);
    RUN(a_bad_path_is_refused_before_any_card_call);
    RUN(a_bad_page_parameter_is_refused_before_any_card_call);
    RUN(no_card_and_a_card_gone_at_both_walks_are_refused);
    RUN(info_reads_a_file_and_a_folder);
    RUN(info_of_another_card_kind_or_the_root_is_refused);
    RUN(the_query_reads_first_values_and_ignores_the_rest);
    fk_free(&g_card);
    free(g_json);
    return test_summary("wa_api");
}
