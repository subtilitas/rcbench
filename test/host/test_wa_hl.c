/*
 * The Wi-Fi page's highlighter against the panel's: the tables and the
 * colours the page carries, and the fixtures check_wa_hl.py runs the
 * page's own scripts on.
 *
 * The failures under test: a class, a unit, a verdict word or a colour the
 * page lacks or holds other than the panel's viewer; and, through
 * check_wa_hl.py and node, a line the page's hl.js classes into other
 * spans than text_hl_line(), a file name it gives another kind, a file it
 * splits into other lines than the viewer, and a size it writes other
 * than FILES.
 *
 * The fixtures: the servo test's report in English and German, passed,
 * failed and aborted; the KST file the programmer writes for the bench
 * image; every ESC profile of record; CSV lines; the edge lines of each
 * rule; lines of random tokens; and a span room of 1 to 4 and 128.
 *
 * SPDX-License-Identifier: MIT
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The panel's headers first: the servo test's rig has a global g, which
 * a parameter of gfx.h would otherwise shadow. */
#include "files_screen.h"
#include "kst_file.h"
#include "kst_reg.h"
#include "kst_sim.h"
#include "servo_str.h"
#include "text_buf.h"
#include "text_view.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "wa_app.h"
#include "wa_fakefs.h"
#include "wa_page.h"

#include "servo_test_rig.h"

#ifndef HL_OUT_DIR
#define HL_OUT_DIR "wa_hl_out"
#endif
#ifndef PROFILE_DIR
#define PROFILE_DIR "profiles"
#endif

/* Spans a fixture line is classed into at most. */
#define SPANS_MAX 256u

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

static wa_hl_t g_hl;
static page_t g_page;

/* The panel's words, cap and colours, as panel_files_web_hl() hands them
 * over. */
static const wa_hl_t *panel_hl(void)
{
    g_hl.words = text_view_words();
    g_hl.spans = TEXT_VIEW_SPANS;
    CHECK_EQ(text_view_class_rgb(UI_THEME_DARK, g_hl.dark, WA_HL_RGB),
             WA_HL_RGB);
    CHECK_EQ(text_view_class_rgb(UI_THEME_LIGHT, g_hl.light, WA_HL_RGB),
             WA_HL_RGB);
    return &g_hl;
}

/* The page in @p de with the panel's highlighter, in g_page. */
static const char *page_with_hl(bool de)
{
    fake_fs_t fs;
    fake_init(&fs);
    const wa_fs_t w = fake_wa(&fs);
    const wa_page_info_t pi = { "rcbench-1A2B", "0.23.2", 0x7C0000u, de,
                                panel_hl() };
    free(g_page.buf);
    memset(&g_page, 0, sizeof(g_page));
    wa_out_t o;
    wa_out_init(&o, sink, &g_page);
    wa_page_main(&o, &pi, &w);
    CHECK(wa_out_flush(&o));
    fake_free(&fs);
    return g_page.buf;
}

/** @brief Expands a row of TEXT_HL_CLASS_ROWS to its css name. */
#define ROW_CSS(name, css, role) css,
/** @brief Expands a row of TEXT_HL_CLASS_ROWS to its colour role. */
#define ROW_ROLE(name, css, role) role,

static const char *const k_css[TEXT_HL_CLASSES] = {
    TEXT_HL_CLASS_ROWS(ROW_CSS)
};
static const ui_color_id_t k_role[TEXT_HL_CLASSES] = {
    TEXT_HL_CLASS_ROWS(ROW_ROLE)
};

/* Each class's variable in the palette block of @p theme holds its role's
 * colour, and --h-bk the text panel's. */
static void check_palette(const char *block, ui_theme_id_t theme)
{
    char want[48];
    for (size_t k = 0u; k < TEXT_HL_CLASSES; ++k) {
        snprintf(want, sizeof(want), "--h-%s:#%06lx;", k_css[k],
                 (unsigned long)ui_theme_rgb24(theme, k_role[k]));
        if (strstr(block, want) == NULL) {
            T_FAIL("theme %d: no \"%s\"", (int)theme, want);
        }
    }
    snprintf(want, sizeof(want), "--h-bk:#%06lx",
             (unsigned long)ui_theme_rgb24(theme, UI_C_PANEL));
    CHECK(strstr(block, want) != NULL);
}

TEST_CASE(the_colours_are_the_viewers_in_both_palettes)
{
    const char *html = page_with_hl(false);
    const char *light = strstr(html, ":root{color-scheme:light dark;");
    const char *dark = strstr(html, "@media (prefers-color-scheme:dark)"
                                    "{:root{");
    CHECK(light != NULL && dark != NULL && light < dark);
    if (light == NULL || dark == NULL) {
        return;
    }
    static char block[2048];
    const size_t n = (size_t)(dark - light);
    CHECK(n < sizeof(block));
    memcpy(block, light, n < sizeof(block) ? n : sizeof(block) - 1u);
    block[n < sizeof(block) ? n : sizeof(block) - 1u] = '\0';
    check_palette(block, UI_THEME_LIGHT);
    const char *end = strstr(dark, "}}");
    CHECK(end != NULL);
    memcpy(block, dark, (size_t)(end - dark));
    block[end - dark] = '\0';
    check_palette(block, UI_THEME_DARK);
    /* The two differ: the browser's dark scheme gets the dark palette. */
    CHECK(ui_theme_rgb24(UI_THEME_DARK, UI_C_TEXT)
          != ui_theme_rgb24(UI_THEME_LIGHT, UI_C_TEXT));
    /* Out of range is black. */
    CHECK_EQ(ui_theme_rgb24(UI_THEME_COUNT, UI_C_TEXT), 0u);
    CHECK_EQ(ui_theme_rgb24(UI_THEME_DARK, UI_C_COUNT), 0u);
    CHECK_EQ(text_view_class_rgb(UI_THEME_DARK, g_hl.dark, 3u), 3u);
    CHECK_EQ(text_view_class_rgb(UI_THEME_DARK, NULL, WA_HL_RGB), 0u);
}

TEST_CASE(the_words_and_the_cap_are_the_viewers)
{
    CHECK_EQ(WA_HL_SPANS, TEXT_VIEW_SPANS);
    CHECK_EQ(WA_HL_RGB, TEXT_VIEW_RGB);
    const char *html = page_with_hl(true);
    const char *data = strstr(html, "id=\"fm-data\">");
    CHECK(data != NULL);
    if (data == NULL) {
        return;
    }
    CHECK(strstr(data, ",\"cap\":128,\"label\":24}") != NULL);
    const text_hl_words_t *w = text_view_words();
    const char *const *lists[3] = { w->ok, w->fail, w->warn };
    const size_t counts[3] = { w->ok_n, w->fail_n, w->warn_n };
    const char *const keys[4] = { "\"ok\":[", "\"fail\":[", "\"warn\":[",
                                  ",\"cap\":" };
    for (size_t k = 0u; k < 3u; ++k) {
        const char *list = strstr(data, keys[k]);
        CHECK(list != NULL);
        if (list == NULL) {
            continue;
        }
        /* The list ends where the next key starts: a word may hold "]". */
        const char *end = strstr(list, keys[k + 1u]);
        CHECK(end != NULL);
        if (end == NULL) {
            continue;
        }
        size_t cores = 0u;
        for (size_t i = 0u; i < counts[k]; ++i) {
            size_t len = 0u;
            const char *c = text_hl_word_core(lists[k][i], &len);
            if (len == 0u) {
                continue;
            }
            ++cores;
            char want[96];
            snprintf(want, sizeof(want), "\"%.*s\"", (int)len, c);
            const char *at = strstr(list, want);
            if (at == NULL || at > end) {
                T_FAIL("list %zu: no %s", k, want);
            }
        }
        /* One string per core: as many quotes as two per core. */
        size_t quotes = 0u;
        for (const char *p = list; p < end; ++p) {
            quotes += (*p == '"') ? 1u : 0u;
        }
        CHECK_EQ(quotes, 2u * cores + 2u);
    }
    /* German and English words both: a report in either language. */
    CHECK(strstr(data, "\"BESTANDEN\"") != NULL);
    CHECK(strstr(data, "\"PASS\"") != NULL);
    CHECK(strstr(data, "\"--\",\"[not loaded]\"]") != NULL);
}

/*
 * A word that would end the data's element or its string is escaped: "<"
 * as \u003c, quotes, backslashes and control bytes as JSON writes them; a
 * NULL word and one with an empty core are left out; the cap is the one
 * handed over.
 */
TEST_CASE(a_word_cannot_end_the_data_element)
{
    static const char *const ok[] = { "a</script>b", NULL, "  ", "(x)." };
    static const char *const fail[] = { "q\"\\\x01\x7f" };
    static const text_hl_words_t words = { ok, 4u, fail, 1u, NULL, 0u };
    wa_hl_t hl = { &words, 7u, { 0u }, { 0u } };
    free(g_page.buf);
    memset(&g_page, 0, sizeof(g_page));
    wa_out_t o;
    wa_out_init(&o, sink, &g_page);
    wa_app_body(&o, false, &hl);
    CHECK(wa_out_flush(&o));
    const char *data = strstr(g_page.buf, "id=\"fm-data\">");
    CHECK(data != NULL);
    if (data == NULL) {
        return;
    }
    const char *end = strstr(data, "</script>");
    CHECK(end != NULL && strstr(data, "\"ok\":[\"a\\u003c/script>b\","
                                      "\"(x)\"]") != NULL);
    CHECK(strstr(data, "\"fail\":[\"q\\\"\\\\\\u0001\\u007f\"]") != NULL);
    CHECK(strstr(data, "\"warn\":[],\"cap\":7,") != NULL);
    /* The data ends at its own end, after the tables. */
    CHECK(end != NULL && end > strstr(data, "\"label\":24}}"));
}

/* ------------------------------------------------------------ fixtures */

static FILE *g_fx;
static size_t g_lines;

static void hex(const void *bytes, size_t n)
{
    const unsigned char *b = bytes;
    fputc('"', g_fx);
    for (size_t i = 0u; i < n; ++i) {
        fprintf(g_fx, "%02x", b[i]);
    }
    fputc('"', g_fx);
}

/* One line: its kind, number, span room, bytes and text_hl_line()'s
 * spans. */
static void line_case(text_hl_kind_t kind, size_t no, size_t cap,
                      const char *s, size_t len)
{
    static text_hl_span_t sp[SPANS_MAX];
    const size_t n = text_hl_line(s, len, no, kind, text_view_words(), sp,
                                  cap);
    fprintf(g_fx, "%s[%d,%zu,%zu,", g_lines == 0u ? "" : ",\n", (int)kind,
            no, cap);
    hex(s, len);
    fputs(",[", g_fx);
    for (size_t k = 0u; k < n; ++k) {
        fprintf(g_fx, "%s[%u,%u,%u]", k == 0u ? "" : ",", sp[k].start,
                sp[k].len, (unsigned)sp[k].cls);
    }
    fputs("]]", g_fx);
    ++g_lines;
}

/* Every line of @p text, LF separated, from line number 0. */
static void text_cases(text_hl_kind_t kind, const char *text)
{
    size_t no = 0u;
    for (const char *p = text; *p != '\0'; ++no) {
        size_t len = strcspn(p, "\n");
        size_t body = len;
        if (body > 0u && p[body - 1u] == '\r') {
            --body;
        }
        line_case(kind, no, TEXT_VIEW_SPANS, p, body);
        p += len + (p[len] == '\n' ? 1u : 0u);
    }
}

/* A run in @p lang: 0 passes, 1 fails the idle limit, 2 is aborted
 * (test_text_hl.c's). */
static void run_in(int lang, int how)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.idle_max_a = (how == 1) ? 0.05f : 0.3f;
    c.hold_max_a = 0.3f;
    c.travel_max_ms = 1000u;
    c.brownout = (lang == 1);
    const ui_language_t *t = ui_text_table((ui_lang_t)lang);
    c.text = (t != NULL) ? t->servo : NULL;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    if (how == 2) {
        run_ms(4000u);
        servo_test_abort(&g.t, SERVO_TEST_AB_STOP, g.now);
    }
    run_out(200000u);
}

static void report_cases(void)
{
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        for (int how = 0; how < 3; ++how) {
            run_in(lang, how);
            CHECK(g.report_len > 500u);
            text_cases(TEXT_HL_KIND_TEXT, g.report);
        }
    }
}

static void kst_cases(void)
{
    static char text[KST_FILE_MAX + 1u];
    const kst_image_t img = sim_bench_image();
    const size_t len = kst_file_format(&img, kst_type(&img), text,
                                       KST_FILE_MAX);
    CHECK(len > 0u);
    text[len] = '\0';
    text_cases(TEXT_HL_KIND_TEXT, text);
}

static void profile_cases(void)
{
    DIR *d = opendir(PROFILE_DIR);
    CHECK(d != NULL);
    if (d == NULL) {
        return;
    }
    size_t files = 0u;
    static char text[65536];
    for (const struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        const size_t n = strlen(e->d_name);
        if (n < 6u || strcmp(e->d_name + n - 5u, ".json") != 0) {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", PROFILE_DIR, e->d_name);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL);
        if (f == NULL) {
            continue;
        }
        const size_t got = fread(text, 1u, sizeof(text) - 1u, f);
        fclose(f);
        text[got] = '\0';
        text_cases(TEXT_HL_KIND_JSON, text);
        ++files;
    }
    closedir(d);
    CHECK(files >= 3u);
}

/* The edges of each rule (text_hl.h), in every kind. */
static const char *const k_edges[] = {
    "", " ", "\t", "#", "  # a comment", "x # c", "x#c", "PASS", "PASSED",
    "pass", "Result: PASS", "Ergebnis: BESTANDEN", "NICHT BESTANDEN",
    "Ergebnis: NICHT BESTANDEN.", "-- --", "a--b", "[not loaded]",
    "x [not loaded] y", "not measured", "(cut short)", "6.00V", "6.00Volt",
    "5x", "12 mV", "12  mV", "1.2.3", "-3", "+4.5A", "-", "+", "50%",
    "3/s", "100m\xce\xa9", "4\xce\xa9", "90deg", "90 Grad", "1.5.", "a1.5V",
    ".5V", "1.V", "rpm 1200rpm 50Hz", "3kB 4MB 5B", "3 kB", "3 kBx",
    "00: 1A 2B 3C", "00: 1A 2B 3C ", "0F: 1A 2", "1A: zz", "checksum 9F3A77C1",
    "checksum  9F3A77C1  ", "checksum 9F3A77C", "checksum9F3A77C1",
    "journal=0a1b", "target=", "port=xyz", "Port=12", "SETTINGS IN FORCE",
    "ENDS (the search before programming)", "AKTIVE EINSTELLUNGEN",
    "\xc3\x84NDERUNGEN", "\xc3\xa4nderungen", "AB", "ABC", "AbC",
    "ABC (def)", "ABC: def", "Neutral = 1500us", "Neutral =1500us",
    "A label that runs far too long: x", " Indented: x", "a=b", "a: b",
    "Name:", "Folder:\tx", "\xff\xfe PASS", "\xc3 PASS", "PASS\xc3",
    "\"key\": 1", "  \"cells_min\": 2, \"x\": -1.5e+3,", "[true, false, null]",
    "nullx truex", "\"a\\\"b\" : 1", "\"unterminated \\", "- 1.", "1e",
    "1e+", "1E5", "{\"a\":[1,2,{\"b\":\"c\"}]}", "time (s);test;step",
    "1.5;2,5;3", "1,2", ";;", ",,", "a;b,c", "\xce\xa9;\xce\xa9",
};

static void edge_cases(void)
{
    for (size_t k = 0u; k < sizeof(k_edges) / sizeof(k_edges[0]); ++k) {
        for (int kind = 0; kind < 4; ++kind) {
            for (size_t no = 0u; no < 2u; ++no) {
                line_case((text_hl_kind_t)kind, no, TEXT_VIEW_SPANS,
                          k_edges[k], strlen(k_edges[k]));
            }
        }
    }
}

/* The tokens random lines are made of. */
static const char *const k_tokens[] = {
    "PASS", "FAIL", "BESTANDEN", "NICHT BESTANDEN", "ABORTED", "--",
    "[not loaded]", "not measured", "nicht gemessen", "6.00V", "0.210A",
    "5x", "12 mV", "1.2.3", "-3", "+4.5", "%", "/s", "m\xce\xa9",
    "\xce\xa9", "deg", "Grad", "#", "# c", "Result:", "Neutral =",
    "checksum", "1A", "FF", ":", " ", "  ", "\t", "(", ")", "=",
    "\xc3\x84\xc3\x96\xc3\x9c", "\xc3\xa4\xc3\xb6\xc3\xbc\xc3\x9f", "\xff",
    "\xc3", "\"", "\\", "{", "}", "[", "]", ",", ";", "true", "null",
    "e+3", "x", "ABC", "abc", "Hz", "rpm", "W", "kB", ".", "0", "9",
    "00:", "journal=", "_", "\xe2\x82\xac",
};

static uint32_t g_rand = 0x2545F491u;

static uint32_t next_rand(void)
{
    g_rand ^= g_rand << 13;
    g_rand ^= g_rand >> 17;
    g_rand ^= g_rand << 5;
    return g_rand;
}

static void random_cases(void)
{
    const size_t n_tok = sizeof(k_tokens) / sizeof(k_tokens[0]);
    char line[512];
    for (int i = 0; i < 4000; ++i) {
        size_t len = 0u;
        const uint32_t count = next_rand() % 16u;
        for (uint32_t t = 0u; t < count; ++t) {
            const char *tok = k_tokens[next_rand() % n_tok];
            const size_t tl = strlen(tok);
            if (len + tl < sizeof(line)) {
                memcpy(line + len, tok, tl);
                len += tl;
            }
        }
        const uint32_t r = next_rand();
        /* Mostly the viewer's room; one in eight a room of 1 to 4. */
        const size_t cap = (r % 8u == 0u) ? 1u + (r >> 8) % 4u
                                          : TEXT_VIEW_SPANS;
        line_case((text_hl_kind_t)((r >> 4) % 4u), (r >> 12) % 3u, cap,
                  line, len);
    }
}

/* Lines with more spans than the viewer's room. */
static void long_cases(void)
{
    static char line[2048];
    size_t len = 0u;
    for (int i = 0; i < 300; ++i) {
        line[len++] = (char)('a' + i % 26);
        line[len++] = ';';
    }
    line_case(TEXT_HL_KIND_CSV, 0u, TEXT_VIEW_SPANS, line, len);
    line_case(TEXT_HL_KIND_CSV, 5u, TEXT_VIEW_SPANS, line, len);
    len = 0u;
    for (int i = 0; i < 150; ++i) {
        len += (size_t)snprintf(line + len, sizeof(line) - len, "%dV x ", i);
    }
    line_case(TEXT_HL_KIND_TEXT, 3u, TEXT_VIEW_SPANS, line, len);
    line_case(TEXT_HL_KIND_JSON, 3u, TEXT_VIEW_SPANS, line, len);
}

static const char *const k_names[] = {
    "LIMIT001.TXT", "notes.txt", "LIMIT001.TXT.NEW", "a.JSON.new",
    "align.json", "ATEST001.CSV", "RCBENCH.LOG", "KST.NEW", "README",
    "a.txtx", "dir.txt/file", ".txt", "", "B.Txt.NeW", ".new",
    "x.csv.txt", "x.txt.csv", "\xc3\x84.txt", "a.TXT\xc3\xa4", "..txt",
    "a.new.new",
};

static void kind_cases(void)
{
    fputs("],\n\"kinds\":[", g_fx);
    for (size_t k = 0u; k < sizeof(k_names) / sizeof(k_names[0]); ++k) {
        fprintf(g_fx, "%s[", k == 0u ? "" : ",");
        hex(k_names[k], strlen(k_names[k]));
        fprintf(g_fx, ",%d]", (int)text_hl_kind_of(k_names[k]));
    }
}

/* Files the viewer splits into lines: its line count (text_view_lines()'s
 * rule) and each line's first byte and end, from the file's first byte. */
static const struct {
    const char *b;
    size_t n;
} k_files[] = {
#define FILE_OF(lit) { (lit), sizeof(lit) - 1u }
    FILE_OF(""), FILE_OF("\n"), FILE_OF("a"), FILE_OF("a\n"),
    FILE_OF("a\r\nb"), FILE_OF("a\rb\r"), FILE_OF("\r\n\r\n"),
    FILE_OF("\xef\xbb\xbfx\n"), FILE_OF("\xef\xbb\xbf"), FILE_OF("\xef\xbbx"),
    FILE_OF("a\n\rb\r\n\nc"), FILE_OF("\n\n"), FILE_OF("a\r\r\nb"),
    FILE_OF("\xef\xbb\xbf\r\n"), FILE_OF("x\n\xef\xbb\xbf"),
#undef FILE_OF
};

static void file_cases(void)
{
    static uint8_t mem[256];
    static uint32_t ends[64];
    text_buf_t tb;
    fputs("],\n\"files\":[", g_fx);
    for (size_t k = 0u; k < sizeof(k_files) / sizeof(k_files[0]); ++k) {
        CHECK_EQ(text_buf_init(&tb, mem, sizeof(mem), ends, 64u),
                 TEXT_BUF_OK);
        CHECK_EQ(text_buf_load(&tb, k_files[k].b, k_files[k].n), TEXT_BUF_OK);
        size_t n = text_buf_lines(&tb);
        if (text_buf_len(&tb) == 0u) {
            n = 0u;
        } else if (n > 1u && text_buf_line_len(&tb, n - 1u) == 0u) {
            --n;
        }
        const size_t bom = text_buf_bom(&tb) ? TEXT_BUF_BOM_LEN : 0u;
        fprintf(g_fx, "%s[", k == 0u ? "" : ",");
        hex(k_files[k].b, k_files[k].n);
        fputs(",[", g_fx);
        for (size_t i = 0u; i < n; ++i) {
            const size_t s = text_buf_line_start(&tb, i) + bom;
            fprintf(g_fx, "%s[%zu,%zu]", i == 0u ? "" : ",", s,
                    s + text_buf_line_len(&tb, i));
        }
        fputs("]]", g_fx);
    }
}

static void size_cases(void)
{
    static const uint64_t k_sizes[] = {
        0u, 1u, 512u, 1023u, 1024u, 1025u, 14848u, 1048575u, 1048576u,
        7392460u, 1073741823u, 1073741824u, 8589934592u, 9234567890u,
        1099511627776u,
    };
    fputs("],\n\"sizes\":[", g_fx);
    for (size_t k = 0u; k < sizeof(k_sizes) / sizeof(k_sizes[0]); ++k) {
        char text[32];
        files_size_text(text, sizeof(text), k_sizes[k]);
        fprintf(g_fx, "%s[%llu,\"%s\"]", k == 0u ? "" : ",",
                (unsigned long long)k_sizes[k], text);
    }
}

static void path_cases(void)
{
    static const char *const k_paths[] = {
        "run #7 & <x>.csv", "M\xc3\xbcller \"q\" 'a'.csv", "100%.txt",
        "a+b c.TXT", "x?y=1&z.txt", "(1)*!~.txt", "USER/LOGS/ATEST001.TXT",
        "BENCH/KST/CONF/Wing left.TXT", "\xc3\x84\xc3\x96\xc3\x9c/\xc3\x9f",
    };
    fputs("],\n\"paths\":[", g_fx);
    for (size_t k = 0u; k < sizeof(k_paths) / sizeof(k_paths[0]); ++k) {
        fputs(k == 0u ? "" : ",", g_fx);
        hex(k_paths[k], strlen(k_paths[k]));
    }
    fputs("]}\n", g_fx);
}

/* The page and the fixtures go to HL_OUT_DIR for check_wa_hl.py. */
TEST_CASE(the_page_and_the_fixtures_are_written_for_the_node_check)
{
    if (mkdir(HL_OUT_DIR, 0755) != 0 && errno != EEXIST) {
        CHECK(false);
        return;
    }
    const char *html = page_with_hl(false);
    FILE *f = fopen(HL_OUT_DIR "/page_en.html", "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        CHECK_EQ(fwrite(html, 1u, strlen(html), f), strlen(html));
        fclose(f);
    }
    g_fx = fopen(HL_OUT_DIR "/hl_fixtures.json", "wb");
    CHECK(g_fx != NULL);
    if (g_fx == NULL) {
        return;
    }
    g_lines = 0u;
    fputs("{\"lines\":[\n", g_fx);
    report_cases();
    kst_cases();
    profile_cases();
    edge_cases();
    random_cases();
    long_cases();
    kind_cases();
    file_cases();
    size_cases();
    path_cases();
    fclose(g_fx);
    CHECK(g_lines > 5000u);
}

int main(void)
{
    RUN(the_colours_are_the_viewers_in_both_palettes);
    RUN(the_words_and_the_cap_are_the_viewers);
    RUN(a_word_cannot_end_the_data_element);
    RUN(the_page_and_the_fixtures_are_written_for_the_node_check);
    free(g_page.buf);
    return test_summary("wa_hl");
}
