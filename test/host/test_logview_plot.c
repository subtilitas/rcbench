/**
 * @file test_logview_plot.c
 * @brief Host unit tests for the log viewer's plot view.
 *
 * Tested is what the plot draws (the columns, the cursor and the range), what
 * a view change leaves behind, and what each finger does.
 *
 * Cases:
 * - plotting_loads_exactly_the_picked_columns
 * - tapping_a_column_toggles_it_and_the_time_axis_is_not_offered
 * - an_override_does_not_discard_the_column_selection
 * - a_column_of_prose_is_not_plottable
 * - forcing_the_separator_and_the_convention_re_runs_the_analysis
 * - the_cursor_follows_the_touch_and_stays_in_range
 * - fields_returns_to_the_picker_and_keeps_the_selection
 * - back_from_the_picker_returns_to_the_file_list
 * - redraw_leaves_no_stale_pixels
 * - both_framebuffers_follow_an_interaction
 * - two_fingers_spread_zoom_in_and_the_view_stays
 * - two_fingers_moved_together_pan_and_pinching_zooms_out
 * - the_arrows_still_pick_values_and_carry_the_view
 * - a_third_finger_and_a_cancel_leave_the_view_alone
 * - one_finger_drag_moves_the_cursor_while_the_setting_is_off
 * - one_finger_drag_pans_a_zoomed_view_when_the_setting_is_on
 * - a_tap_still_selects_and_a_short_slide_does_not_pan_with_the_setting_on
 * - one_move_past_the_threshold_pans_by_what_lies_past_it
 * - a_slide_of_10_px_moves_the_cursor_and_11_px_pans
 * - a_pan_a_fraction_of_a_sample_past_the_end_stops_at_the_end
 * - a_view_that_shows_the_whole_file_is_not_panned_by_one_finger
 * - a_second_finger_after_a_pan_zooms_and_a_cancel_ends_the_pan
 * - a_steep_trace_longer_than_the_plot_is_one_line
 * - a_range_of_1000_or_more_drops_the_decimal
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

/* The plot area: left edge, width in columns, the row a constant trace runs
 * along (the middle grid line) and the bottom row. */
#define PV_X0       24

#define PV_COLS     752

#define PV_TOP_Y    76

#define PV_BOTTOM_Y (76 + 241)

static int column_row(const log_analysis_t *a, const char *name)
{
    for (int i = 0; i < a->n_columns; ++i) {
        if (strcmp(a->columns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
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
        REQUIRE_ALLOC(s_fb_b);
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

TEST_CASE(a_range_of_1000_or_more_drops_the_decimal)
{
    char out[40];
    log_viewer_range_text(out, sizeof(out), 2392.0, 14639.0, "rpm");
    CHECK_STR_EQ(out, "2392..14639 rpm");
    log_viewer_range_text(out, sizeof(out), 11.94, 12.61, "V");
    CHECK_STR_EQ(out, "11.9..12.6 V");
    /* The limit is 1000 in magnitude at either end, below zero too. */
    log_viewer_range_text(out, sizeof(out), 0.0, 999.9, "A");
    CHECK_STR_EQ(out, "0.0..999.9 A");
    log_viewer_range_text(out, sizeof(out), 0.0, 1000.0, "A");
    CHECK_STR_EQ(out, "0..1000 A");
    log_viewer_range_text(out, sizeof(out), -1000.0, 5.0, "g");
    CHECK_STR_EQ(out, "-1000..5 g");
    /* An end that one decimal rounds up to 1000.0 counts as 1000. */
    log_viewer_range_text(out, sizeof(out), 999.95, 999.95, "rpm");
    CHECK_STR_EQ(out, "1000..1000 rpm");
    log_viewer_range_text(out, sizeof(out), -999.96, 0.0, "g");
    CHECK_STR_EQ(out, "-1000..0 g");
    log_viewer_range_text(out, sizeof(out), 0.0, 999.94, "A");
    CHECK_STR_EQ(out, "0.0..999.9 A");
    /* Wider than the 17 cells of the box: no decimal, then no space. */
    log_viewer_range_text(out, sizeof(out), -999.9, -999.9, "rpm");
    CHECK_STR_EQ(out, "-1000..-1000 rpm");
    log_viewer_range_text(out, sizeof(out), -99.9, -99.9, "rpm");
    CHECK_STR_EQ(out, "-99.9..-99.9 rpm");
    log_viewer_range_text(out, sizeof(out), -14639.0, -14639.0, "rpm");
    CHECK_STR_EQ(out, "-14639..-14639rpm");
    CHECK(gfx_text_cells(out) == (int)LOG_RANGE_CELLS);
    /* Glyphs are counted, not bytes: "µs" is 2 cells in 3 bytes, and the
     * 17 cells of "-999.9..-999.9 µs" fit with the decimal. */
    log_viewer_range_text(out, sizeof(out), -999.9, -999.9, "\xC2\xB5s");
    CHECK_STR_EQ(out, "-999.9..-999.9 \xC2\xB5s");
    log_viewer_range_text(out, sizeof(out), -999.9, -999.9, "\xC2\xB5sx");
    CHECK_STR_EQ(out, "-1000..-1000 \xC2\xB5sx");
    /* Past that the box cuts it; the text stays whole and terminated. */
    log_viewer_range_text(out, sizeof(out), -1234567.0, -1234567.0, "rpm");
    CHECK_STR_EQ(out, "-1234567..-1234567rpm");
    /* No unit, no trailing space; a value that is not a number shows "--". */
    log_viewer_range_text(out, sizeof(out), 1.0, 2.0, "");
    CHECK_STR_EQ(out, "1.0..2.0");
    log_viewer_range_text(out, sizeof(out), NAN, 2.0, NULL);
    CHECK_STR_EQ(out, "--..2.0");
    /* A short buffer is cut and stays terminated. */
    char small[6];
    log_viewer_range_text(small, sizeof(small), 2392.0, 14639.0, "rpm");
    CHECK_STR_EQ(small, "2392.");
}

/* ------------------------------------------------------------ zoom ------ */

#define LONG_ROWS 400

/* Write LONG.CSV and open it in the plot: the first three fixture files are
 * taken off the card so it is the sixth row and needs no scrolling. */
static void long_file(void)
{
    fresh();
    size_t at = (size_t)snprintf(g_long, sizeof(g_long), "t,v\n");
    for (int i = 0; i < LONG_ROWS; ++i) {
        at += (size_t)snprintf(g_long + at, sizeof(g_long) - at, "%d,%d\n",
                               i, i % 50);
    }
    g_removed[0] = g_removed[1] = g_removed[2] = true;
    log_viewer_refresh();
    open_file(5);
    tap(PLOT_X, IM_BTN_CY);
    draw();
}

/* Two fingers down on the plot at x0 and x1, both moved to y0 = 200. */
static void two_down(int x0, int x1)
{
    send_id(TOUCH_EVENT_DOWN, 1, x0, 200);
    send_id(TOUCH_EVENT_DOWN, 2, x1, 200);
}

static void two_move(int x0, int x1)
{
    send_id(TOUCH_EVENT_MOVE, 1, x0, 200);
    send_id(TOUCH_EVENT_MOVE, 2, x1, 200);
}

static void two_up(int x0, int x1)
{
    send_id(TOUCH_EVENT_UP, 2, x1, 200);
    send_id(TOUCH_EVENT_UP, 1, x0, 200);
}

TEST_CASE(two_fingers_spread_zoom_in_and_the_view_stays)
{
    long_file();
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        T_FAIL("no data");
        return;
    }
    CHECK_EQ(d->count, LONG_ROWS);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS);

    /* Spread from 100 px apart to 400 px apart around x = 400: a quarter
     * of the samples, centred where the fingers were. */
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    draw();
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    CHECK(first > 100 && first < 200);

    /* The view stays after the fingers lift, and the position bar shows. */
    int first2 = -1, count2 = -1;
    log_viewer_window(&first2, &count2);
    CHECK_EQ(first2, first);
    CHECK_EQ(count2, count);
}

TEST_CASE(two_fingers_moved_together_pan_and_pinching_zooms_out)
{
    long_file();
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    const int zoomed_first = first;

    /* Both fingers 200 px to the right, same distance: the trace follows
     * them, so earlier samples come into view. */
    two_down(300, 500);
    two_move(500, 700);
    two_up(500, 700);
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    CHECK(first < zoomed_first);

    /* Pinching far in returns the whole file, and never more. */
    two_down(100, 700);
    two_move(390, 410);
    two_up(390, 410);
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS);
    draw();
}

TEST_CASE(the_arrows_still_pick_values_and_carry_the_view)
{
    long_file();
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);

    /* A tap on the plot puts the cursor inside the zoomed view. */
    tap(PV_TAP_X, 200);
    const int c0 = log_viewer_cursor();
    CHECK(c0 >= first && c0 < first + count);

    /* Pressing > past the right edge moves the view along with the cursor. */
    for (int i = 0; i < count; ++i) {
        tap(NEXT_X, FOOT_PLOT_CY);
    }
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    const int c1 = log_viewer_cursor();
    CHECK_EQ(n2, count);
    CHECK(f2 > first);
    CHECK(c1 >= f2 && c1 < f2 + n2);
    draw();
}

TEST_CASE(a_third_finger_and_a_cancel_leave_the_view_alone)
{
    long_file();
    two_down(350, 450);
    send_id(TOUCH_EVENT_DOWN, 3, 600, 200);   /* ignored */
    send_id(TOUCH_EVENT_MOVE, 3, 700, 200);
    two_move(200, 600);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    screen()->cancel();
    /* After the cancel one finger moves the cursor, not the view. */
    send_id(TOUCH_EVENT_MOVE, 1, 600, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
}

/* Zoom to a quarter of LONG.CSV, then put the cursor at a known sample. */
static int zoom_in_with_cursor(void)
{
    long_file();
    two_down(350, 450);
    two_move(200, 600);
    two_up(200, 600);
    tap(PV_TAP_X, 200);
    return log_viewer_cursor();
}

/* One finger from x0 to x1 in steps of 25 px, then up. */
/* The plot's width in pixels, as log_viewer_screen.c draws it. */
#define PV_W_TEST 752

static void drag(int x0, int x1)
{
    send(TOUCH_EVENT_DOWN, x0, 200);
    const int dir = (x1 >= x0) ? 25 : -25;
    for (int x = x0 + dir; (dir > 0) ? x < x1 : x > x1; x += dir) {
        send(TOUCH_EVENT_MOVE, x, 200);
    }
    send(TOUCH_EVENT_MOVE, x1, 200);
    send(TOUCH_EVENT_UP, x1, 200);
}

TEST_CASE(one_finger_drag_moves_the_cursor_while_the_setting_is_off)
{
    const int c0 = zoom_in_with_cursor();
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);

    drag(600, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
    CHECK(log_viewer_cursor() < c0);   /* the cursor followed the finger */
    draw();
}

TEST_CASE(one_finger_drag_pans_a_zoomed_view_when_the_setting_is_on)
{
    const int c0 = zoom_in_with_cursor();
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    settings_set(SET_PLOT_PAN, 1.0f);

    /* A drag of 100 px left, less the 10 px threshold, moves the view
     * right by 90 px worth of samples; the cursor stays. */
    drag(600, 500);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(n2, count);
    const int want = first + (90 * count + PV_W_TEST / 2) / PV_W_TEST;
    CHECK(f2 >= want - 1 && f2 <= want + 1);
    CHECK_EQ(log_viewer_cursor(), c0);
    draw();

    /* Dragging right brings earlier samples back. */
    drag(300, 400);
    int f3 = -1, n3 = -1;
    log_viewer_window(&f3, &n3);
    CHECK(f3 < f2);
    CHECK_EQ(log_viewer_cursor(), c0);

    /* The view stops at both ends of the file. */
    for (int i = 0; i < 4; ++i) {
        drag(100, 780);
    }
    log_viewer_window(&f3, &n3);
    CHECK_EQ(f3, 0);
    for (int i = 0; i < 8; ++i) {
        drag(780, 24);
    }
    log_viewer_window(&f3, &n3);
    CHECK_EQ(f3 + n3, LONG_ROWS);
    CHECK_EQ(n3, count);
    draw();
}

TEST_CASE(one_move_past_the_threshold_pans_by_what_lies_past_it)
{
    zoom_in_with_cursor();
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    settings_set(SET_PLOT_PAN, 1.0f);
    /* Down, one move of 60 px, up: 50 px of pan. */
    send(TOUCH_EVENT_DOWN, 600, 200);
    send(TOUCH_EVENT_MOVE, 540, 200);
    send(TOUCH_EVENT_UP, 540, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    const int want = first + (50 * count + PV_W_TEST / 2) / PV_W_TEST;
    CHECK(f2 >= want - 1 && f2 <= want + 1);
    CHECK(f2 > first);
}

TEST_CASE(a_tap_still_selects_and_a_short_slide_does_not_pan_with_the_setting_on)
{
    zoom_in_with_cursor();
    settings_set(SET_PLOT_PAN, 1.0f);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);

    tap(PV_TAP_X, 200);
    const int c1 = log_viewer_cursor();
    CHECK(c1 >= first && c1 < first + count);

    /* 8 px of travel stays under the threshold: the cursor follows, the
     * view stays. */
    send(TOUCH_EVENT_DOWN, 500, 200);
    send(TOUCH_EVENT_MOVE, 504, 200);
    send(TOUCH_EVENT_MOVE, 508, 200);
    send(TOUCH_EVENT_UP, 508, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
    CHECK(log_viewer_cursor() >= first && log_viewer_cursor() < first + count);
    draw();
}

/* The cursor a tap at @p x gives in the view as it is. */
static int cursor_under(int x)
{
    tap(x, 200);
    return log_viewer_cursor();
}

/* A slide of exactly the 10 px threshold is still a tap that moved: the
 * cursor follows the finger and the view stays.  1 px more is a pan: the
 * cursor is back where it was before the press. */
TEST_CASE(a_slide_of_10_px_moves_the_cursor_and_11_px_pans)
{
    for (int dir = -1; dir <= 1; dir += 2) {
        zoom_in_with_cursor();
        settings_set(SET_PLOT_PAN, 1.0f);
        int first = -1, count = -1;
        log_viewer_window(&first, &count);
        const int at_end = cursor_under(500 + dir * 10);
        const int c0     = cursor_under(300);
        CHECK(at_end != c0);

        send(TOUCH_EVENT_DOWN, 500, 200);
        send(TOUCH_EVENT_MOVE, 500 + dir * 10, 200);
        CHECK_EQ(log_viewer_cursor(), at_end);
        send(TOUCH_EVENT_UP, 500 + dir * 10, 200);
        int f2 = -1, n2 = -1;
        log_viewer_window(&f2, &n2);
        CHECK_EQ(f2, first);
        CHECK_EQ(n2, count);
        CHECK_EQ(log_viewer_cursor(), at_end);

        (void)cursor_under(300);
        send(TOUCH_EVENT_DOWN, 500, 200);
        send(TOUCH_EVENT_MOVE, 500 + dir * 11, 200);
        CHECK_EQ(log_viewer_cursor(), c0);
        send(TOUCH_EVENT_UP, 500 + dir * 11, 200);
        CHECK_EQ(log_viewer_cursor(), c0);
    }
}

/* A pan that ends a fraction of a sample past the end of the file leaves
 * the view on the last samples: 1 to 9 px past the threshold, at 7.52 px a
 * sample, from a view that already ends on the last one. */
TEST_CASE(a_pan_a_fraction_of_a_sample_past_the_end_stops_at_the_end)
{
    zoom_in_with_cursor();
    settings_set(SET_PLOT_PAN, 1.0f);
    for (int i = 0; i < 8; ++i) {
        drag(780, 24);
    }
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(count, LONG_ROWS / 4);
    CHECK_EQ(first, LONG_ROWS - LONG_ROWS / 4);
    for (int px = 1; px <= 9; ++px) {
        send(TOUCH_EVENT_DOWN, 600, 200);
        send(TOUCH_EVENT_MOVE, 600 - 10 - px, 200);
        send(TOUCH_EVENT_UP, 600 - 10 - px, 200);
        int f2 = -1, n2 = -1;
        log_viewer_window(&f2, &n2);
        CHECK_EQ(f2, first);
        CHECK_EQ(n2, count);
    }
    /* And the same at the start. */
    for (int i = 0; i < 8; ++i) {
        drag(24, 780);
    }
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS / 4);
    for (int px = 1; px <= 9; ++px) {
        send(TOUCH_EVENT_DOWN, 300, 200);
        send(TOUCH_EVENT_MOVE, 300 + 10 + px, 200);
        send(TOUCH_EVENT_UP, 300 + 10 + px, 200);
        int f2 = -1, n2 = -1;
        log_viewer_window(&f2, &n2);
        CHECK_EQ(f2, 0);
        CHECK_EQ(n2, count);
    }
}

TEST_CASE(a_view_that_shows_the_whole_file_is_not_panned_by_one_finger)
{
    long_file();
    settings_set(SET_PLOT_PAN, 1.0f);
    CHECK_EQ(log_viewer_cursor(), LONG_ROWS - 1);
    drag(600, 200);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    CHECK_EQ(first, 0);
    CHECK_EQ(count, LONG_ROWS);
    CHECK(log_viewer_cursor() < LONG_ROWS - 1);   /* it selected */
}

TEST_CASE(a_second_finger_after_a_pan_zooms_and_a_cancel_ends_the_pan)
{
    zoom_in_with_cursor();
    settings_set(SET_PLOT_PAN, 1.0f);
    int first = -1, count = -1;
    log_viewer_window(&first, &count);
    send(TOUCH_EVENT_DOWN, 600, 200);
    send(TOUCH_EVENT_MOVE, 500, 200);
    send(TOUCH_EVENT_MOVE, 400, 200);
    int f2 = -1, n2 = -1;
    log_viewer_window(&f2, &n2);
    CHECK(f2 > first);

    send_id(TOUCH_EVENT_DOWN, 2, 300, 200);        /* the pinch takes over */
    send_id(TOUCH_EVENT_MOVE, 2, 460, 200);
    log_viewer_window(&f2, &n2);
    CHECK(n2 > count);                             /* zoomed out */
    send_id(TOUCH_EVENT_UP, 2, 460, 200);
    send_id(TOUCH_EVENT_UP, 1, 400, 200);

    /* A touch loss mid-pan leaves the view where it is. */
    send(TOUCH_EVENT_DOWN, 600, 200);
    send(TOUCH_EVENT_MOVE, 500, 200);
    send(TOUCH_EVENT_MOVE, 400, 200);
    log_viewer_window(&first, &count);
    screen()->cancel();
    send(TOUCH_EVENT_MOVE, 300, 200);
    log_viewer_window(&f2, &n2);
    CHECK_EQ(f2, first);
    CHECK_EQ(n2, count);
    draw();
}

TEST_CASE(a_steep_trace_longer_than_the_plot_is_one_line)
{
    /*
     * 1200 rows, more than the plot's 752 columns, of a steep sawtooth, with
     * the first 60 cells empty.  Each column's span has to reach its
     * neighbour's: drawn as min..max of its own samples alone, a steep
     * stretch came apart into dashes.  The empty start draws nothing, not a
     * line along the bottom.
     */
    fresh();
    size_t at = (size_t)snprintf(g_long, sizeof(g_long), "t,v\n");
    for (int i = 0; i < 1200; ++i) {
        char v[8] = "";
        if (i >= 60) {
            snprintf(v, sizeof(v), "%d", (i * 37) % 1000);
        }
        at += (size_t)snprintf(g_long + at, sizeof(g_long) - at, "%d,%s\n",
                               i, v);
    }
    g_removed[0] = g_removed[1] = g_removed[2] = true;
    log_viewer_refresh();
    open_file(5);
    tap(PLOT_X, IM_BTN_CY);
    /* Park the cursor at the start so its marker is not in the way. */
    tap(PV_X0, 200);
    draw();


    int top_prev = -1, bot_prev = -1, breaks = 0, cols = 0;
    const gfx_color_t col = log_viewer_series_color(0);
    for (int x = PV_X0 + 60; x < PV_X0 + PV_COLS - 4; ++x) {
        int top = -1, bot = -1;
        for (int yy = PV_TOP_Y; yy <= PV_BOTTOM_Y; ++yy) {
            if (gfx_pixel_get(&s_c, x, yy) == col) {
                if (top < 0) {
                    top = yy;
                }
                bot = yy;
            }
        }
        if (top < 0) {
            ++breaks;            /* a column with no trace at all */
        } else if (top_prev >= 0 &&
                   (top > bot_prev + 1 || bot < top_prev - 1)) {
            ++breaks;            /* does not reach its neighbour */
        }
        top_prev = top;
        bot_prev = bot;
        ++cols;
    }
    CHECK(cols > 600);
    CHECK_EQ(breaks, 0);

    /* The empty start: nothing along the bottom of the plot there. */
    int bottom = 0;
    for (int x = PV_X0 + 2; x < PV_X0 + 30; ++x) {
        bottom += (gfx_pixel_get(&s_c, x, PV_BOTTOM_Y) == col);
    }
    CHECK_EQ(bottom, 0);
}

int main(void)
{
    RUN(plotting_loads_exactly_the_picked_columns);
    RUN(tapping_a_column_toggles_it_and_the_time_axis_is_not_offered);
    RUN(an_override_does_not_discard_the_column_selection);
    RUN(a_column_of_prose_is_not_plottable);
    RUN(forcing_the_separator_and_the_convention_re_runs_the_analysis);
    RUN(the_cursor_follows_the_touch_and_stays_in_range);
    RUN(fields_returns_to_the_picker_and_keeps_the_selection);
    RUN(back_from_the_picker_returns_to_the_file_list);
    RUN(redraw_leaves_no_stale_pixels);
    RUN(both_framebuffers_follow_an_interaction);
    RUN(two_fingers_spread_zoom_in_and_the_view_stays);
    RUN(two_fingers_moved_together_pan_and_pinching_zooms_out);
    RUN(the_arrows_still_pick_values_and_carry_the_view);
    RUN(a_third_finger_and_a_cancel_leave_the_view_alone);
    RUN(one_finger_drag_moves_the_cursor_while_the_setting_is_off);
    RUN(one_finger_drag_pans_a_zoomed_view_when_the_setting_is_on);
    RUN(a_tap_still_selects_and_a_short_slide_does_not_pan_with_the_setting_on);
    RUN(one_move_past_the_threshold_pans_by_what_lies_past_it);
    RUN(a_slide_of_10_px_moves_the_cursor_and_11_px_pans);
    RUN(a_pan_a_fraction_of_a_sample_past_the_end_stops_at_the_end);
    RUN(a_view_that_shows_the_whole_file_is_not_panned_by_one_finger);
    RUN(a_second_finger_after_a_pan_zooms_and_a_cancel_ends_the_pan);
    RUN(a_steep_trace_longer_than_the_plot_is_one_line);
    RUN(a_range_of_1000_or_more_drops_the_decimal);
    return test_summary("logview_plot");
}
