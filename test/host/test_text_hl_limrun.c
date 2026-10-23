/*
 * The highlighter on the limit run's report and on the KST files the
 * programmer writes and a person edits.
 *
 * Under test: the limit run's report in English and German, passed and
 * aborted, line by line (text_hl_rig.h's checks); the text
 * kst_file_format() writes for the bench image, every line by its kind;
 * an edited KST file with comments.  A separate suite from test_text_hl
 * because the limit run's rig and the servo test's each define the rig
 * state g.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_limrun_rig.h"
#include "text_hl_rig.h"

#include "kst_file.h"

static const servo_str_t k_heads[] = {
    SERVO_STR_L_R_TITLE, SERVO_STR_L_R_ENDS_HEAD, SERVO_STR_L_R_CHECK_HEAD,
    SERVO_STR_L_R_PROG_HEAD,
};

/* The limit run's report in both languages, passed and stopped. */
TEST_CASE(the_limit_run_report_is_classed_line_by_line)
{
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        for (int stop = 0; stop < 2; ++stop) {
            rig_init(0u, false);
            g.enc = true;
            g.cfg.text = hl_table(lang);
            CHECK(start());
            if (stop) {
                run_for(3000u);
                servo_limrun_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
            }
            run_for(RUN_MS);
            CHECK_EQ(g.run.verdict, stop ? SERVO_LIMRUN_ABORTED
                                         : SERVO_LIMRUN_PASS);
            const size_t heads = hl_report(
                g.report, lang, k_heads, HL_N(k_heads),
                stop ? SERVO_STR_ABORTED : SERVO_STR_PASS,
                stop ? TEXT_HL_WARN : TEXT_HL_OK);
            if (heads < (stop ? 1u : 4u)) {
                T_FAIL("lang %d stop %d: %zu headings", lang, stop, heads);
            }
        }
    }
}

/* Line @p no of @p text, without its LF, into @p out. */
static const char *nth_line(const char *text, size_t no, char *out,
                            size_t n)
{
    const char *p = text;
    for (size_t k = 0u; k < no && p != NULL; ++k) {
        p = strchr(p, '\n');
        p = (p != NULL) ? p + 1 : NULL;
    }
    if (p == NULL || *p == '\0') {
        return NULL;
    }
    size_t len = strcspn(p, "\n");
    len = (len < n - 1u) ? len : n - 1u;
    memcpy(out, p, len);
    out[len] = '\0';
    return out;
}

/* The bench image as kst_file_format() writes it: the header a heading,
 * the type and the fields labels, a value with a unit in its class, the
 * not-loaded mark WARN, the register block and the checksum hex. */
TEST_CASE(the_kst_file_the_programmer_writes_is_classed)
{
    static char text[KST_FILE_MAX + 1u];
    const kst_image_t img = sim_bench_image();
    const size_t len = kst_file_format(&img, text, KST_FILE_MAX);
    CHECK(len > 0u);
    text[len] = '\0';
    char line[KST_FILE_LINE_MAX + 2u];
    size_t labels = 0u, fields = 0u, units = 0u, marks = 0u, hex = 0u;
    size_t no = 0u;
    for (; nth_line(text, no, line, sizeof(line)) != NULL; ++no) {
        const size_t n = hl_line(line, no, TEXT_HL_KIND_TEXT);
        if (no == 0u) {
            CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
            continue;
        }
        if (n == 1u && hl_spans[0].cls == TEXT_HL_HEX) {
            hex++;
            continue;
        }
        if (n == 1u && hl_spans[0].cls == TEXT_HL_HEADING) {
            T_FAIL("line %zu \"%s\" is a heading", no, line);
        }
        if (strstr(line, " = ") != NULL || strncmp(line, "Type:", 5u) == 0) {
            fields++;
            labels += hl_spans[0].cls == TEXT_HL_LABEL;
        }
        const char *mark = strstr(line, "[not loaded]");
        if (mark != NULL) {
            marks++;
            if (hl_class_at(n, (size_t)(mark - line)) != TEXT_HL_WARN) {
                T_FAIL("line %zu: the mark is not WARN", no);
            }
        }
        for (size_t k = 0u; k < n; ++k) {
            units += hl_spans[k].cls == TEXT_HL_UNIT;
        }
    }
    CHECK_EQ(labels, fields);
    CHECK(fields >= 20u);
    CHECK_EQ(hex, 5u);                   /* 4 register lines, the checksum */
    CHECK(marks >= 5u);
    CHECK(units >= 10u);
    /* "registers 32" is text, not a heading or hex. */
    const char *reg = strstr(text, "registers 32\n");
    CHECK(reg != NULL);
    size_t n = hl_line("registers 32", 30u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_TEXT);
    n = hl_line("Boost = 300.0 us", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 8u, 8u, TEXT_HL_UNIT));
    n = hl_line("Max. Duty = 96 %", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 11u, TEXT_HL_LABEL));
    CHECK(hl_span_is(n, 12u, 4u, TEXT_HL_UNIT));
    n = hl_line("prot_pot_err = 0 of 12 [not loaded]", 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 23u, 12u, TEXT_HL_WARN));
}

/* A file a person edited: comments, a value without its unit, a raw
 * value, no checksum. */
TEST_CASE(an_edited_kst_file_is_classed)
{
    static const char *const k_lines[] = {
        "KST servo configuration, format 1",
        "# the left aileron, set by hand",
        "Neutral = 1520 us   # was 1500.2",
        "Dead Band = 2,0",
        "Boost = raw 75",
        "",
        "  # indented note",
    };
    size_t n = hl_line(k_lines[0], 0u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_HEADING);
    n = hl_line(k_lines[1], 1u, TEXT_HL_KIND_TEXT);
    CHECK(n == 1u && hl_spans[0].cls == TEXT_HL_COMMENT);
    n = hl_line(k_lines[2], 2u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 9u, TEXT_HL_LABEL));
    CHECK(hl_span_is(n, 10u, 7u, TEXT_HL_UNIT));
    CHECK(hl_span_is(n, 20u, 12u, TEXT_HL_COMMENT));
    n = hl_line(k_lines[3], 3u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 0u, 11u, TEXT_HL_LABEL));
    CHECK_EQ(n, 2u);
    n = hl_line(k_lines[4], 4u, TEXT_HL_KIND_TEXT);
    CHECK_EQ(n, 2u);
    CHECK_EQ(hl_line(k_lines[5], 5u, TEXT_HL_KIND_TEXT), 0u);
    n = hl_line(k_lines[6], 6u, TEXT_HL_KIND_TEXT);
    CHECK(hl_span_is(n, 2u, 15u, TEXT_HL_COMMENT));
}

int main(void)
{
    RUN(the_limit_run_report_is_classed_line_by_line);
    RUN(the_kst_file_the_programmer_writes_is_classed);
    RUN(an_edited_kst_file_is_classed);
    return test_summary("text_hl_limrun");
}
