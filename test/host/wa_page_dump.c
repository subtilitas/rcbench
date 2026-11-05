/**
 * @file
 * @brief Writes the Wi-Fi page as the panel builds it, and the reports the
 *        panel writes, for the browser test.
 *
 * wa_mock_server.py runs this program and serves the files it writes, so
 * the page a browser gets in the test is the bytes wa_page_main() sends: the
 * same texts, tables, colours and scripts, with the panel's highlighter.
 *
 * Usage: wa_page_dump OUTDIR [CARD_EN [CARD_DE]]
 *
 * Writes into OUTDIR:
 *   - page_en.html and page_de.html, the page in each language.  CARD_EN
 *     and CARD_DE are text files of the card each page's ZIP line counts,
 *     one entry per line: a path, a tab and the size in bytes; a path that
 *     ends in "/" is a folder.  Without a card file the card is empty;
 *     without CARD_DE the German page counts CARD_EN's card.
 *   - report_<lang>_<how>.txt, the servo test's report as the panel writes
 *     it, for lang en and de and how pass (limits kept) and fail (idle
 *     current over its limit).
 *   - spans.json, an object from each report's file name to its lines;
 *     each line is the list of its spans as [class, text], as
 *     text_hl_line() classes them with the panel viewer's words and span
 *     room.  The class is the style sheet's two-letter name.
 *
 * Exit status 0 when every file is written, 1 when a file cannot be read or
 * written, 2 for a wrong call.
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rcbench_version.h"
#include "text_view.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "wa_app.h"
#include "wa_fakefs.h"
#include "wa_page.h"

#include "servo_test_rig.h"

/** @brief Bytes of one line of the card file at most, path and size. */
#define LINE_MAX_BYTES 1024u

/** @brief The page as it is written: a growing buffer. */
typedef struct {
    char  *buf;  /**< the bytes                          */
    size_t len;  /**< bytes in use                       */
    size_t cap;  /**< bytes allocated                    */
} page_t;

/**
 * @brief wa_out_t's sink: appends @p n bytes to the page.
 * @param ctx   the page_t
 * @param data  the bytes
 * @param n     how many
 * @return false when memory is gone
 */
static bool sink(void *ctx, const void *data, size_t n)
{
    page_t *p = ctx;
    if (p->len + n + 1u > p->cap) {
        const size_t cap = (p->len + n + 1u) * 2u;
        char *grown = realloc(p->buf, cap);
        if (grown == NULL) {
            return false;
        }
        p->buf = grown;
        p->cap = cap;
    }
    memcpy(p->buf + p->len, data, n);
    p->len += n;
    p->buf[p->len] = '\0';
    return true;
}

/**
 * @brief Adds the entries of the card file to the fake card.
 * @param fs    the card
 * @param file  the card file, or NULL for none
 * @return false when the file cannot be read
 */
static bool load_card(fake_fs_t *fs, const char *file)
{
    if (file == NULL) {
        return true;
    }
    FILE *f = fopen(file, "r");
    if (f == NULL) {
        fprintf(stderr, "wa_page_dump: cannot read %s: %s\n", file,
                strerror(errno));
        return false;
    }
    char line[LINE_MAX_BYTES];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *tab = strchr(line, '\t');
        if (line[0] == '\0' || tab == NULL) {
            continue;
        }
        *tab = '\0';
        const size_t n = strlen(line);
        const bool dir = (n > 0u && line[n - 1u] == '/');
        if (dir) {
            line[n - 1u] = '\0';
        }
        fake_add(fs, line, dir, strtoull(tab + 1, NULL, 10), 0);
    }
    fclose(f);
    return true;
}

/**
 * @brief The panel's highlighter words, span cap and colours, as
 *        panel_files_web_hl() hands them to the page.
 * @param hl  takes them
 */
static void panel_hl(wa_hl_t *hl)
{
    hl->words = text_view_words();
    hl->spans = TEXT_VIEW_SPANS;
    (void)text_view_class_rgb(UI_THEME_DARK, hl->dark, WA_HL_RGB);
    (void)text_view_class_rgb(UI_THEME_LIGHT, hl->light, WA_HL_RGB);
}

/**
 * @brief Writes one page.
 * @param dir  the folder
 * @param de   German page
 * @param fs   the card
 * @param hl   the highlighter
 * @return true when the file is written
 */
static bool write_page(const char *dir, bool de, const fake_fs_t *fs,
                       const wa_hl_t *hl)
{
    const wa_page_info_t pi = { "rcbench-1A2B", RCBENCH_VERSION_STRING,
                                0x7C0000u, de, hl };
    const wa_fs_t w = fake_wa((fake_fs_t *)fs);
    page_t page;
    memset(&page, 0, sizeof(page));
    wa_out_t o;
    wa_out_init(&o, sink, &page);
    wa_page_main(&o, &pi, &w);
    bool ok = wa_out_flush(&o) && page.buf != NULL;
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/page_%s.html", dir,
                   de ? "de" : "en");
    FILE *f = ok ? fopen(path, "wb") : NULL;
    if (f == NULL) {
        fprintf(stderr, "wa_page_dump: cannot write %s\n", path);
        ok = false;
    } else {
        ok = fwrite(page.buf, 1u, page.len, f) == page.len;
        ok = (fclose(f) == 0) && ok;
    }
    free(page.buf);
    return ok;
}

/**
 * @brief Writes @p text to OUTDIR/@p name.
 * @param dir   the folder
 * @param name  the file's name
 * @param text  the bytes
 * @param n     their count
 * @return true when the file is written
 */
static bool write_file(const char *dir, const char *name, const char *text,
                       size_t n)
{
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "wa_page_dump: cannot write %s\n", path);
        return false;
    }
    bool ok = fwrite(text, 1u, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

/**
 * @brief One servo test run to its end, its report in the rig.
 * @param lang  0 English, 1 German
 * @param fail  the idle current limit is under the idle current
 * @return true when the run started
 */
static bool run_report(int lang, bool fail)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.idle_max_a = fail ? 0.05f : 0.3f;
    c.hold_max_a = 0.3f;
    c.travel_max_ms = 1000u;
    c.brownout = (lang == 1);
    const ui_language_t *t = ui_text_table((ui_lang_t)lang);
    c.text = (t != NULL) ? t->servo : NULL;
    if (start(&c) != SERVO_TEST_START_OK) {
        return false;
    }
    run_out(200000u);
    return g.report_len > 0u;
}

/**
 * @brief One line's spans as JSON: [["hd","text"],...].
 * @param f     the file
 * @param line  the line's bytes
 * @param len   their count
 * @param no    the line's index
 */
static void line_json(FILE *f, const char *line, size_t len, size_t no)
{
    text_hl_span_t sp[TEXT_VIEW_SPANS];
    const size_t n = text_hl_line(line, len, no, TEXT_HL_KIND_TEXT,
                                  text_view_words(), sp, TEXT_VIEW_SPANS);
    fputc('[', f);
    for (size_t i = 0u; i < n; ++i) {
        fprintf(f, "%s[\"%s\",\"", (i == 0u) ? "" : ",",
                text_hl_class_css((text_hl_class_t)sp[i].cls));
        for (size_t k = sp[i].start; k < sp[i].start + sp[i].len; ++k) {
            const unsigned char b = (unsigned char)line[k];
            if (b == '"' || b == '\\') {
                fputc('\\', f);
                fputc(b, f);
            } else if (b < 0x20u) {
                fprintf(f, "\\u%04x", (unsigned)b);
            } else {
                fputc(b, f);
            }
        }
        fputs("\"]", f);
    }
    fputc(']', f);
}

/**
 * @brief Writes the four reports and spans.json.
 * @param dir  the folder
 * @return true when every file is written
 */
static bool write_reports(const char *dir)
{
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/spans.json", dir);
    FILE *js = fopen(path, "wb");
    if (js == NULL) {
        fprintf(stderr, "wa_page_dump: cannot write %s\n", path);
        return false;
    }
    bool ok = true;
    fputc('{', js);
    for (int i = 0; i < 4 && ok; ++i) {
        const int lang = i / 2;
        const bool fail = (i % 2) == 1;
        char name[64];
        (void)snprintf(name, sizeof(name), "report_%s_%s.txt",
                       lang == 0 ? "en" : "de", fail ? "fail" : "pass");
        ok = run_report(lang, fail)
             && write_file(dir, name, g.report, g.report_len);
        fprintf(js, "%s\"%s\":[", (i == 0) ? "" : ",", name);
        size_t no = 0u;
        for (const char *p = g.report; ok && *p != '\0'; ++no) {
            const size_t len = strcspn(p, "\n");
            fputs((no == 0u) ? "" : ",", js);
            line_json(js, p, len, no);
            p += len + (p[len] == '\n' ? 1u : 0u);
        }
        fputc(']', js);
    }
    fputc('}', js);
    ok = (fclose(js) == 0) && ok;
    return ok;
}

/**
 * @brief The program's entry.
 * @param argc  the argument count
 * @param argv  the arguments: OUTDIR and optionally the two card files
 * @return 0, 1 or 2 as the file's head says
 */
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: wa_page_dump OUTDIR [CARD_EN [CARD_DE]]\n");
        return 2;
    }
    if (mkdir(argv[1], 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "wa_page_dump: cannot make %s: %s\n", argv[1],
                strerror(errno));
        return 1;
    }
    wa_hl_t hl;
    memset(&hl, 0, sizeof(hl));
    panel_hl(&hl);
    bool ok = true;
    for (int de = 0; de < 2 && ok; ++de) {
        const char *card = (argc > 2) ? argv[(de != 0 && argc == 4) ? 3 : 2]
                                      : NULL;
        fake_fs_t fs;
        fake_init(&fs);
        ok = load_card(&fs, card) && write_page(argv[1], de != 0, &fs, &hl);
        fake_free(&fs);
    }
    ok = ok && write_reports(argv[1]);
    /* The rig's checks count their failures in the harness of greatest.h. */
    t_install_crash_handler();
    return (ok && test_summary("wa_page_dump") == 0) ? 0 : 1;
}
