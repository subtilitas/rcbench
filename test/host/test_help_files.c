/**
 * @file test_help_files.c
 * @brief FILES' screen guide: three pages over the panes, none over a box or
 * the text viewer; every page fits in both languages and themes, SAFE and
 * ARMED, and the footer's eight buttons are each marked once.
 *
 * Cases:
 * - every_page_of_files_fits
 * - the_pages_mark_every_footer_button_once
 * - a_box_or_the_viewer_leaves_no_page
 *
 * SPDX-License-Identifier: MIT
 */

#include "files_rig.h"

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

/** @brief The panes over the rig's card. */
static void pose_panes(void) { fresh(); }

/** @brief The panes over a slot without a card. */
static void pose_nocard(void)
{
    fresh_bare();
    g_h.card.in = false;
    screen()->enter();
    settle();
}

TEST_CASE(every_page_of_files_fits)
{
    CHECK_EQ(check_every_state(screen(), pose_panes, "panes"), 3);
    CHECK_EQ(check_every_state(screen(), pose_nocard, "no card"), 3);
}

TEST_CASE(the_pages_mark_every_footer_button_once)
{
    pose_panes();
    int marked[8] = { 0 };
    for (int n = 0; n < 3; ++n) {
        ui_help_page_t p;
        REQUIRE(ui_help_page(screen(), false, n, &p));
        for (int i = 0; i < p.count; ++i) {
            for (int b = 0; b < 8; ++b) {
                const gfx_rect_t r = p.item[i].rect;
                marked[b] += r.y == FS_SPOT_FOOT_Y
                             && gfx_rect_contains(r, FS_SPOT_FOOT_CX(b),
                                                  FS_SPOT_FOOT_CY);
            }
        }
    }
    for (int b = 0; b < 8; ++b) {
        if (marked[b] != 1) {
            T_FAIL("footer button %d marked %d times", b, marked[b]);
        }
    }
    /* The row is marked while the left pane lists one, not without. */
    ui_help_page_t p;
    REQUIRE(ui_help_page(screen(), false, 0, &p));
    CHECK_EQ(p.count, 4);
    pose_nocard();
    REQUIRE(ui_help_page(screen(), false, 0, &p));
    CHECK_EQ(p.count, 3);
}

TEST_CASE(a_box_or_the_viewer_leaves_no_page)
{
    pose_panes();
    REQUIRE(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_INFO);
    settle();
    REQUIRE(files_screen_view() == FILES_VIEW_INFO);
    CHECK_EQ(ui_help_count(screen(), false), 0);
    pose_panes();
    REQUIRE(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_OPEN);
    settle();
    REQUIRE(files_screen_view() == FILES_VIEW_TEXT);
    CHECK_EQ(ui_help_count(screen(), false), 0);
    ui_help_page_t p;
    CHECK(!ui_help_page(screen(), false, 0, &p));
}

int main(void)
{
    RUN(every_page_of_files_fits);
    RUN(the_pages_mark_every_footer_button_once);
    RUN(a_box_or_the_viewer_leaves_no_page);
    fh_free(&g_h);
    return test_summary("help_files");
}
