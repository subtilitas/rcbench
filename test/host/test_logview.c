/*
 * Host unit tests for the log viewer screen.
 *
 * The screen is handed an I/O vtable, so the whole browse -> import -> plot
 * path runs here against strings in memory: no card, no filesystem, no board.
 * What is tested is what the screen decides -- which file, which columns,
 * which convention, where the cursor lands -- and one property about pixels:
 * that a redraw over an older frame leaves nothing of it behind.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>

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
 * Both conventions proven in one file: the screen has to say so rather than
 * pick a winner behind the user's back.
 *
 * Semicolon-delimited on purpose.  The comma-delimited version of this fixture
 * did not contain a single comma-decimal cell -- "10,23" was split into two
 * fields by the delimiter -- so it proved nothing, and the assertion that used
 * it was carried entirely by its raggedness.
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

/* Geometry mirrored from the screen; if it moves, these move with it. */
/* The layout moved up 40 px when the router took the band and the home
 * tag; these follow it. */
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

    /* And the banner that reports it is actually painted -- the plumbing from
     * log_votes_result() to the screen was covered by nothing. */
    CHECK(pixels_of(UI_DANGER) > 0);
}

/* Raggedness is the other branch of the same banner, and it has to be
 * reachable without a convention conflict or neither is really tested. */
TEST_CASE(a_ragged_file_is_flagged_without_a_convention_conflict)
{
    fresh();
    /* Eight cards, seven visible rows: scroll to the bottom first, where the
     * last file lands on the last visible row.  That the row has to be
     * scrolled to is part of the point -- the browse list had an eighth row
     * that was drawn but could not be touched. */
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
    /* The rpm column is written 1.234 there and must not become 1.234. */
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
 * listed -- and opening one used to run the whole analysis against something
 * that is not a log.  There is no directory navigation yet, so it has to say
 * so rather than fail obscurely three steps later. */
/* Both override buttons re-run the analysis, so rebuilding the picked set
 * from scratch each time meant the two controls the import view exists to
 * provide silently destroyed the third one's state. */
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

    /* Cycle the decimal-convention override all the way round -- AUTO, DE, EN,
     * AUTO -- so the file ends up analysed exactly as it started.  Each press
     * re-runs the analysis, which used to rebuild the picked set from scratch
     * and silently undo the tap above.  Going round the loop matters: stopping
     * on DE makes voltage and current unparseable, so both a preserved
     * selection and a rebuilt one would agree by accident. */
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
    RUN(a_second_tap_opens_the_file_and_analyses_it);
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
    return test_summary("logview");
}
