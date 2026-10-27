/*
 * The servo test report's tables against the text viewer's row: 92 cells
 * of the 10 x 20 face (TEXT_VIEW_CELLS_NORMAL in shared/ui/text_view.h).
 *
 * The failures under test: a step table line wider than the row, which the
 * viewer breaks so that the last column lands on a row of its own; a
 * heading word that does not end where its column ends; a value at the
 * end of its range that pushes the columns after it out of line; and an
 * encoder table heading whose words do not start where its columns do.  In
 * every language, with every value at the widest it reaches: voltages to
 * 99.99V, currents to -99.999A (the INA3221 reads to 32.760A on its
 * smallest shunt, 5.0mOhm), travel times to the window of the longest lag a
 * meter can carry, counts to SERVO_TEST_MOVES_MAX and angles at -180deg.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "servo_move.h"
#include "servo_test.h"
#include "ui_text.h"

/** @brief Cells of a row of the text viewer in its 10 x 20 face:
 *         TEXT_VIEW_CELLS_NORMAL in text_view.h. */
#define VIEW_COLS 92

/** @brief The last cell of each step table column, as step_row() writes
 *         them. */
static const int k_ends[] = { 4, 10, 16, 23, 30, 37, 44, 51, 58, 65, 73, 79,
                              84, 91 };
#define N_COLS ((int)(sizeof(k_ends) / sizeof(k_ends[0])))

/* The report of a run that is never started: each table filled by hand. */
static servo_test_t g_t;

/* How many cells @p s takes, as the viewer's face draws it. */
static int cells_of(const char *s)
{
    int n = 0;
    for (uint32_t c = gfx_utf8_next(&s); c != 0u; c = gfx_utf8_next(&s)) {
        ++n;
    }
    return n;
}

/* @p s decoded into @p cp, one code point a cell; returns the cells. */
static int decode(const char *s, uint32_t *cp, int cap)
{
    int n = 0;
    for (uint32_t c = gfx_utf8_next(&s); c != 0u && n < cap;
         c = gfx_utf8_next(&s)) {
        cp[n++] = c;
    }
    return n;
}

/* A mean of @p n readings of @p a each. */
static servo_test_mean_t mean_of(float a, uint32_t n)
{
    servo_test_mean_t m = { a * (float)n, n };
    return m;
}

/* A characterisation step at @p set_v with every value at its widest: cut
 * short, and every counted move unseen, so both notes follow its line. */
static void widest_step(servo_test_step_t *s, float set_v, float amps)
{
    memset(s, 0, sizeof(*s));
    const uint32_t window = servo_move_window_ms(UINT16_MAX);
    s->set_v   = set_v;
    s->begun   = true;
    s->done    = false;
    s->v       = mean_of(99.99f, 1u);
    s->v_min   = 99.99f;
    s->idle    = mean_of(amps, 1u);
    s->move_a  = fabsf(amps);       /* a threshold is above zero */
    s->move    = mean_of(amps, 1u);
    s->move_peak_a = amps;
    s->hold[0] = mean_of(amps, 1u);
    s->hold[1] = mean_of(amps, 1u);
    s->travels = 1u;
    s->travel_sum_ms = window;
    s->travel_max_ms = window;
    s->moves    = SERVO_TEST_MOVES_MAX;
    s->timeouts = SERVO_TEST_MOVES_MAX;
    s->no_rise  = SERVO_TEST_MOVES_MAX;
    s->enc_moves   = SERVO_TEST_MOVES_MAX;
    s->enc_unmoved = SERVO_TEST_MOVES_MAX;
    s->enc_late    = SERVO_TEST_MOVES_MAX;
    s->enc_travels = 1u;
    s->enc_travel_sum_ms = window;
    s->enc_travel_max_ms = window;
    for (unsigned end = 0; end < 2u; ++end) {
        s->enc_end_ref[end] = 0u;
        s->enc_end_off[end] = mean_of(-180.0f, 1u);
    }
}

/* A run in language @p l whose steps carry the widest values, the
 * encoder on. */
static void widest_run(int l, float amps)
{
    memset(&g_t, 0, sizeof(g_t));
    const ui_language_t *t = ui_text_table((ui_lang_t)l);
    g_t.cfg.text = (t != NULL) ? t->servo : NULL;
    g_t.cfg.enc_on = true;
    g_t.step_count = SERVO_TEST_STEPS_MAX;
    for (unsigned k = 0; k < SERVO_TEST_STEPS_MAX; ++k) {
        widest_step(&g_t.steps[k], 8.40f, amps);
    }
}

/* The report line that is @p want's text, or -1. */
static int line_of(const char *want)
{
    char line[SERVO_TEST_LINE_MAX];
    for (unsigned k = 0; servo_report_line(&g_t, k, line, sizeof(line));
         ++k) {
        if (strcmp(line, want) == 0) {
            return (int)k;
        }
    }
    return -1;
}

/* Every line of either table in every language, with the widest values,
 * fits the viewer's row: the headings, the step lines and the notes under
 * them. */
TEST_CASE(every_table_line_fits_the_viewer_row)
{
    static const float k_amps[] = { -99.999f, -32.76f, 32.76f, 99.999f };
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (size_t a = 0; a < sizeof(k_amps) / sizeof(k_amps[0]); ++a) {
            widest_run(l, k_amps[a]);
            const char *const *tbl = g_t.cfg.text;
            const int heads[] = {
                line_of(servo_str_in(tbl, SERVO_STR_R_COLUMNS)),
                line_of(servo_str_in(tbl, SERVO_STR_R_ENC_COLUMNS)),
            };
            unsigned rows = 0u;
            for (size_t h = 0; h < 2u; ++h) {
                CHECK(heads[h] >= 0);
                /* The headings, then a line and its note per step. */
                const unsigned from = (unsigned)heads[h];
                const unsigned n = (h == 0u) ? 2u + 2u * SERVO_TEST_STEPS_MAX
                                             : 1u + SERVO_TEST_STEPS_MAX;
                for (unsigned k = from; k < from + n; ++k) {
                    char line[SERVO_TEST_LINE_MAX];
                    CHECK(servo_report_line(&g_t, k, line, sizeof(line)));
                    if (cells_of(line) > VIEW_COLS) {
                        T_FAIL("language %d, %.3fA: line %u is %d cells: "
                               "\"%s\"", l, (double)k_amps[a], k,
                               cells_of(line), line);
                    }
                    ++rows;
                }
            }
            CHECK_EQ(rows, 2u + 2u * SERVO_TEST_STEPS_MAX + 1u
                               + SERVO_TEST_STEPS_MAX);
        }
    }
}

/* A step that ran to its end with movement seen has one line; the note
 * under a step says cut short, NOT MEASURABLE or both. */
TEST_CASE(a_note_follows_a_step_cut_short_or_not_measurable)
{
    widest_run(0, 0.5f);
    g_t.cfg.enc_on = false;
    g_t.step_count = 4u;
    g_t.steps[0].done = true;
    g_t.steps[0].no_rise = 0u;            /* one line */
    g_t.steps[1].done = true;             /* NOT MEASURABLE */
    g_t.steps[2].no_rise = 0u;            /* cut short */
    /* steps[3]: cut short and NOT MEASURABLE */
    const int head = line_of(servo_str_in(NULL, SERVO_STR_R_COLUMNS));
    CHECK(head >= 0);
    char line[SERVO_TEST_LINE_MAX];
    unsigned k = (unsigned)head + 2u;
    /* Steps 0 and 1 each have their line, step 0 no note. */
    CHECK(servo_report_line(&g_t, k, line, sizeof(line)));
    CHECK_EQ(cells_of(line), VIEW_COLS);
    CHECK(servo_report_line(&g_t, k + 1u, line, sizeof(line)));
    CHECK_EQ(cells_of(line), VIEW_COLS);
    CHECK(servo_report_line(&g_t, k + 2u, line, sizeof(line)));
    CHECK_STR_EQ(line, " 8.40  NOT MEASURABLE");
    CHECK(servo_report_line(&g_t, k + 4u, line, sizeof(line)));
    CHECK_STR_EQ(line, " 8.40  (cut short)");
    CHECK(servo_report_line(&g_t, k + 6u, line, sizeof(line)));
    CHECK_STR_EQ(line, " 8.40  (cut short) NOT MEASURABLE");
    /* After the last note, the threshold rule. */
    CHECK(servo_report_line(&g_t, k + 7u, line, sizeof(line)));
    CHECK(strncmp(line, "Thresh: ", 8) == 0);
}

/* Currents are whole mA; one beyond 99.999A, or not a number, is "--". */
TEST_CASE(a_current_is_whole_ma_and_dashes_past_its_column)
{
    static const struct {
        float a;
        const char *cell;
    } k[] = {
        { 0.0004f, "0" }, { 0.0006f, "1" }, { -0.0006f, "-1" },
        { 0.020f, "20" }, { 32.76f, "32760" }, { -32.76f, "-32760" },
        { 99.999f, "99999" }, { -99.999f, "-99999" },
        { 100.0f, "--" }, { -100.0f, "--" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        widest_run(0, k[i].a);
        g_t.cfg.enc_on = false;
        g_t.step_count = 1u;
        const int head = line_of(servo_str_in(NULL, SERVO_STR_R_COLUMNS));
        CHECK(head >= 0);
        char line[SERVO_TEST_LINE_MAX];
        CHECK(servo_report_line(&g_t, (unsigned)head + 2u, line,
                                sizeof(line)));
        /* The idle column: cells 18 to 23. */
        char cell[8];
        snprintf(cell, sizeof(cell), "%.6s", line + 18);
        const char *v = cell;
        while (*v == ' ') {
            ++v;
        }
        if (strcmp(v, k[i].cell) != 0) {
            T_FAIL("%.4fA: idle cell \"%s\", want \"%s\" in \"%s\"",
                   (double)k[i].a, v, k[i].cell, line);
        }
    }
    widest_run(0, NAN);
    g_t.step_count = 1u;
    const int head = line_of(servo_str_in(NULL, SERVO_STR_R_COLUMNS));
    char line[SERVO_TEST_LINE_MAX];
    CHECK(servo_report_line(&g_t, (unsigned)head + 2u, line, sizeof(line)));
    CHECK(strncmp(line + 18, "    --", 6) == 0);
}

/* Each word of the two headings ends where its column ends, and a step
 * line with the widest values puts each value there too: no cell between
 * two columns holds anything, in every language. */
TEST_CASE(the_step_table_columns_line_up)
{
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        widest_run(l, -99.999f);
        const char *const *tbl = g_t.cfg.text;
        const int head = line_of(servo_str_in(tbl, SERVO_STR_R_COLUMNS));
        CHECK(head >= 0);
        for (unsigned k = 0; k < 3u; ++k) {
            char line[SERVO_TEST_LINE_MAX];
            CHECK(servo_report_line(&g_t, (unsigned)head + k, line,
                                    sizeof(line)));
            uint32_t cp[128];
            const int n = decode(line, cp, 128);
            /* The upper heading may leave a column empty; the lower one
             * and the step line fill every one. */
            for (int c = 0; c < N_COLS; ++c) {
                const int end = k_ends[c];
                const bool filled = end < n && cp[end] != ' ';
                if (k > 0u && !filled) {
                    T_FAIL("language %d: nothing ends at cell %d of line %u "
                           "\"%s\"", l, end, k, line);
                }
                if (end + 1 < n && cp[end + 1] != ' ') {
                    T_FAIL("language %d: cell %d of line %u is not a space: "
                           "\"%s\"", l, end + 1, k, line);
                }
            }
            if (n > VIEW_COLS) {
                T_FAIL("language %d: line %u is %d cells", l, k, n);
            }
        }
    }
}

/* The encoder table's heading starts each word where its column starts,
 * as encoder_lines() writes them, in every language. */
TEST_CASE(the_encoder_heading_starts_where_its_columns_do)
{
    static const int k_starts[] = { 0, 7, 16, 25, 34, 43, 50, 58, 64, 72 };
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        const ui_language_t *t = ui_text_table((ui_lang_t)l);
        const char *head = servo_str_in(t != NULL ? t->servo : NULL,
                                        SERVO_STR_R_ENC_COLUMNS);
        uint32_t cp[128];
        const int n = decode(head, cp, 128);
        for (size_t c = 0; c < sizeof(k_starts) / sizeof(k_starts[0]); ++c) {
            const int at = k_starts[c];
            if (at >= n || cp[at] == ' ' || (at > 0 && cp[at - 1] != ' ')) {
                T_FAIL("language %d: no heading starts at cell %d of \"%s\"",
                       l, at, head);
            }
        }
    }
}

int main(void)
{
    RUN(every_table_line_fits_the_viewer_row);
    RUN(a_note_follows_a_step_cut_short_or_not_measurable);
    RUN(a_current_is_whole_ma_and_dashes_past_its_column);
    RUN(the_step_table_columns_line_up);
    RUN(the_encoder_heading_starts_where_its_columns_do);
    return test_summary("servo_report_width");
}
