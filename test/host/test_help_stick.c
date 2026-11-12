/**
 * @file test_help_stick.c
 * @brief ESC STICK's screen guide: two pages on the makers and on a maker's
 * models, two on a profile, the OPTIONS list's two over it, none over the
 * keyboard, the RUN warning, the manual steps, a run or its result; every
 * page fits in both languages and themes, SAFE and ARMED.
 *
 * The session runs behind the rig's screen (esc_stick_panel_rig.h) with
 * the guide's hooks added, as MOTOR & ESC hands them on.
 *
 * Cases:
 * - every_page_of_esc_stick_fits
 * - the_list_marks_back_a_row_search_and_the_count
 * - a_profile_marks_its_note_and_run_and_its_manual_steps
 * - what_covers_the_session_takes_every_page
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

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

/** @brief The rig's screen with the guide's hooks and the session's
 *         safe_at, as MOTOR & ESC offers them over the session. */
static ui_screen_t s_guided;

/**
 * @brief The guided screen, built from the rig's on first use.
 * @return The screen.
 */
static const ui_screen_t *guided(void)
{
    s_guided = k_rig_screen;
    s_guided.help_count = esc_stick_help_count;
    s_guided.help_page = esc_stick_help_page;
    s_guided.safe_at = esc_stick_pages.safe_at;
    return &s_guided;
}

/** @brief The makers. */
static void pose_makers(void) { at_makers(); }
/** @brief Hobbywing's models. */
static void pose_models(void) { at_models(); }
/** @brief A search applied, the keyboard closed. */
static void pose_searched(void) { at_searched(); }
/** @brief Hobbywing's profile, nothing picked. */
static void pose_items(void) { at_items(); }
/** @brief Hobbywing's profile, Cutoff mode picked: RUN live. */
static void pose_picked(void) { at_picked(); }
/** @brief Kontronik Jazz's profile, with manual steps, their pop-up
 *         closed. */
static void pose_steps(void)
{
    at_steps();
    if (esc_stick_panel_hand_shown()) {
        (void)scr->back();
    }
}
/** @brief The OPTIONS list over a profile. */
static void pose_timing(void) { at_timing(); }

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

TEST_CASE(every_page_of_esc_stick_fits)
{
    const ui_screen_t *g = guided();
    CHECK_EQ(check_every_state(g, pose_makers, "makers"), 2);
    CHECK_EQ(check_every_state(g, pose_models, "models"), 2);
    CHECK_EQ(check_every_state(g, pose_searched, "searched"), 2);
    CHECK_EQ(check_every_state(g, pose_items, "items"), 2);
    CHECK_EQ(check_every_state(g, pose_picked, "picked"), 2);
    CHECK_EQ(check_every_state(g, pose_steps, "steps"), 2);
    CHECK_EQ(check_every_state(g, pose_timing, "timing"), 2);
}

TEST_CASE(the_list_marks_back_a_row_search_and_the_count)
{
    const ui_screen_t *g = guided();
    ui_help_page_t p;
    pose_makers();
    REQUIRE(ui_help_page(g, false, 0, &p));
    CHECK_EQ(p.title, TX_HG_ES_TITLE_LIST);
    CHECK(marks(&p, TX_HG_ES_BACK));
    CHECK(marks(&p, TX_HG_ES_MAKER));
    REQUIRE(ui_help_page(g, false, 1, &p));
    CHECK_EQ(p.title, TX_HG_ES_TITLE_FIND);
    CHECK_EQ(p.count, 3);
    CHECK(marks(&p, TX_HG_ES_FIND));
    CHECK(marks(&p, TX_HG_ES_PAGES));
    CHECK(marks(&p, TX_HG_ES_COUNT));
    /* The list drives nothing: both pages while ARMED. */
    CHECK_EQ(ui_help_count(g, true), 2);
    pose_models();
    REQUIRE(esc_stick_panel_level() == 1);
    REQUIRE(ui_help_page(g, false, 0, &p));
    CHECK(marks(&p, TX_HG_ES_MODEL));
    CHECK(!marks(&p, TX_HG_ES_MAKER));
}

TEST_CASE(a_profile_marks_its_note_and_run_and_its_manual_steps)
{
    const ui_screen_t *g = guided();
    ui_help_page_t p;
    pose_items();
    REQUIRE(esc_stick_panel_stage() == 2);
    REQUIRE(ui_help_page(g, false, 0, &p));
    CHECK_EQ(p.title, TX_HG_ES_TITLE_ITEMS);
    CHECK(marks(&p, TX_HG_ES_ITEM));
    CHECK(marks(&p, TX_HG_ES_NOTE));
    REQUIRE(ui_help_page(g, false, 1, &p));
    CHECK_EQ(p.title, TX_HG_ES_TITLE_RUN);
    CHECK(marks(&p, TX_HG_ES_RUN));
    CHECK(marks(&p, TX_HG_ES_OPTIONS));
    CHECK(!marks(&p, TX_HG_ES_HAND));
    /* RUN's slot turns into ABORT: no page while ARMED. */
    CHECK_EQ(ui_help_count(g, true), 0);
    pose_steps();
    REQUIRE(!esc_stick_panel_hand_shown());
    REQUIRE(ui_help_page(g, false, 1, &p));
    CHECK(marks(&p, TX_HG_ES_HAND));
    CHECK_EQ(p.count, 5);
}

TEST_CASE(what_covers_the_session_takes_every_page)
{
    const ui_screen_t *g = guided();
    /* The search's keyboard. */
    at_makers();
    tap(FIND_X, FIND_Y);
    REQUIRE(esc_stick_panel_typing());
    CHECK_EQ(ui_help_count(g, false), 0);
    /* The RUN warning. */
    at_warning();
    CHECK_EQ(ui_help_count(g, false), 0);
    /* The manual steps' pop-up, open by itself on a profile's first
     * opening. */
    at_steps();
    if (esc_stick_panel_hand_shown()) {
        CHECK_EQ(ui_help_count(g, false), 0);
    } else {
        tap(HAND_X, HAND_Y);
        REQUIRE(esc_stick_panel_hand_shown());
        CHECK_EQ(ui_help_count(g, false), 0);
    }
    /* A run's result, until OK. */
    at_result();
    CHECK_EQ(ui_help_count(g, false), 0);
    ui_help_page_t p;
    CHECK(!ui_help_page(g, false, 0, &p));
    tap(WRITE_X, BTN_CY);                    /* OK: the profile again */
    CHECK_EQ(ui_help_count(g, false), 2);
}

int main(void)
{
    RUN(every_page_of_esc_stick_fits);
    RUN(the_list_marks_back_a_row_search_and_the_count);
    RUN(a_profile_marks_its_note_and_run_and_its_manual_steps);
    RUN(what_covers_the_session_takes_every_page);
    return test_summary("help_stick");
}
