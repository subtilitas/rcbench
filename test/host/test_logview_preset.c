/**
 * @file test_logview_preset.c
 * @brief LOGS' routes with the presets (log_preset.h): OPEN on a file the
 *        bench wrote goes to the plot with the preset's columns; a foreign
 *        CSV and a run's name over columns no preset knows go to the import
 *        view; MORE > COLUMNS reaches the import view; BACK and the home
 *        tag return where the plot came from; a press beside MORE's list
 *        closes it and presses nothing; FILES' OPEN takes the same route.
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The list's row of the fixture card's file @p name, as the
 *         lister writes it: LONG.CSV only while a case wrote it. */
static int row_of(const char *name)
{
    int row = 0;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]); ++i) {
        if (k_card[i].text == g_long && g_long[0] == '\0') {
            continue;
        }
        if (bench_file(k_card[i].text) && !g_bench_files) {
            continue;
        }
        if (strcmp(k_card[i].name, name) == 0) {
            return row;
        }
        ++row;
    }
    return -1;
}

/** @brief fresh(), with the bench's three files on the card. */
static void fresh_bench(void)
{
    fresh();
    g_bench_files = true;
    log_viewer_refresh();
}

/** @brief The names of the series the plot loaded, joined by ','. */
static const char *series(char *out, size_t n)
{
    out[0] = '\0';
    const log_data_t *d = log_viewer_data();
    if (d == NULL) {
        return out;
    }
    size_t at = 0;
    for (int k = 0; k < d->n_fields && at < n; ++k) {
        at += (size_t)snprintf(out + at, n - at, "%s%s", (k > 0) ? "," : "",
                               d->field[k].name);
    }
    return out;
}

/** @brief Open @p name from the list, as two taps on its row do. */
static void open_named(const char *name)
{
    const int row = row_of(name);
    REQUIRE(row >= 0);
    /* The list back at its top first: open_file() counts rows from it. */
    send(TOUCH_EVENT_DOWN, 400, BR_ROW_Y(0));
    send(TOUCH_EVENT_MOVE, 400, BR_ROW_Y(0) + 400);
    send(TOUCH_EVENT_UP, 400, BR_ROW_Y(0) + 400);
    open_file(row);
}

TEST_CASE(a_motor_run_opens_in_the_plot_with_its_presets_columns)
{
    char got[128];
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_MOTOR);
    CHECK_STR_EQ(series(got, sizeof(got)), "throttle,rpm,current,voltage");
    draw();
}

TEST_CASE(a_limit_run_opens_in_the_plot_with_its_pulse_first)
{
    char got[128];
    fresh_bench();
    open_named("LIMIT007.CSV");
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_LIMIT);
    CHECK_STR_EQ(series(got, sizeof(got)), "pulse,current,angle");
    draw();
}

TEST_CASE(a_foreign_csv_opens_in_the_import_view)
{
    fresh_bench();
    open_named("BENCH_01.CSV");
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_NONE);
    CHECK(log_viewer_data() == NULL);
}

TEST_CASE(a_run_name_over_columns_no_preset_knows_opens_in_the_import_view)
{
    fresh_bench();
    open_named("RUDDR003.CSV");
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_RUDDR_SEARCH);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_data() == NULL);
}

TEST_CASE(a_file_that_vanished_opens_in_the_import_view_with_the_reason)
{
    fresh_bench();
    open_named("GONE.CSV");
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_NONE);
    CHECK(log_viewer_analysis() == NULL);
}

TEST_CASE(back_from_a_plot_opened_from_the_list_returns_to_the_list)
{
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    REQUIRE(log_viewer_view() == LOG_VIEW_PLOT);
    tap(LV_PV_BACK_CX, FOOT_PLOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);

    /* The home tag climbs the same way, and leaves the screen from the
     * list. */
    open_named("MOTOR042_kv1200.CSV");
    REQUIRE(log_viewer_view() == LOG_VIEW_PLOT);
    CHECK(screen()->back());
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(!screen()->back());
}

TEST_CASE(more_columns_reaches_the_import_view_and_plot_comes_back)
{
    char got[128];
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    tap(LV_PV_MORE_CX, FOOT_PLOT_CY);
    draw();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    tap(LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    draw();
    /* The import view shows the preset's columns picked, so PLOT draws
     * what the plot showed. */
    tap(PLOT_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK_STR_EQ(series(got, sizeof(got)), "throttle,rpm,current,voltage");
    /* Opened by PLOT, BACK and the home tag return to the import view. */
    tap(LV_PV_BACK_CX, FOOT_PLOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tap(PLOT_X, IM_BTN_CY);
    CHECK(screen()->back());
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    /* Opened from the list again, BACK goes to the list. */
    open_named("MOTOR042_kv1200.CSV");
    tap(LV_PV_BACK_CX, FOOT_PLOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
}

TEST_CASE(a_press_beside_mores_list_closes_it_and_presses_nothing)
{
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    tap(LV_PV_MORE_CX, FOOT_PLOT_CY);
    /* On BACK: the list closes, and the release does not go back. */
    tap(LV_PV_BACK_CX, FOOT_PLOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    /* The list is closed: its row's place is the plot again. */
    tap(LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    /* A press on the alert band leaves the list open. */
    tap(LV_PV_MORE_CX, FOOT_PLOT_CY);
    tap(LV_PV_BACK_CX, UI_BODY_H - UI_ALERT_H + 10);
    tap(LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
}

TEST_CASE(a_press_that_slides_off_columns_opens_nothing)
{
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    tap(LV_PV_MORE_CX, FOOT_PLOT_CY);
    send(TOUCH_EVENT_DOWN, LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    draw();
    send(TOUCH_EVENT_MOVE, LV_MORE_ROW_CX, LV_MORE_ROW_CY - 120);
    send(TOUCH_EVENT_UP, LV_MORE_ROW_CX, LV_MORE_ROW_CY - 120);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    /* A second finger on the row while the first holds a press. */
    send_id(TOUCH_EVENT_DOWN, 1, 700, 380);
    send_id(TOUCH_EVENT_DOWN, 2, LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    send_id(TOUCH_EVENT_UP, 2, LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    send_id(TOUCH_EVENT_UP, 1, 700, 380);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
}

TEST_CASE(the_home_tag_closes_mores_list_first)
{
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    tap(LV_PV_MORE_CX, FOOT_PLOT_CY);
    CHECK(screen()->back());
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    tap(LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK(screen()->back());
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
}

TEST_CASE(the_way_back_in_closes_mores_list)
{
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    tap(LV_PV_MORE_CX, FOOT_PLOT_CY);
    screen()->enter();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    tap(LV_MORE_ROW_CX, LV_MORE_ROW_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
}

TEST_CASE(files_open_takes_the_same_route)
{
    char got[128];
    fresh_bench();
    CHECK_EQ(log_viewer_show(LOG_TAB_BENCH, "LIMIT007.CSV", g_mounts),
             LOG_SHOW_OPEN);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK_STR_EQ(series(got, sizeof(got)), "pulse,current,angle");
    CHECK_EQ(log_viewer_show(LOG_TAB_BENCH, "BENCH_01.CSV", g_mounts),
             LOG_SHOW_OPEN);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
}

TEST_CASE(a_foreign_file_after_a_run_keeps_no_preset)
{
    fresh_bench();
    open_named("MOTOR042_kv1200.CSV");
    tap(LV_PV_BACK_CX, FOOT_PLOT_CY);
    open_named("PLAIN.CSV");
    CHECK_EQ(log_viewer_preset(), LOG_PRESET_NONE);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
}

int main(void)
{
    RUN(a_motor_run_opens_in_the_plot_with_its_presets_columns);
    RUN(a_limit_run_opens_in_the_plot_with_its_pulse_first);
    RUN(a_foreign_csv_opens_in_the_import_view);
    RUN(a_run_name_over_columns_no_preset_knows_opens_in_the_import_view);
    RUN(a_file_that_vanished_opens_in_the_import_view_with_the_reason);
    RUN(back_from_a_plot_opened_from_the_list_returns_to_the_list);
    RUN(more_columns_reaches_the_import_view_and_plot_comes_back);
    RUN(a_press_beside_mores_list_closes_it_and_presses_nothing);
    RUN(a_press_that_slides_off_columns_opens_nothing);
    RUN(the_home_tag_closes_mores_list_first);
    RUN(the_way_back_in_closes_mores_list);
    RUN(files_open_takes_the_same_route);
    RUN(a_foreign_file_after_a_run_keeps_no_preset);
    return test_summary("logview_preset");
}
