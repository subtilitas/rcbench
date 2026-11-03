/*
 * The Wi-Fi page as a browser receives it.
 *
 * The failures under test: a page that loads anything from elsewhere and so
 * fails on the panel's own access point, a ZIP size on the page that
 * differs from the archive sent, a card missing that takes the upload with
 * it, a browser gone part way that leaves a folder handle open, a text
 * with an apostrophe that ends a string of the page's script and leaves
 * the Upload button without its handler, and a file manager whose data,
 * texts or scripts the page leaves out or lets end their element early.
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
                                     false, NULL };
static const wa_page_info_t k_de = { "rcbench-1A2B", "0.19.0", 0x3E0000u,
                                     true, NULL };

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

/* The text between @p open and the next "</script>", copied into
 * g_elem; false when the page has no such element. */
static char g_elem[65536];

static bool element(const char *html, const char *open)
{
    const char *b = strstr(html, open);
    if (b == NULL) {
        return false;
    }
    b += strlen(open);
    const char *e = strstr(b, "</script>");
    if (e == NULL || (size_t)(e - b) >= sizeof(g_elem)) {
        return false;
    }
    memcpy(g_elem, b, (size_t)(e - b));
    g_elem[e - b] = '\0';
    return true;
}

/* How often @p needle stands in @p hay. */
static int count_of(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = strstr(hay, needle); p != NULL;
         p = strstr(p + 1, needle)) {
        ++n;
    }
    return n;
}

TEST_CASE(the_card_part_is_the_zip_link_and_the_file_manager)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "LOG00001.CSV", false, 1234u, 1791961199);
    fake_add(&fs, "logs", true, 0u, 0);
    fake_add(&fs, "logs/run #7 & <x>.csv", false, 0u, 1791961199);
    fake_add(&fs, "logs/sub", true, 0u, 0);
    fake_add(&fs, "logs/sub/100%.txt", false, 5u, 0);
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    self_contained(g_p.buf);
    /* The ZIP's size, as the archive will be. */
    wa_zip_plan_t plan;
    CHECK_EQ(wa_zip_plan(&w, &plan), WA_ZIP_OK);
    char line[160];
    snprintf(line, sizeof(line),
             "3 files, 1239 bytes. The ZIP file is %llu bytes.",
             (unsigned long long)plan.bytes);
    CHECK(strstr(g_p.buf, line) != NULL);
    CHECK(strstr(g_p.buf, "href=\"/card.zip\"") != NULL);
    /* No name of the card is in the page: the file manager reads the
     * folders through the file API, a page at a time. */
    CHECK(strstr(g_p.buf, "LOG00001.CSV") == NULL);
    CHECK(strstr(g_p.buf, "href=\"/card/") == NULL);
    /* The manager's element, its data, and the highlighter, the writes,
     * the editor and the app, in that order, each once: the app attaches
     * the writes and the editor it finds defined. */
    const char *app = strstr(g_p.buf, "<div id=\"fm\" class=\"fm\"></div>");
    const char *data = strstr(g_p.buf, "<script type=\"application/json\" "
                                       "id=\"fm-data\">{\"tx\":{");
    const char *hl = strstr(g_p.buf, "<script>var RcbHl");
    const char *wr = strstr(g_p.buf, "<script>var RcbWr");
    const char *ed = strstr(g_p.buf, "<script>var RcbEd");
    const char *js = strstr(g_p.buf, "<script>var RcbFm");
    CHECK(app != NULL && data != NULL && hl != NULL && wr != NULL
          && ed != NULL && js != NULL);
    CHECK(app < data && data < hl && hl < wr && wr < ed && ed < js);
    CHECK_EQ(count_of(g_p.buf, "<script>"), 5);
    CHECK_EQ(count_of(g_p.buf, "</script>"), 6);
    CHECK(strstr(g_p.buf, "<noscript><p>The file list needs JavaScript."
                          "</p></noscript>") != NULL);
    /* The texts are FILES' own where the panel has one. */
    CHECK(element(g_p.buf, "id=\"fm-data\">"));
    CHECK(strstr(g_elem, "\"rescan\":\"RESCAN\"") != NULL);
    CHECK(strstr(g_elem, "\"k.KST\":\"SERVO FILE\"") != NULL);
    CHECK(strstr(g_elem, "\"of\":\"ENTRIES %d TO %d OF %d\"") != NULL);
    CHECK(strstr(g_elem, "\"dl\":\"DOWNLOAD\"") != NULL);
    /* The writes' buttons are FILES' words; UPLOAD is the page's. */
    CHECK(strstr(g_elem, "\"upload\":\"UPLOAD\"") != NULL);
    CHECK(strstr(g_elem, "\"newdir\":\"NEW FOLDER\"") != NULL);
    CHECK(strstr(g_elem, "\"rename\":\"RENAME\"") != NULL);
    CHECK(strstr(g_elem, "\"move\":\"MOVE\"") != NULL);
    CHECK(strstr(g_elem, "\"delete\":\"DELETE\"") != NULL);
    CHECK(strstr(g_elem, "\"e.TOO_LARGE\":\"Too large for the panel: "
                         "1MB at most.\"") != NULL);
    CHECK(strstr(g_elem, "\"e.TOO_BIG\":\"Over 1MB: too large to open. "
                         "Download it.\"") != NULL);
    /* Without the viewer's words: none, and the viewer's cap. */
    CHECK(strstr(g_elem, "\"ok\":[],\"fail\":[],\"warn\":[],\"cap\":128,"
                         "\"label\":24}") != NULL);
    /* The style: the page's tokens in the browser's scheme, a rule per
     * class, no palette by class. */
    CHECK(strstr(g_p.buf, ":root{color-scheme:light dark;--pg-bg:")
          != NULL);
    CHECK(strstr(g_p.buf, "a{color:var(--pg-ac);word-break:break-all}"
                          ".h-tx{color:var(--h-tx)}") != NULL);
    CHECK(strstr(g_p.buf, ".h-ch{color:var(--h-ch)}") != NULL);
    CHECK(strstr(g_p.buf, "--h-tx:") == NULL);
    /* The upload is offered, with the slot as the size limit. */
    CHECK(strstr(g_p.buf, "f.size>4063232") != NULL);
    CHECK(strstr(g_p.buf, "Installed: 0.19.0") != NULL);
    CHECK(strstr(g_p.buf, "INSTALL is held for 2s on the panel") != NULL);
    CHECK_EQ(fs.dirs_open, 0);

    /* In German, the panel's German. */
    begin(-1);
    wa_page_main(&g_o, &k_de, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(element(g_p.buf, "id=\"fm-data\">"));
    CHECK(strstr(g_elem, "\"rescan\":\"AKTUALISIEREN\"") != NULL);
    CHECK(strstr(g_elem, "\"dl\":\"HERUNTERLADEN\"") != NULL);
    CHECK(strstr(g_elem, "\"next\":\"N\xc3\x84" "CHSTE\"") != NULL);
    CHECK(strstr(g_p.buf, "Die Dateiliste braucht JavaScript.") != NULL);
    fake_free(&fs);
}

/*
 * No text of the data can end its element: a "<" in a text is written as
 * \u003c, and the page carries no "</" between the data's start and its
 * end but the end itself.
 */
TEST_CASE(the_data_keeps_inside_its_element_in_both_languages)
{
    fake_fs_t fs;
    fake_init(&fs);
    const wa_fs_t w = fake_wa(&fs);
    for (int de = 0; de < 2; ++de) {
        begin(-1);
        wa_page_main(&g_o, de != 0 ? &k_de : &k_en, &w);
        CHECK(wa_out_flush(&g_o));
        CHECK(element(g_p.buf, "id=\"fm-data\">"));
        CHECK(strstr(g_elem, "<") == NULL);
        CHECK(g_elem[strlen(g_elem) - 1u] == '}');
        /* The highlighter's classes in the order of the X-macro. */
        CHECK(strstr(g_elem, "\"cls\":[\"tx\",\"hd\",\"lb\",\"ok\",\"fl\","
                             "\"wn\",\"vo\",\"cu\",\"pw\",\"rt\",\"un\","
                             "\"cm\",\"hx\",\"ky\",\"nm\",\"lt\",\"pu\","
                             "\"ch\"]") != NULL);
        /* The unit table, the ohm sign as UTF-8. */
        CHECK(strstr(g_elem, "\"units\":[[\"V\",6],[\"mV\",6],[\"A\",7],")
              != NULL);
        CHECK(strstr(g_elem, ",[\"m\xce\xa9\",10],[\"\xce\xa9\",10]]")
              != NULL);
    }
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
    /* No ZIP link; the file manager is there to say so and to read the
     * card once it is in. */
    CHECK(strstr(g_p.buf, "<div id=\"fm\" class=\"fm\"></div>") != NULL);
    CHECK(strstr(g_p.buf, "POST','/update'") != NULL);
    /* The upload's own header, which the panel requires (wa_guard.h): a
     * page of another site cannot send it without a preflight. */
    CHECK(strstr(g_p.buf, "x.setRequestHeader('X-Rcbench-Upload','1');")
          != NULL);
    CHECK(strstr(g_p.buf, "/card.zip") == NULL);
    CHECK(strstr(g_p.buf, "The card holds no files.") == NULL);
    begin(-1);
    wa_page_main(&g_o, &k_de, &w);
    CHECK(wa_out_flush(&g_o));
    CHECK(strstr(g_p.buf, "/card.zip") == NULL);
    CHECK(strstr(g_p.buf, "<div id=\"fm\" class=\"fm\"></div>") != NULL);
    fake_free(&fs);
}

TEST_CASE(an_empty_card_is_said_and_names_left_out_are_not_zipped)
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
    CHECK(strstr(g_p.buf, "1 Dateien, 1 Bytes.") != NULL);
    fake_free(&fs);
}

TEST_CASE(the_page_streams_and_stops_when_the_browser_goes)
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
    /* The page's size does not grow with the card: no name is in it. */
    CHECK(strstr(g_p.buf, "5000 files, 5000 bytes.") != NULL);
    CHECK(strstr(g_p.buf, "F4999.CSV") == NULL);
    CHECK(g_p.len < 64u * 1024u);
    CHECK(g_p.pieces > 20);            /* in pieces, not one buffer */
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

/* The bench tree's files are all counted for the ZIP file. */
TEST_CASE(the_bench_tree_is_counted_for_the_zip_file)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_bench_tree(&fs);
    const wa_fs_t w = fake_wa(&fs);
    begin(-1);
    wa_page_main(&g_o, &k_en, &w);
    CHECK(wa_out_flush(&g_o));
    char line[96];
    snprintf(line, sizeof(line), "%d files, %u bytes.", FAKE_TREE_FILES,
             FAKE_TREE_BYTES);
    CHECK(strstr(g_p.buf, line) != NULL);
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
    RUN(the_card_part_is_the_zip_link_and_the_file_manager);
    RUN(the_data_keeps_inside_its_element_in_both_languages);
    RUN(the_bench_tree_is_counted_for_the_zip_file);
    RUN(without_a_card_the_page_says_so_and_still_takes_an_upload);
    RUN(an_empty_card_is_said_and_names_left_out_are_not_zipped);
    RUN(the_page_streams_and_stops_when_the_browser_goes);
    RUN(texts_in_the_upload_script_are_escaped_in_both_languages);
    RUN(the_pages_are_written_for_the_script_check);
    RUN(every_upload_answer_has_its_own_text_in_both_languages);
    return test_summary("wa_page");
}
