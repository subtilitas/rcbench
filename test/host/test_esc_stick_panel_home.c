/*
 * ESC STICK's two-line strings and the home tag: it climbs a page at a
 * time, leaves the stick class one level at a time and ends a run.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* The pages the controls are on.  Closed: PROGRAM's list shows, and the
 * session is not open. */
static void at_closed(void)    { fresh(); }

/*
 * A string the screen breaks onto two lines is held, in ui_text.def, to the
 * cells of both: the note left of RUN (2 x 73), a parameter's help (2 x
 * 96), and the prompt's and the warning's two lines (2 x 92).  Each has to
 * break into its two lines as the screen breaks it, in every language.
 * SUPPLY OFF NOT CONFIRMED is one line of 93 on the result and two of 69
 * on MANUAL INTERVENTION REQUIRED.
 */
TEST_CASE(every_two_line_string_breaks_into_two_lines)
{
    static const struct {
        const char *prefix;
        int cells, line;
    } k[] = {
        { "SP_", 146, 73 },      { "ESC_WHY_", 146, 73 },
        { "PG_HELP_", 192, 96 }, { "SP_", 184, 92 },
    };
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        for (int i = 0; i < TX_COUNT; ++i) {
            const char *name = ui_text_name((ui_text_id_t)i);
            const char *s = ui_tr_in((ui_lang_t)l, (ui_text_id_t)i);
            for (size_t j = 0; j < sizeof(k) / sizeof(k[0]); ++j) {
                if (strncmp(name, k[j].prefix, strlen(k[j].prefix)) == 0
                    && ui_text_cells((ui_text_id_t)i) == k[j].cells
                    && esc_stick_panel_wrap_lines(s, k[j].line) > 2) {
                    T_FAIL("%s in language %d takes %d lines of %d", name,
                           l, esc_stick_panel_wrap_lines(s, k[j].line),
                           k[j].line);
                }
            }
        }
        CHECK(esc_stick_panel_wrap_lines(
                  ui_tr_in((ui_lang_t)l, TX_SP_HAND_OVER_LIVE), 69) <= 2);
    }
}

/* A line of no cells, or fewer, still holds one: every text ends. */
TEST_CASE(a_wrap_into_no_cells_still_ends)
{
    CHECK_EQ(esc_stick_panel_wrap_lines("abc", 0), 3);
    CHECK_EQ(esc_stick_panel_wrap_lines("abc", -4), 3);
    CHECK_EQ(esc_stick_panel_wrap_lines("a b", 0), 2);
    CHECK_EQ(esc_stick_panel_wrap_lines("", 0), 0);
    CHECK_EQ(esc_stick_panel_wrap_lines("abc", 1), 3);
}

/* ------------------------------------------------- the home tag */

/* The makers with the search's keyboard open. */
static void at_typing(void)    { at_makers(); tap(FIND_X, FIND_Y); }

/*
 * The home tag climbs one level, and on each page it does what that page's
 * own way back does: BACK, CANCEL over the RUN warning, OK over the manual
 * steps.  The screen after back() looks as the screen after the tap, its
 * settings and a save asked included.  On the classes back() climbs
 * nothing and leaves the screen to the router.
 */
TEST_CASE(the_home_tag_climbs_as_each_pages_way_back)
{
    static const control_t k_ways[] = {
        { "makers",       at_makers,    BACK_X, BACK_Y },
        { "models",       at_models,    BACK_X, BACK_Y },
        { "searched",     at_searched,  BACK_X, BACK_Y },
        { "typing",       at_typing,    BACK_X, BACK_Y },
        { "items",        at_items,     BACK_X, BACK_Y },
        { "picked",       at_picked,    BACK_X, BACK_Y },
        { "timing",       at_timing,    BACK_X, BACK_Y },
        { "result",       at_result,    BACK_X, BACK_Y },
        { "warning",      at_warning,   CANCEL_X, HOLD_Y },
        { "manual steps", at_steps,     CANCEL_X, HOLD_Y },
    };
    for (size_t i = 0; i < sizeof(k_ways) / sizeof(k_ways[0]); ++i) {
        const control_t *w = &k_ways[i];
        w->page();
        const uint32_t before = look();
        tap(w->x, w->y);
        const uint32_t want = look();
        if (want == before) {
            T_FAIL("%s: the way back changed nothing", w->name);
        }
        w->page();
        if (!scr->back()) {
            T_FAIL("%s: the home tag left the screen", w->name);
        }
        if (look() != want) {
            T_FAIL("%s: the home tag did not do what the way back does",
                   w->name);
        }
    }
    at_closed();
    const uint32_t was = look();
    CHECK(!scr->back());
    CHECK_EQ(look(), was);
}

/*
 * From a profile's page the home tag takes four steps to leave: the list
 * of the maker's models, the makers, the classes, then the router.  TIMING
 * adds one, and a save asked for what it changed.
 */
TEST_CASE(the_home_tag_leaves_the_stick_class_one_level_at_a_time)
{
    at_items();
    CHECK(esc_stick_panel_page() != NULL);
    CHECK(scr->back());
    CHECK(esc_stick_panel_page() == NULL);
    CHECK_EQ(esc_stick_panel_level(), 1);
    CHECK(scr->back());
    CHECK_EQ(esc_stick_panel_level(), 0);
    CHECK(scr->back());
    CHECK(!scr->back());

    at_timing();
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());
    CHECK(scr->back());
    CHECK(settings_save_asked());
    CHECK(esc_stick_panel_page() != NULL);
    CHECK(scr->back());
    CHECK(esc_stick_panel_page() == NULL);
    settings_cancel_save();
}

/*
 * A run under way is a level: the home tag ends it as ABORT does, the
 * motor and the supply off, and the result stays on the page.  The next
 * tag goes on to the list.  The same over a manual step's prompt.
 */
TEST_CASE(the_home_tag_ends_a_run_and_the_result_stays)
{
    fresh();
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    static rig_t r;
    rig_start(&r);
    for (int i = 0; i < 12000; ++i) {
        rig_step(&r);
    }
    CHECK(r.on && r.armed);
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    CHECK(scr->back());
    rig_run(&r, 1000u);
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK_EQ(run->phase, ESC_STICK_ABORTED);
    CHECK_EQ(run->reason, ESC_STICK_R_USER);
    CHECK(!r.armed);
    CHECK(!r.on);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    CHECK(esc_stick_panel_page() != NULL);
    CHECK(scr->back());
    CHECK(esc_stick_panel_page() == NULL);
    CHECK_EQ(esc_stick_panel_level(), 1);

    fresh();
    descend_to_jazz();
    tap(CANCEL_X, HOLD_Y);
    for (int i = 0; i < 3; ++i) {
        tap(STEP_UP_X, STEP_CY(0));
    }
    supply_reads_off();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
    rig_start(&r);
    run = esc_stick_panel_stick();
    for (uint32_t i = 0; i < 60000u && !run->hand_menu; ++i) {
        rig_step(&r);
    }
    CHECK(run->hand_menu);
    CHECK(scr->back());
    rig_run(&r, 1000u);
    CHECK_EQ(run->phase, ESC_STICK_ABORTED);
    CHECK_EQ(run->reason, ESC_STICK_R_USER);
    CHECK(!r.armed);
    CHECK(!r.on);
    CHECK(esc_stick_panel_page() != NULL);
}

int main(void)
{
    RUN(the_home_tag_climbs_as_each_pages_way_back);
    RUN(the_home_tag_leaves_the_stick_class_one_level_at_a_time);
    RUN(the_home_tag_ends_a_run_and_the_result_stays);
    RUN(a_wrap_into_no_cells_still_ends);
    RUN(every_two_line_string_breaks_into_two_lines);
    return test_summary("esc_stick_panel_home");
}
