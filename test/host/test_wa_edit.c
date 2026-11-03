/*
 * The Wi-Fi page's editor (edit.js) against the panel: the page carries
 * it and its texts, and the fixtures check_wa_edit.py runs the page's own
 * script on under node.
 *
 * The failures under test, through check_wa_edit.py: a file whose line
 * ends, BOM (byte order mark) or line-end count the editor reads other
 * than the panel (text_buf.h, the API's X-Rcbench-Eol), or does not give
 * back byte for byte; a limit other than the viewer's; a line the editor
 * takes for a checksum line other than kst_file_checksum_line(); and a KST
 * file whose SAVE keeps a checksum line or cuts another line.  The save's
 * If-Match, its 412 and the unsaved question are the harness's own cases
 * (check_wa_edit.js).
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "kst_file.h"
#include "kst_sim.h"
#include "greatest.h"
#include "text_buf.h"
#include "wa_fakefs.h"
#include "wa_page.h"

#ifndef EDIT_OUT_DIR
#define EDIT_OUT_DIR "wa_edit_out"
#endif

/* ------------------------------------------------------------ the page */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} page_t;

static bool sink(void *ctx, const void *data, size_t n)
{
    page_t *p = ctx;
    if (p->len + n + 1u > p->cap) {
        p->cap = (p->len + n + 1u) * 2u;
        REQUIRE_REALLOC(p->buf, p->cap);
    }
    memcpy(p->buf + p->len, data, n);
    p->len += n;
    p->buf[p->len] = '\0';
    return true;
}

static page_t g_page;

/* The page in @p de, in g_page. */
static const char *page_in(bool de)
{
    fake_fs_t fs;
    fake_init(&fs);
    const wa_fs_t w = fake_wa(&fs);
    const wa_page_info_t pi = { "rcbench-1A2B", "0.23.3", 0x7C0000u, de,
                                NULL };
    free(g_page.buf);
    memset(&g_page, 0, sizeof(g_page));
    wa_out_t o;
    wa_out_init(&o, sink, &g_page);
    wa_page_main(&o, &pi, &w);
    CHECK(wa_out_flush(&o));
    fake_free(&fs);
    return g_page.buf;
}

/*
 * The editor's script stands between the writes and the app, and its
 * texts are in the data in both languages: the panel's editor's words,
 * and the page's two of its own.
 */
TEST_CASE(the_page_carries_the_editor_and_its_texts)
{
    const char *en = page_in(false);
    const char *wr = strstr(en, "<script>var RcbWr");
    const char *ed = strstr(en, "<script>var RcbEd");
    const char *app = strstr(en, "<script>var RcbFm");
    CHECK(wr != NULL && ed != NULL && app != NULL);
    CHECK(wr < ed && ed < app);
    CHECK(strstr(en, "\"edit\":\"EDIT\"") != NULL);
    CHECK(strstr(en, "\"readagain\":\"READ AGAIN\"") != NULL);
    CHECK(strstr(en, "\"stay\":\"STAY\"") != NULL);
    CHECK(strstr(en, "\"mixedlf\":\"Mixed line ends: SAVE writes every "
                     "line end as LF.\"") != NULL);
    const char *de = page_in(true);
    CHECK(strstr(de, "\"edit\":\"BEARBEITEN\"") != NULL);
    CHECK(strstr(de, "\"readagain\":\"NEU LESEN\"") != NULL);
    CHECK(strstr(de, "\"save\":\"SPEICHERN\"") != NULL);
}

/* ------------------------------------------------------------ fixtures */

static FILE *g_fx;
static size_t g_n;

static void hex(const void *bytes, size_t n)
{
    const unsigned char *b = bytes;
    fputc('"', g_fx);
    for (size_t i = 0u; i < n; ++i) {
        fprintf(g_fx, "%02x", b[i]);
    }
    fputc('"', g_fx);
}

static void sep(void)
{
    fputs(g_n == 0u ? "" : ",\n", g_fx);
    ++g_n;
}

/* The X-Rcbench-Eol word of a loaded file, as wa_api.c writes it. */
static const char *eol_word(const text_buf_t *tb)
{
    if (text_buf_mixed(tb)) {
        return "mixed";
    }
    switch (text_buf_eol(tb)) {
    case TEXT_EOL_CRLF:
        return "crlf";
    case TEXT_EOL_CR:
        return "cr";
    case TEXT_EOL_LF:
    case TEXT_EOL_KINDS:
    default:
        return "lf";
    }
}

/* One file: its bytes, the panel's eol word, BOM and line ends. */
static void file_case(const char *bytes, size_t len)
{
    static uint8_t mem[256];
    static uint32_t ends[64];
    text_buf_t tb;
    CHECK_EQ(text_buf_init(&tb, mem, sizeof(mem), ends, 64u), TEXT_BUF_OK);
    CHECK_EQ(text_buf_load(&tb, bytes, len), TEXT_BUF_OK);
    sep();
    fputs("[", g_fx);
    hex(bytes, len);
    fprintf(g_fx, ",\"%s\",%s,%zu]", eol_word(&tb),
            text_buf_bom(&tb) ? "true" : "false", text_buf_lines(&tb) - 1u);
}

static void file_cases(void)
{
    static const char *const k_files[] = {
        "", "a", "a\n", "a\nb\n", "a\nb", "a\r\nb\r\n", "a\r\nb",
        "a\rb\r", "\r\n\r\n", "\n\n\n", "a\nb\r\n", "a\r\nb\rc\n",
        "a\r\r\n", "x\r", "\xc3\xa4\xc3\xb6\xc3\x9f\r\n\xe2\x82\xac\r\n",
        "\ttab\tline\n", "\xef\xbb\xbf", "\xef\xbb\xbf" "a\n",
        "\xef\xbb\xbf" "a\r\nb\r\n", "\xef\xbb\xbf" "a\rb",
        "\xef\xbb\xbf" "a\nb\r\n", "\xef\xbb\xbf\xef\xbb\xbf" "a\n",
    };
    fputs("\"files\":[\n", g_fx);
    g_n = 0u;
    for (size_t k = 0u; k < sizeof(k_files) / sizeof(k_files[0]); ++k) {
        file_case(k_files[k], strlen(k_files[k]));
    }
    fputs("],\n", g_fx);
}

/* Lines and kst_file_checksum_line()'s verdict on each. */
static void sum_cases(void)
{
    static const char *const k_lines[] = {
        "checksum", "checksum 00000000", "CHECKSUM 00000000",
        "Checksum\t00000000", "checksum=1", "checksum:1", "   checksum 0",
        "\t \tCheckSum: 1", "checksums 1", "check sum 1", "# checksum 1",
        "x checksum", "checksum0", "checksum-1", "", " ", "chec", "CHECKSUM",
        "cHeCkSuM=", "checksum\r", "\xc3\xa7hecksum 1", "Type: V6",
    };
    fputs("\"sum\":[\n", g_fx);
    g_n = 0u;
    for (size_t k = 0u; k < sizeof(k_lines) / sizeof(k_lines[0]); ++k) {
        sep();
        fputs("[", g_fx);
        hex(k_lines[k], strlen(k_lines[k]));
        fprintf(g_fx, ",%s]",
                kst_file_checksum_line(k_lines[k], strlen(k_lines[k]))
                    ? "true" : "false");
    }
    fputs("],\n", g_fx);
}

/*
 * @p text without its checksum lines, each with its line end, line 0
 * kept: the cut FILES' editor makes (text_view_keys.c), in @p out.
 */
static size_t cut(const char *text, size_t len, char *out)
{
    size_t o = 0u;
    size_t at = 0u;
    for (size_t no = 0u; at <= len; ++no) {
        const char *nl = memchr(text + at, '\n', len - at);
        const size_t end = (nl != NULL) ? (size_t)(nl - text) : len;
        const size_t next = (nl != NULL) ? end + 1u : len;
        if (no == 0u || !kst_file_checksum_line(text + at, end - at)) {
            memcpy(out + o, text + at, next - at);
            o += next - at;
        }
        if (nl == NULL) {
            break;
        }
        at = next;
    }
    return o;
}

/*
 * A KST file of the bench image with one checksum line @p sum in place of
 * its own, a comment line under the header (an edit), and the text the
 * cut leaves, which the programmer reads as EDITED; or a file without the
 * header, which keeps every line.
 */
static void kst_case(const char *sum, bool head)
{
    static char base[KST_FILE_MAX + 1u];
    static char text[KST_FILE_MAX + 256u];
    static char want[KST_FILE_MAX + 256u];
    const kst_image_t img = sim_bench_image();
    const size_t len = kst_file_format(&img, KST_TYPE_V6, base,
                                       KST_FILE_MAX);
    CHECK(len > 0u && len < KST_FILE_MAX);
    base[len] = '\0';
    const char *nl = strchr(base, '\n');
    const char *own = strstr(base, "checksum ");
    if (nl == NULL || own == NULL || own < nl) {
        CHECK(false);
        return;
    }
    const char *own_end = strchr(own, '\n');
    /* Line 0 with its end, the comment, the lines up to the file's own
     * checksum line, @p sum in its place, and the rest. */
    const int n = snprintf(text, sizeof(text), "%.*s# bench edit\n%.*s%s%s",
                           (int)(nl + 1 - base), base,
                           (int)(own - (nl + 1)), nl + 1, sum,
                           (own_end != NULL) ? own_end : "");
    CHECK(n > 0 && (size_t)n < sizeof(text));
    if (!head) {
        text[0] = 'X';
    }
    const size_t m = cut(text, (size_t)n, want);
    if (head) {
        CHECK(m < (size_t)n);
        kst_file_t f;
        unsigned bad = 0u;
        CHECK_EQ(kst_file_parse(want, m, &f, &bad), KST_FILE_OK);
        CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
    }
    sep();
    fputs("[", g_fx);
    hex(text, (size_t)n);
    fputs(",", g_fx);
    hex(head ? want : text, head ? m : (size_t)n);
    fputs("]", g_fx);
}

static void kst_cases(void)
{
    static const char *const k_sums[] = {
        "checksum 00000000", "CHECKSUM 00000000", "checksum=00000000",
        "Checksum\t00000000", "   checksum 0", "checksum",
        "checksum 0\nCheckSum: 1",
    };
    fputs("\"kst\":[\n", g_fx);
    g_n = 0u;
    for (size_t k = 0u; k < sizeof(k_sums) / sizeof(k_sums[0]); ++k) {
        kst_case(k_sums[k], true);
    }
    kst_case("checksum 00000000", false);
    fputs("],\n", g_fx);
}

/* The page and the fixtures go to EDIT_OUT_DIR for check_wa_edit.py. */
TEST_CASE(the_page_and_the_fixtures_are_written_for_the_node_check)
{
    if (mkdir(EDIT_OUT_DIR, 0755) != 0 && errno != EEXIST) {
        CHECK(false);
        return;
    }
    const char *html = page_in(false);
    FILE *f = fopen(EDIT_OUT_DIR "/page_en.html", "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        CHECK_EQ(fwrite(html, 1u, strlen(html), f), strlen(html));
        fclose(f);
    }
    g_fx = fopen(EDIT_OUT_DIR "/edit_fixtures.json", "wb");
    CHECK(g_fx != NULL);
    if (g_fx == NULL) {
        return;
    }
    fprintf(g_fx, "{\"limits\":{\"bytes\":%u,\"ends\":%u},\n",
            (unsigned)TEXT_BUF_FILE_MAX, (unsigned)TEXT_BUF_ENDS_MAX);
    file_cases();
    sum_cases();
    kst_cases();
    fputs("\"end\":true}\n", g_fx);
    fclose(g_fx);
}

int main(void)
{
    RUN(the_page_carries_the_editor_and_its_texts);
    RUN(the_page_and_the_fixtures_are_written_for_the_node_check);
    free(g_page.buf);
    return test_summary("wa_edit");
}
