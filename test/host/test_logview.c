/*
 * Host unit tests for the log viewer screen.
 *
 * The screen is handed an I/O (input/output) vtable, so the whole browse ->
 * import -> plot path runs here against strings in memory: no card, no
 * filesystem, no board.  Tested is what the screen decides (which file, which
 * columns, which convention, where the cursor lands) and one property about
 * pixels: a redraw over an older frame leaves nothing of it behind.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>

#include "log_name.h"
#include "log_select.h"
#include "log_viewer_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

/* --------------------------------------------------------- the fake card -- */

static const char k_en[] =
    "time,voltage,current,rpm,temp\n"
    "0.000,22.34V,10.23A,1234,25\n"
    "0.020,22.31V,11.05A,1567,25\n"
    "0.040,22.28V,12.40A,1890,26\n"
    "0.060,22.20V,15.02A,2345,26\n";

static const char k_de[] =
    "Zeit;Spannung;Strom;Drehzahl;Temperatur\n"
    "0,000;22,34V;10,23A;1.234;25\n"
    "0,020;22,31V;11,05A;1.567;25\n"
    "0,040;22,28V;12,40A;1.890;26\n"
    "0,060;22,20V;15,02A;2.345;26\n";

static const char k_prose[] = "note\nnothing numeric here\nnor here\n";

/*
 * Both conventions proven in one file: the screen reports the conflict rather
 * than picking a winner.
 *
 * Semicolon-delimited, so that "10,23" is one cell.  With a comma delimiter it
 * splits into two fields and the fixture holds no comma-decimal cell at all.
 */
static const char k_mixed[] = "t;a;b\n0;10.23;1\n1;10,23;2\n2;11.5;3\n";

/* Ragged for its own sake, so the two conditions can be told apart. */
static const char k_ragged[] = "t,a,b\n0,1,2\n1,2\n2,3,4\n";

/* No time column, and a column that is mostly but not entirely numeric. */
static const char k_plain[] =
    "left,right\n5,1.0\n3,2.0\n9,n/a\n4,4.0\n8,5.0\n7,6.0\n";

static const struct {
    const char *name;
    const char *text;
} k_card[] = {
    { "BENCH_01.CSV", k_en },
    { "PRUEFUNG.CSV", k_de },
    { "README.CSV", k_prose },
    { "MIXED.CSV", k_mixed },
    { "PLAIN.CSV", k_plain },
    { "TINY.CSV", "a,b\n1,2\n" },
    { "GONE.CSV", NULL },
    /* Appended, not inserted: the cases above address the card by index. */
    { "RAGGED.CSV", k_ragged },
};

static bool g_no_card;
static bool g_empty_card;

/*
 * A card holding more runs than the list has room for, or 0 for the fixture
 * card above.  The lister writes the newest that fit, in the order the browse
 * list draws them, and returns what the card holds -- what card_list() does
 * on the panel with storage_walk()'s count.
 */
static int g_card_runs;

/* Which listed entry is a subdirectory, or -1 for none.  A card full of
 * directories is not the normal case, and every other case here wants to open
 * the file it selected. */
static int g_dir_index = -1;
static log_mem_ctx_t g_ctx;

static int fake_list(log_viewer_file_t *out, int max_entries, void *ctx)
{
    (void)ctx;
    if (g_no_card) {
        return -1;
    }
    if (g_empty_card) {
        return 0;       /* mounted, and nothing on it the viewer can open */
    }
    if (g_card_runs > 0) {
        const int fits = (g_card_runs < max_entries) ? g_card_runs : max_entries;
        for (int i = 0; i < fits; ++i) {
            log_run_name(out[i].name, sizeof(out[i].name),
                         g_card_runs - fits + 1 + i);
            out[i].size = 1024u;
            out[i].is_dir = false;
        }
        return g_card_runs;
    }
    static const uint32_t sizes[] = { 900u, 4096u, 300u, 200u, 120u,
                                      3u * 1024u * 1024u, 12u };
    int n = 0;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]) && n < max_entries;
         ++i) {
        snprintf(out[n].name, sizeof(out[n].name), "%s", k_card[i].name);
        out[n].size = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
        out[n].is_dir = ((int)i == g_dir_index);
        ++n;
    }
    return n;
}

static bool fake_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]); ++i) {
        if (strcmp(name, k_card[i].name) == 0) {
            if (k_card[i].text == NULL) {
                return false; /* listed, but gone by the time it was opened */
            }
            log_source_memory(src, &g_ctx, k_card[i].text,
                              strlen(k_card[i].text));
            return true;
        }
    }
    return false;
}

static const char *fake_volume(void *ctx)
{
    (void)ctx;
    return g_no_card ? "" : "BENCH01";
}

static const log_viewer_io_t k_io = {
    .list = fake_list,
    .open = fake_open,
    .close = NULL,
    .volume = fake_volume,
    .ctx = NULL,
};

/* ------------------------------------------------------------- harness --- */

static gfx_color_t *s_fb;
static gfx_color_t *s_fb_b;
static gfx_canvas_t s_c;


static const ui_screen_t *screen(void)
{
    return log_viewer_screen();
}

/* Put the screen back to its start state without touching any framebuffer --
 * the stale-pixel case needs to replay the same interaction into a second
 * buffer, and wiping the first one would compare a render against a blank. */
static void reset_screen(void)
{
    ui_theme_set(UI_THEME_DARK);
    g_no_card = false;
    g_empty_card = false;
    g_card_runs = 0;
    screen()->reset();
    log_viewer_set_io(&k_io);
    screen()->enter();
}

static void fresh(void)
{
    g_dir_index = -1;
    if (s_fb == NULL) {
        s_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&s_c, s_fb, W, H, W);
    reset_screen();
}

static void send(int type, int x, int y)
{
    touch_event_t e = {
        .type = (touch_event_type_t)type,
        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y },
    };
    screen()->event(&e);
}

static void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
}

/* Draw the current state.  Called after every interaction in the cases below,
 * because a screen that decides correctly and then draws nothing is still
 * broken -- and because a crash in a rarely-reached panel should surface
 * here rather than on the bench. */
static void draw(void)
{
    screen()->render(&s_c, 0);
}

/* Geometry mirrored from the screen; if it moves, these move with it.  Panel
 * coordinates: the band is UI_BAND_H rows tall and the screen starts below. */
#define BR_ROW_Y(i) (36 + 30 + (i) * 44 + 18)
#define IM_ROW_Y(i) (36 + 30 + (i) * 36 + 14)
#define IM_BTN_CY   (368 + 23)
#define SEP_X       110
#define NUM_X       308
#define BACK_X      500
#define PLOT_X      690
#define FOOT_CY     411

static int pixels_of(gfx_color_t color)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (s_fb[i] == color) {
            ++n;
        }
    }
    return n;
}

static void open_file(int index)
{
    tap(400, BR_ROW_Y(index)); /* select */
    tap(400, BR_ROW_Y(index)); /* open   */
}

static int column_row(const log_analysis_t *a, const char *name)
{
    for (int i = 0; i < a->n_columns; ++i) {
        if (strcmp(a->columns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- cases --- */

TEST_CASE(every_view_draws_something)
{
    fresh();
    draw();                                /* browse, nothing selected      */
    tap(400, BR_ROW_Y(0));
    draw();                                /* browse, a row highlighted     */

    /* Drag the list: with more files than rows it scrolls, and the sizes
     * exercise every unit the formatter has. */
    send(TOUCH_EVENT_DOWN, 400, BR_ROW_Y(4));
    send(TOUCH_EVENT_MOVE, 400, BR_ROW_Y(4) - 90);
    send(TOUCH_EVENT_UP, 400, BR_ROW_Y(4) - 90);
    draw();

    /* Back to the top, then all the way through to a plot. */
    send(TOUCH_EVENT_DOWN, 400, BR_ROW_Y(0));
    send(TOUCH_EVENT_MOVE, 400, BR_ROW_Y(0) + 400);
    send(TOUCH_EVENT_UP, 400, BR_ROW_Y(0) + 400);
    open_file(0);
    draw();
    tap(PLOT_X, IM_BTN_CY);
    draw();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

TEST_CASE(a_file_that_vanished_between_listing_and_opening)
{
    /* The card is removable, so "it was there a moment ago" is a real state
     * and not an assertion failure. */
    fresh();
    open_file(6);
    CHECK(log_viewer_analysis() == NULL);
    draw();
    tap(PLOT_X, IM_BTN_CY);
    draw();
    CHECK(log_viewer_data() == NULL);
}

TEST_CASE(with_no_io_at_all_the_screen_still_works)
{
    fresh();
    log_viewer_set_io(NULL);
    log_viewer_refresh();
    draw();
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    draw();
    CHECK(log_viewer_analysis() == NULL);
}

TEST_CASE(a_file_that_proves_both_conventions_is_flagged)
{
    fresh();
    open_file(3);
    draw();
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    /* Two assertions, not a disjunction: "10.23" and "10,23" are both present
     * as whole cells, so the conflict must be reported on its own merits and
     * not because the file also happens to be ragged. */
    CHECK(a->convention_conflict);
    CHECK_EQ(a->ragged_rows, 0);

    /* And the banner that reports it is painted. */
    CHECK(pixels_of(UI_DANGER) > 0);
}

/* Raggedness is the other branch of the same banner, reached here without a
 * convention conflict so the two branches are tested apart. */
TEST_CASE(a_ragged_file_is_flagged_without_a_convention_conflict)
{
    fresh();
    /* Eight files, seven visible rows: scroll to the bottom first, so the last
     * file lands on the last visible row.  Pins: a row reached by scrolling
     * accepts a touch. */
    send(TOUCH_EVENT_DOWN, 400, BR_ROW_Y(4));
    send(TOUCH_EVENT_MOVE, 400, BR_ROW_Y(4) - 400);
    send(TOUCH_EVENT_UP, 400, BR_ROW_Y(4) - 400);
    open_file(6);
    draw();
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK(a->ragged_rows > 0);
    CHECK(!a->convention_conflict);
}

TEST_CASE(a_file_with_no_time_column_plots_against_the_row_number)
{
    fresh();
    open_file(4);
    draw();
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->time_index, -1);

    tap(PLOT_X, IM_BTN_CY);
    draw();
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_STR_EQ(d->time_name, "");
    CHECK(d->unparsed_cells >= 1);
}

TEST_CASE(a_two_row_file_still_draws_a_trace)
{
    /* Fewer samples than pixel columns: the trace has to be joined up rather
     * than left as a row of isolated marks. */
    fresh();
    open_file(5);
    draw();
    tap(PLOT_X, IM_BTN_CY);
    draw();
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->count, 1);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

TEST_CASE(no_card_says_so_and_stays_put)
{
    fresh();
    g_no_card = true;
    log_viewer_refresh();

    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    /* And it still renders: an empty card is a normal state, not a fault. */
    screen()->render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

/*
 * A card that is there and carries nothing the viewer can open is not the same
 * state as no card, and the screen says so: one tells the operator to insert a
 * card and tap RESCAN, the other would send them looking for a card that is
 * already in.
 */
TEST_CASE(an_empty_card_is_not_the_same_as_no_card)
{
    /*
     * The two states must not draw the same panel.  Asserting that each one
     * renders something would pass with one message for both, which is the
     * bug: it would send an operator to look for a card that is already in.
     * So the two are rendered and compared.
     */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
        CHECK(s_fb_b != NULL);
    }

    fresh();
    g_no_card = true;
    log_viewer_refresh();
    screen()->render(&s_c, 0);
    gfx_color_t *no_card = calloc((size_t)W * H, sizeof(gfx_color_t));
    CHECK(no_card != NULL);
    memcpy(no_card, s_fb, (size_t)W * H * sizeof(gfx_color_t));

    fresh();
    g_empty_card = true;
    log_viewer_refresh();

    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    /* Nothing to open, the same as no card -- that much they do share. */
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    screen()->render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);

    /*
     * And what they SAY is different -- compared over the message line alone,
     * not the whole frame.  The whole frame differs anyway because the header
     * carries the volume name, which is empty with no card; asserting on that
     * would pass with one message for both, which is the defect.
     *
     * The panel is drawn at (120,160) 560x160 and the reason sits at
     * box.y + 52, so the band below covers that line and nothing else.
     */
    int differing = 0;
    for (int y = 205; y < 226; ++y) {
        for (int x = 140; x < 660; ++x) {
            if (no_card[y * W + x] != s_fb[y * W + x]) {
                ++differing;
            }
        }
    }
    CHECK(differing > 0);
    free(no_card);
}

/*
 * A cancel abandons the press in progress.  Every view here acts on the
 * release, and a press left standing after a lost event would let a later
 * contact's release open the file that was selected.
 */
TEST_CASE(a_cancelled_press_opens_nothing)
{
    fresh();
    tap(400, BR_ROW_Y(0));                      /* selects */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 400,
                                   .y = (int16_t)BR_ROW_Y(0), .strength = 40 } };
    log_viewer_screen()->event(&e);
    log_viewer_screen()->cancel();
    e.type = TOUCH_EVENT_UP;
    log_viewer_screen()->event(&e);              /* would have opened */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);

    /* And nothing is stuck: a fresh tap on the selected row opens. */
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
}

TEST_CASE(a_second_tap_opens_the_file_and_analyses_it)
{
    fresh();
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE); /* first tap only selects */
    tap(400, BR_ROW_Y(0));
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "BENCH_01.CSV");

    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->convention, LOG_CONV_EN);
    CHECK_EQ(a->delimiter, ',');
    CHECK_EQ(a->n_columns, 5);
    CHECK_EQ(a->row_count, 4);
    CHECK(a->time_index >= 0);
    CHECK_STR_EQ(a->columns[a->time_index].name, "time");
}

TEST_CASE(a_german_file_is_read_as_german)
{
    fresh();
    open_file(1);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK_EQ(a->convention, LOG_CONV_DE);
    CHECK_EQ(a->delimiter, ';');

    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    /* The rpm column is written 1.234 there, a grouped 1234, and must not
     * read as 1.234. */
    for (int k = 0; k < d->n_fields; ++k) {
        if (strcmp(d->field[k].name, "Drehzahl") == 0) {
            CHECK_NEAR(d->value[k][0], 1234.0, 1e-3);
        }
    }
}

TEST_CASE(plotting_loads_exactly_the_picked_columns)
{
    fresh();
    open_file(0);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    /* Four numeric non-time columns, so all four slots are preselected. */
    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);

    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->n_fields, 4);
    CHECK_EQ(d->count, 4);
    CHECK_NEAR(d->duration_s, 0.06, 1e-4);
    for (int k = 0; k < d->n_fields; ++k) {
        if (strcmp(d->field[k].name, "voltage") == 0) {
            CHECK_NEAR(d->value[k][0], 22.34, 1e-3);
            CHECK_STR_EQ(d->field[k].unit, "V");
        }
    }
}

TEST_CASE(tapping_a_column_toggles_it_and_the_time_axis_is_not_offered)
{
    fresh();
    open_file(0);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }

    int temp = column_row(a, "temp");
    CHECK(temp >= 0);
    tap(600, IM_ROW_Y(temp)); /* off */
    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->n_fields, 3);
    for (int k = 0; k < d->n_fields; ++k) {
        CHECK(strcmp(d->field[k].name, "temp") != 0);
    }

    /* Back to the picker: the time column must refuse to become a trace,
     * because plotting it against itself just draws a diagonal. */
    tap(100, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tap(600, IM_ROW_Y(a->time_index));
    tap(PLOT_X, IM_BTN_CY);
    d = log_viewer_data();
    if (d != NULL) {
        for (int k = 0; k < d->n_fields; ++k) {
            CHECK(strcmp(d->field[k].name, "time") != 0);
        }
    }
}

/* storage_list applies the suffix filter only to files, so subdirectories are
 * listed.  There is no directory navigation; opening a directory is reported
 * as such rather than analysed as a log. */
/* Both override buttons re-run the analysis.  Pins: re-running the analysis
 * keeps the operator's column selection. */
TEST_CASE(an_override_does_not_discard_the_column_selection)
{
    fresh();
    open_file(0);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }

    /* Turn one column off, so the selection is something the operator chose
     * rather than the default. */
    int row = column_row(a, "current");
    if (row < 0) {
        T_FAIL("no current column");
        return;
    }
    tap(600, IM_ROW_Y(row));

    /* Cycle the decimal-convention override all the way round (AUTO, DE, EN,
     * AUTO) so the file ends up analysed as it started.  Each press re-runs
     * the analysis, which must keep the selection made above.  Going round
     * the whole loop matters: stopping on DE makes voltage and current
     * unparseable, and then a preserved selection and a rebuilt one agree by
     * accident. */
    tap(NUM_X, IM_BTN_CY);
    tap(NUM_X, IM_BTN_CY);
    tap(NUM_X, IM_BTN_CY);
    if (log_viewer_analysis() == NULL) {
        T_FAIL("no analysis after the override");
        return;
    }

    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("nothing plotted");
        return;
    }
    for (int i = 0; i < d->n_fields; ++i) {
        if (strcmp(d->field[i].name, "current") == 0) {
            T_FAIL("the override put 'current' back");
        }
    }
    CHECK(d->n_fields > 0);
}

TEST_CASE(a_directory_row_cannot_be_opened_as_a_log)
{
    fresh();
    g_dir_index = 2;
    log_viewer_refresh();
    open_file(2);

    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    /* A real file on the same card still opens. */
    open_file(0);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_analysis() != NULL);
}

TEST_CASE(a_column_of_prose_is_not_plottable)
{
    fresh();
    open_file(2);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis");
        return;
    }
    CHECK(!a->columns[0].numeric);

    tap(PLOT_X, IM_BTN_CY);
    /* Nothing to draw, so the screen stays where the user can fix it. */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_data() == NULL);
}

TEST_CASE(forcing_the_separator_and_the_convention_re_runs_the_analysis)
{
    fresh();
    open_file(0);

    /* AUTO -> ';' on a comma file: the columns collapse to one. */
    tap(SEP_X, IM_BTN_CY);
    const log_analysis_t *a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis after forcing");
        return;
    }
    CHECK_EQ(a->delimiter, ';');
    CHECK_EQ(a->n_columns, 1);

    /* Back round the cycle to AUTO and the file reads correctly again. */
    for (int i = 0; i < 4; ++i) {
        tap(SEP_X, IM_BTN_CY);
    }
    a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis after cycling back");
        return;
    }
    CHECK_EQ(a->delimiter, ',');
    CHECK_EQ(a->n_columns, 5);

    /* Forcing German on an English file must reject values, not corrupt them. */
    tap(NUM_X, IM_BTN_CY);
    a = log_viewer_analysis();
    if (a == NULL) {
        T_FAIL("no analysis after forcing german");
        return;
    }
    CHECK(a->convention_forced);
    CHECK_EQ(a->convention, LOG_CONV_DE);
    int v = column_row(a, "voltage");
    CHECK(v >= 0);
    if (v >= 0) {
        CHECK_EQ(a->columns[v].parsed, 0);
    }
}

TEST_CASE(the_cursor_follows_the_touch_and_stays_in_range)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }

    /* Dragging off the left edge clamps to the first sample, not to -1. */
    send(TOUCH_EVENT_DOWN, 400, 250);
    send(TOUCH_EVENT_MOVE, -50, 250);
    send(TOUCH_EVENT_UP, -50, 250);
    screen()->render(&s_c, 0);

    send(TOUCH_EVENT_DOWN, 400, 250);
    send(TOUCH_EVENT_MOVE, 2000, 250);
    send(TOUCH_EVENT_UP, 2000, 250);
    screen()->render(&s_c, 0);

    /* The step buttons walk it one sample at a time and stop at the ends. */
    for (int i = 0; i < 10; ++i) {
        tap(236, FOOT_CY);
    }
    screen()->render(&s_c, 0);
    for (int i = 0; i < 10; ++i) {
        tap(324, FOOT_CY);
    }
    screen()->render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 240) != 0);
}

TEST_CASE(fields_returns_to_the_picker_and_keeps_the_selection)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);

    tap(100, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);

    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    const log_data_t *d = log_viewer_data();
    if (d != NULL) {
        CHECK_EQ(d->n_fields, 4);
    }
}

TEST_CASE(back_from_the_picker_returns_to_the_file_list)
{
    fresh();
    open_file(0);
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
}

static int first_diff(const gfx_color_t *a, const gfx_color_t *b, int *count)
{
    int at = -1;
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (a[i] != b[i]) {
            if (at < 0) {
                at = i;
            }
            ++n;
        }
    }
    *count = n;
    return at;
}

TEST_CASE(redraw_leaves_no_stale_pixels)
{
    /* With two framebuffers, anything drawn outside the region a screen clears
     * shows up as flicker rather than as an obviously stale pixel.  Rendering
     * state B over state A must equal rendering B onto a fresh buffer. */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_t cb;

    static const int cursor_x[] = { 30, 700, 400, 24, 775 };

    for (size_t i = 1; i < sizeof(cursor_x) / sizeof(cursor_x[0]); ++i) {
        fresh();
        open_file(0);
        tap(PLOT_X, IM_BTN_CY);
        tap(cursor_x[i - 1], 250);
        screen()->render(&s_c, 0);
        tap(cursor_x[i], 250);
        screen()->render(&s_c, 0);

        memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
        gfx_canvas_init(&cb, s_fb_b, W, H, W);
        reset_screen();
        open_file(0);
        tap(PLOT_X, IM_BTN_CY);
        tap(cursor_x[i], 250);
        screen()->render(&cb, 0);

        int count = 0;
        int at = first_diff(s_fb, s_fb_b, &count);
        if (at >= 0) {
            T_FAIL("cursor %d -> %d: %d stale pixel(s), first at (%d,%d)",
                   cursor_x[i - 1], cursor_x[i], count, at % W, at / W);
        }
    }
}

TEST_CASE(both_framebuffers_follow_an_interaction)
{
    /* The screen caches its frame per framebuffer, and the panel alternates
     * between two.  An interaction that invalidates only the buffer being
     * drawn leaves the other one a frame behind, which reads as flicker rather
     * than as an obviously wrong pixel -- so assert the two agree. */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_t cb;
    memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cb, s_fb_b, W, H, W);

    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);

    screen()->render(&s_c, 0);
    screen()->render(&cb, 1);

    tap(700, 250); /* move the cursor */
    screen()->render(&s_c, 0);
    screen()->render(&cb, 1);

    int count = 0;
    int at = first_diff(s_fb, s_fb_b, &count);
    if (at >= 0) {
        T_FAIL("%d pixel(s) differ between framebuffers, first at (%d,%d)",
               count, at % W, at / W);
    }

    /* And with nothing happening, a repeat render is a no-op, which is what
     * makes the cache worth having. */
    screen()->render(&s_c, 0);
    at = first_diff(s_fb, s_fb_b, &count);
    CHECK_EQ(at, -1);
}

/* ------------------------------------------------- what a full card shows -- */

/*
 * The accent tab on the browse panel's top edge is the title's width plus
 * 26 px, and its top row is the one row the chamfer does not cut, so the run
 * of accent pixels along it measures the title.  Cheaper than reading pixels
 * back as letters, and it fails when the title stops saying what it should.
 */
#define BR_TAB_X 16
#define BR_TAB_Y 36

static int browse_tab_width(void)
{
    int w = 0;
    while (BR_TAB_X + w < W &&
           gfx_pixel_get(&s_c, BR_TAB_X + w, BR_TAB_Y) == UI_ACCENT) {
        ++w;
    }
    return w;
}

static int tab_width_for(const char *title)
{
    return gfx_text_width(UI_FONT_LABEL, title, 1) + 26;
}

TEST_CASE(a_run_name_and_its_number_are_one_rule)
{
    /*
     * The logger writes the name and the viewer reads the number back out of
     * it, and that number is the only order the card carries: the board has no
     * clock that survives a power cycle, so every file on it is dated
     * 1980-01-01.  Every run in range has to survive the round trip, because
     * one that does not stops looking like a run and sorts with the files
     * nobody can date.
     */
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        char name[LOG_RUN_NAME_MAX];
        log_run_name(name, sizeof(name), i);
        if (log_run_number(name) != i) {
            T_FAIL("run %d wrote \"%s\", read back %d", i, name,
                   log_run_number(name));
            break;
        }
    }
    char first[LOG_RUN_NAME_MAX];
    log_run_name(first, sizeof(first), LOG_RUN_FIRST);
    CHECK_STR_EQ(first, "BENCH001.CSV");
    log_run_name(first, sizeof(first), LOG_RUN_LAST);
    CHECK_STR_EQ(first, "BENCH999.CSV");

    /* A name off a computer, in lower case, is the same run. */
    CHECK_EQ(log_run_number("bench042.csv"), 42);

    /* And what is not a run of this bench's: a short number, a long one, one
     * below where the numbering starts, another suffix, and a name that only
     * begins like one. */
    CHECK_EQ(log_run_number("BENCH12.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH0001.CSV"), -1);
    CHECK_EQ(log_run_number("BENCHABC.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH000.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH001.TXT"), -1);
    CHECK_EQ(log_run_number("BENCH001.CSV.BAK"), -1);
    CHECK_EQ(log_run_number("BENCH"), -1);
    CHECK_EQ(log_run_number("SWEEP_920KV.CSV"), -1);
    CHECK_EQ(log_run_number(""), -1);
    CHECK_EQ(log_run_number(NULL), -1);

    /* A number it cannot write is not written as a near miss. */
    char name[LOG_RUN_NAME_MAX];
    log_run_name(name, sizeof(name), LOG_RUN_FIRST - 1);
    CHECK_STR_EQ(name, "");
    log_run_name(name, sizeof(name), LOG_RUN_LAST + 1);
    CHECK_STR_EQ(name, "");
    char small[LOG_RUN_NAME_MAX - 1];
    log_run_name(small, sizeof(small), LOG_RUN_FIRST);
    CHECK_STR_EQ(small, "");
    log_run_name(NULL, sizeof(name), LOG_RUN_FIRST); /* nowhere to write it */
}

TEST_CASE(the_newest_run_outranks_the_rest_of_the_card)
{
    /* Which entry a full list keeps.  A run is the only entry whose age is
     * known, so runs come first and the newest of them first of all;
     * everything else follows in the order it is drawn. */
    CHECK(log_name_rank("BENCH200.CSV", "BENCH199.CSV") < 0);
    CHECK(log_name_rank("BENCH049.CSV", "BENCH050.CSV") > 0);
    CHECK(log_name_rank("BENCH100.CSV", "BENCH099.CSV") < 0);
    CHECK_EQ(log_name_rank("BENCH007.CSV", "BENCH007.CSV"), 0);

    /* A run against a file nobody can date, both ways round. */
    CHECK(log_name_rank("BENCH001.CSV", "SWEEP_920KV.CSV") < 0);
    CHECK(log_name_rank("SWEEP_920KV.CSV", "BENCH999.CSV") > 0);

    /* And two of those: by name, with case folded, as the list draws them. */
    CHECK(log_name_rank("aaa.csv", "BBB.CSV") < 0);
    CHECK(log_name_rank("SWEEP.CSV", "PRUEFUNG.CSV") > 0);
    CHECK_EQ(log_name_rank("SWEEP.CSV", "sweep.csv"), 0);
    CHECK(log_name_rank(NULL, "SWEEP.CSV") < 0);
    CHECK(log_name_rank("SWEEP.CSV", NULL) > 0);
}

TEST_CASE(a_card_of_999_runs_offers_the_newest_that_fit)
{
    /*
     * The runs arrive oldest first, which is the order a FAT (File Allocation
     * Table) directory hands its entries back until a file is deleted and its
     * slot filled again.  Keeping the first LOG_VIEWER_MAX_FILES to arrive
     * leaves every later run off the screen for good: the browse list has one
     * page and no way to reach past it.
     */
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    int held = 0;
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        log_run_name(f.name, sizeof(f.name), i);
        f.size = (uint32_t)i;
        held = log_select_keep(set, held, LOG_VIEWER_MAX_FILES, &f);
    }
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);

    log_select_sort(set, held);
    for (int r = 0; r < held; ++r) {
        char want[LOG_RUN_NAME_MAX];
        log_run_name(want, sizeof(want), LOG_RUN_LAST - held + 1 + r);
        if (strcmp(set[r].name, want) != 0) {
            T_FAIL("row %d: got \"%s\", want \"%s\"", r, set[r].name, want);
            break;
        }
    }
    /* The whole entry travels with the name it was kept by. */
    CHECK_EQ(set[held - 1].size, (uint32_t)LOG_RUN_LAST);
}

/*
 * Directories sort above files whichever side of the comparison they arrive
 * on. qsort decides that, so a set whose only directory is already first
 * exercises one answer and never the other, and a comparator that returned
 * the same sign both ways would order correctly here and wrongly on a card
 * that happened to be read in another order.
 */
TEST_CASE(a_directory_sorts_first_from_either_side_of_the_comparison)
{
    static const char *const names[] = { "A.CSV", "DIR", "B.CSV", "ZDIR" };
    static const bool dirs[]         = { false,   true,  false,   true };

    log_viewer_file_t set[4];
    for (int i = 0; i < 4; ++i) {
        memset(&set[i], 0, sizeof(set[i]));
        snprintf(set[i].name, sizeof(set[i].name), "%s", names[i]);
        set[i].is_dir = dirs[i];
    }
    log_select_sort(set, 4);
    CHECK_STR_EQ(set[0].name, "DIR");
    CHECK_STR_EQ(set[1].name, "ZDIR");
    CHECK_STR_EQ(set[2].name, "A.CSV");
    CHECK_STR_EQ(set[3].name, "B.CSV");

    /* And again with the input reversed, so the comparator meets the same
     * pairs the other way round. */
    for (int i = 0; i < 4; ++i) {
        memset(&set[i], 0, sizeof(set[i]));
        snprintf(set[i].name, sizeof(set[i].name), "%s", names[3 - i]);
        set[i].is_dir = dirs[3 - i];
    }
    log_select_sort(set, 4);
    CHECK_STR_EQ(set[0].name, "DIR");
    CHECK_STR_EQ(set[1].name, "ZDIR");
    CHECK_STR_EQ(set[2].name, "A.CSV");
    CHECK_STR_EQ(set[3].name, "B.CSV");
}

TEST_CASE(a_full_list_keeps_runs_over_what_it_cannot_date)
{
    /* Four slots and five entries, offered in the order a card holds them.
     * Runs take the slots; of what is left the name decides, so the file that
     * sorts last is the one that goes. */
    static const struct {
        const char *name;
        bool is_dir;
    } offered[] = {
        { "SWEEP.CSV", false },
        { "BENCH001.CSV", false },
        { "OLD", true },
        { "BENCH002.CSV", false },
        { "BENCH003.CSV", false },
    };
    log_viewer_file_t set[4];
    int held = 0;
    for (size_t i = 0; i < sizeof(offered) / sizeof(offered[0]); ++i) {
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", offered[i].name);
        f.is_dir = offered[i].is_dir;
        held = log_select_keep(set, held, (int)(sizeof(set) / sizeof(set[0])),
                               &f);
    }
    CHECK_EQ(held, 4);

    /* A candidate that ranks below a full set is dropped where it stands, and
     * the set does not move: the four already held are the four to keep. */
    log_viewer_file_t late;
    memset(&late, 0, sizeof(late));
    snprintf(late.name, sizeof(late.name), "%s", "ZZZ_IMPORT.CSV");
    CHECK_EQ(log_select_keep(set, held, 4, &late), 4);

    log_select_sort(set, held);
    /* Directories first, then names: the order the browse list draws. */
    CHECK_STR_EQ(set[0].name, "OLD");
    CHECK_STR_EQ(set[1].name, "BENCH001.CSV");
    CHECK_STR_EQ(set[2].name, "BENCH002.CSV");
    CHECK_STR_EQ(set[3].name, "BENCH003.CSV");

    /* A set with no room takes nothing and says so. */
    log_viewer_file_t one;
    memset(&one, 0, sizeof(one));
    snprintf(one.name, sizeof(one.name), "%s", "BENCH999.CSV");
    CHECK_EQ(log_select_keep(set, 0, 0, &one), 0);
    CHECK_EQ(log_select_keep(NULL, 0, 4, &one), 0);
    CHECK_EQ(log_select_keep(set, held, 4, NULL), held);
    log_select_sort(NULL, 4);
    log_select_sort(set, 1);
    CHECK_STR_EQ(set[0].name, "OLD");
}

TEST_CASE(a_list_that_was_cut_says_so)
{
    /*
     * A card takes LOG_RUN_LAST runs and the list holds LOG_VIEWER_MAX_FILES,
     * so most of a full card is not on the screen.  The browse panel's tab
     * carries both numbers when that happens: a list that quietly shows a
     * subset reads as the whole card, and the run that is missing from it
     * reads as a run that was never written.
     */
    fresh();
    g_card_runs = LOG_VIEWER_MAX_FILES;  /* exactly fits: nothing to say */
    log_viewer_refresh();
    draw();
    CHECK_EQ(browse_tab_width(), tab_width_for("FILES"));

    fresh();
    g_card_runs = 200;
    log_viewer_refresh();
    draw();
    char want[32];
    snprintf(want, sizeof(want), "%d OF %d FILES", LOG_VIEWER_MAX_FILES, 200);
    CHECK_EQ(browse_tab_width(), tab_width_for(want));
}

TEST_CASE(a_count_above_the_list_does_not_reach_past_it)
{
    /*
     * The count is what the card holds, which is more than the lister wrote
     * into the array.  Rows come out of the array, so the list has to stop at
     * what is in it: scrolling to the bottom of a 200-run card lands on the
     * newest run written, not on whatever follows the array.
     */
    fresh();
    g_card_runs = 200;
    log_viewer_refresh();

    /* Two drags: one press scrolls by its own travel and the bottom of a
     * 48-row list is further down than that. */
    for (int i = 0; i < 2; ++i) {
        send(TOUCH_EVENT_DOWN, 400, 300);
        send(TOUCH_EVENT_MOVE, 400, -700);
        send(TOUCH_EVENT_UP, 400, -700);
    }
    draw();

    /* The top row is now seven from the end of what was written, and the last
     * run written is the newest one on the card. */
    char want[LOG_RUN_NAME_MAX];
    log_run_name(want, sizeof(want), g_card_runs - 7 + 1);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_STR_EQ(log_viewer_open_name(), want);
}

int main(void)
{
    RUN(every_view_draws_something);
    RUN(a_file_that_vanished_between_listing_and_opening);
    RUN(with_no_io_at_all_the_screen_still_works);
    RUN(a_file_that_proves_both_conventions_is_flagged);
    RUN(a_ragged_file_is_flagged_without_a_convention_conflict);
    RUN(a_file_with_no_time_column_plots_against_the_row_number);
    RUN(a_two_row_file_still_draws_a_trace);
    RUN(no_card_says_so_and_stays_put);
    RUN(an_empty_card_is_not_the_same_as_no_card);
    RUN(a_second_tap_opens_the_file_and_analyses_it);
    RUN(a_cancelled_press_opens_nothing);
    RUN(a_german_file_is_read_as_german);
    RUN(plotting_loads_exactly_the_picked_columns);
    RUN(tapping_a_column_toggles_it_and_the_time_axis_is_not_offered);
    RUN(an_override_does_not_discard_the_column_selection);
    RUN(a_directory_row_cannot_be_opened_as_a_log);
    RUN(a_column_of_prose_is_not_plottable);
    RUN(forcing_the_separator_and_the_convention_re_runs_the_analysis);
    RUN(the_cursor_follows_the_touch_and_stays_in_range);
    RUN(fields_returns_to_the_picker_and_keeps_the_selection);
    RUN(back_from_the_picker_returns_to_the_file_list);
    RUN(redraw_leaves_no_stale_pixels);
    RUN(both_framebuffers_follow_an_interaction);
    RUN(a_run_name_and_its_number_are_one_rule);
    RUN(the_newest_run_outranks_the_rest_of_the_card);
    RUN(a_card_of_999_runs_offers_the_newest_that_fit);
    RUN(a_directory_sorts_first_from_either_side_of_the_comparison);
    RUN(a_full_list_keeps_runs_over_what_it_cannot_date);
    RUN(a_list_that_was_cut_says_so);
    RUN(a_count_above_the_list_does_not_reach_past_it);
    return test_summary("logview");
}
