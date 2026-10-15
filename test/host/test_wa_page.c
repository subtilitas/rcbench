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
#include <sys/stat.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "wa_fakefs.h"
#include "wa_page.h"
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
    CHECK(strstr(g_p.buf, "INSTALL is held for 2 s on the panel") != NULL);
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

    /* Without an apostrophe a text passes unchanged, umlaut included. */
    begin(-1);
    wa_page_main(&g_o, &k_de, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(script_of(g_p.buf));
    CHECK(strstr(g_js, "r.textContent='Die Datei ist gr\xc3\xb6\xc3\x9f"
                       "er als der Slot des Panels.';return;}") != NULL);
    CHECK(strstr(g_js, "r.textContent='Der Upload brach ab.';};") != NULL);
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
                          "install, hold INSTALL on the panel for 2 s.");
    begin(-1);
    wa_page_upload_result(&g_o, true, FW_OK, &info, true);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "0.20.1. Das ist der Build") != NULL);
    free(g_p.buf);
    memset(&g_p, 0, sizeof(g_p));
}

int main(void)
{
    RUN(the_code_form_asks_for_the_code_and_shows_nothing_else);
    RUN(every_file_is_listed_with_a_link_back_to_it);
    RUN(without_a_card_the_page_says_so_and_still_takes_an_upload);
    RUN(an_empty_card_and_names_left_out_are_said);
    RUN(a_long_listing_streams_and_stops_when_the_browser_goes);
    RUN(texts_in_the_upload_script_are_escaped_in_both_languages);
    RUN(the_pages_are_written_for_the_script_check);
    RUN(every_upload_answer_has_its_own_text_in_both_languages);
    return test_summary("wa_page");
}
