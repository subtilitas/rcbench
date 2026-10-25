/*
 * The highlighter: each rule at its edges, the units against the report
 * formats of ui_text.def, the servo test's report in English and German
 * line by line (passed, failed and aborted runs), an ESC profile of
 * record as JSON, CSV, and the span room running out.
 *
 * The limit run's report and the KST files are test_text_hl_limrun.c; the
 * words and the report checks are text_hl_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "servo_test_rig.h"
#include "text_hl_rig.h"

/* --- the classes and the units ----------------------------------------- */

/* The X-macro gives one style name per class, all different. */
TEST_CASE(every_class_has_its_own_style_name)
{
    for (int a = 0; a < TEXT_HL_CLASSES; ++a) {
        const char *s = text_hl_class_css((text_hl_class_t)a);
        CHECK_EQ(strlen(s), 2u);
        for (int b = 0; b < a; ++b) {
            if (strcmp(s, text_hl_class_css((text_hl_class_t)b)) == 0) {
                T_FAIL("classes %d and %d are both %s", a, b, s);
            }
        }
    }
    CHECK_EQ(TEXT_HL_TEXT, 0);
    CHECK_EQ(TEXT_HL_CSV_HEAD, 17);
    CHECK_EQ(TEXT_HL_CLASSES, 18);
    CHECK_STR_EQ(text_hl_class_css(TEXT_HL_HEADING), "hd");
    CHECK_STR_EQ(text_hl_class_css(TEXT_HL_CLASSES), "tx");
    CHECK_STR_EQ(text_hl_class_css((text_hl_class_t)-1), "tx");
}

TEST_CASE(the_unit_table_classes_each_unit)
{
    CHECK_EQ(text_hl_unit_class("V", 1u), TEXT_HL_VOLT);
    CHECK_EQ(text_hl_unit_class("mV", 2u), TEXT_HL_VOLT);
    CHECK_EQ(text_hl_unit_class("mA", 2u), TEXT_HL_CURR);
    CHECK_EQ(text_hl_unit_class("W", 1u), TEXT_HL_POWER);
    CHECK_EQ(text_hl_unit_class("rpm", 3u), TEXT_HL_RATE);
    CHECK_EQ(text_hl_unit_class("Hz", 2u), TEXT_HL_RATE);
    CHECK_EQ(text_hl_unit_class("%", 1u), TEXT_HL_UNIT);
    CHECK_EQ(text_hl_unit_class("m\xCE\xA9", 3u), TEXT_HL_UNIT);
    CHECK_EQ(text_hl_unit_class("\xCE\xA9", 2u), TEXT_HL_UNIT);
    CHECK_EQ(text_hl_unit_class("m\xCE", 2u), TEXT_HL_TEXT);
    CHECK_EQ(text_hl_unit_class("v", 1u), TEXT_HL_TEXT);
    CHECK_EQ(text_hl_unit_class("Vx", 1u), TEXT_HL_VOLT);
    CHECK_EQ(text_hl_unit_class("Vx", 2u), TEXT_HL_TEXT);
    CHECK_EQ(text_hl_unit_class(NULL, 1u), TEXT_HL_TEXT);
    for (size_t k = 0u; k < TEXT_HL_UNIT_COUNT; ++k) {
        const text_hl_unit_t *u = &text_hl_units[k];
        CHECK_EQ(text_hl_unit_class(u->unit, strlen(u->unit)), u->cls);
        CHECK(u->cls >= TEXT_HL_VOLT && u->cls <= TEXT_HL_UNIT);
    }
}

/* Words the report formats put behind a number that are no unit.  A new
 * word behind a conversion fails the case below until it is a unit of
 * the table or listed here. */
static const char *const k_not_units[] = {
    "x", "of", "an", "too", "mit", "von", "nicht", "not", "late", "lost",
    "free", "frei", "time", "after", "angle", "bound", "counted", "reading",
    "readings", "supply", "unseen", "values", "windows", "written",
    "movements", "Bewegungen", "Fenster", "Messwert", "Messwerte", "Werte",
    "Winkelmesswerte", "blockiert", "geschrieben", "nachher",
    "gez\xC3\xA4hlten", "before", "vorher",
};

/* Whether @p w of @p n bytes is in k_not_units. */
static int not_unit(const char *w, size_t n)
{
    for (size_t k = 0u; k < sizeof(k_not_units) / sizeof(k_not_units[0]);
         ++k) {
        if (strlen(k_not_units[k]) == n && memcmp(k_not_units[k], w, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Every word a report format of either language puts behind a numeric
 * conversion, after at most one space, is a unit of the table or a word
 * of k_not_units: a unit a writer adds is classed or this fails. */
TEST_CASE(every_unit_the_report_formats_write_is_in_the_table)
{
    size_t units = 0u;
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (int id = 0; id < SERVO_STR_COUNT; ++id) {
            const char *s = servo_str_in(hl_table(l), (servo_str_t)id);
            for (const char *p = strchr(s, '%'); p != NULL;
                 p = strchr(p, '%')) {
                p++;
                if (*p == '%') {
                    p++;
                    continue;
                }
                p += strspn(p, "-+ #0123456789.lzh");
                const char conv = *p;
                if (conv == '\0') {
                    break;
                }
                p++;
                if (strchr("diouxXfFeEgG", conv) == NULL) {
                    continue;
                }
                const char *w = (*p == ' ') ? p + 1 : p;
                size_t n;
                if (w[0] == '%' && w[1] == '%') {
                    w++;
                    n = 1u;
                } else if (w[0] == '/') {
                    n = 1u + strcspn(w + 1, " ,;:.()\n");
                } else {
                    n = strcspn(w, " ,;:.()\n%-+/\x27");
                }
                if (n == 0u) {
                    continue;
                }
                if (text_hl_unit_class(w, n) != TEXT_HL_TEXT) {
                    units++;
                } else if (!not_unit(w, n)) {
                    T_FAIL("lang %d id %d: \"%.*s\" behind a number is no "
                           "unit and not listed", l, id, (int)n, w);
                }
            }
        }
    }
    CHECK(units > 50u);
}

/* --- the rules at their edges ------------------------------------------- */

TEST_CASE(a_heading_is_upper_case_before_its_parenthesis)
{
    size_t n = hl_line("SETTINGS IN FORCE", 5u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
    n = hl_line("RESULTS PER STEP (currents in A, times in ms)", 5u,
                TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
    n = hl_line("\xC3\x9C" "BERSICHT", 5u, TEXT_HL_KIND_TEXT);   /* ÜBERSICHT */
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
    n = hl_line("ERGEBNISSE JE STUFE (Str\xC3\xB6me in A)", 5u,
                TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
    /* Not headings: a lower-case letter, a German one too; a digit first;
     * 2 characters; letters after a space at the start. */
    static const char *const k_no[] = {
        "Set V  Meas V  V min", "LEFT ANGLE:     150 before",
        /* A German letter before a hexadecimal one takes two literals. */
        /* NOLINTNEXTLINE(bugprone-suspicious-missing-comma) */
        "\xC3\x9C" "bersicht", "GR\xC3\x96\xC3\x9F" "E", "4.80 V 6.00 V",
        "OK", "  NOT MEASURED", "registers 32", "Ab",
        "PROG PORT 1 GPIO 13, 333 Hz",
    };
    for (size_t k = 0u; k < sizeof(k_no) / sizeof(k_no[0]); ++k) {
        n = hl_line(k_no[k], 5u, TEXT_HL_KIND_TEXT);
        if (n == 1u && hl_spans[0].cls == TEXT_HL_HEADING) {
            T_FAIL("\"%s\" is a heading", k_no[k]);
        }
    }
    /* A label line in capitals is a label: a KST field. */
    n = hl_line("SPD = 1.0 %", 5u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 5u, TEXT_HL_LABEL));
    n = hl_line("STOP: x", 5u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 5u, TEXT_HL_LABEL));
    /* Line 1 is a heading whatever it holds. */
    n = hl_line("KST servo configuration, format 1", 0u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
    n = hl_line("ABC", 1u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
}

TEST_CASE(a_label_is_at_column_0_and_ends_at_its_mark)
{
    size_t n = hl_line("Device:         MG90S", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 7u, TEXT_HL_LABEL));
    n = hl_line("Neutral = 1500.0 us", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 9u, TEXT_HL_LABEL));
    CHECK(hl_span_is(n, 10u, 9u, TEXT_HL_UNIT));
    n = hl_line("Pr\xC3\xBC" "fling:        DS3218", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 10u, TEXT_HL_LABEL));
    n = hl_line("Result:", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 7u, TEXT_HL_LABEL));
    n = hl_line(":", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 1u, TEXT_HL_LABEL));
    /* The mark in the 24th cell is a label's, in the 25th not. */
    n = hl_line("abcdefghijklmnopqrstuvw: x", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 24u, TEXT_HL_LABEL));
    n = hl_line("abcdefghijklmnopqrstuvwx: x", 3u, TEXT_HL_KIND_TEXT);
    CHECK_EQ(hl_class_at(n, 0u), TEXT_HL_TEXT);
    /* 23 German letters and the mark: cells, not bytes. */
    char de[64] = "";
    for (int k = 0; k < 23; ++k) {
        strcat(de, "\xC3\xA4");
    }
    strcat(de, ": x");
    n = hl_line(de, 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 47u, TEXT_HL_LABEL));
    /* No label: no blank after the first mark, a blank in front, a URL. */
    static const char *const k_no[] = {
        "Time:12", "http://x.y z", " Device: x", "a=b c: d",
        "Idle current     highest 0.123 A, limit 0.30 A: PASS",
    };
    for (size_t k = 0u; k < sizeof(k_no) / sizeof(k_no[0]); ++k) {
        n = hl_line(k_no[k], 3u, TEXT_HL_KIND_TEXT);
        if (hl_class_at(n, 0u) == TEXT_HL_LABEL) {
            T_FAIL("\"%s\" has a label", k_no[k]);
        }
    }
}

/* A word matches whole and in its case; the longest at a place wins. */
TEST_CASE(a_verdict_word_matches_whole_and_in_its_case)
{
    size_t n = hl_line("Moves seen       0 unseen: PASS", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 27u, 4u, TEXT_HL_OK));
    n = hl_line("x PASSED y", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("x XPASS y", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("xPASS y", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("x PASSz", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("x zPASS", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("x pass y", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("x PASS, FAIL.", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 2u, 4u, TEXT_HL_OK));
    CHECK(hl_span_is(n, 8u, 4u, TEXT_HL_FAIL));
    /* NICHT BESTANDEN is FAIL, not NICHT and an OK BESTANDEN. */
    n = hl_line("Ergebnis:        NICHT BESTANDEN", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 17u, 15u, TEXT_HL_FAIL));
    n = hl_line("x BESTANDEN", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 2u, 9u, TEXT_HL_OK));
    /* A word at the line's end and at its start. */
    n = hl_line("ABORTED - the operator", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 7u, TEXT_HL_WARN));
    /* The writers' strings by their core: "(cut short)", "Not run.", the
     * tail of "Brown-out:      not run". */
    n = hl_line("Steps: 3 (cut short)", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 10u, 9u, TEXT_HL_WARN));
    n = hl_line("x Not run. y not run", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 2u, 7u, TEXT_HL_WARN));
    CHECK(hl_span_is(n, 13u, 7u, TEXT_HL_WARN));
    n = hl_line("x verk\xC3\xBCrzt y", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 2u, 9u, TEXT_HL_WARN));
    /* A German letter beside a word is part of a longer word. */
    n = hl_line("x \xC3\xA4PASS y", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    /* The marks: "--" alone, not in "---"; "[not loaded]". */
    n = hl_line("Supply:         --", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 16u, 2u, TEXT_HL_WARN));
    n = hl_line("x --- y", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("Lock = 1 [not loaded]", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 9u, 12u, TEXT_HL_WARN));
    /* No words: plain text. */
    n = text_hl_line("x PASS", 6u, 3u, TEXT_HL_KIND_TEXT, NULL, hl_spans,
                     HL_SPANS);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    const char *const empty_words[] = {NULL, "", "  ", "()"};
    const text_hl_words_t w = {empty_words, 4u, NULL, 0u, NULL, 3u};
    n = text_hl_line("x PASS ()", 9u, 3u, TEXT_HL_KIND_TEXT, &w, hl_spans,
                     HL_SPANS);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    /* A word of one byte. */
    const char *const one[] = {"X"};
    const text_hl_words_t w1 = {one, 1u, NULL, 0u, NULL, 0u};
    n = text_hl_line("a X b", 5u, 3u, TEXT_HL_KIND_TEXT, &w1, hl_spans,
                     HL_SPANS);
    CHECK(n == 3u && hl_spans[1].cls == TEXT_HL_OK && hl_spans[1].len == 1u);
}

TEST_CASE(a_word_core_is_the_word_itself)
{
    size_t n = 0u;
    const char *w = text_hl_word_core("Brown-out:      not run", &n);
    CHECK(n == 7u && strncmp(w, "not run", n) == 0);
    w = text_hl_word_core(" (cut short)", &n);
    CHECK(n == 9u && strncmp(w, "cut short", n) == 0);
    w = text_hl_word_core("Nicht gelaufen.", &n);
    CHECK(n == 14u && strncmp(w, "Nicht gelaufen", n) == 0);
    w = text_hl_word_core("PASS", &n);
    CHECK(n == 4u && strncmp(w, "PASS", n) == 0);
    w = text_hl_word_core("[not loaded]", &n);
    CHECK(n == 12u && w[0] == '[');
    w = text_hl_word_core("a:b", &n);
    CHECK(n == 3u && w[0] == 'a');
    w = text_hl_word_core("  x  ", &n);
    CHECK(n == 1u && w[0] == 'x');
    w = text_hl_word_core("   ", &n);
    CHECK(n == 0u && w != NULL);
    w = text_hl_word_core(NULL, &n);
    CHECK(n == 0u && w != NULL);
}

/* A number with a unit is one span of the unit's class; a bare number,
 * one in a word or one with a word behind it is text. */
TEST_CASE(a_number_with_a_unit_takes_the_units_class)
{
    static const struct {
        const char *line;
        unsigned at, len;
        int cls;
    } k[] = {
        {"x 4.80 V", 2u, 6u, TEXT_HL_VOLT},
        {"x 12 mV,", 2u, 5u, TEXT_HL_VOLT},
        {"x 0.30 A, y", 2u, 6u, TEXT_HL_CURR},
        {"x +0.15 A", 2u, 7u, TEXT_HL_CURR},
        {"x 5 W", 2u, 3u, TEXT_HL_POWER},
        {"x 333 Hz", 2u, 6u, TEXT_HL_RATE},
        {"x 12000 rpm", 2u, 9u, TEXT_HL_RATE},
        {"x 1500 us", 2u, 7u, TEXT_HL_UNIT},
        {"x 80 %", 2u, 4u, TEXT_HL_UNIT},
        {"x 80%", 2u, 3u, TEXT_HL_UNIT},
        {"x 10.0 /s", 2u, 7u, TEXT_HL_UNIT},
        {"x -31.5 Grad", 2u, 10u, TEXT_HL_UNIT},
        {"+/-90 deg", 2u, 7u, TEXT_HL_UNIT},
        {"x 100.0 mOhm", 2u, 10u, TEXT_HL_UNIT},
        /* The ohm sign is two bytes of UTF-8: 0xCE 0xA9. */
        {"x 100.0 m\xCE\xA9,", 2u, 9u, TEXT_HL_UNIT},
        {"x 120 \xCE\xA9", 2u, 6u, TEXT_HL_UNIT},
        {"x 300 B", 2u, 5u, TEXT_HL_UNIT},
        {"(4 kB)", 1u, 4u, TEXT_HL_UNIT},
        {"x 3.0 MB", 2u, 6u, TEXT_HL_UNIT},
        {"x 2.0 s", 2u, 5u, TEXT_HL_UNIT},
        /* Joined, as the reports write a unit: no space between. */
        {"x 6.00V", 2u, 5u, TEXT_HL_VOLT},
        {"x 6.00V,", 2u, 5u, TEXT_HL_VOLT},
        {"6.00V", 0u, 5u, TEXT_HL_VOLT},
        {"x 12mV", 2u, 4u, TEXT_HL_VOLT},
        {"x +0.15A", 2u, 6u, TEXT_HL_CURR},
        {"x 333Hz)", 2u, 5u, TEXT_HL_RATE},
        {"x 40s", 2u, 3u, TEXT_HL_UNIT},
        {"x 1500us", 2u, 6u, TEXT_HL_UNIT},
        {"x 10.0/s", 2u, 6u, TEXT_HL_UNIT},
        {"x 100.0m\xCE\xA9,", 2u, 8u, TEXT_HL_UNIT},
        {"x 120\xCE\xA9", 2u, 5u, TEXT_HL_UNIT},
        {"x 800-2200us", 6u, 6u, TEXT_HL_UNIT},
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        const size_t n = hl_line(k[i].line, 3u, TEXT_HL_KIND_TEXT);
        if (!hl_span_is(n, k[i].at, k[i].len, k[i].cls)) {
            T_FAIL("\"%s\" has no span %d of %u bytes at %u", k[i].line,
                   k[i].cls, k[i].len, k[i].at);
        }
    }
    static const char *const k_text[] = {
        "x 1.8 x free", "AS5600 x", "GP13 A", "rcbench 0.0.0 V", "x 20k_sel",
        "x 3 Ab", "x 4.80  V", "x 1.5.", "x 800-2200", "x 3. A",
        "Stretch = x1", "x 1500 usx", "x 5 v", "x 5 \xCE\xA9x", "x 5 m\xCE",
        /* A word joined to a number is no unit, nor are the letters of
         * one that run on. */
        "x 6.00Volt", "x 5x", "x 5x free", "x 3Ab", "x 5v", "x 1500usx",
        "x 5\xCE\xA9x", "x 5m\xCE", "x 4.80Vs", "x 5V_",
    };
    for (size_t i = 0u; i < sizeof(k_text) / sizeof(k_text[0]); ++i) {
        const size_t n = hl_line(k_text[i], 3u, TEXT_HL_KIND_TEXT);
        for (size_t s = 0u; s < n; ++s) {
            if (hl_spans[s].cls >= TEXT_HL_VOLT &&
                hl_spans[s].cls <= TEXT_HL_UNIT) {
                T_FAIL("\"%s\" has a unit span at %u", k_text[i],
                       (unsigned)hl_spans[s].start);
            }
        }
    }
    /* A range: the second number takes the unit. */
    size_t n = hl_line("x 800-2200 us", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 6u, 7u, TEXT_HL_UNIT));
    /* At the line's end and right after a German letter's word. */
    n = hl_line("3.00 A", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 6u, TEXT_HL_CURR));
    n = hl_line("x \xC3\xBC" "3.0 V", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
}

TEST_CASE(a_comment_runs_from_its_mark_to_the_end)
{
    size_t n = hl_line("# set by hand: 1500 us", 3u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_COMMENT);
    n = hl_line("  # indented", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 2u, 10u, TEXT_HL_COMMENT));
    n = hl_line("Neutral = 1520 us  # was 1500.2", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 9u, TEXT_HL_LABEL));
    CHECK(hl_span_is(n, 10u, 7u, TEXT_HL_UNIT));
    CHECK(hl_span_is(n, 19u, 12u, TEXT_HL_COMMENT));
    n = hl_line("step#3 PASS", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 7u, 4u, TEXT_HL_OK));
    /* A heading-like comment is a comment. */
    n = hl_line("# NOTES", 0u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_COMMENT);
}

TEST_CASE(register_checksum_and_journal_lines_are_hex)
{
    static const char *const k_hex[] = {
        "00: 00 F5 F5 14 48 77 04 65", "18: 00 00 00 0A 00 10 00 00  ",
        "08: 41", "checksum FE83C002", "checksum  fe83c002",
        "journal=00112233AABB", "target=FF", "port=1", "pin=FF", "check=1A2B",
    };
    for (size_t k = 0u; k < sizeof(k_hex) / sizeof(k_hex[0]); ++k) {
        const size_t n = hl_line(k_hex[k], 3u, TEXT_HL_KIND_TEXT);
        if (n != 1u || hl_spans[0].cls != TEXT_HL_HEX) {
            T_FAIL("\"%s\" is not hex", k_hex[k]);
        }
    }
    static const char *const k_no[] = {
        "00: 1A 2", "00: 1G", "00: 1", "00: ", "0: 1A", "00:1A 2B", "checksum FE83",
        "checksumFE83C002", "checksum FE83C00G", "Journal=00", "journal=",
        "journal=xyz", "=00", "registers 32",
    };
    for (size_t k = 0u; k < sizeof(k_no) / sizeof(k_no[0]); ++k) {
        const size_t n = hl_line(k_no[k], 3u, TEXT_HL_KIND_TEXT);
        if (n == 1u && hl_spans[0].cls == TEXT_HL_HEX) {
            T_FAIL("\"%s\" is hex", k_no[k]);
        }
    }
}

/* --- the servo test's report --------------------------------------------- */

static const servo_str_t k_heads[] = {
    SERVO_STR_R_TITLE,  SERVO_STR_R_SETTINGS, SERVO_STR_R_PER_STEP,
    SERVO_STR_R_BO_HEAD, SERVO_STR_R_LIM_HEAD, SERVO_STR_R_UNM_HEAD,
    SERVO_STR_R_ENC_HEAD,
};

/* A run in @p lang: 0 passes, 1 fails the idle limit, 2 is aborted. */
static void run_in(int lang, int how)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.idle_max_a = (how == 1) ? 0.05f : 0.3f;
    c.hold_max_a = 0.3f;
    c.travel_max_ms = 1000u;
    c.brownout = (lang == 1);
    c.text = hl_table(lang);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    if (how == 2) {
        run_ms(4000u);
        servo_test_abort(&g.t, SERVO_TEST_AB_STOP, g.now);
    }
    run_out(200000u);
}

/* The report of every verdict in both languages, line by line. */
TEST_CASE(the_servo_test_report_is_classed_line_by_line)
{
    static const servo_str_t k_verdict[3] = {SERVO_STR_PASS, SERVO_STR_FAIL,
                                             SERVO_STR_ABORTED};
    static const int k_cls[3] = {TEXT_HL_OK, TEXT_HL_FAIL, TEXT_HL_WARN};
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        for (int how = 0; how < 3; ++how) {
            run_in(lang, how);
            CHECK(g.report_len > 500u);
            const size_t heads = hl_report(g.report, lang, k_heads,
                                           HL_N(k_heads), k_verdict[how],
                                           k_cls[how]);
            if (heads < (how == 2 ? 2u : 5u)) {
                T_FAIL("lang %d run %d: %zu headings", lang, how, heads);
            }
        }
    }
    /* The units the report joins to its numbers ("6.00V") are classed. */
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        run_in(lang, 0);
        size_t volts = 0u;
        for (const char *ln = g.report; *ln != '\0';) {
            const size_t len = strcspn(ln, "\n");
            char line[200];
            if (len < sizeof(line)) {
                memcpy(line, ln, len);
                line[len] = '\0';
                const size_t n = hl_line(line, 20u, TEXT_HL_KIND_TEXT);
                for (size_t sp = 0u; sp < n; ++sp) {
                    volts += (hl_spans[sp].cls == TEXT_HL_VOLT) ? 1u : 0u;
                }
            }
            ln += len + (ln[len] == '\n' ? 1u : 0u);
        }
        if (volts < 3u) {
            T_FAIL("lang %d: %zu voltages classed", lang, volts);
        }
    }
    /* The step table's rows are numbers alone: text. */
    run_in(0, 0);
    const char *row = strstr(g.report, "\n 4.80 ");
    CHECK(row != NULL);
    if (row != NULL) {
        char line[160];
        const size_t len = strcspn(row + 1, "\n");
        memcpy(line, row + 1, len);
        line[len] = '\0';
        const size_t n = hl_line(line, 20u, TEXT_HL_KIND_TEXT);
        CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    }
}

/* --- JSON, CSV, plain ----------------------------------------------------- */

TEST_CASE(a_json_line_is_classed_by_its_tokens)
{
    size_t n = hl_line("  \"cells_min\": 2, \"x\": -1.5e+3,", 3u,
                       TEXT_HL_KIND_JSON);
    CHECK(hl_span_is(n, 2u, 11u, TEXT_HL_KEY));
    CHECK(hl_span_is(n, 13u, 1u, TEXT_HL_PUNCT));
    CHECK(hl_span_is(n, 15u, 1u, TEXT_HL_NUMBER));
    CHECK(hl_span_is(n, 18u, 3u, TEXT_HL_KEY));
    CHECK(hl_span_is(n, 23u, 7u, TEXT_HL_NUMBER));
    n = hl_line("\"bec\": \"linear\"", 3u, TEXT_HL_KIND_JSON);
    CHECK(hl_span_is(n, 6u, 9u, TEXT_HL_TEXT));
    n = hl_line("[true, false, null, nullx]", 3u, TEXT_HL_KIND_JSON);
    CHECK(hl_span_is(n, 1u, 4u, TEXT_HL_LITERAL));
    CHECK(hl_span_is(n, 7u, 5u, TEXT_HL_LITERAL));
    CHECK(hl_span_is(n, 14u, 4u, TEXT_HL_LITERAL));
    CHECK(hl_class_at(n, 20u) == TEXT_HL_TEXT);
    /* An escaped quote, a string the line cuts, a lone minus, a key with
     * spaces before its colon. */
    n = hl_line("\"a\\\"b\" : 1", 3u, TEXT_HL_KIND_JSON);
    CHECK(hl_span_is(n, 0u, 6u, TEXT_HL_KEY));
    n = hl_line("\"unterminated \\", 3u, TEXT_HL_KIND_JSON);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("- 1.", 3u, TEXT_HL_KIND_JSON);
    CHECK(hl_class_at(n, 0u) == TEXT_HL_TEXT);
    CHECK(hl_class_at(n, 2u) == TEXT_HL_NUMBER);
    n = hl_line("1e", 3u, TEXT_HL_KIND_JSON);
    CHECK(hl_span_is(n, 0u, 1u, TEXT_HL_NUMBER));
}

/* Every line of an ESC profile of record: the invariants, and its keys,
 * numbers and literals counted. */
TEST_CASE(an_esc_profile_of_record_is_classed)
{
    FILE *f = fopen(PROFILE_DIR "/align-rce-bl15x.json", "rb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    char line[2048];
    size_t keys = 0u, nums = 0u, no = 0u;
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        const size_t n = hl_line(line, no++, TEXT_HL_KIND_JSON);
        for (size_t k = 0u; k < n; ++k) {
            keys += hl_spans[k].cls == TEXT_HL_KEY;
            nums += hl_spans[k].cls == TEXT_HL_NUMBER;
        }
    }
    fclose(f);
    CHECK(keys > 20u);
    CHECK(nums > 3u);
}

TEST_CASE(a_csv_header_and_its_delimiters)
{
    size_t n = hl_line("time (s);test;step", 0u, TEXT_HL_KIND_CSV);
    CHECK(hl_span_is(n, 0u, 8u, TEXT_HL_CSV_HEAD));
    CHECK(hl_span_is(n, 8u, 1u, TEXT_HL_PUNCT));
    CHECK_EQ(n, 5u);
    n = hl_line("1.5;2,5;3", 4u, TEXT_HL_KIND_CSV);
    CHECK(hl_span_is(n, 0u, 3u, TEXT_HL_TEXT));
    CHECK(hl_span_is(n, 3u, 1u, TEXT_HL_PUNCT));
    CHECK(hl_span_is(n, 4u, 3u, TEXT_HL_TEXT));
    n = hl_line("1,2", 4u, TEXT_HL_KIND_CSV);
    CHECK(hl_span_is(n, 1u, 1u, TEXT_HL_PUNCT));
    n = hl_line("SETTINGS IN FORCE", 0u, TEXT_HL_KIND_PLAIN);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
}

TEST_CASE(the_kind_follows_the_extension)
{
    CHECK_EQ(text_hl_kind_of("LIMIT001.TXT"), TEXT_HL_KIND_TEXT);
    CHECK_EQ(text_hl_kind_of("notes.txt"), TEXT_HL_KIND_TEXT);
    CHECK_EQ(text_hl_kind_of("LIMIT001.TXT.NEW"), TEXT_HL_KIND_TEXT);
    CHECK_EQ(text_hl_kind_of("align.json"), TEXT_HL_KIND_JSON);
    CHECK_EQ(text_hl_kind_of("a.JSON.new"), TEXT_HL_KIND_JSON);
    CHECK_EQ(text_hl_kind_of("ATEST001.CSV"), TEXT_HL_KIND_CSV);
    CHECK_EQ(text_hl_kind_of("RCBENCH.LOG"), TEXT_HL_KIND_PLAIN);
    CHECK_EQ(text_hl_kind_of("KST.NEW"), TEXT_HL_KIND_PLAIN);
    CHECK_EQ(text_hl_kind_of("README"), TEXT_HL_KIND_PLAIN);
    CHECK_EQ(text_hl_kind_of("a.txtx"), TEXT_HL_KIND_PLAIN);
    CHECK_EQ(text_hl_kind_of("dir.txt/file"), TEXT_HL_KIND_PLAIN);
    CHECK_EQ(text_hl_kind_of(".txt"), TEXT_HL_KIND_TEXT);
    CHECK_EQ(text_hl_kind_of(""), TEXT_HL_KIND_PLAIN);
    CHECK_EQ(text_hl_kind_of(NULL), TEXT_HL_KIND_PLAIN);
}

/* --- room, long lines, NULL ----------------------------------------------- */

/* With room for fewer spans, the last one is TEXT to the line's end. */
TEST_CASE(the_last_span_takes_the_rest_when_the_room_runs_out)
{
    const char *line = "Result: PASS 3.0 V 4.0 A 5 W";
    const size_t len = strlen(line);
    text_hl_span_t sp[3];
    size_t n = text_hl_line(line, len, 3u, TEXT_HL_KIND_TEXT, hl_bench_words(),
                            sp, 3u);
    CHECK_EQ(n, 3u);
    CHECK(sp[0].cls == TEXT_HL_LABEL && sp[0].len == 7u);
    CHECK(sp[1].cls == TEXT_HL_TEXT && sp[1].len == 1u);
    CHECK(sp[2].cls == TEXT_HL_TEXT && sp[2].start == 8u &&
          sp[2].start + sp[2].len == len);
    n = text_hl_line(line, len, 3u, TEXT_HL_KIND_TEXT, hl_bench_words(), sp,
                     1u);
    CHECK(n == 1u && sp[0].cls == TEXT_HL_TEXT && sp[0].len == len);
    n = text_hl_line(line, len, 3u, TEXT_HL_KIND_JSON, NULL, sp, 1u);
    CHECK(n == 1u && sp[0].len == len);
    CHECK_EQ(text_hl_line(line, len, 3u, TEXT_HL_KIND_TEXT, NULL, sp, 0u), 0u);
    CHECK_EQ(text_hl_line(line, 0u, 3u, TEXT_HL_KIND_TEXT, NULL, sp, 3u), 0u);
    CHECK_EQ(text_hl_line(NULL, 3u, 3u, TEXT_HL_KIND_TEXT, NULL, sp, 3u), 0u);
    CHECK_EQ(text_hl_line(line, len, 3u, TEXT_HL_KIND_TEXT, NULL, NULL, 3u),
             0u);
}

/* A 120-character report line with a token at its end, and lines of
 * German letters: every invariant holds. */
TEST_CASE(a_long_line_and_german_letters_keep_the_invariants)
{
    char line[200] = "Holding current  highest 0.122 A, limit 0.30 A: ";
    while (strlen(line) < 116u) {
        strcat(line, "x");
    }
    strcat(line, " PASS");
    CHECK_EQ(strlen(line), 121u);
    size_t n = hl_line(line, 40u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 117u, 4u, TEXT_HL_OK));
    n = hl_line("Stra\xC3\x9F" "e: \xC3\x84\xC3\x96 3.0 V \xC3\xBC" "ber "
                "BESTANDEN \xC3\x9F", 5u, TEXT_HL_KIND_TEXT);
    CHECK(n >= 4u);
    n = hl_line("\xC3\xA4\xC3\xB6\xC3\xBC #\xC3\x9F", 5u, TEXT_HL_KIND_JSON);
    CHECK(n >= 1u);
    n = hl_line("\xC3\x84;\xC3\x96", 0u, TEXT_HL_KIND_CSV);
    CHECK_EQ(n, 3u);
    /* A malformed byte and a NUL inside a line are text. */
    const char odd[8] = {'a', ' ', (char)0xFF, ' ', '\0', ' ', 'b', '\0'};
    n = text_hl_line(odd, 7u, 5u, TEXT_HL_KIND_TEXT, hl_bench_words(),
                     hl_spans, HL_SPANS);
    CHECK(n == 1u && hl_spans[0].len == 7u);
    n = text_hl_line(odd, 7u, 5u, TEXT_HL_KIND_JSON, NULL, hl_spans,
                     HL_SPANS);
    CHECK(n == 1u && hl_spans[0].len == 7u);
}

int main(void)
{
    RUN(every_class_has_its_own_style_name);
    RUN(the_unit_table_classes_each_unit);
    RUN(every_unit_the_report_formats_write_is_in_the_table);
    RUN(a_heading_is_upper_case_before_its_parenthesis);
    RUN(a_label_is_at_column_0_and_ends_at_its_mark);
    RUN(a_verdict_word_matches_whole_and_in_its_case);
    RUN(a_word_core_is_the_word_itself);
    RUN(a_number_with_a_unit_takes_the_units_class);
    RUN(a_comment_runs_from_its_mark_to_the_end);
    RUN(register_checksum_and_journal_lines_are_hex);
    RUN(the_servo_test_report_is_classed_line_by_line);
    RUN(a_json_line_is_classed_by_its_tokens);
    RUN(an_esc_profile_of_record_is_classed);
    RUN(a_csv_header_and_its_delimiters);
    RUN(the_kind_follows_the_extension);
    RUN(the_last_span_takes_the_rest_when_the_room_runs_out);
    RUN(a_long_line_and_german_letters_keep_the_invariants);
    free(g.csv_text);
    return test_summary("text_hl");
}
