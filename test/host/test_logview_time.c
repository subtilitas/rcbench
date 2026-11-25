/**
 * @file test_logview_time.c
 * @brief LOGS' graph of a time file (log_graph.h, log_graph_int.h): an
 *        AUTOMATIC TEST recorded on the bench (fixtures/servo-mg90s.csv)
 *        and a SERVO run of INA3221 windows (fixtures/servo-ms24-windows.csv)
 *        drawn over their time; a step in time of exactly the gap threshold
 *        and one row interval more; a time that steps back; 257 marks; a
 *        strip that changes in every pixel column; the spans of `>` and
 *        `clipped`; the chips' taps and the presses MORE, BACK and a reload
 *        cancel; two fingers' zoom; the frame work of a steady frame; and
 *        the summary in German.
 *
 * The failures under test: rows spaced by their number where their time
 * differs; a gap not hatched, or hatched at the threshold itself; a trace
 * drawn across a gap; a strip that lags a column or holds a value into a
 * gap; a mark past the ceiling that overwrites one; a released press that
 * taps a chip after MORE, BACK or a reload.
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

#include <stdio.h>

#include "gfx_work.h"
#include "log_graph.h"
#include "log_preset.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The plot's first and last pixel column. */
#define PLOT_X0 LV_PV_X0
#define PLOT_X1 (LV_PV_X0 + LV_PV_COLS - 1)

/* ------------------------------------------------------------- the card */

/** @brief The bench's recordings, read from fixtures/. */
static char g_mg90s[64 * 1024];
static char g_ms24[96 * 1024];
/** @brief The cases' own files, written by each case. */
static char g_made[64 * 1024];

/** @brief The time card's files. */
static const struct {
    const char *name;
    const char *text;
} k_files[] = {
    { "ATEST012.CSV", g_mg90s },
    { "SERVO014.CSV", g_ms24 },
    { "MOTOR050.CSV", g_made },
    { "SUPPL051.CSV", g_made },
    { "ATEST052.CSV", g_made },
};

/**
 * @brief Read fixture @p name into @p out.
 * @return true when the whole file fits.
 */
static bool read_fixture(const char *name, char *out, size_t cap)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    const size_t n = fread(out, 1, cap - 1, f);
    const bool whole = feof(f) != 0;
    fclose(f);
    out[n] = '\0';
    return whole && n > 0;
}

/** @brief List hook: the time card's files, one page. */
static int time_list(log_viewer_file_t *out, int max_entries,
                     log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    int n = 0;
    for (size_t i = 0; i < sizeof(k_files) / sizeof(k_files[0])
                       && n < max_entries; ++i, ++n) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].name, sizeof(out[n].name), "%s", k_files[i].name);
        out[n].size = (uint64_t)strlen(k_files[i].text);
    }
    if (page != NULL) {
        page->ahead = 0;
        page->held = n;
    }
    return n;
}

static log_mem_ctx_t g_time_ctx;

/** @brief Open hook: a time card's file as an in-memory source. */
static bool time_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < sizeof(k_files) / sizeof(k_files[0]); ++i) {
        if (strcmp(name, k_files[i].name) == 0) {
            log_source_memory(src, &g_time_ctx, k_files[i].text,
                              strlen(k_files[i].text));
            return true;
        }
    }
    return false;
}

static const log_viewer_io_t k_time_io = {
    .list = time_list,
    .open = time_open,
    .volume = fake_volume,
    .mounts = fake_mounts,
};

/** @brief The screen fresh, the time card in, @p name opened and drawn. */
static bool open_time(const char *name)
{
    fresh();
    log_viewer_set_io(&k_time_io);
    log_viewer_refresh();
    if (log_viewer_show(LOG_TAB_BENCH, name, g_mounts) != LOG_SHOW_OPEN) {
        return false;
    }
    draw();
    return log_graph_shown() && log_graph_time();
}

/** @brief The pixel at (@p x, @p y) of the screen's frame. */
static gfx_color_t px(int x, int y)
{
    return s_fb[y * W + x];
}

/** @brief Pixels of colour @p col in column @p x between rows @p y0 and
 *         @p y1, @p y1 excluded. */
static int column_count(int x, int y0, int y1, gfx_color_t col)
{
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        n += (px(x, y) == col);
    }
    return n;
}

/** @brief Write g_made: a header and @p rows rows from @p fn. */
static void make(const char *header, int rows,
                 int (*fn)(char *out, size_t n, int i))
{
    size_t at = (size_t)snprintf(g_made, sizeof(g_made), "%s\n", header);
    for (int i = 0; i < rows && at < sizeof(g_made); ++i) {
        at += (size_t)fn(g_made + at, sizeof(g_made) - at, i);
    }
}

/* ------------------------------------------------------------- the files */

/* A MOTOR run of 0.25s rows: 2.00s to 2.75s, exactly the threshold of
 * 3 x 0.25s, is no gap; 4.75s to 5.75s, one row interval more, is one. */
static float gap_time(int i)
{
    if (i <= 8) {
        return 0.25f * (float)i;                    /* 0 .. 2.00  */
    }
    if (i <= 16) {
        return 2.75f + 0.25f * (float)(i - 9);      /* 2.75 .. 4.50 */
    }
    if (i == 17) {
        return 4.75f;
    }
    return 5.75f + 0.25f * (float)(i - 18);         /* 5.75 ..   */
}

static int gap_row(char *out, size_t n, int i)
{
    return snprintf(out, n, "%.2f;12.0;%.2f;1000;20;measured\n",
                    (double)gap_time(i), 2.0 + 0.1 * (double)i);
}

/* A time that steps back at row 3. */
static int back_row(char *out, size_t n, int i)
{
    static const float k_t[] = { 0.0f, 1.0f, 2.0f, 1.5f, 3.0f, 4.0f };
    return snprintf(out, n, "%.2f;12.0;%.2f;1000;20;measured\n",
                    (double)k_t[i], 1.0 + (double)i);
}

/* 257 steps of an AUTOMATIC TEST, one a row. */
static int step_row(char *out, size_t n, int i)
{
    return snprintf(out, n, "%.2f;STEP;%d;MOVE;1500;0.10;6.00;CV\n",
                    0.1 * (double)i, i + 1);
}

/* 514 rows of a MOTOR run, clipped every other row: 257 spans. */
static int clip_row(char *out, size_t n, int i)
{
    return snprintf(out, n, "%.2f;12.0;1.0;1000;20;measured;%d\n",
                    0.05 * (double)i, (i % 2 == 0) ? 1 : 0);
}

/* 992 rows of a SUPPLY run, CC every other row, modelled every third. */
static int cc_row(char *out, size_t n, int i)
{
    return snprintf(out, n, "%.2f;6.00;5.90;2.00;1.00;5.9;%s;0.1;0.01;%s\n",
                    0.05 * (double)i, (i % 2 == 0) ? "CC" : "CV",
                    (i % 3 == 0) ? "model" : "measured");
}

/* A MOTOR run: rpm past its register's ceiling on rows 4 to 6, clipped
 * on rows 10 and 11. */
static int over_row(char *out, size_t n, int i)
{
    const bool over = i >= 4 && i <= 6;
    return snprintf(out, n, "%.2f;12.0;%s;30;measured;%d\n", 0.05 * (double)i,
                    over ? ">65535" : "40000", (i == 10 || i == 11) ? 2 : 0);
}

/* ------------------------------------------------------------- the cases */

/* The AUTOMATIC TEST the bench recorded: drawn over its time, its steps a
 * line each, its phases a strip, the rows the logger did not write while a
 * position was held hatched. */
TEST_CASE(an_automatic_test_recorded_on_the_bench_draws_over_its_time)
{
    char got[160];
    REQUIRE(read_fixture("servo-mg90s.csv", g_mg90s, sizeof(g_mg90s)));
    REQUIRE(open_time("ATEST012.CSV"));
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_ATEST);
    const log_data_t *d = log_viewer_data();
    REQUIRE(d != NULL);
    CHECK_EQ(d->count, 870);
    log_viewer_summary(got, sizeof(got));
    CHECK(strstr(got, "ATEST012.CSV   870 ROWS   ") == got);
    CHECK(strstr(got, "AUTOMATIC TEST") != NULL);
    /* Each row at its time: the first and the last on the plot's ends,
     * row 100 at its share of the run. */
    const float t1 = d->time[d->count - 1];
    CHECK_EQ(log_graph_row_px(0), PLOT_X0);
    CHECK_EQ(log_graph_row_px(d->count - 1), PLOT_X1);
    CHECK_EQ(log_graph_row_px(100),
             PLOT_X0 + (int)lroundf(d->time[100] / t1 * (LV_PV_COLS - 1)));
    /* Three steps, a line at each one's first row. */
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    int steps = 0;
    for (int m = 0; m < mk->n_marks; ++m) {
        steps += mk->mark[m].kind == LOG_MARK_STEP;
    }
    CHECK_EQ(steps, 3);
    CHECK_EQ(mk->mark[0].row, 0);
    CHECK_EQ(mk->mark[1].row, 405);
    CHECK_EQ(mk->mark[2].row, 813);
    /* PHASE kept; CC dropped, the run never left CV. */
    CHECK_EQ(mk->n_strips, 1);
    CHECK_EQ(mk->strip[0].kind, LOG_STRIP_PHASE);
    CHECK_EQ(mk->strip[0].n_values, 5);
    CHECK_STR_EQ(mk->strip[0].value[0], "SET");
    CHECK_EQ(log_graph_strip_at(0, PLOT_X0), 1u);
    CHECK(log_graph_strip_y(0) > LV_PV_TOP);
    CHECK_EQ(log_graph_strip_y(1), -1);
    /* 3.846s to 5.485s, rows 36 and 37: 1.639s is past 3 medians and
     * 0.5s, so it is a gap, hatched between its rows. */
    CHECK(log_graph_gap_before(37));
    CHECK(!log_graph_gap_before(36));
    const int a = log_graph_row_px(36);
    const int b = log_graph_row_px(37);
    CHECK(log_graph_gap_col((a + b) / 2));
    CHECK(!log_graph_gap_col(a));
    CHECK(!log_graph_gap_col(b));
    /* No trace in the gap's middle column, under the bars and tags. */
    for (int k = 0; k < d->n_fields; ++k) {
        CHECK_EQ(column_count((a + b) / 2, LV_PV_TOP + 50,
                              log_graph_strip_y(0) - 6,
                              log_viewer_series_color(k)), 0);
    }
    /* The step line of step 2 down the traces, in the faint text. */
    CHECK(column_count(log_graph_row_px(405), LV_PV_TOP + 50,
                       log_graph_strip_y(0) - 6, UI_TEXT_DIM) > 100);
}

/* The SERVO run of INA3221 windows: current and voltage over 69s, no strip
 * (no `source`), no gap. */
TEST_CASE(a_servo_run_of_ina3221_windows_draws_over_its_time)
{
    char got[160];
    REQUIRE(read_fixture("servo-ms24-windows.csv", g_ms24, sizeof(g_ms24)));
    REQUIRE(open_time("SERVO014.CSV"));
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_SERVO);
    const log_data_t *d = log_viewer_data();
    REQUIRE(d != NULL);
    CHECK_EQ(d->count, 1310);
    /* Its `current` and `voltage` cells are empty: INA3221 channel 1's. */
    REQUIRE(d->n_fields == 2);
    CHECK_STR_EQ(d->field[0].name, "ch1 current");
    CHECK_STR_EQ(d->field[1].name, "ch1 voltage");
    log_viewer_summary(got, sizeof(got));
    CHECK(strstr(got, "SERVO RUN") != NULL);
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    CHECK_EQ(mk->n_strips, 0);
    CHECK_EQ(mk->n_marks, 0);
    int gaps = 0;
    int prev = PLOT_X0;
    bool rising = true;
    for (int i = 1; i < d->count; ++i) {
        gaps += log_graph_gap_before(i);
        rising = rising && log_graph_row_px(i) >= prev;
        prev = log_graph_row_px(i);
    }
    CHECK_EQ(gaps, 0);
    CHECK(rising);
    CHECK_EQ(prev, PLOT_X1);
    /* The cursor by finger: the row whose time is nearest. */
    tap(PLOT_X0 + (LV_PV_COLS - 1) / 2, LV_PV_CY);
    const float half = d->time[d->count - 1] * (float)((LV_PV_COLS - 1) / 2)
                       / (float)(LV_PV_COLS - 1);
    const int c = log_viewer_cursor();
    REQUIRE(c > 0 && c < d->count - 1);
    CHECK(fabsf(d->time[c] - half) <= fabsf(d->time[c - 1] - half));
    CHECK(fabsf(d->time[c] - half) <= fabsf(d->time[c + 1] - half));
    draw();
}

/* A step of exactly the threshold is no gap; one row interval more is. */
TEST_CASE(a_gap_of_the_threshold_is_none_and_one_interval_more_is_one)
{
    make("time (s);voltage (V);current (A);rpm (rpm);throttle (%);source",
         30, gap_row);
    REQUIRE(open_time("MOTOR050.CSV"));
    const log_data_t *d = log_viewer_data();
    REQUIRE(d != NULL);
    CHECK(fabs(d->median_dt_s - 0.25) < 1e-6);
    CHECK(!log_graph_gap_before(9));       /* 2.00s to 2.75s: 0.75s  */
    CHECK(log_graph_gap_before(18));       /* 4.75s to 5.75s: 1.00s  */
    int gaps = 0;
    for (int i = 1; i < d->count; ++i) {
        gaps += log_graph_gap_before(i);
    }
    CHECK_EQ(gaps, 1);
    /* Hatched between rows 17 and 18 only; the trace broken there and
     * drawn across the 0.75s step. */
    const int a = log_graph_row_px(17);
    const int b = log_graph_row_px(18);
    REQUIRE(b - a > 4);
    int hatched = 0;
    for (int x = PLOT_X0; x <= PLOT_X1; ++x) {
        hatched += log_graph_gap_col(x);
    }
    CHECK_EQ(hatched, b - a - 1);
    const gfx_color_t cur = log_viewer_series_color(2);
    int in_gap = 0;
    for (int x = a + 1; x < b; ++x) {
        in_gap += column_count(x, LV_PV_TOP, LV_PV_BOTTOM, cur);
    }
    CHECK_EQ(in_gap, 0);
    const int c = (log_graph_row_px(8) + log_graph_row_px(9)) / 2;
    CHECK(column_count(c, LV_PV_TOP, LV_PV_BOTTOM, cur) > 0);
    /* The hatch's faint pixels in the gap's columns. */
    int faint = 0;
    for (int x = a + 1; x < b; ++x) {
        faint += column_count(x, LV_PV_TOP + 60, LV_PV_BOTTOM - 20,
                              UI_TEXT_FAINT);
    }
    CHECK(faint > 0);
}

/* A time that steps back: the build numbers the rows, so the graph spaces
 * them evenly, finds no gap and says it has no time column. */
TEST_CASE(a_time_that_steps_back_spaces_the_rows_evenly)
{
    char got[160];
    make("time (s);voltage (V);current (A);rpm (rpm);throttle (%);source",
         6, back_row);
    REQUIRE(open_time("MOTOR050.CSV"));
    const log_data_t *d = log_viewer_data();
    REQUIRE(d != NULL);
    CHECK_STR_EQ(d->time_name, "");
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(log_graph_row_px(i),
                 PLOT_X0 + (int)lroundf((float)i * (LV_PV_COLS - 1) / 5.0f));
        CHECK(!log_graph_gap_before(i));
    }
    log_viewer_summary(got, sizeof(got));
    CHECK_STR_EQ(got, "MOTOR050.CSV   6 ROWS   MOTOR RUN");
    REQUIRE(log_graph_cursor_line(got, sizeof(got)) == false);
    tap(PLOT_X1, LV_PV_CY);
    CHECK_EQ(log_viewer_cursor(), 5);
    tap(PLOT_X0, LV_PV_CY);
    CHECK_EQ(log_viewer_cursor(), 0);
    draw();
}

/* 257 steps: 256 lines kept, the 257th counted; the tags that find no room
 * are left out and the frame draws. */
TEST_CASE(marks_past_256_are_counted_not_drawn)
{
    make("time (s);test;step;phase;command (us);current (A);voltage (V);"
         "mode", 257, step_row);
    REQUIRE(open_time("ATEST052.CSV"));
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    CHECK_EQ(mk->n_marks, LOG_MARKS_MAX);
    CHECK_EQ(mk->dropped, 1);
    CHECK_EQ(mk->mark[LOG_MARKS_MAX - 1].row, LOG_MARKS_MAX - 1);
    CHECK_FEQ(mk->mark[LOG_MARKS_MAX - 1].at, (float)LOG_MARKS_MAX);
    /* 257 clipped spans of a MOTOR run: one span a run of rows. */
    make("time (s);voltage (V);current (A);rpm (rpm);throttle (%);source;"
         "clipped", 514, clip_row);
    REQUIRE(open_time("MOTOR050.CSV"));
    mk = log_graph_marks();
    REQUIRE(mk != NULL);
    CHECK_EQ(mk->n_marks, LOG_MARKS_MAX);
    CHECK_EQ(mk->dropped, 1);
    CHECK_EQ(mk->mark[0].kind, LOG_MARK_CLIP);
    CHECK_EQ(mk->mark[0].row_end, 0);
    CHECK_EQ(mk->mark[1].row, 2);
}

/* `>65535` on rows 4 to 6 is one OVER span, `clipped` on rows 10 and 11
 * one CLIP span, each a bar at the plot's top in its colour. */
TEST_CASE(a_cell_above_its_range_and_a_clipped_window_are_spans)
{
    make("time (s);voltage (V);rpm (rpm);throttle (%);source;clipped", 20,
         over_row);
    REQUIRE(open_time("MOTOR050.CSV"));
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    REQUIRE(mk->n_marks == 2);
    CHECK_EQ(mk->mark[0].kind, LOG_MARK_OVER);
    CHECK_EQ(mk->mark[0].row, 4);
    CHECK_EQ(mk->mark[0].row_end, 6);
    CHECK_EQ(mk->mark[1].kind, LOG_MARK_CLIP);
    CHECK_EQ(mk->mark[1].row, 10);
    CHECK_EQ(mk->mark[1].row_end, 11);
    const int over = (log_graph_row_px(4) + log_graph_row_px(6)) / 2;
    const int clip = (log_graph_row_px(10) + log_graph_row_px(11)) / 2;
    CHECK_EQ(column_count(over, LV_PV_TOP, LV_PV_TOP + 4, UI_WARN), 4);
    CHECK_EQ(column_count(clip, LV_PV_TOP, LV_PV_TOP + 4, UI_DANGER), 4);
    CHECK_EQ(column_count(log_graph_row_px(15), LV_PV_TOP, LV_PV_TOP + 4,
                          UI_WARN), 0);
}

/* A strip that changes in every pixel column: 992 rows, CC every other
 * one, each in its own column; the MODEL strip under it. */
TEST_CASE(a_strip_that_changes_in_every_pixel_column)
{
    make("time (s);set (V);voltage (V);limit (A);current (A);power (W);"
         "mode;charge (mAh);energy (Wh);source", LV_PV_COLS, cc_row);
    REQUIRE(open_time("SUPPL051.CSV"));
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    REQUIRE(mk->n_strips == 2);
    CHECK_EQ(mk->strip[0].kind, LOG_STRIP_CC);
    CHECK_EQ(mk->strip[1].kind, LOG_STRIP_MODEL);
    const int y = log_graph_strip_y(0) + 2;
    int wrong = 0;
    int warn = 0;
    for (int i = 0; i < LV_PV_COLS; ++i) {
        const int x = log_graph_row_px(i);
        wrong += x != PLOT_X0 + i;
        wrong += log_graph_strip_at(0, x) != ((i % 2 == 0) ? 1u : 0u);
        wrong += log_graph_strip_at(1, x) != ((i % 3 == 0) ? 1u : 0u);
        /* Past the label at the strip's left end, its colour. */
        if (x > PLOT_X0 + 40) {
            warn += px(x, y) == UI_WARN;
            wrong += (px(x, y) == UI_WARN) != (i % 2 == 0);
        }
    }
    CHECK_EQ(wrong, 0);
    CHECK(warn > 450);
    /* The traces end above the strips. */
    CHECK(log_graph_strip_y(1) > log_graph_strip_y(0));
    CHECK(log_graph_strip_y(1) + 6 <= LV_PV_BOTTOM + 1);
}

/* Two fingers zoom a time file: the window narrows, the rows in it spread
 * over the plot by their time, the strip follows. */
TEST_CASE(two_fingers_zoom_a_time_file)
{
    make("time (s);set (V);voltage (V);limit (A);current (A);power (W);"
         "mode;charge (mAh);energy (Wh);source", LV_PV_COLS, cc_row);
    REQUIRE(open_time("SUPPL051.CSV"));
    send_id(TOUCH_EVENT_DOWN, 1, 400, LV_PV_CY);
    send_id(TOUCH_EVENT_DOWN, 2, 500, LV_PV_CY);
    send_id(TOUCH_EVENT_MOVE, 2, 900, LV_PV_CY);
    send_id(TOUCH_EVENT_UP, 2, 900, LV_PV_CY);
    send_id(TOUCH_EVENT_UP, 1, 400, LV_PV_CY);
    int first = 0;
    int count = 0;
    log_viewer_window(&first, &count);
    REQUIRE(count < LV_PV_COLS);
    CHECK_EQ(log_graph_row_px(first), PLOT_X0);
    CHECK_EQ(log_graph_row_px(first + count - 1), PLOT_X1);
    CHECK_EQ(log_graph_row_px(first + count), -1);
    /* Each row's strip code in its column; the columns between held. */
    const int a = log_graph_row_px(first);
    const int b = log_graph_row_px(first + 1);
    REQUIRE(b > a + 1);
    CHECK_EQ(log_graph_strip_at(0, a), (first % 2 == 0) ? 1u : 0u);
    CHECK_EQ(log_graph_strip_at(0, b - 1), log_graph_strip_at(0, a));
    /* > past the window's end moves it along. */
    while (log_viewer_cursor() > first + count - 1) {
        tap(LV_PV_PREV_CX, LV_FOOT_CY);
    }
    draw();
}

/* A tap on a time file's chip focuses its series, the first from slot 0;
 * a second hides it; a third brings it back. */
TEST_CASE(a_chip_tap_focuses_and_hides_a_time_series)
{
    make("time (s);voltage (V);current (A);rpm (rpm);throttle (%);source",
         30, gap_row);
    REQUIRE(open_time("MOTOR050.CSV"));
    const gfx_color_t cur = log_viewer_series_color(2);
    int shown = 0;
    for (int x = PLOT_X0; x <= PLOT_X1; ++x) {
        shown += column_count(x, LV_PV_TOP, LV_PV_BOTTOM, cur);
    }
    CHECK(shown > 100);
    tap(LV_GR_CHIP_CX(2), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 2);
    tap(LV_GR_CHIP_CX(2), LV_GR_CHIP_CY);
    CHECK(log_graph_hidden(2));
    draw();
    int hidden = 0;
    for (int x = PLOT_X0; x <= PLOT_X1; ++x) {
        hidden += column_count(x, LV_PV_TOP, LV_PV_BOTTOM, cur);
    }
    CHECK_EQ(hidden, 0);
    tap(LV_GR_CHIP_CX(2), LV_GR_CHIP_CY);
    CHECK(!log_graph_hidden(2));
    CHECK_EQ(log_graph_focus(), 2);
    tap(LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 0);
    draw();
}

/* The presses a time file's graph holds end as the XY graph's: a cut
 * release taps nothing, MORE, BACK and a reload drop a held chip. */
TEST_CASE(held_presses_on_a_time_file_end_as_on_the_xy_graph)
{
    make("time (s);voltage (V);current (A);rpm (rpm);throttle (%);source",
         30, gap_row);
    REQUIRE(open_time("MOTOR050.CSV"));
    send(TOUCH_EVENT_DOWN, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    touch_event_t up = {
        .type = TOUCH_EVENT_UP,
        .flags = TOUCH_FLAG_NO_TAP,
        .point = { .id = 1, .x = LV_GR_CHIP_CX(0), .y = LV_GR_CHIP_CY },
    };
    screen()->event(&up);
    CHECK_EQ(log_graph_focus(), -1);
    /* MORE while a chip is held. */
    send_id(TOUCH_EVENT_DOWN, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    REQUIRE(log_graph_pressed());
    send_id(TOUCH_EVENT_DOWN, 2, LV_PV_MORE_CX, LV_FOOT_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_PV_MORE_CX, LV_FOOT_CY);
    CHECK(!log_graph_pressed());
    send_id(TOUCH_EVENT_UP, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), -1);
    send_id(TOUCH_EVENT_DOWN, 3, LV_PV_CX, LV_PV_CY);
    send_id(TOUCH_EVENT_UP, 3, LV_PV_CX, LV_PV_CY);
    REQUIRE(log_viewer_view() == LOG_VIEW_PLOT);
    /* A reload while a chip is held. */
    send_id(TOUCH_EVENT_DOWN, 1, LV_GR_CHIP_CX(1), LV_GR_CHIP_CY);
    REQUIRE(log_viewer_show(LOG_TAB_BENCH, "MOTOR050.CSV", g_mounts)
            == LOG_SHOW_OPEN);
    CHECK(!log_graph_pressed());
    send_id(TOUCH_EVENT_UP, 1, LV_GR_CHIP_CX(1), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), -1);
    /* BACK while a chip is held. */
    send_id(TOUCH_EVENT_DOWN, 1, LV_GR_CHIP_CX(1), LV_GR_CHIP_CY);
    send_id(TOUCH_EVENT_DOWN, 2, LV_PV_BACK_CX, LV_FOOT_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_PV_BACK_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(!log_graph_pressed());
    send_id(TOUCH_EVENT_UP, 1, LV_GR_CHIP_CX(1), LV_GR_CHIP_CY);
    draw();
}

/* A steady frame of a time file's graph repaints nothing: under the 1,000
 * fills tools/frame_cost.py holds the logs mode to.  A moved cursor
 * repaints the whole screen once. */
TEST_CASE(a_steady_frame_of_a_time_file_does_no_work)
{
    REQUIRE(read_fixture("servo-mg90s.csv", g_mg90s, sizeof(g_mg90s)));
    REQUIRE(open_time("ATEST012.CSV"));
    screen()->render(&s_c, 0);
    screen()->render(&s_c, 1);
    const unsigned long before = gfx_work();
    screen()->render(&s_c, 0);
    screen()->render(&s_c, 1);
    CHECK_EQ(gfx_work() - before, 0ul);
    tap(LV_PV_PREV_CX, LV_FOOT_CY);
    const unsigned long moved = gfx_work();
    screen()->render(&s_c, 0);
    const unsigned long whole = gfx_work() - moved;
    CHECK(whole > (unsigned long)(W * (H - 60)));
    CHECK(whole < 3ul * (unsigned long)W * (unsigned long)H);
}

/* German: the summary's rows and kind, a strip's label. */
TEST_CASE(a_time_file_speaks_german)
{
    char got[160];
    make("time (s);voltage (V);current (A);rpm (rpm);throttle (%);source",
         6, back_row);
    REQUIRE(open_time("MOTOR050.CSV"));
    ui_text_set_language(UI_LANG_DE);
    log_viewer_summary(got, sizeof(got));
    CHECK_STR_EQ(got, "MOTOR050.CSV   6 ZEILEN   MOTOR-LAUF");
    draw();
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(an_automatic_test_recorded_on_the_bench_draws_over_its_time);
    RUN(a_servo_run_of_ina3221_windows_draws_over_its_time);
    RUN(a_gap_of_the_threshold_is_none_and_one_interval_more_is_one);
    RUN(a_time_that_steps_back_spaces_the_rows_evenly);
    RUN(marks_past_256_are_counted_not_drawn);
    RUN(a_cell_above_its_range_and_a_clipped_window_are_spans);
    RUN(a_strip_that_changes_in_every_pixel_column);
    RUN(two_fingers_zoom_a_time_file);
    RUN(a_chip_tap_focuses_and_hides_a_time_series);
    RUN(held_presses_on_a_time_file_end_as_on_the_xy_graph);
    RUN(a_steady_frame_of_a_time_file_does_no_work);
    RUN(a_time_file_speaks_german);
    return test_summary("logview_time");
}
