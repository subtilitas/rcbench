/**
 * @file test_logview_layout.c
 * @brief LOGS at the 1024 x 600 sizes: the file list shows seven rows on a
 *        48px pitch, each row a finger tall from its top pixel to its
 *        bottom one; the scroll bar reaches the last row of a card of 20
 *        runs; no text or control of any view reaches the alert band, in
 *        English or German, with the longest warnings and messages showing.
 *        A report opens in the text viewer, test_files_view.c's.
 *
 * Cases:
 * - a_card_of_20_runs_shows_six_rows_a_finger_tall
 * - the_list_draws_seven_rows_and_the_gaps_between_them
 * - an_empty_folder_and_a_full_page_keep_to_the_list
 * - nothing_is_drawn_under_the_alert_band
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

/** @brief The body row 4px over the alert band: nothing reaches it. */
#define TEXT_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)

/** @brief The top pixel row of the list's row @p r, its pitch's first. */
#define ROW_TOP(r) (LV_LIST_Y + 30 + (r) * LV_ROW_PITCH)

/*
 * 20 runs, the newest at the top: row r is run 20 - r.  A press on the
 * first pixel row of a row's pitch selects it and one on the pitch's last
 * row, 47px lower, opens it: the hit area is the whole 48px.
 */
TEST_CASE(a_card_of_20_runs_shows_six_rows_a_finger_tall)
{
    CHECK_EQ(LV_ROW_PITCH, UI_TAP_MIN);
    for (int r = 0; r < LV_ROWS; ++r) {
        runs_card(20);
        tap(LV_ROW_CX, ROW_TOP(r));
        tap(LV_ROW_CX, ROW_TOP(r) + LV_ROW_PITCH - 1);
        CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
        CHECK_EQ(log_run_number(log_viewer_open_name()), 20 - r);
    }
    /* The bar's foot brings the last six runs up: the top row is run 6,
     * the bottom one run 1. */
    runs_card(20);
    tap(LV_BAR_CX, LV_BAR_TOP + LV_BAR_H - 2);
    CHECK_EQ(top_row_run(), LV_ROWS);
    tap(LV_ROW_CX, LV_ROW_CY(LV_ROWS - 1));
    tap(LV_ROW_CX, LV_ROW_CY(LV_ROWS - 1));
    CHECK_EQ(log_run_number(log_viewer_open_name()), 1);
}

/*
 * Each of the seven rows is drawn 44px tall in the sunk fill, the
 * selected one raised, with 4px of the list's own fill under it.
 */
TEST_CASE(the_list_draws_seven_rows_and_the_gaps_between_them)
{
    runs_card(20);
    draw();
    const int x = LV_LIST_X + 8 + 16;
    for (int r = 0; r < LV_ROWS; ++r) {
        const gfx_color_t fill = (r == 0) ? UI_PANEL_HI : UI_PANEL_SUNK;
        CHECK_EQ(gfx_pixel_get(&s_c, x, ROW_TOP(r) + 1), fill);
        CHECK_EQ(gfx_pixel_get(&s_c, x, ROW_TOP(r) + 42), fill);
        CHECK_EQ(gfx_pixel_get(&s_c, x, ROW_TOP(r) + 45), UI_PANEL);
    }
    /* No eighth row: under the seventh the list's fill runs to its end. */
    CHECK_EQ(gfx_pixel_get(&s_c, x, ROW_TOP(LV_ROWS) + 1), UI_PANEL);
}

/*
 * A full page of 48 runs and an empty folder: the rows and the box that
 * says why the list is empty stay inside the list, above the footer.
 */
TEST_CASE(an_empty_folder_and_a_full_page_keep_to_the_list)
{
    runs_card(LOG_VIEWER_MAX_FILES);
    tap(LV_BAR_CX, LV_BAR_TOP + LV_BAR_H - 2);
    CHECK_EQ(top_row_run(), LV_ROWS);
    fresh();
    g_empty_card = true;
    log_viewer_refresh();
    draw();
    /* The box's two lines in the body face: something drawn on each. */
    int why = 0;
    int hint = 0;
    for (int x = LV_EMPTY_X0; x < LV_EMPTY_X1; ++x) {
        for (int y = 0; y < 24; ++y) {
            why += gfx_pixel_get(&s_c, x, LV_EMPTY_WHY_Y + y) == UI_TEXT;
            hint += gfx_pixel_get(&s_c, x, LV_EMPTY_HINT_Y + y)
                    == UI_TEXT_FAINT;
        }
    }
    CHECK(why > 0);
    CHECK(hint > 0);
    g_empty_card = false;
}

/** @brief The views the floor check draws, each from a fresh screen. */
static void at_list(void)
{
    runs_card(20);
}

static void at_message(void)
{
    /* A refused delete: the message about the file in the footer, on two
     * lines where one is too short. */
    fresh();
    g_remove_refuses = true;
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    g_remove_refuses = false;
}

static void at_nocard(void)
{
    fresh();
    g_no_card = true;
    log_viewer_refresh();
}

static void at_question(void)
{
    fresh();
    ask_to_delete(0);
}

static void at_warnings(void)
{
    /* Mixed conventions and a cut last row: the two warnings at once. */
    fresh();
    snprintf(g_long, sizeof(g_long), "%s",
             "t;a;b\n0;10.23;1\n1;10,23;2\n2;11.5;3\n3;12.5");
    g_removed[0] = g_removed[1] = g_removed[2] = true;
    log_viewer_refresh();
    open_file(5);
}

static void at_plot(void)
{
    /* No time column and a cell that does not read: the footer's two
     * lines. */
    fresh();
    open_file(4);
    tap(PLOT_X, IM_BTN_CY);
}

/*
 * The router's alert band covers body y 496 to 539 while an alert shows.
 * Nothing of LOGS reaches 4px over it: every pixel from body y 492 down
 * is the background, on every view, in English and in German.
 */
TEST_CASE(nothing_is_drawn_under_the_alert_band)
{
    static const struct {
        const char *name;
        void (*page)(void);
    } pages[] = {
        { "list", at_list },       { "message", at_message },
        { "no card", at_nocard },  { "question", at_question },
        { "warnings", at_warnings }, { "plot", at_plot },
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
            pages[i].page();
            log_viewer_invalidate();
            draw();
            int at = -1;
            for (int y = TEXT_FLOOR; y < UI_BODY_H && at < 0; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (gfx_pixel_get(&s_c, x, y) != UI_BG) {
                        at = y;
                        break;
                    }
                }
            }
            if (at >= 0) {
                T_FAIL("%s, language %d: drawn at body y %d", pages[i].name,
                       lang, at);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    g_removed[0] = g_removed[1] = g_removed[2] = false;
    g_long[0] = '\0';
}

int main(void)
{
    RUN(a_card_of_20_runs_shows_six_rows_a_finger_tall);
    RUN(the_list_draws_seven_rows_and_the_gaps_between_them);
    RUN(an_empty_folder_and_a_full_page_keep_to_the_list);
    RUN(nothing_is_drawn_under_the_alert_band);
    return test_summary("logview_layout");
}
