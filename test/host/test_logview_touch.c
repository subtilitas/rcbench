/**
 * @file test_logview_touch.c
 * @brief Host unit tests for the log viewer's touch rules and its text.
 *
 * Tested is that a button or a row acts on a tap and on nothing less, what a
 * second finger presses, and that no message needs more than two lines.
 *
 * Cases:
 * - the_home_tag_climbs_as_each_views_way_back
 * - a_button_or_a_row_acts_on_a_tap_and_on_nothing_less
 * - a_second_finger_presses_nothing_on_the_list_views
 * - a_press_left_behind_holds_nothing_on_the_way_back_in
 * - nothing_that_acts_is_under_the_alert_band
 * - every_warning_and_file_message_takes_two_lines_at_most
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

/* ------------------------------------------------- taps, tracker-fed */

/*
 * The cases below put finger frames through the tracker (touch_feed.h) and
 * hand its events to the screen.  A button or a row is a tap: pressed and
 * released in place it acts, and a finger that leaves it, crosses it or
 * travels past 8px does nothing.
 */
#define TAP_ID 3

static void f_at(int x, int y)    { finger(TAP_ID, x, y + UI_BAND_H); }

static void f_to(int x, int y, int step)
{
    glide(TAP_ID, x, y + UI_BAND_H, step);
}

static void f_lift(void)          { lift(TAP_ID); }

/* Everything the screen shows and what it has asked of the card, as one
 * number. */
static uint32_t look(void)
{
    uint32_t h = 2166136261u;
    log_viewer_invalidate();
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    screen()->render(&s_c, 0);
    for (int i = 0; i < W * H; ++i) {
        h = (h ^ (uint32_t)s_fb[i]) * 16777619u;
    }
    const uint32_t more[] = {
        (uint32_t)log_viewer_view(), (uint32_t)g_remove_calls,
        (uint32_t)g_list_calls, (uint32_t)log_viewer_cursor(),
    };
    for (size_t i = 0; i < sizeof(more) / sizeof(more[0]); ++i) {
        h = (h ^ more[i]) * 16777619u;
    }
    return h;
}

static void at_list(void)     { fresh(); tap(400, BR_ROW_Y(1)); }

static void at_import(void)   { fresh(); open_file(0); }

static void at_plot(void)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    tap(PV_TAP_X, 200);
}

static void at_question(void) { fresh(); ask_to_delete(0); }

typedef struct {
    const char *name;
    void (*page)(void);
    int x, y;
} control_t;

static const control_t k_controls[] = {
    { "RESCAN",          at_list,     RESCAN_X, FOOT_CY },
    { "OPEN",            at_list,     OPEN_X, FOOT_CY },
    { "DELETE",          at_list,     DELETE_X, FOOT_CY },
    { "file row",        at_list,     LV_ROW_CX, BR_ROW_Y(3) },
    { "file row 7",      at_list,     LV_ROW_CX, BR_ROW_Y(BR_ROWS_SEEN - 1) },
    { "separator",       at_import,   SEP_X, IM_BTN_CY },
    { "number format",   at_import,   NUM_X, IM_BTN_CY },
    { "BACK",            at_import,   BACK_X, IM_BTN_CY },
    { "PLOT",            at_import,   PLOT_X, IM_BTN_CY },
    { "column row",      at_import,   LV_IM_ROW_CX, IM_ROW_Y(2) },
    { "FIELDS",          at_plot,     LV_PV_BACK_CX, FOOT_PLOT_CY },
    { "previous sample", at_plot,     LV_PV_PREV_CX, FOOT_PLOT_CY },
    { "next sample",     at_plot,     NEXT_X, FOOT_PLOT_CY },
    { "question CANCEL", at_question, DQ_CANCEL_X, DQ_CY },
    { "question DELETE", at_question, DQ_DELETE_X, DQ_CY },
};

#define CONTROLS ((int)(sizeof(k_controls) / sizeof(k_controls[0])))

/* The defect: the footers acted on the release wherever the finger was, so
 * a press on DELETE that slid away still asked the question. */
TEST_CASE(a_button_or_a_row_acts_on_a_tap_and_on_nothing_less)
{
    for (int i = 0; i < CONTROLS; ++i) {
        const control_t *c = &k_controls[i];
        c->page();
        feed_reset();
        feed_to_screen(screen());
        const uint32_t was = look();
        int moved = 0;

        /* Pressed, carried away down the glass or across it, and lifted. */
        f_at(c->x, c->y);
        f_to(c->x, c->y + 60, 9);
        f_lift();
        moved += (look() != was) ? 1 : 0;
        f_at(c->x, c->y);
        f_to(c->x - 110, c->y, 9);
        f_lift();
        moved += (look() != was) ? 2 : 0;
        /* A swipe across it from 110px to its left. */
        f_at(c->x - 110, c->y);
        f_to(c->x + 110, c->y, 9);
        f_lift();
        moved += (look() != was) ? 4 : 0;
        /* Pressed, carried 60px away and back onto it. */
        f_at(c->x, c->y);
        f_to(c->x + 60, c->y, 9);
        f_to(c->x, c->y, 9);
        f_lift();
        moved += (look() != was) ? 8 : 0;
        /* 9px from the press, in one report and px by px. */
        f_at(c->x, c->y);
        f_to(c->x + 9, c->y, 9);
        f_lift();
        f_at(c->x, c->y);
        f_to(c->x - 9, c->y, 1);
        f_lift();
        moved += (look() != was) ? 16 : 0;
        /* The contact next reported 300px away, in the header row right
         * of the tabs (x 368 on), where nothing acts: the tracker's
         * release. */
        f_at(c->x, c->y);
        f_at((c->x < 500) ? c->x + 300 : c->x - 300, 20);
        f_lift();
        moved += (look() != was) ? 32 : 0;
        if (moved != 0) {
            T_FAIL("%s acted without a tap (%d)", c->name, moved);
        }

        /* A tap acts; so does one that moves 8px, or wobbles. */
        for (int k = 0; k < 3; ++k) {
            c->page();
            feed_reset();
            feed_to_screen(screen());
            const uint32_t before = look();
            f_at(c->x, c->y);
            if (k == 1) {
                f_to(c->x - 8, c->y, 8);
            } else if (k == 2) {
                f_to(c->x + 2, c->y - 3, 1);
                f_to(c->x, c->y, 1);
            }
            f_lift();
            if (look() == before) {
                T_FAIL("%s did not act on a tap (%d)", c->name, k);
            }
        }
    }
}

/* One contact holds a press on the list views; a second one presses
 * nothing and ends nothing. */
TEST_CASE(a_second_finger_presses_nothing_on_the_list_views)
{
    at_list();
    feed_reset();
    feed_to_screen(screen());
    /* The first rests on a row; the second taps OPEN and DELETE. */
    finger(1, 400, BR_ROW_Y(3) + UI_BAND_H);
    finger(2, OPEN_X, FOOT_CY + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    finger(2, DELETE_X, FOOT_CY + UI_BAND_H);
    lift(2);
    lift(1);                                  /* selects its row: README */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    feed_tap(2, DELETE_X, FOOT_CY + UI_BAND_H);
    feed_tap(2, DQ_DELETE_X, DQ_CY + UI_BAND_H);
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "MIXED.CSV");

    /* The first holds OPEN; the second's release ends nothing. */
    at_list();
    feed_reset();
    feed_to_screen(screen());
    finger(1, OPEN_X, FOOT_CY + UI_BAND_H);
    finger(2, 400, 200 + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    lift(1);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);

    /* On the import view the same. */
    finger(1, 560, IM_ROW_Y(2) + UI_BAND_H);
    finger(2, BACK_X, IM_BTN_CY + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    lift(1);
    feed_tap(2, BACK_X, IM_BTN_CY + UI_BAND_H);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);

    /* On the plot, a finger on a footer button keeps its press while
     * another comes and goes. */
    at_plot();
    feed_reset();
    feed_to_screen(screen());
    const int c0 = log_viewer_cursor();
    finger(1, NEXT_X, FOOT_PLOT_CY + UI_BAND_H);
    finger(2, 100, FOOT_PLOT_CY + UI_BAND_H);
    lift(2);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    CHECK_EQ(log_viewer_cursor(), c0);
    lift(1);
    CHECK_EQ(log_viewer_cursor(), c0 + 1);
}

/* A press held when the screen is left has no release coming; it holds
 * nothing when the screen is entered again.  The way in chooses the newest
 * run, which OPEN then opens. */
TEST_CASE(a_press_left_behind_holds_nothing_on_the_way_back_in)
{
    runs_card(3);
    feed_reset();
    feed_to_screen(screen());
    finger(1, OPEN_X, FOOT_CY + UI_BAND_H);
    screen()->enter();
    feed_tap(2, OPEN_X, FOOT_CY + UI_BAND_H);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "BENCH003.CSV");
}

/*
 * The router's alert band covers the bottom UI_ALERT_H px of the panel
 * while an alert shows, and takes the touches there.  Nothing of the log
 * viewer that acts lies under it: with no alert showing, on each view and
 * on the DELETE question, a tap changes nothing and goes nowhere on every
 * 16th px of the band's top row, middle row and bottom row (body y 496, 518
 * and 539).  A control that reaches into the band from above crosses the
 * top row; one inside it at least 16px wide and 22px high crosses a row
 * and a column of the grid.  960 taps, each followed by a full frame of
 * 1024 x 600: a finer grid pushes gfx.c's fill loop past the 2^32 hits
 * gcovr accepts.
 */
static void at_nocard(void)
{
    fresh();
    g_no_card = true;
    log_viewer_refresh();
}

TEST_CASE(nothing_that_acts_is_under_the_alert_band)
{
    static const struct {
        const char *name;
        void (*page)(void);
    } pages[] = {
        { "file list", at_list },     { "no card", at_nocard },
        { "import", at_import },      { "plot", at_plot },
        { "question", at_question },
    };
    ui_router_init();
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        pages[i].page();
        const uint32_t was = look();
        const uint32_t nav = ui_router_navigations();
        const int top = UI_ALERT_Y - UI_BAND_H;
        const int rows[] = { top, top + UI_ALERT_H / 2, top + UI_ALERT_H - 1 };
        int acted_at = -1;
        for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]) && acted_at < 0;
             ++r) {
            const int y = rows[r];
            for (int x = 0; x < W; x += 16) {
                tap(x, y);
                if (ui_router_navigations() != nav || look() != was) {
                    acted_at = y;
                    break;
                }
            }
        }
        if (acted_at >= 0) {
            T_FAIL("%s: a tap at body y %d acted", pages[i].name, acted_at);
        }
    }
}

/*
 * The import's warnings break into lines of DETECTED's 368px, 36 cells of
 * the 10 x 20 face, and its file messages into the footer's 400px between
 * WI-FI and DELETE, 33 cells of the 12 x 24 face: ui_text.def declares two
 * lines of 33 cells, 66 cells, for each warning, and this holds each to the
 * two lines as the screen breaks it, the longest count and file name
 * included, in every language.
 */
TEST_CASE(every_warning_and_file_message_takes_two_lines_at_most)
{
    static const ui_text_id_t k_warn[] = {
        TX_LOG_MIXED, TX_LOG_ROWS_DIFFER, TX_LOG_LAST_CUT, TX_LOG_UNSETTLED,
    };
    static const ui_text_id_t k_file[] = {
        TX_LOG_CANNOT_OPEN, TX_LOG_IS_FOLDER, TX_LOG_CANNOT_DELETE,
        TX_LOG_DELETED, TX_LOG_TOO_BIG,
    };
    const gfx_rect_t warn = gfx_rect_make(24, 0, LV_WARN_W, LV_WARN_LH);
    const gfx_rect_t foot = gfx_rect_make(LV_MSG_X, 0, LV_MSG_W, LV_MSG_LH);
    char line[96];
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(k_warn) / sizeof(k_warn[0]); ++i) {
            CHECK(ui_text_wrap(NULL, warn, LV_WARN_LH, ui_tr(k_warn[i]),
                               &gfx_font_10x20, UI_TEXT) <= 2);
        }
        snprintf(line, sizeof(line), ui_tr(TX_LOG_TOO_LONG), 2147483647);
        CHECK(ui_text_wrap(NULL, warn, LV_WARN_LH, line, &gfx_font_10x20,
                           UI_TEXT) <= 2);
        for (size_t i = 0; i < sizeof(k_file) / sizeof(k_file[0]); ++i) {
            /* A run's name, as the logger writes it. */
            snprintf(line, sizeof(line), ui_tr(k_file[i]), "BENCH999.CSV");
            CHECK(ui_text_wrap(NULL, foot, LV_MSG_LH, line, UI_FONT_BODY,
                               UI_TEXT) <= 2);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The home tag climbs one level, as each view's own way back: BACK from
 * the plot to the import, BACK from the import to the file list, CANCEL
 * over the DELETE question.  The view after back() looks as the view after the
 * tap.  On the file list back() climbs nothing: the router leaves the
 * screen.
 */
TEST_CASE(the_home_tag_climbs_as_each_views_way_back)
{
    static const control_t k_ways[] = {
        { "plot",     at_plot,     100, FOOT_PLOT_CY },
        { "import",   at_import,   BACK_X, IM_BTN_CY },
        { "question", at_question, DQ_CANCEL_X, DQ_CY },
    };
    for (size_t i = 0; i < sizeof(k_ways) / sizeof(k_ways[0]); ++i) {
        const control_t *w = &k_ways[i];
        g_list_calls = 0;                   /* each build lists the card */
        w->page();
        const uint32_t before = look();
        tap(w->x, w->y);
        const uint32_t want = look();
        if (want == before) {
            T_FAIL("%s: the way back changed nothing", w->name);
        }
        g_list_calls = 0;
        w->page();
        if (!screen()->back()) {
            T_FAIL("%s: the home tag left the screen", w->name);
        }
        if (look() != want) {
            T_FAIL("%s: the home tag did not do what the way back does",
                   w->name);
        }
    }
    CHECK_EQ(g_remove_calls, 0);

    at_list();
    const uint32_t was = look();
    CHECK(!screen()->back());
    CHECK_EQ(look(), was);

    /* Through the router: plot, import, list, then the menu. */
    ui_router_init();
    at_plot();
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_PLOT);
    const log_viewer_view_t k_climb[] = { LOG_VIEW_IMPORT, LOG_VIEW_BROWSE };
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_LOGS));
    for (size_t i = 0; i < 2; ++i) {
        touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 4, .x = (int16_t)(home.x + 20),
                                       .y = (int16_t)(home.y + 10) } };
        ui_router_event(&e);
        e.type = TOUCH_EVENT_UP;
        ui_router_event(&e);
        CHECK_EQ(ui_router_current(), SCREEN_LOGS);
        CHECK_EQ(log_viewer_view(), k_climb[i]);
    }
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 4, .x = (int16_t)(home.x + 20),
                                   .y = (int16_t)(home.y + 10) } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

int main(void)
{
    RUN(the_home_tag_climbs_as_each_views_way_back);
    RUN(a_button_or_a_row_acts_on_a_tap_and_on_nothing_less);
    RUN(a_second_finger_presses_nothing_on_the_list_views);
    RUN(a_press_left_behind_holds_nothing_on_the_way_back_in);
    RUN(nothing_that_acts_is_under_the_alert_band);
    RUN(every_warning_and_file_message_takes_two_lines_at_most);
    return test_summary("logview_touch");
}
