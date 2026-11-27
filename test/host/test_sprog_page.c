/*
 * The servo programmers' frame's page builder (sprog_page.c): the lines and
 * their room, the groups that fold into "and n more", the fit at the 28px
 * pitch and the parts of a page too long for one, on the fake brand of
 * sprog_rig.h.
 *
 * On SERVO's left card the lines start at y 132 and end 8px above the
 * buttons at y 432: 14 rows of 20px, or a line of one row and 8px each at
 * the widest pitch.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sprog_rig.h"

/** @brief A text of @p rows rows: 10 words of 5 letters a row. */
static const char *rows_of(int rows)
{
    static char text[4][400];
    static int which;
    char *t = text[which = (which + 1) % 4];
    int n = 0;

    for (int i = 0; i < rows * 10 && n < 390; ++i) {
        n += snprintf(t + n, sizeof(text[0]) - (size_t)n, "%saaaaa",
                      i > 0 ? " " : "");
    }
    return t;
}

/** @brief Opens a plain page on the rig. */
static void page(void)
{
    sprog_page_open(&rig.f, 1, false, "CHECK THIS CHANGE", "HOLD 2s TO APPLY",
                    "CANCEL", TX_HOLD_HINT_APPLY, false);
}

/* The text's rows at 60 cells: words wrap, a word longer than the line is
 * cut, NULL and "" take none. */
TEST_CASE(a_text_wraps_at_the_areas_cells)
{
    char word[80];

    rig_start();
    CHECK_EQ(sprog_text_rows(&rig.f, NULL), 0);
    CHECK_EQ(sprog_text_rows(&rig.f, ""), 0);
    CHECK_EQ(sprog_text_rows(&rig.f, "Short."), 1);
    CHECK_EQ(sprog_text_rows(&rig.f, rows_of(1)), 1);
    CHECK_EQ(sprog_text_rows(&rig.f, rows_of(3)), 3);
    memset(word, 'w', 70);
    word[70] = '\0';
    CHECK_EQ(sprog_text_rows(&rig.f, word), 2);
    /* A line break starts a row; the spaces after it are dropped. */
    CHECK_EQ(sprog_text_rows(&rig.f, "Writing was interrupted.\n  Tap READ ALL."),
             2);
    CHECK_EQ(sprog_text_rows(&rig.f, "One.\n"), 1);
}

/* The lines, the rows they take and the room left; a page holds
 * SPROG_PAGE_LINES lines; a new page keeps the flash of the last one's
 * hold and starts empty. */
TEST_CASE(a_page_holds_its_lines_and_counts_its_room)
{
    rig_start();
    page();
    CHECK(rig.f.page.open);
    CHECK_STR_EQ(rig.f.page.cancel, "CANCEL");
    CHECK_EQ(rig.f.page.hint, TX_HOLD_HINT_APPLY);
    CHECK_EQ(sprog_page_room(&rig.f), 14);
    CHECK(sprog_page_line(&rig.f, rows_of(2), SPROG_LV_WARN));
    CHECK_EQ(sprog_page_rows(&rig.f), 2);
    CHECK_EQ(sprog_page_room(&rig.f), 12);
    CHECK_EQ(rig.f.page.level[0], SPROG_LV_WARN);
    for (int i = 1; i < SPROG_PAGE_LINES; ++i) {
        CHECK(sprog_page_line(&rig.f, "x", SPROG_LV_TEXT));
    }
    CHECK(!sprog_page_line(&rig.f, "x", SPROG_LV_TEXT));
    CHECK_EQ(rig.f.page.count, SPROG_PAGE_LINES);
    /* A flash under way runs on into the next page. */
    rig.f.page.hold.flash_ms = 200;
    page();
    CHECK_EQ(rig.f.page.count, 0);
    CHECK_EQ(rig.f.page.hold.flash_ms, 200);
    CHECK(!rig.f.page.hold.down);
}

/* A group by lines: an item that would leave no line for the fold line
 * is not written and one line counts the rest; later items fold too. */
TEST_CASE(a_group_folds_by_its_lines)
{
    rig_start();
    page();
    (void)sprog_page_line(&rig.f, "SPEED: 50% -> 100%", SPROG_LV_TEXT);
    sprog_page_group(&rig.f, 10, 4, SPROG_ALL, SPROG_FOLD_VALUES,
                     SPROG_LV_DIM);
    CHECK(sprog_page_item(&rig.f, "one", SPROG_LV_TEXT));
    CHECK(sprog_page_item(&rig.f, "two", SPROG_LV_TEXT));
    CHECK(sprog_page_item(&rig.f, "three", SPROG_LV_WARN));
    CHECK(!sprog_page_item(&rig.f, "four", SPROG_LV_TEXT));
    CHECK(!sprog_page_item(&rig.f, "five", SPROG_LV_TEXT));
    CHECK_EQ(rig.f.page.count, 5);
    CHECK_STR_EQ(rig.f.page.line[3], "three");
    CHECK_STR_EQ(rig.f.page.line[4], "and 7 more");
    CHECK_EQ(rig.f.page.level[4], SPROG_LV_DIM);
    /* All the items fit: the last needs no line after it. */
    page();
    sprog_page_group(&rig.f, 3, 3, SPROG_ALL, SPROG_FOLD_VALUES,
                     SPROG_LV_TEXT);
    for (int i = 0; i < 3; ++i) {
        CHECK(sprog_page_item(&rig.f, "item", SPROG_LV_TEXT));
    }
    CHECK_EQ(rig.f.page.count, 3);
    /* No group open: an item is not written. */
    page();
    CHECK(!sprog_page_item(&rig.f, "item", SPROG_LV_TEXT));
    CHECK_EQ(rig.f.page.count, 0);
}

/* A group by rows: a tall last item folds into the line for one; the
 * group's caps are no more than the page has left. */
TEST_CASE(a_group_folds_by_its_rows_and_the_page_left)
{
    rig_start();
    page();
    sprog_page_group(&rig.f, 3, SPROG_ALL, 4, SPROG_FOLD_WARNINGS,
                     SPROG_LV_WARN);
    CHECK(sprog_page_item(&rig.f, "one", SPROG_LV_WARN));
    CHECK(sprog_page_item(&rig.f, "two", SPROG_LV_WARN));
    CHECK(!sprog_page_item(&rig.f, rows_of(3), SPROG_LV_WARN));
    CHECK_STR_EQ(rig.f.page.line[2], "And 1 more warning.");
    CHECK_EQ(rig.f.page.group.lines, 3);
    /* SPROG_ALL takes what the page has left: 12 lines less those used. */
    page();
    for (int i = 0; i < 9; ++i) {
        (void)sprog_page_line(&rig.f, "x", SPROG_LV_TEXT);
    }
    sprog_page_group(&rig.f, 5, SPROG_ALL, SPROG_ALL, SPROG_FOLD_WARNINGS,
                     SPROG_LV_TEXT);
    CHECK_EQ(rig.f.page.group.cap, 3);
    CHECK_EQ(rig.f.page.group.cap_rows, 5);
    CHECK(sprog_page_item(&rig.f, "a", SPROG_LV_TEXT));
    CHECK(sprog_page_item(&rig.f, "b", SPROG_LV_TEXT));
    CHECK(!sprog_page_item(&rig.f, "c", SPROG_LV_TEXT));
    /* Warnings, more than one left. */
    CHECK_STR_EQ(rig.f.page.line[11], "And 3 more warnings.");
}

/* The fit: a page whose lines end under its buttons at 28px gives up its
 * group's last line, the line before it counting the rest, until it fits. */
TEST_CASE(the_fit_folds_the_last_group_until_the_page_fits)
{
    rig_start();
    page();
    (void)sprog_page_line(&rig.f, "Backup of the first settings.",
                          SPROG_LV_TEXT);
    sprog_page_group(&rig.f, 9, SPROG_ALL, SPROG_ALL, SPROG_FOLD_VALUES,
                     SPROG_LV_TEXT);
    int written = 0;
    for (int i = 0; i < 9; ++i) {
        written += sprog_page_item(&rig.f, rows_of(2), SPROG_LV_TEXT) ? 1 : 0;
    }
    /* 13 rows for the group: 6 items of 2 rows and the fold line. */
    CHECK_EQ(written, 6);
    CHECK_EQ(rig.f.page.count, 8);
    CHECK_STR_EQ(rig.f.page.line[7], "and 3 more");
    /* 14 rows and 7 gaps of 8px end at y 468, under 424: one item goes. */
    CHECK(sprog_page_fit(&rig.f));
    CHECK_EQ(rig.f.page.count, 7);
    CHECK_STR_EQ(rig.f.page.line[6], "and 4 more");
    CHECK_EQ(sprog_page_rows(&rig.f), 12);
    /* A page that fits is left as it is. */
    CHECK(sprog_page_fit(&rig.f));
    CHECK_EQ(rig.f.page.count, 7);
    /* A group folded to nothing: the count goes as well, and a page that
     * still does not fit says so. */
    page();
    for (int i = 0; i < 7; ++i) {
        (void)sprog_page_line(&rig.f, rows_of(2), SPROG_LV_TEXT);
    }
    sprog_page_group(&rig.f, 1, SPROG_ALL, SPROG_ALL, SPROG_FOLD_VALUES,
                     SPROG_LV_TEXT);
    CHECK(!sprog_page_item(&rig.f, rows_of(1), SPROG_LV_TEXT));
    CHECK_EQ(rig.f.page.count, 8);
    CHECK(!sprog_page_fit(&rig.f));
    CHECK_EQ(rig.f.page.count, 7);
    CHECK_EQ(rig.f.page.group.lines, 0);
}

/* The parts of a page too long for one: the first part from line 0, a
 * later one keeps line 0 and the lines that fit; each returns where the
 * next starts, the last the page's end. */
TEST_CASE(a_long_page_takes_parts)
{
    rig_start();
    int from = 0, parts = 0;
    int starts[4] = { 0 };

    while (from < SPROG_PAGE_LINES && parts < 4) {
        page();
        for (int i = 0; i < SPROG_PAGE_LINES; ++i) {
            char line[120];
            int n = snprintf(line, sizeof(line), "L%02d", i);

            for (int w = 0; w < 15; ++w) {
                n += snprintf(line + n, sizeof(line) - (size_t)n, " aaaaa");
            }
            CHECK_EQ(sprog_text_rows(&rig.f, line), 2);
            (void)sprog_page_line(&rig.f, line, SPROG_LV_TEXT);
        }
        starts[parts++] = from;
        from = sprog_page_part(&rig.f, from);
        CHECK_EQ(strncmp(rig.f.page.line[0], "L00", 3), 0);
    }
    /* 2 rows a line: 6 lines at 28px fit in 292px. */
    CHECK_EQ(parts, 3);
    CHECK_EQ(starts[1], 6);
    CHECK_EQ(starts[2], 11);
    CHECK_EQ(from, SPROG_PAGE_LINES);
    CHECK_EQ(rig.f.page.count, 2);
    CHECK_EQ(strncmp(rig.f.page.line[1], "L11", 3), 0);
    CHECK_EQ(sprog_page_part(&rig.f, SPROG_PAGE_LINES), 2);
}

/* The notes' pages in rooms of 1, 2 and 3 rows: every row of the room
 * holds a note, the last one beside the counter. */
TEST_CASE(the_notes_take_every_row_of_their_room)
{
    static const int pages[3] = { 5, 3, 2 };

    for (int room = 1; room <= 3; ++room) {
        sprog_notes_shown_t sh;

        rig_start();
        for (int i = 0; i < 5; ++i) {
            rig.note[i] = "One row.";
        }
        rig.notes = 5;
        rig.bottom = rows_of(6 - room);  /* 132px less 20px a row */
        rig_view();
        CHECK_EQ(sprog_note_pages(&rig.f), pages[room - 1]);
        sprog_notes_shown(&rig.f, 0, &sh);
        CHECK_EQ(sh.notes, room);
        CHECK_EQ(sh.end - sh.top, room * 20);
    }
}

int main(void)
{
    RUN(a_text_wraps_at_the_areas_cells);
    RUN(a_page_holds_its_lines_and_counts_its_room);
    RUN(a_group_folds_by_its_lines);
    RUN(a_group_folds_by_its_rows_and_the_page_left);
    RUN(the_fit_folds_the_last_group_until_the_page_fits);
    RUN(a_long_page_takes_parts);
    RUN(the_notes_take_every_row_of_their_room);
    return test_summary("sprog_page");
}
