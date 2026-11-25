/**
 * @file test_logview_graph.c
 * @brief LOGS' graph of an XY file (log_graph.h): a LIMIT RUN's and a RUDDER
 *        AUTO's file open in the graph over the pulse; the markers at the
 *        plot's first and last pixel column; a side with no bound; a file of
 *        one row; the chips' value at the cursor; a chip tap that focuses,
 *        hides and brings back its series, and the focused scale at the
 *        left edge; the cursor by finger and by < and >, over the rows
 *        drawn only; RUDDR's SEARCH and STEPS tabs; a second finger on a
 *        footer key while one holds the cursor; BACK; and the frame work of
 *        a steady frame under the logs limit of tools/frame_cost.py.
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

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

/** @brief The plot's first and last pixel column, and its rows. */
#define PLOT_X0 LV_PV_X0
#define PLOT_X1 (LV_PV_X0 + LV_PV_COLS - 1)
/** @brief Below the two rows of marker tags at the plot's top. */
#define PLOT_Y0 (LV_PV_TOP + 2 * 20 + 4)

/* ------------------------------------------------------------- the card */

/* A LIMIT RUN: the low side bound at 1000us, its end point 1025us; the high
 * side free to 2000us.  20us steps from the center, the angle 0.04° a us. */
static char g_limit[8192];
/* A LIMIT RUN of one row. */
static const char k_one[] =
    "row;search;side;pulse (us);current (A);voltage (V);angle (deg);state;"
    "free (A);threshold (A);end point (us);info\n"
    "1;1;LOW;1500;0.050;6.00;0.00;BASE;0.050;;;\n";
/* RUDDER AUTO: steps, verifies, then the search down and up. */
static const char k_ruddr[] =
    "step;side (us);write;value;measured (deg);goal (deg);measured (mm);"
    "goal (mm);within;kind;pulse (us);current (A);info\n"
    "1;1000;0;;-24.00;-30.00;;;0;STEP;1000;;\n"
    "2;1000;1;120;-29.20;-30.00;;;1;STEP;1000;;\n"
    "3;2000;0;;25.00;30.00;;;0;STEP;2000;;\n"
    "4;2000;1;125;30.10;30.00;;;1;STEP;2000;;\n"
    "5;1000;V;;-29.50;-30.00;;;1;VERIFY;1000;;\n"
    "6;2000;V;;30.00;30.00;;;1;VERIFY;2000;;\n"
    "7;1500;V;;0.40;0.00;;;0;VERIFY;1500;;\n"
    "8;1500;S;50;0.00;;;;;SEARCH;1500;0.050\n"
    "9;1490;S;52;-0.90;;;;;SEARCH;1490;0.052\n"
    "10;1480;S;300;-1.80;;;;;SEARCH;1480;0.300\n"
    "11;1500;S;50;0.00;;;;;SEARCH;1500;0.050\n"
    "12;1510;S;51;0.90;;;;;SEARCH;1510;0.051\n"
    "13;1520;S;310;1.80;;;;;SEARCH;1520;0.310\n";

/** @brief The graph card's files. */
static const struct {
    const char *name;
    const char *text;
} k_files[] = {
    { "LIMIT008.CSV", g_limit },
    { "LIMIT009.CSV", k_one },
    { "RUDDR004.CSV", k_ruddr },
    { "MOTOR042_kv1200.CSV", k_motor_run },
};

/** @brief Write g_limit. */
static void make_limit(void)
{
    size_t at = (size_t)snprintf(g_limit, sizeof(g_limit),
                                 "row;search;side;pulse (us);current (A);"
                                 "voltage (V);angle (deg);state;free (A);"
                                 "threshold (A);end point (us);info\n");
    int row = 1;
    for (int us = 1500; us >= 1000; us -= 20, ++row) {
        const bool bound = us == 1000;
        const char *state = row <= 2 ? "BASE" : bound ? "BOUND" : "FREE";
        at += (size_t)snprintf(g_limit + at, sizeof(g_limit) - at,
                               "%d;1;LOW;%d;%.3f;6.00;%.2f;%s;0.050;%s;%s;\n",
                               row, us, bound ? 0.400 : 0.050 + 0.0001 * row,
                               (us - 1500) * 0.04, state,
                               row <= 2 ? "" : "0.200", bound ? "1025" : "");
    }
    for (int us = 1500; us <= 2000; us += 20, ++row) {
        at += (size_t)snprintf(g_limit + at, sizeof(g_limit) - at,
                               "%d;1;HIGH;%d;%.3f;6.00;%.2f;%s;0.050;%s;;\n",
                               row, us, 0.050 + 0.0001 * row,
                               (us - 1500) * 0.04,
                               us <= 1520 ? "BASE" : "FREE",
                               us <= 1520 ? "" : "0.200");
    }
}

/** @brief List hook: the graph card's files, one page. */
static int graph_list(log_viewer_file_t *out, int max_entries,
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

static log_mem_ctx_t g_graph_ctx;

/** @brief Open hook: a graph card's file as an in-memory source. */
static bool graph_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < sizeof(k_files) / sizeof(k_files[0]); ++i) {
        if (strcmp(name, k_files[i].name) == 0) {
            log_source_memory(src, &g_graph_ctx, k_files[i].text,
                              strlen(k_files[i].text));
            return true;
        }
    }
    return false;
}

static const log_viewer_io_t k_graph_io = {
    .list = graph_list,
    .open = graph_open,
    .volume = fake_volume,
    .mounts = fake_mounts,
};

/** @brief The screen fresh, the graph card in, @p name opened. */
static void open_graph(const char *name)
{
    fresh();
    make_limit();
    log_viewer_set_io(&k_graph_io);
    log_viewer_refresh();
    REQUIRE(log_viewer_show(LOG_TAB_USER, name, g_mounts) == LOG_SHOW_OPEN);
    draw();
}

/** @brief The pixel at (@p x, @p y) of the screen's frame. */
static gfx_color_t px(int x, int y)
{
    return s_fb[y * W + x];
}

/** @brief Pixels of colour @p col in column @p x from PLOT_Y0 down. */
static int column_count(int x, gfx_color_t col)
{
    int n = 0;
    for (int y = PLOT_Y0; y < LV_PV_BOTTOM; ++y) {
        n += (px(x, y) == col);
    }
    return n;
}

/** @brief Pixels of colour @p col inside the plot from row @p y0 down. */
static int plot_count_from(int y0, gfx_color_t col)
{
    int n = 0;
    for (int y = y0; y < LV_PV_BOTTOM; ++y) {
        for (int x = PLOT_X0; x <= PLOT_X1; ++x) {
            n += (px(x, y) == col);
        }
    }
    return n;
}

/** @brief Pixels of colour @p col inside the plot, under its tags. */
static int plot_count(gfx_color_t col)
{
    return plot_count_from(PLOT_Y0, col);
}

/** @brief The names of the series loaded, joined by ','. */
static const char *series(char *out, size_t n)
{
    out[0] = '\0';
    const log_data_t *d = log_viewer_data();
    size_t at = 0;
    for (int k = 0; d != NULL && k < d->n_fields && at < n; ++k) {
        at += (size_t)snprintf(out + at, n - at, "%s%s", (k > 0) ? "," : "",
                               d->field[k].name);
    }
    return out;
}

/* ------------------------------------------------------------- the cases */

/* LIMIT opens in the graph over the pulse: current and angle in the chips,
 * the kind in the summary, the cursor on the last row. */
TEST_CASE(a_limit_file_opens_in_the_graph_over_its_pulse)
{
    char got[160];
    open_graph("LIMIT008.CSV");
    CHECK(log_graph_shown());
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_LIMIT);
    CHECK_STR_EQ(series(got, sizeof(got)), "pulse,current,angle");
    log_viewer_summary(got, sizeof(got));
    CHECK_STR_EQ(got, "LIMIT008.CSV   52 ROWS   LIMIT RUN");
    CHECK_EQ(log_viewer_cursor(), 51);
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    CHECK_EQ(mk->n_traces, 2);
    CHECK(!log_graph_time());
    /* A MOTOR run's graph is over its time (test_logview_time.c). */
    open_graph("MOTOR042_kv1200.CSV");
    CHECK(log_graph_shown());
    CHECK(log_graph_time());
}

/* The x scale runs from the lowest pulse to the highest with no pad: the
 * low side's BOUND line stands on the plot's first column, the free high
 * side's LAST on its last, each drawn down the plot in its colour. */
TEST_CASE(markers_stand_on_the_first_and_last_pixel_column)
{
    open_graph("LIMIT008.CSV");
    /* The cursor off the last row, whose column it would cover. */
    tap(LV_PV_PREV_CX, LV_FOOT_CY);
    draw();
    CHECK_EQ(log_graph_x_px(1000.0f), PLOT_X0);
    CHECK_EQ(log_graph_x_px(2000.0f), PLOT_X1);
    CHECK_EQ(log_graph_row_px(25), PLOT_X0);
    CHECK_EQ(log_graph_row_px(51), PLOT_X1);
    /* Dashed: 6 of every 10 rows, less what the traces cover. */
    CHECK(column_count(PLOT_X0, UI_DANGER) > 120);
    CHECK(column_count(PLOT_X1, UI_TEXT_FAINT) > 120);
    /* END POINT 25us inside the bound, the center solid. */
    CHECK(column_count(log_graph_x_px(1025.0f), UI_OK) > 120);
    CHECK(column_count(log_graph_x_px(1500.0f), UI_TEXT_DIM)
          > LV_PV_BOTTOM - PLOT_Y0 - 40);
    /* The free side has no BOUND line: no danger pixels at its end. */
    CHECK_EQ(column_count(PLOT_X1, UI_DANGER), 0);
}

/* A side with no bound: LAST at its outermost pulse, no BOUND nor END
 * POINT there; the levels of both sides drawn. */
TEST_CASE(a_side_with_no_bound_shows_its_last_position_only)
{
    open_graph("LIMIT008.CSV");
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    int bound = 0;
    int last = 0;
    int levels = 0;
    for (int m = 0; m < mk->n_marks; ++m) {
        bound += mk->mark[m].kind == LOG_MARK_BOUND;
        last += mk->mark[m].kind == LOG_MARK_LAST
                && mk->mark[m].at == 2000.0f;
        levels += mk->mark[m].level;
    }
    CHECK_EQ(bound, 1);
    CHECK_EQ(last, 1);
    CHECK_EQ(levels, 4);
    /* The threshold's level, amber across the plot. */
    CHECK(plot_count(UI_WARN) > 200);
}

/* One row: the graph opens, the row on the cursor, a point drawn. */
TEST_CASE(a_file_of_one_row_opens_in_the_graph)
{
    char got[160];
    open_graph("LIMIT009.CSV");
    CHECK(log_graph_shown());
    CHECK_EQ(log_viewer_cursor(), 0);
    log_viewer_summary(got, sizeof(got));
    CHECK_STR_EQ(got, "LIMIT009.CSV   1 ROWS   LIMIT RUN");
    tap(LV_PV_NEXT_CX, LV_FOOT_CY);
    tap(LV_PV_PREV_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_cursor(), 0);
    tap(LV_PV_CX, LV_PV_CY);
    CHECK_EQ(log_viewer_cursor(), 0);
    draw();
    /* The cursor's line on the one column, in the text colour. */
    CHECK(column_count(log_graph_row_px(0), UI_TEXT) > 100);
}

/* A tap on a chip focuses its series and puts its scale at the plot's
 * left edge; a second hides it, a third brings it back focused. */
TEST_CASE(a_chip_tap_focuses_then_hides_then_brings_back)
{
    open_graph("LIMIT008.CSV");
    const gfx_color_t cur = log_viewer_series_color(1);
    const int shown = plot_count(cur);
    CHECK(shown > 300);
    CHECK_EQ(log_graph_focus(), -1);
    /* No scale at the left edge yet. */
    int edge = 0;
    for (int y = PLOT_Y0; y < PLOT_Y0 + 20; ++y) {
        for (int x = PLOT_X0; x < PLOT_X0 + 60; ++x) {
            edge += px(x, y) == cur;
        }
    }
    tap(LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 1);
    CHECK(!log_graph_hidden(1));
    draw();
    int edge_f = 0;
    for (int y = PLOT_Y0; y < PLOT_Y0 + 20; ++y) {
        for (int x = PLOT_X0; x < PLOT_X0 + 60; ++x) {
            edge_f += px(x, y) == cur;
        }
    }
    CHECK(edge_f > edge + 20);
    /* Focused: drawn 2px thick. */
    CHECK(plot_count(cur) > shown);
    tap(LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK(log_graph_hidden(1));
    CHECK_EQ(log_graph_focus(), -1);
    draw();
    CHECK_EQ(plot_count(cur), 0);
    tap(LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK(!log_graph_hidden(1));
    CHECK_EQ(log_graph_focus(), 1);
    /* The other chip takes the focus; the first stays shown. */
    tap(LV_GR_CHIP_CX(1), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 2);
    CHECK(!log_graph_hidden(1));
    /* A press that slides off its chip is no tap. */
    send(TOUCH_EVENT_DOWN, LV_GR_CHIP_CX(1), LV_GR_CHIP_CY);
    send(TOUCH_EVENT_UP, LV_GR_CHIP_CX(1), LV_GR_CHIP_CY + 200);
    CHECK_EQ(log_graph_focus(), 2);
    draw();
}

/* The chip shows its value at the cursor in the scale's decimals. */
TEST_CASE(a_chip_shows_the_value_at_the_cursor)
{
    char got[160];
    open_graph("LIMIT008.CSV");
    /* The cursor on row 26, the high side's first: 1500us. */
    for (int i = 0; i < 25; ++i) {
        tap(LV_PV_PREV_CX, LV_FOOT_CY);
    }
    CHECK_EQ(log_viewer_cursor(), 26);
    const log_data_t *d = log_viewer_data();
    REQUIRE(d != NULL);
    CHECK_FEQ(d->value[0][26], 1500.0f);
    draw();
    /* The footer's line names the x column, its value and the row. */
    REQUIRE(log_graph_cursor_line(got, sizeof(got)));
    CHECK_STR_EQ(got, "pulse 1500us   row 27 of 52");
}

/* A finger on the plot puts the cursor on the row drawn under it; < and >
 * step over the rows the picture does not draw. */
TEST_CASE(the_cursor_moves_over_the_rows_drawn_only)
{
    open_graph("RUDDR004.CSV");
    CHECK(log_graph_shown());
    /* The search's last row, row 13. */
    CHECK_EQ(log_viewer_cursor(), 12);
    tap(log_graph_x_px(1480.0f), LV_PV_CY);
    CHECK_EQ(log_viewer_cursor(), 9);
    /* < to the first SEARCH row and no further: the steps are not drawn. */
    tap(LV_PV_PREV_CX, LV_FOOT_CY);
    tap(LV_PV_PREV_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_cursor(), 7);
    tap(LV_PV_PREV_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_cursor(), 7);
    tap(LV_PV_NEXT_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_cursor(), 8);
    /* A finger dragged to the plot's right end: the highest pulse. */
    send(TOUCH_EVENT_DOWN, LV_PV_CX, LV_PV_CY);
    send(TOUCH_EVENT_MOVE, PLOT_X1, LV_PV_CY);
    CHECK_EQ(log_viewer_cursor(), 12);
    /* A second finger on > while the first holds the cursor: > acts. */
    send_id(TOUCH_EVENT_DOWN, 2, LV_PV_PREV_CX, LV_FOOT_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_PV_PREV_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_cursor(), 11);
    send(TOUCH_EVENT_UP, PLOT_X1, LV_PV_CY);
    draw();
}

/* RUDDR's tabs: SEARCH first, STEPS loads its own columns, SEARCH again. */
TEST_CASE(ruddr_tabs_switch_between_search_and_steps)
{
    char got[160];
    open_graph("RUDDR004.CSV");
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_RUDDR_SEARCH);
    CHECK_STR_EQ(series(got, sizeof(got)), "pulse,current,measured");
    log_viewer_summary(got, sizeof(got));
    CHECK_STR_EQ(got, "RUDDR004.CSV   13 ROWS   RUDDER AUTO");
    tap(LV_GR_TAB_CX(1), LV_GR_CHIP_CY);
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_RUDDR_STEPS);
    CHECK_STR_EQ(series(got, sizeof(got)), "step,measured,goal");
    CHECK(log_graph_shown());
    /* The cursor on the last verify, the last row STEPS draws. */
    CHECK_EQ(log_viewer_cursor(), 6);
    const log_markset_t *mk = log_graph_marks();
    REQUIRE(mk != NULL);
    CHECK_EQ(mk->n_traces, 3);
    draw();
    /* The measures outside the tolerance in the warning colour: step 1's
     * point at the plot's first column, half of it inside, and step 3's;
     * the cursor's point covers the center's verify. */
    CHECK(plot_count_from(LV_PV_TOP, UI_WARN) >= 15 + 25);
    tap(LV_GR_TAB_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_RUDDR_SEARCH);
    CHECK_EQ(log_viewer_cursor(), 12);
    draw();
}

/* A release the touch layer cut, a contact that jumped, taps no chip and
 * ends the press: the next tap focuses. */
TEST_CASE(a_cut_release_taps_no_chip)
{
    open_graph("LIMIT008.CSV");
    send(TOUCH_EVENT_DOWN, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    touch_event_t up = {
        .type = TOUCH_EVENT_UP,
        .flags = TOUCH_FLAG_NO_TAP,
        .point = { .id = 1, .x = LV_GR_CHIP_CX(0), .y = LV_GR_CHIP_CY },
    };
    screen()->event(&up);
    CHECK_EQ(log_graph_focus(), -1);
    CHECK(!log_graph_hidden(1));
    tap(LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 1);
}

/* Two fingers on RUDDR's graph: one on a chip, a second holding the tab
 * already shown.  The first lifts first: its release ends its own press and
 * taps its chip; the tab's release then changes nothing, and the next press
 * on the plot moves the cursor. */
TEST_CASE(each_finger_ends_its_own_press_beside_a_held_tab)
{
    open_graph("RUDDR004.CSV");
    REQUIRE(log_graph_shown());
    send_id(TOUCH_EVENT_DOWN, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    send_id(TOUCH_EVENT_DOWN, 2, LV_GR_TAB_CX(0), LV_GR_CHIP_CY);
    send_id(TOUCH_EVENT_UP, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 1);
    send_id(TOUCH_EVENT_UP, 2, LV_GR_TAB_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_RUDDR_SEARCH);
    /* The plot takes a new press: the cursor goes to the lowest pulse. */
    send_id(TOUCH_EVENT_DOWN, 3, PLOT_X0, LV_PV_CY);
    send_id(TOUCH_EVENT_UP, 3, PLOT_X0, LV_PV_CY);
    CHECK_EQ(log_viewer_cursor(), 9);
    /* The same with a finger dragging the plot's cursor. */
    send_id(TOUCH_EVENT_DOWN, 1, LV_PV_CX, LV_PV_CY);
    send_id(TOUCH_EVENT_DOWN, 2, LV_GR_TAB_CX(0), LV_GR_CHIP_CY);
    send_id(TOUCH_EVENT_MOVE, 1, PLOT_X1, LV_PV_CY);
    CHECK_EQ(log_viewer_cursor(), 12);
    send_id(TOUCH_EVENT_UP, 1, PLOT_X1, LV_PV_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_GR_TAB_CX(0), LV_GR_CHIP_CY);
    tap(LV_GR_CHIP_CX(1), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 2);
    draw();
}

/* A finger on a chip while another taps MORE: the list cancels the
 * chip's press; its release, under the list, taps nothing, and once the
 * list is closed a new tap on the chip focuses it. */
TEST_CASE(more_cancels_a_held_graph_press)
{
    open_graph("LIMIT008.CSV");
    send_id(TOUCH_EVENT_DOWN, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    REQUIRE(log_graph_pressed());
    send_id(TOUCH_EVENT_DOWN, 2, LV_PV_MORE_CX, LV_FOOT_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_PV_MORE_CX, LV_FOOT_CY);
    CHECK(!log_graph_pressed());
    send_id(TOUCH_EVENT_UP, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), -1);
    /* A press beside the list closes it. */
    send_id(TOUCH_EVENT_DOWN, 3, LV_PV_CX, LV_PV_CY);
    send_id(TOUCH_EVENT_UP, 3, LV_PV_CX, LV_PV_CY);
    REQUIRE(log_viewer_view() == LOG_VIEW_PLOT);
    send_id(TOUCH_EVENT_DOWN, 4, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    send_id(TOUCH_EVENT_UP, 4, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 1);
    draw();
}

/* A finger on the plot while another taps BACK: the press ends with the
 * graph left, and its release on the list moves no cursor. */
TEST_CASE(back_cancels_a_held_graph_press)
{
    open_graph("LIMIT008.CSV");
    send_id(TOUCH_EVENT_DOWN, 1, LV_PV_CX, LV_PV_CY);
    REQUIRE(log_graph_pressed());
    send_id(TOUCH_EVENT_DOWN, 2, LV_PV_BACK_CX, LV_FOOT_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_PV_BACK_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(!log_graph_pressed());
    send_id(TOUCH_EVENT_UP, 1, LV_PV_CX, LV_PV_CY);
    draw();
}

/* A finger on a chip while another taps STEPS: the picture reloads, the
 * press is dropped with the old data, and the release taps nothing. */
TEST_CASE(a_tab_change_drops_a_held_graph_press)
{
    open_graph("RUDDR004.CSV");
    send_id(TOUCH_EVENT_DOWN, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    send_id(TOUCH_EVENT_DOWN, 2, LV_GR_TAB_CX(1), LV_GR_CHIP_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_GR_TAB_CX(1), LV_GR_CHIP_CY);
    REQUIRE(log_viewer_preset() == LOG_PRESET_RUDDR_STEPS);
    CHECK(!log_graph_pressed());
    send_id(TOUCH_EVENT_UP, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), -1);
    tap(LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), 1);
}

/* A finger on a chip while the file is opened again (FILES' OPEN): the
 * reload drops the press, and the release taps nothing. */
TEST_CASE(a_reload_drops_a_held_graph_press)
{
    open_graph("LIMIT008.CSV");
    send_id(TOUCH_EVENT_DOWN, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    REQUIRE(log_viewer_show(LOG_TAB_USER, "LIMIT008.CSV", g_mounts)
            == LOG_SHOW_OPEN);
    CHECK(!log_graph_pressed());
    send_id(TOUCH_EVENT_UP, 1, LV_GR_CHIP_CX(0), LV_GR_CHIP_CY);
    CHECK_EQ(log_graph_focus(), -1);
}

/* MORE > COLUMNS, then the import view's PLOT: the columns picked by row
 * number, every row, no graph. */
TEST_CASE(plot_from_the_import_view_draws_rows_not_the_graph)
{
    open_graph("RUDDR004.CSV");
    REQUIRE(log_graph_shown());
    tap(LV_PV_MORE_CX, LV_FOOT_CY);
    tap(LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    REQUIRE(log_viewer_view() == LOG_VIEW_IMPORT);
    tap(LV_IM_BTN_CX(3), LV_FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK(!log_graph_shown());
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_NONE);
    const log_data_t *d = log_viewer_data();
    REQUIRE(d != NULL);
    CHECK_EQ(d->count, 13);
    CHECK_EQ(log_viewer_cursor(), 12);
    draw();
}

/* BACK leaves the graph for the list; MORE > COLUMNS reaches the import
 * view, and its PLOT the time plot of the columns picked. */
TEST_CASE(back_and_more_work_from_the_graph)
{
    open_graph("LIMIT008.CSV");
    tap(LV_PV_BACK_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(!log_graph_shown());
    REQUIRE(log_viewer_show(LOG_TAB_USER, "LIMIT008.CSV", g_mounts)
            == LOG_SHOW_OPEN);
    tap(LV_PV_MORE_CX, LV_FOOT_CY);
    tap(LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(!log_graph_shown());
    draw();
}

/* A steady frame of the graph repaints nothing: under the 1,000 fills
 * tools/frame_cost.py holds the logs mode to.  A changed cursor repaints. */
TEST_CASE(a_steady_frame_of_the_graph_does_no_work)
{
    open_graph("LIMIT008.CSV");
    screen()->render(&s_c, 0);
    screen()->render(&s_c, 1);
    const unsigned long before = gfx_work();
    screen()->render(&s_c, 0);
    screen()->render(&s_c, 1);
    CHECK_EQ(gfx_work() - before, 0ul);
    tap(LV_PV_PREV_CX, LV_FOOT_CY);
    const unsigned long moved = gfx_work();
    screen()->render(&s_c, 0);
    /* A whole repaint: the screen cleared, the plot's bands, the marks. */
    const unsigned long whole = gfx_work() - moved;
    CHECK(whole > (unsigned long)(W * (H - 60)));
    CHECK(whole < 3ul * (unsigned long)W * (unsigned long)H);
}

/* German: the summary's rows and the footer's line in German, the kind's
 * name as on the bench. */
TEST_CASE(the_graph_speaks_german)
{
    char got[160];
    open_graph("LIMIT008.CSV");
    ui_text_set_language(UI_LANG_DE);
    log_viewer_summary(got, sizeof(got));
    CHECK_STR_EQ(got, "LIMIT008.CSV   52 ZEILEN   LIMIT RUN");
    REQUIRE(log_graph_cursor_line(got, sizeof(got)));
    CHECK_STR_EQ(got, "pulse 2000us   Zeile 52 von 52");
    draw();
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_limit_file_opens_in_the_graph_over_its_pulse);
    RUN(markers_stand_on_the_first_and_last_pixel_column);
    RUN(a_side_with_no_bound_shows_its_last_position_only);
    RUN(a_file_of_one_row_opens_in_the_graph);
    RUN(a_chip_tap_focuses_then_hides_then_brings_back);
    RUN(a_chip_shows_the_value_at_the_cursor);
    RUN(the_cursor_moves_over_the_rows_drawn_only);
    RUN(ruddr_tabs_switch_between_search_and_steps);
    RUN(back_and_more_work_from_the_graph);
    RUN(a_cut_release_taps_no_chip);
    RUN(more_cancels_a_held_graph_press);
    RUN(back_cancels_a_held_graph_press);
    RUN(a_tab_change_drops_a_held_graph_press);
    RUN(a_reload_drops_a_held_graph_press);
    RUN(each_finger_ends_its_own_press_beside_a_held_tab);
    RUN(plot_from_the_import_view_draws_rows_not_the_graph);
    RUN(a_steady_frame_of_the_graph_does_no_work);
    RUN(the_graph_speaks_german);
    return test_summary("logview_graph");
}
