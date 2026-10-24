/*
 * The Wi-Fi page as a browser receives it.
 *
 * The failures under test: a page that loads anything from elsewhere and so
 * fails on the panel's own access point, a file link that does not lead
 * back to the file it names, a name that breaks the HTML (hypertext markup
 * language), a ZIP size on the page that differs from the archive sent, a
 * card missing that takes the upload with it, and a browser gone part way
 * that leaves a folder handle open, and a text with an apostrophe that
 * ends a string of the page's script and leaves the Upload button without
 * its handler.
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "wa_fakefs.h"
#include "wa_page.h"
#include "wa_send.h"
#include "wa_zip.h"

/* Where the pages are written for check_wa_page.py, which hands each
 * script to node. */
#ifndef PAGE_OUT_DIR
#define PAGE_OUT_DIR "wa_page_out"
#endif

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    pieces;
    int    refuse_after;
} page_t;

/* Far above any page here: a writer that never stops fails the case
 * rather than taking the machine's memory. */
#define SINK_CAP (64u * 1024u * 1024u)

static bool sink(void *ctx, const void *data, size_t n)
{
    page_t *p = ctx;
    if (p->refuse_after >= 0 && p->pieces >= p->refuse_after) {
        return false;
    }
    ++p->pieces;
    CHECK(p->len + n <= SINK_CAP);
    if (p->len + n > SINK_CAP) {
        return false;
    }
    if (p->len + n + 1u > p->cap) {
        p->cap = (p->len + n + 1u) * 2u;
        REQUIRE_REALLOC(p->buf, p->cap);
    }
    memcpy(p->buf + p->len, data, n);
    p->len += n;
    p->buf[p->len] = '\0';
    return true;
}

static page_t g_p;
static wa_out_t g_o;

static void begin(int refuse_after)
{
    free(g_p.buf);
    memset(&g_p, 0, sizeof(g_p));
    g_p.refuse_after = refuse_after;
    wa_out_init(&g_o, sink, &g_p);
}

static const wa_page_info_t k_en = { "rcbench-1A2B", "0.19.0", 0x3E0000u,
                                     false };
static const wa_page_info_t k_de = { "rcbench-1A2B", "0.19.0", 0x3E0000u,
                                     true };

/* Nothing the page needs comes from another address. */
static void self_contained(const char *html)
{
    CHECK(strstr(html, "http://") == NULL);
    CHECK(strstr(html, "https://") == NULL);
    CHECK(strstr(html, "src=") == NULL);
    CHECK(strstr(html, "<link") == NULL);
    CHECK(strstr(html, "@import") == NULL);
    CHECK(strncmp(html, "<!doctype html>", 15u) == 0);
    CHECK(strstr(html, "</html>") != NULL);
}

TEST_CASE(the_code_form_asks_for_the_code_and_shows_nothing_else)
{
    begin(-1);
    wa_page_code(&g_o, &k_en, false);
    CHECK(wa_out_flush(&g_o));
    self_contained(g_p.buf);
    CHECK(strstr(g_p.buf, "action=\"/code\"") != NULL);
    CHECK(strstr(g_p.buf, "name=\"code\"") != NULL);
    CHECK(strstr(g_p.buf, "Access code") != NULL);
    CHECK(strstr(g_p.buf, "Wrong code.") == NULL);
    CHECK(strstr(g_p.buf, "/card") == NULL);
    CHECK(strstr(g_p.buf, "/update") == NULL);
    begin(-1);
    wa_page_code(&g_o, &k_de, true);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "Zugangscode") != NULL);
    CHECK(strstr(g_p.buf, "Falscher Code.") != NULL);
    CHECK(strstr(g_p.buf, "lang=\"de\"") != NULL);
}

/* Every file link on the page, decoded as the server decodes it. */
static int links(const char *html, char found[][WA_PATH_MAX + 1u], int max)
{
    int n = 0;
    const char *p = html;
    while ((p = strstr(p, "href=\"" WA_URL_FILE)) != NULL && n < max) {
        p += strlen("href=\"" WA_URL_FILE);
        const char *end = strchr(p, '"');
        CHECK(end != NULL);
        CHECK_EQ(wa_path_decode(p, (size_t)(end - p), found[n],
                                WA_PATH_MAX + 1u),
                 WA_PATH_OK);
        ++n;
        p = end;
    }
    return n;
}

static char g_found[16][WA_PATH_MAX + 1u];

TEST_CASE(every_file_is_listed_with_a_link_back_to_it)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "LOG00001.CSV", false, 1234u, 1791961199);
    fake_add(&fs, "logs", true, 0u, 0);
    fake_add(&fs, "logs/run #7 & <x>.csv", false, 0u, 1791961199);
    fake_add(&fs, "logs/M\xc3\xbcller \"q\" 'a'.csv", false, 99u, 0);
    fake_add(&fs, "logs/sub", true, 0u, 0);
    fake_add(&fs, "logs/sub/100%.txt", false, 5u, 0);
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    self_contained(g_p.buf);
    const char *want[] = { "LOG00001.CSV", "logs/run #7 & <x>.csv",
                           "logs/M\xc3\xbcller \"q\" 'a'.csv",
                           "logs/sub/100%.txt" };
    CHECK_EQ(links(g_p.buf, g_found, 16), 4);
    for (int i = 0; i < 4; ++i) {
        CHECK_STR_EQ(g_found[i], want[i]);
    }
    /* Names are text, not markup. */
    CHECK(strstr(g_p.buf, "<x>") == NULL);
    CHECK(strstr(g_p.buf, "run #7 &amp; &lt;x&gt;.csv") != NULL);
    CHECK(strstr(g_p.buf, "M\xc3\xbcller &quot;q&quot; &#39;a&#39;.csv")
          != NULL);
    /* Folders are headings, in walk order, with their files under them. */
    const char *h1 = strstr(g_p.buf, "<h3>/</h3>");
    const char *h2 = strstr(g_p.buf, "<h3>/logs</h3>");
    const char *h3 = strstr(g_p.buf, "<h3>/logs/sub</h3>");
    CHECK(h1 != NULL && h2 != NULL && h3 != NULL);
    CHECK(h1 < h2 && h2 < h3);
    CHECK(strstr(g_p.buf, "<td class=\"n\">1234</td><td>2026-10-14 06:59"
                          "</td>") != NULL);
    /* The ZIP's size, as the archive will be. */
    wa_zip_plan_t plan;
    CHECK_EQ(wa_zip_plan(&w, &plan), WA_ZIP_OK);
    char line[160];
    snprintf(line, sizeof(line),
             "4 files, 1338 bytes. The ZIP file is %llu bytes.",
             (unsigned long long)plan.bytes);
    CHECK(strstr(g_p.buf, line) != NULL);
    CHECK(strstr(g_p.buf, "href=\"/card.zip\"") != NULL);
    /* The upload is offered, with the slot as the size limit. */
    CHECK(strstr(g_p.buf, "f.size>4063232") != NULL);
    CHECK(strstr(g_p.buf, "Installed: 0.19.0") != NULL);
    CHECK(strstr(g_p.buf, "INSTALL is held for 2s on the panel") != NULL);
    CHECK_EQ(fs.dirs_open, 0);
    fake_free(&fs);
}

TEST_CASE(without_a_card_the_page_says_so_and_still_takes_an_upload)
{
    fake_fs_t fs;
    fake_init(&fs);
    fs.pulled = true;
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    self_contained(g_p.buf);
    CHECK(strstr(g_p.buf, "No card, or the card cannot be read.") != NULL);
    CHECK(strstr(g_p.buf, "POST','/update'") != NULL);
    CHECK(strstr(g_p.buf, "/card.zip") == NULL);
    begin(-1);
    wa_page_main(&g_o, &k_de, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "Keine Karte, oder die Karte ist nicht lesbar.")
          != NULL);
    fake_free(&fs);
}

TEST_CASE(an_empty_card_and_names_left_out_are_said)
{
    fake_fs_t fs;
    fake_init(&fs);
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "The card holds no files.") != NULL);
    CHECK(strstr(g_p.buf, "/card.zip") == NULL);
    fake_add(&fs, "bad\xff", false, 1u, 0);
    fake_add(&fs, "worse\xfe", false, 1u, 0);
    fake_add(&fs, "ok", false, 1u, 0);
    begin(-1);
    wa_page_main(&g_o, &k_de, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "<p>2 Einträge fehlen in der Liste") != NULL);
    CHECK(strstr(g_p.buf, "1 Dateien, 1 Bytes.") != NULL);
    fake_free(&fs);
}

TEST_CASE(a_long_listing_streams_and_stops_when_the_browser_goes)
{
    fake_fs_t fs;
    fake_init(&fs);
    char name[16];
    for (int i = 0; i < 5000; ++i) {
        snprintf(name, sizeof(name), "F%04d.CSV", i);
        fake_add_in(&fs, 0, name, false, 1u, 0);
    }
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK_EQ(links(g_p.buf, g_found, 16), 16);
    CHECK(strstr(g_p.buf, "F4999.CSV") != NULL);
    CHECK(g_p.pieces > 100);           /* in pieces, not one buffer */
    begin(10);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(g_o.failed);
    CHECK_EQ(fs.dirs_open, 0);
    CHECK(g_p.len <= 10u * WA_OUT_BUF);
    fake_free(&fs);
}

/* The body of the page's one script, copied into g_js; false when the
 * page has none.  The end is the first "</script>", as a browser reads
 * it. */
static char g_js[8192];

static bool script_of(const char *html)
{
    const char *b = strstr(html, "<script>");
    if (b == NULL) {
        return false;
    }
    b += strlen("<script>");
    const char *e = strstr(b, "</script>");
    CHECK(e != NULL);
    if (e == NULL || (size_t)(e - b) >= sizeof(g_js)) {
        return false;
    }
    memcpy(g_js, b, (size_t)(e - b));
    g_js[e - b] = '\0';
    return true;
}

/* Walks @p js as JavaScript reads quotes: a string opens at ' or " and
 * ends at the same quote, a backslash takes the next byte, and a string
 * does not run over a line.  Returns the number of strings, or -1 for one
 * that is open at the end or at a line break. */
static int strings_in(const char *js, size_t n)
{
    int count = 0;
    char q = '\0';
    for (size_t i = 0; i < n; ++i) {
        const char c = js[i];
        if (q == '\0') {
            if (c == '\'' || c == '"') {
                q = c;
                ++count;
            }
        } else if (c == '\\') {
            ++i;
        } else if (c == q) {
            q = '\0';
        } else if (c == '\n' || c == '\r') {
            return -1;
        }
    }
    return q == '\0' ? count : -1;
}

TEST_CASE(texts_in_the_upload_script_are_escaped_in_both_languages)
{
    fake_fs_t fs;
    fake_init(&fs);
    const wa_fs_t w = fake_wa(&fs);
    int count[2] = { 0, 0 };
    for (int de = 0; de < 2; ++de) {
        begin(-1);
        wa_page_main(&g_o, de != 0 ? &k_de : &k_en, &w);
        CHECK(wa_out_flush(&g_o));
        CHECK(script_of(g_p.buf));
        /* Every string closes where the script means it to. */
        count[de] = strings_in(g_js, strlen(g_js));
        CHECK(count[de] > 0);
    }
    /* The texts add no string of their own. */
    CHECK_EQ(count[0], count[1]);

    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(script_of(g_p.buf));
    CHECK(strstr(g_js, "r.textContent='The file is larger than the panel"
                       "\\'s slot.';return;}") != NULL);
    CHECK(strstr(g_js, "r.textContent='The upload broke off.';};") != NULL);
    CHECK(strstr(g_js, "panel's") == NULL);
    /* No file chosen: the browser says so; the guard stays as well. */
    CHECK(strstr(g_p.buf, "type=\"file\" id=\"i\" accept=\".bin\" required>")
          != NULL);
    CHECK(strstr(g_js, "if(!f)return;") != NULL);
    /* A refusal without a body (403, 503) says so with its status and
     * ends the progress bar; so does a broken connection. */
    CHECK(strstr(g_js, "if(t===''&&x.status!==200)t='The panel did not take "
                       "the upload: HTTP '+x.status+'.';r.textContent=t;};")
          != NULL);
    CHECK(strstr(g_js, "x.onload=function(){var t=x.responseText;"
                       "p.hidden=true;p.value=0;") != NULL);
    CHECK(strstr(g_js, "x.onerror=function(){p.hidden=true;p.value=0;") != NULL);

    /* Without an apostrophe a text passes unchanged, umlaut included. */
    begin(-1);
    wa_page_main(&g_o, &k_de, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(script_of(g_p.buf));
    CHECK(strstr(g_js, "r.textContent='Die Datei ist gr\xc3\xb6\xc3\x9f"
                       "er als der Slot des Panels.';return;}") != NULL);
    CHECK(strstr(g_js, "r.textContent='Der Upload brach ab.';};") != NULL);
    CHECK(strstr(g_js, "t='Das Panel hat den Upload nicht angenommen: HTTP "
                       "'+x.status+'.';") != NULL);
    CHECK(strstr(g_p.buf, "accept=\".bin\" required>") != NULL);
    fake_free(&fs);
}

/* Both pages go to PAGE_OUT_DIR for check_wa_page.py. */
TEST_CASE(the_pages_are_written_for_the_script_check)
{
    if (mkdir(PAGE_OUT_DIR, 0755) != 0 && errno != EEXIST) {
        CHECK(false);
        return;
    }
    fake_fs_t fs;
    fake_init(&fs);
    const wa_fs_t w = fake_wa(&fs);
    for (int de = 0; de < 2; ++de) {
        begin(-1);
        wa_page_main(&g_o, de != 0 ? &k_de : &k_en, &w);
        CHECK(wa_out_flush(&g_o));
        char path[512];
        snprintf(path, sizeof(path), "%s/page_%s.html", PAGE_OUT_DIR,
                 de != 0 ? "de" : "en");
        FILE *f = fopen(path, "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            CHECK_EQ(fwrite(g_p.buf, 1u, g_p.len, f), g_p.len);
            fclose(f);
        }
    }
    fake_free(&fs);
}

TEST_CASE(every_upload_answer_has_its_own_text_in_both_languages)
{
    const fw_err_t errs[] = { FW_ERR_EMPTY, FW_ERR_SHORT, FW_ERR_TOO_BIG,
                              FW_ERR_NOT_IMAGE, FW_ERR_CHIP, FW_ERR_PROJECT,
                              FW_ERR_VERSION, FW_ERR_NO_HASH, FW_ERR_LENGTH,
                              FW_ERR_HASH, FW_ERR_READ, FW_ERR_BUSY,
                              FW_ERR_MEMORY };
    static char seen[2][16][200];
    for (int de = 0; de < 2; ++de) {
        for (unsigned i = 0; i < sizeof(errs) / sizeof(errs[0]); ++i) {
            begin(-1);
            wa_page_upload_result(&g_o, de != 0, errs[i], NULL, false);
            CHECK(wa_out_flush(&g_o));
            CHECK(g_p.len > 10u);
            CHECK(g_p.len < sizeof(seen[0][0]));
            strcpy(seen[de][i], g_p.buf);
            for (unsigned k = 0; k < i; ++k) {
                CHECK(strcmp(seen[de][k], seen[de][i]) != 0);
            }
        }
    }
    const fw_image_info_t info = fw_fix_running("0.20.1", 1u);
    begin(-1);
    wa_page_upload_result(&g_o, false, FW_OK, &info, false);
    CHECK(wa_out_flush(&g_o));
    CHECK_STR_EQ(g_p.buf, "Received and checked: panel firmware 0.20.1. To "
                          "install, hold INSTALL on the panel for 2s.");
    begin(-1);
    wa_page_upload_result(&g_o, true, FW_OK, &info, true);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "0.20.1. Das ist der Build") != NULL);
    free(g_p.buf);
    memset(&g_p, 0, sizeof(g_p));
}

TEST_CASE(a_stamp_from_an_unset_clock_lists_no_date)
{
    fake_fs_t fs;
    fake_init(&fs);
    /* 1980-01-02 06:54: FatFs's stamp from a clock 1d 6h 54min after a
     * power-on (wall_clock.h). */
    fake_add(&fs, "SERVO001.CSV", false, 11u, 315644040);
    /* No stamp at all, and the epoch. */
    fake_add(&fs, "SERVO002.CSV", false, 12u, -1);
    fake_add(&fs, "SERVO003.CSV", false, 13u, 0);
    /* 1981-01-03 00:00 UTC: a date, written. */
    fake_add(&fs, "SERVO004.CSV", false, 14u, 347328000);
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "<td class=\"n\">11</td><td></td>") != NULL);
    CHECK(strstr(g_p.buf, "<td class=\"n\">12</td><td></td>") != NULL);
    CHECK(strstr(g_p.buf, "<td class=\"n\">13</td><td></td>") != NULL);
    CHECK(strstr(g_p.buf, "<td class=\"n\">14</td><td>1981-01-03 00:00"
                          "</td>") != NULL);
    CHECK(strstr(g_p.buf, "1980-") == NULL);
    CHECK(strstr(g_p.buf, "1970-") == NULL);
    fake_free(&fs);
}

TEST_CASE(the_bench_tree_lists_every_file_under_its_folder)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_bench_tree(&fs);
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    static char found[FAKE_TREE_FILES + 4][WA_PATH_MAX + 1u];
    CHECK_EQ(links(g_p.buf, found, FAKE_TREE_FILES + 4), FAKE_TREE_FILES);
    const char *want[] = {
        "RCBENCH.LOG", "KST/KST001.TXT", "BENCH/RCBENCH.LOG",
        "BENCH/RCBENCH.OLD", "BENCH/LOGS/MOTOR002.CSV",
        "BENCH/ESC/CONF/hobbywing-flyfun-8item.json",
        "BENCH/KST/CONF/KST.TXT", "BENCH/KST/CONF/Wing left.TXT",
        "USER/LOGS/ATEST001.TXT", "USER/LOGS/LIMIT002.TXT",
    };
    for (size_t i = 0u; i < sizeof(want) / sizeof(want[0]); ++i) {
        bool seen = false;
        for (int j = 0; j < FAKE_TREE_FILES; ++j) {
            seen = seen || strcmp(found[j], want[i]) == 0;
        }
        CHECK(seen);
    }
    /* Each folder of the tree is a heading, in walk order. */
    const char *h_bench = strstr(g_p.buf, "<h3>/BENCH</h3>");
    const char *h_kst = strstr(g_p.buf, "<h3>/BENCH/KST/CONF</h3>");
    const char *h_user = strstr(g_p.buf, "<h3>/USER/LOGS</h3>");
    CHECK(h_bench != NULL && h_kst != NULL && h_user != NULL);
    CHECK(h_bench < h_kst && h_kst < h_user);
    char line[96];
    snprintf(line, sizeof(line), "%d files, %u bytes.", FAKE_TREE_FILES,
             FAKE_TREE_BYTES);
    CHECK(strstr(g_p.buf, line) != NULL);
    CHECK_EQ(fs.dirs_open, 0);
    fake_free(&fs);
}

/*
 * A report standing alone, a LIMIT RUN's LIMITnnn.TXT or the SERVOnnn.TXT
 * alone of a card of firmware 0.21.0, is offered as a run's report is: a
 * row of its folder's table whose link downloads it, and the link serves
 * every byte of it.  A card of such reports alone lists each of them.
 */
TEST_CASE(a_report_standing_alone_is_a_download_as_a_runs_report_is)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "SERVO043.TXT", false, 1702u, 315532800);
    fake_add(&fs, CARD_USER, true, 0u, 0);
    fake_add(&fs, CARD_USER_LOGS, true, 0u, 0);
    const int limit = fake_add(&fs, CARD_USER_LOGS "/LIMIT001.TXT", false,
                               1873u, 315532800);
    fake_add(&fs, CARD_USER_LOGS "/LIMIT002.TXT", false, 1940u, 315532800);
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK_EQ(links(g_p.buf, g_found, 16), 3);
    CHECK_STR_EQ(g_found[0], "SERVO043.TXT");
    CHECK_STR_EQ(g_found[1], CARD_USER_LOGS "/LIMIT001.TXT");
    CHECK_STR_EQ(g_found[2], CARD_USER_LOGS "/LIMIT002.TXT");
    /* The row a run's report has: name, bytes, date. */
    CHECK(strstr(g_p.buf, "<tr><td><a href=\"/card/USER/LOGS/LIMIT001.TXT\" "
                          "download>LIMIT001.TXT</a></td><td class=\"n\">"
                          "1873</td>") != NULL);
    CHECK(strstr(g_p.buf, "3 files") != NULL);
    /* The link serves the file whole. */
    wa_send_t snd;
    CHECK(wa_send_begin(&snd, &w, g_found[1], 1873u));
    uint8_t buf[512];
    uint64_t at = 0u;
    bool same = true;
    for (;;) {
        const int32_t n = wa_send_next(&snd, buf, sizeof(buf));
        CHECK(n >= 0);
        if (n <= 0) {
            break;
        }
        for (int32_t i = 0; i < n; ++i) {
            same = same && buf[i] == fake_byte(limit, at + (uint64_t)i);
        }
        at += (uint64_t)n;
    }
    wa_send_end(&snd);
    CHECK(same);
    CHECK_EQ(at, 1873u);
    CHECK_EQ(fs.files_open, 0);
    CHECK_EQ(fs.dirs_open, 0);
    fake_free(&fs);
}

int main(void)
{
    /* Dates as UTC whatever the shell's zone: the page and the ZIP read
     * stamps in the C library's zone (wall_local_s()). */
    (void)setenv("TZ", "UTC0", 1);
    tzset();
    RUN(the_code_form_asks_for_the_code_and_shows_nothing_else);
    RUN(every_file_is_listed_with_a_link_back_to_it);
    RUN(a_stamp_from_an_unset_clock_lists_no_date);
    RUN(the_bench_tree_lists_every_file_under_its_folder);
    RUN(a_report_standing_alone_is_a_download_as_a_runs_report_is);
    RUN(without_a_card_the_page_says_so_and_still_takes_an_upload);
    RUN(an_empty_card_and_names_left_out_are_said);
    RUN(a_long_listing_streams_and_stops_when_the_browser_goes);
    RUN(texts_in_the_upload_script_are_escaped_in_both_languages);
    RUN(the_pages_are_written_for_the_script_check);
    RUN(every_upload_answer_has_its_own_text_in_both_languages);
    return test_summary("wa_page");
}
