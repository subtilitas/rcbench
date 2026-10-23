/*
 * The highlighter's rig, for test_text_hl.c and test_text_hl_limrun.c:
 * the verdict words from the report writers' text IDs in every language,
 * the invariants every line's spans keep, and the checks a whole report
 * is held to.
 *
 * The words are built here from the IDs, as the viewer will build them:
 * a word changed in ui_text.def changes them, and a report that writes a
 * word the lists lack fails the report checks.  "--" is what
 * servo_report.c writes for a value it has none of; "[not loaded]" is
 * kst_file.c's mark of a field no load writes.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "servo_str.h"
#include "text_hl.h"
#include "ui_text.h"

#define HL_SPANS 64

static const servo_str_t k_hl_ok[] = {SERVO_STR_PASS};
static const servo_str_t k_hl_fail[] = {SERVO_STR_FAIL};
static const servo_str_t k_hl_warn[] = {
    SERVO_STR_ABORTED,       SERVO_STR_NOT_MEASURABLE,
    SERVO_STR_R_NOT_CHECKED, SERVO_STR_R_NOT_MEASURED,
    SERVO_STR_R_BO_NOT_RUN,  SERVO_STR_R_CUT_SHORT,
    SERVO_STR_R_BROWNOUT_NOT_RUN,
};
static const char *const k_hl_marks[] = {"--", "[not loaded]"};

#define HL_N(a) (sizeof(a) / sizeof((a)[0]))

static const char *hl_ok[HL_N(k_hl_ok) * UI_LANG_COUNT];
static const char *hl_fail[HL_N(k_hl_fail) * UI_LANG_COUNT];
static const char *hl_warn[HL_N(k_hl_warn) * UI_LANG_COUNT + HL_N(k_hl_marks)];
static text_hl_words_t hl_words;

/* The servo table of @p lang, NULL for English. */
static inline const char *const *hl_table(int lang)
{
    const ui_language_t *t = ui_text_table((ui_lang_t)lang);
    return (t != NULL) ? t->servo : NULL;
}

/* @p ids in every language into @p out; their count. */
static inline size_t hl_fill(const char **out, const servo_str_t *ids,
                             size_t n)
{
    size_t k = 0u;
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (size_t i = 0u; i < n; ++i) {
            out[k++] = servo_str_in(hl_table(l), ids[i]);
        }
    }
    return k;
}

/* The verdict words of every language. */
static inline const text_hl_words_t *hl_bench_words(void)
{
    hl_words.ok = hl_ok;
    hl_words.ok_n = hl_fill(hl_ok, k_hl_ok, HL_N(k_hl_ok));
    hl_words.fail = hl_fail;
    hl_words.fail_n = hl_fill(hl_fail, k_hl_fail, HL_N(k_hl_fail));
    hl_words.warn = hl_warn;
    size_t w = hl_fill(hl_warn, k_hl_warn, HL_N(k_hl_warn));
    for (size_t i = 0u; i < HL_N(k_hl_marks); ++i) {
        hl_warn[w++] = k_hl_marks[i];
    }
    hl_words.warn_n = w;
    return &hl_words;
}

/* The spans of @p line in hl_spans; their count, with every invariant
 * checked: they cover the line in order without a gap, neighbours differ,
 * each class is in range, and no edge splits a UTF-8 sequence. */
static text_hl_span_t hl_spans[HL_SPANS];

static inline size_t hl_line(const char *line, size_t line_no,
                             text_hl_kind_t kind)
{
    const size_t len = strlen(line);
    const size_t n = text_hl_line(line, len, line_no, kind, hl_bench_words(),
                                  hl_spans, HL_SPANS);
    size_t at = 0u;
    for (size_t k = 0u; k < n; ++k) {
        const text_hl_span_t *s = &hl_spans[k];
        if (s->start != at || s->len == 0u || s->cls >= TEXT_HL_CLASSES ||
            (k > 0u && s->cls == hl_spans[k - 1u].cls) ||
            (s->start < len &&
             ((unsigned char)line[s->start] & 0xC0u) == 0x80u)) {
            T_FAIL("line \"%s\": span %zu at %u is out of order", line, k,
                   (unsigned)s->start);
        }
        at = s->start + s->len;
    }
    if (at != len || (len > 0u && n == 0u)) {
        T_FAIL("line \"%s\": the spans cover %zu of %zu bytes", line, at,
               len);
    }
    return n;
}

/* The class of the byte at @p at after hl_line(). */
static inline int hl_class_at(size_t n, size_t at)
{
    for (size_t k = 0u; k < n; ++k) {
        if (at >= hl_spans[k].start && at < hl_spans[k].start + hl_spans[k].len) {
            return hl_spans[k].cls;
        }
    }
    return -1;
}

/* Whether @p line's text from @p at is one span of @p cls of @p len
 * bytes. */
static inline int hl_span_is(size_t n, size_t at, size_t len, int cls)
{
    for (size_t k = 0u; k < n; ++k) {
        if (hl_spans[k].start == at) {
            return hl_spans[k].len == len && hl_spans[k].cls == cls;
        }
    }
    return 0;
}

/* Whether @p line is one of the heading strings @p heads in @p lang. */
static inline int hl_is_head(const char *line, int lang,
                             const servo_str_t *heads, size_t n_heads)
{
    for (size_t i = 0u; i < n_heads; ++i) {
        if (strcmp(line, servo_str_in(hl_table(lang), heads[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * A whole report in @p lang, lines separated by LF:
 *   - line 1 and the lines that are a heading entry are each one heading
 *     span, and no other line is one;
 *   - the Result line has its label, then the verdict @p verdict in
 *     @p want_cls;
 *   - every "--" standing alone and every "not measured" of the language
 *     is WARN;
 *   - every unit span ends with a unit of its class.
 * Returns the count of headings found.
 */
static inline size_t hl_report(const char *report, int lang,
                               const servo_str_t *heads, size_t n_heads,
                               servo_str_t verdict, int want_cls)
{
    static char line[512];
    const char *const *tab = hl_table(lang);
    const char *result_fmt = servo_str_in(tab, SERVO_STR_R_RESULT);
    const size_t result_len = (size_t)(strchr(result_fmt, '%') - result_fmt);
    const char *word = servo_str_in(tab, verdict);
    const char *unmeasured = servo_str_in(tab, SERVO_STR_R_NOT_MEASURED);
    size_t heads_found = 0u, results = 0u, no = 0u;

    for (const char *p = report; *p != '\0'; ++no) {
        const char *e = strchr(p, '\n');
        const size_t len = (e != NULL) ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof(line)) {
            T_FAIL("line %zu is %zu bytes", no, len);
            return 0u;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p += len + (e != NULL ? 1u : 0u);

        const size_t n = hl_line(line, no, TEXT_HL_KIND_TEXT);
        const int is_head = (n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
        const int want_head = (no == 0u) ||
                              hl_is_head(line, lang, heads, n_heads);
        if (is_head != want_head) {
            T_FAIL("lang %d line %zu \"%s\": heading %d, want %d", lang, no,
                   line, is_head, want_head);
        }
        heads_found += (size_t)is_head;
        if (len > result_len && strncmp(line, result_fmt, result_len) == 0) {
            const size_t label = (size_t)(strchr(line, ':') - line) + 1u;
            results++;
            if (!hl_span_is(n, 0u, label, TEXT_HL_LABEL) ||
                strncmp(line + result_len, word, strlen(word)) != 0 ||
                !hl_span_is(n, result_len, strlen(word), want_cls)) {
                T_FAIL("lang %d: \"%s\" is not label and verdict %d", lang,
                       line, want_cls);
            }
        }
        for (const char *d = strstr(line, "--"); d != NULL;
             d = strstr(d + 2, "--")) {
            const size_t at = (size_t)(d - line);
            if ((at == 0u || line[at - 1u] == ' ') &&
                (d[2] == '\0' || d[2] == ' ') &&
                hl_class_at(n, at) != TEXT_HL_WARN) {
                T_FAIL("lang %d: \"--\" in \"%s\" is not WARN", lang, line);
            }
        }
        for (const char *d = strstr(line, unmeasured); d != NULL;
             d = strstr(d + 1, unmeasured)) {
            if (!is_head && hl_class_at(n, (size_t)(d - line)) != TEXT_HL_WARN) {
                T_FAIL("lang %d: \"%s\" in \"%s\" is not WARN", lang,
                       unmeasured, line);
            }
        }
        for (size_t k = 0u; k < n; ++k) {
            const int c = hl_spans[k].cls;
            if (c < TEXT_HL_VOLT || c > TEXT_HL_UNIT) {
                continue;
            }
            const char *s = line + hl_spans[k].start;
            size_t u = hl_spans[k].len;
            while (u > 0u && s[u - 1u] != ' ' &&
                   !(s[u - 1u] >= '0' && s[u - 1u] <= '9')) {
                u--;
            }
            if ((int)text_hl_unit_class(s + u, hl_spans[k].len - u) != c) {
                T_FAIL("lang %d: span \"%.*s\" is class %d", lang,
                       (int)hl_spans[k].len, s, c);
            }
        }
    }
    if (results != 1u) {
        T_FAIL("lang %d: %zu Result lines", lang, results);
    }
    return heads_found;
}
