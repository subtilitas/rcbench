/**
 * @file test_help_logs.c
 * @brief LOGS' screen guide: two pages on the file list, two on the import
 * view, one on the plot, none over the DELETE question; every page fits in
 * both languages and themes, SAFE and ARMED.
 *
 * Cases:
 * - every_page_of_logs_fits
 * - the_list_marks_paging_and_the_bar_only_where_they_show
 * - the_import_view_and_the_plot_mark_their_controls
 * - the_delete_question_leaves_no_page
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

#include "help_check.h"
#include "ui_help.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The file list of the rig's card. */
static void pose_list(void)
{
    ui_router_init();
    fresh();
}

/** @brief The file list on the first page of a card of 200 runs. */
static void pose_paged(void)
{
    ui_router_init();
    runs_card(200);
}

/** @brief The file list of a card with nothing on it. */
static void pose_empty(void)
{
    ui_router_init();
    fresh();
    g_empty_card = true;
    log_viewer_refresh();
}

/** @brief BENCH_01.CSV in the import view. */
static void pose_import(void)
{
    pose_list();
    open_file(0);
}

/** @brief BENCH_01.CSV plotted. */
static void pose_plot(void)
{
    pose_import();
    tap(PLOT_X, IM_BTN_CY);
}

/**
 * @brief Whether @p p marks an item with @p text.
 * @param p    The page.
 * @param text The text.
 * @return True when one of its items has it.
 */
static bool marks(const ui_help_page_t *p, ui_text_id_t text)
{
    for (int i = 0; i < p->count; ++i) {
        if (p->item[i].text == text) {
            return true;
        }
    }
    return false;
}

TEST_CASE(every_page_of_logs_fits)
{
    CHECK_EQ(check_every_state(screen(), pose_list, "list"), 2);
    CHECK_EQ(check_every_state(screen(), pose_paged, "paged"), 2);
    CHECK_EQ(check_every_state(screen(), pose_empty, "empty"), 2);
    CHECK_EQ(check_every_state(screen(), pose_import, "import"), 2);
    CHECK_EQ(check_every_state(screen(), pose_plot, "plot"), 1);
}

TEST_CASE(the_list_marks_paging_and_the_bar_only_where_they_show)
{
    ui_help_page_t p;
    pose_list();
    REQUIRE(ui_help_page(screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_LOG_TITLE_LIST);
    CHECK(marks(&p, TX_HG_LOG_ROW));
    REQUIRE(ui_help_page(screen(), false, 1, &p));
    CHECK(!marks(&p, TX_HG_LOG_PAGES));
    CHECK(marks(&p, TX_HG_LOG_BAR));
    CHECK_STR_EQ(p.item[0].label, "FILES");
    pose_paged();
    REQUIRE(ui_help_page(screen(), false, 1, &p));
    CHECK(marks(&p, TX_HG_LOG_PAGES));
    CHECK_EQ(p.count, 5);
    pose_empty();
    REQUIRE(ui_help_page(screen(), false, 0, &p));
    CHECK(!marks(&p, TX_HG_LOG_ROW));
    REQUIRE(ui_help_page(screen(), false, 1, &p));
    CHECK(!marks(&p, TX_HG_LOG_BAR));
}

TEST_CASE(the_import_view_and_the_plot_mark_their_controls)
{
    ui_help_page_t p;
    pose_import();
    REQUIRE(log_viewer_view() == LOG_VIEW_IMPORT);
    REQUIRE(ui_help_page(screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_LOG_TITLE_READ);
    CHECK_EQ(p.count, 3);
    REQUIRE(ui_help_page(screen(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_LOG_TITLE_COLS);
    CHECK(marks(&p, TX_HG_LOG_BACK_LIST));
    CHECK(marks(&p, TX_HG_LOG_PLOT));
    pose_plot();
    REQUIRE(log_viewer_view() == LOG_VIEW_PLOT);
    REQUIRE(ui_help_page(screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_LOG_TITLE_PLOT);
    CHECK_EQ(p.count, 4);
    CHECK(marks(&p, TX_HG_LOG_CHIP));
    CHECK(marks(&p, TX_HG_LOG_BACK_IMPORT));
    CHECK(!ui_help_page(screen(), false, 1, &p));
}

TEST_CASE(the_delete_question_leaves_no_page)
{
    pose_list();
    ask_to_delete(0);
    REQUIRE(screen()->covered());
    CHECK_EQ(ui_help_count(screen(), false), 0);
    ui_help_page_t p;
    CHECK(!ui_help_page(screen(), false, 0, &p));
}

int main(void)
{
    RUN(every_page_of_logs_fits);
    RUN(the_list_marks_paging_and_the_bar_only_where_they_show);
    RUN(the_import_view_and_the_plot_mark_their_controls);
    RUN(the_delete_question_leaves_no_page);
    return test_summary("help_logs");
}
