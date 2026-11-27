/*
 * The servo programmers' frame (sprog_draw.c): the flow of the rows, the
 * head, the notes and their pager, the lists of 2 to 7 entries, the files
 * list, the rows' controls and the hold page, on the fake brand of
 * sprog_rig.h.
 *
 * On SERVO's left card, 620x428 at x 8, y 68: rows at y 132 + 56 x n,
 * column 0 at x 18 and column 1 at x 323, 295px wide; the notes from 8px
 * under the last line of rows; controls at the foot end at y 488.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sprog_rig.h"

/** @brief The pixels of @p r in colour @p c. */
static int count(gfx_rect_t r, gfx_color_t c)
{
    int n = 0;

    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            n += rig_px(x, y) == c ? 1 : 0;
        }
    }
    return n;
}

/** @brief Whether @p a and @p b are the same rectangle. */
static bool same(gfx_rect_t a, int x, int y, int w, int h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

/* The SERVO tab's flow: the head on line 0, READ ALL beside the brand
 * slot, FILES beside MORE, RESTORE beside WRITE n VALUES of 220px. */
TEST_CASE(the_servo_tab_flows_in_table_order)
{
    rig_start();
    CHECK(same(sprog_row_rect(&rig.f, R_HEAD), 18, 132, 600, 48));
    CHECK(same(sprog_value_rect(&rig.f, R_HEAD), 358, 132, 260, 48));
    CHECK(same(sprog_head_rect(&rig.f), 18, 132, 330, 48));
    CHECK(same(sprog_row_rect(&rig.f, R_READ), 18, 188, 295, 48));
    CHECK(same(sprog_value_rect(&rig.f, R_READ), 163, 188, 150, 48));
    CHECK(same(sprog_row_rect(&rig.f, R_TYPE), 323, 188, 295, 48));
    CHECK(same(sprog_row_rect(&rig.f, R_FILES), 18, 244, 295, 48));
    CHECK(same(sprog_row_rect(&rig.f, R_MORE), 323, 244, 295, 48));
    CHECK(same(sprog_row_rect(&rig.f, R_RESTORE), 18, 300, 295, 48));
    CHECK(same(sprog_value_rect(&rig.f, R_WRITE), 398, 300, 220, 48));
    CHECK_EQ(rig.f.lines, 4);
    CHECK_EQ(rig.f.cells, 7);
    /* Rows of the other tab place on it; its cells are not the view's. */
    CHECK(same(sprog_row_rect(&rig.f, R_DEAD), 18, 132, 600, 48));
    CHECK(sprog_cell(&rig.f, R_DEAD) == NULL);
    CHECK(sprog_cell(&rig.f, R_READ) != NULL);
    /* No slot: the rows close up. */
    rig.mask = M_TUNE;
    rig_view();
    CHECK_EQ(sprog_row_rect(&rig.f, R_TYPE).w, 0);
    CHECK(sprog_cell(&rig.f, R_TYPE) == NULL);
    CHECK(same(sprog_row_rect(&rig.f, R_FILES), 323, 188, 295, 48));
    CHECK(same(sprog_row_rect(&rig.f, R_MORE), 18, 244, 295, 48));
    CHECK(same(sprog_row_rect(&rig.f, R_WRITE), 18, 300, 295, 48));
    /* Out of range. */
    CHECK_EQ(sprog_row_rect(&rig.f, -1).w, 0);
    CHECK_EQ(sprog_row_rect(&rig.f, R_COUNT).w, 0);
    CHECK_EQ(sprog_step_rect(&rig.f, R_COUNT, true).w, 0);
    CHECK_EQ(sprog_pair_rect(&rig.f, R_COUNT, 0).w, 0);
    CHECK_EQ(sprog_value_rect(&rig.f, R_COUNT).w, 0);
}

/* A value tab: the step's keys of 56px around its value, the value of a
 * narrow row 150px; a view that turns the value into a step moves it. */
TEST_CASE(a_value_tab_flows_and_its_step_has_keys)
{
    rig_start();
    sprog_tab_set(&rig.f, 1);
    rig_view();
    CHECK_EQ(rig.f.lines, 2);
    CHECK(same(sprog_step_rect(&rig.f, R_DEAD, false), 358, 132, 56, 48));
    CHECK(same(sprog_step_rect(&rig.f, R_DEAD, true), 562, 132, 56, 48));
    CHECK(same(sprog_value_rect(&rig.f, R_DEAD), 418, 132, 140, 48));
    CHECK(same(sprog_value_rect(&rig.f, R_SPEED), 163, 188, 150, 48));
    rig_cell(R_SPEED, SPROG_K_STEP, SPROG_P_TAP);
    rig_view();
    CHECK(same(sprog_value_rect(&rig.f, R_SPEED), 223, 188, 30, 48));
    CHECK(same(sprog_pair_rect(&rig.f, R_DEAD, 0), 358, 132, 126, 48));
    CHECK(same(sprog_pair_rect(&rig.f, R_DEAD, 1), 492, 132, 126, 48));
}

/* The flow rule on a table of its own: a wide row after a lone narrow one
 * starts the next line; rows past the grid's fifth line are not placed,
 * and a tab takes SPROG_CELLS cells at most.  Every tab shows without
 * show bits. */
TEST_CASE(the_flow_rule_closes_up_and_stops_at_the_grid)
{
    static const ui_text_id_t tabs[] = { TX_KP_TAB_SERVO };
    static sprog_row_t rows[13];
    sprog_def_t def = { tabs, NULL, 1, rows, 13, TX_KP_EDIT_NOTE_1 };
    sprog_t *f = &rig.f;

    rig_start();
    for (int i = 0; i < 13; ++i) {
        rows[i] = (sprog_row_t){ 0, 0, SPROG_K_VALUE, (uint8_t)i, 0, 0, -1,
                                 "V" };
    }
    rows[1].wide = 1;           /* narrow, WIDE, narrow ... */
    sprog_init(f, &def, gfx_rect_make(8, 68, 620, 428));
    CHECK_EQ(f->tabs.count, 1);
    CHECK(same(sprog_row_rect(f, 0), 18, 132, 295, 48));
    CHECK(same(sprog_row_rect(f, 1), 18, 188, 600, 48));
    CHECK(same(sprog_row_rect(f, 2), 18, 244, 295, 48));
    CHECK(same(sprog_row_rect(f, 3), 323, 244, 295, 48));
    CHECK(same(sprog_row_rect(f, 7), 323, 356, 295, 48));
    CHECK_EQ(sprog_row_rect(f, 8).w, 0);
    CHECK_EQ(f->cells, 8);
    CHECK_EQ(f->lines, 5);
    rows[1].wide = 0;
    sprog_view_begin(f, 0u);
    CHECK_EQ(f->cells, SPROG_CELLS);
    CHECK_EQ(sprog_row_rect(f, 10).w, 0);
}

/* The head: its two lines left of the 260px button, the first in warn
 * while no port is bound. */
TEST_CASE(the_head_draws_its_lines_and_button)
{
    rig_start();
    const gfx_rect_t t = sprog_head_rect(&rig.f);
    const gfx_rect_t line1 = gfx_rect_make(t.x, t.y + 4, t.w, 20);
    const gfx_rect_t line2 = gfx_rect_make(t.x, t.y + 24, t.w, 20);

    rig_render();
    CHECK(count(line1, UI_TEXT) > 50);
    CHECK(count(line2, UI_TEXT_DIM) > 50);
    CHECK_EQ(count(line1, UI_WARN), 0);
    CHECK(count(sprog_value_rect(&rig.f, R_HEAD), UI_OK) > 5000);
    rig.head_warn = true;
    rig.head[1] = NULL;
    rig_render();
    CHECK(count(line1, UI_WARN) > 50);
    CHECK_EQ(count(line2, UI_TEXT_DIM), 0);
}

/* The notes flow from 8px under the last line of rows, the bottom line
 * ends at the foot, each in its level's colour. */
TEST_CASE(the_notes_flow_from_the_top_and_the_bottom_line_ends_at_the_foot)
{
    rig_start();
    rig.note[0] = "Port 1 is in programming mode. PWM is off.";
    rig.notes = 1;
    rig.bottom = "All 32 registers read.";
    rig_render();
    CHECK(count(gfx_rect_make(18, 356, 600, 20), UI_TEXT) > 50);
    CHECK_EQ(count(gfx_rect_make(18, 376, 600, 20), UI_TEXT), 0);
    CHECK(count(gfx_rect_make(18, 468, 600, 20), UI_TEXT) > 50);
    CHECK_EQ(sprog_note_pages(&rig.f), 1);
    CHECK_EQ(sprog_notes_rect(&rig.f).w, 0);
    /* The levels' colours, the hint dim, in 5 rows: one page. */
    rig_view();
    sprog_note(&rig.f, "accent", SPROG_LV_ACCENT);
    sprog_note(&rig.f, "warn", SPROG_LV_WARN);
    sprog_note(&rig.f, "danger", SPROG_LV_DANGER);
    sprog_note(&rig.f, "faint", SPROG_LV_FAINT);
    rig.f.view.bottom_level = SPROG_LV_DANGER;
    sprog_render(&rig.f, &rig.cv);
    CHECK_EQ(sprog_note_pages(&rig.f), 1);
    CHECK(count(gfx_rect_make(18, 376, 600, 20), UI_ACCENT) > 10);
    CHECK(count(gfx_rect_make(18, 396, 600, 20), UI_WARN) > 10);
    CHECK(count(gfx_rect_make(18, 416, 600, 20), UI_DANGER) > 10);
    CHECK(count(gfx_rect_make(18, 436, 600, 20), UI_TEXT_FAINT) > 10);
    CHECK(count(gfx_rect_make(18, 468, 600, 20), UI_DANGER) > 50);
    rig_view();
    sprog_hint(&rig.f);
    sprog_render(&rig.f, &rig.cv);
    CHECK(count(gfx_rect_make(18, 376, 600, 20), UI_TEXT_DIM) > 50);
    CHECK_EQ(rig.f.view.notes, 2);
    /* No more than SPROG_NOTES; NULL is no note. */
    for (int i = 0; i < 10; ++i) {
        sprog_note(&rig.f, "x", SPROG_LV_TEXT);
    }
    sprog_note(&rig.f, NULL, SPROG_LV_TEXT);
    CHECK_EQ(rig.f.view.notes, SPROG_NOTES);
}

/** @brief A note of 3 rows: 130 characters at 60 cells a row. */
static const char k_three[] =
    "Settings from before the interrupted write are available under "
    "RESTORE, with the values read before it and the file of the session.";

/** @brief A text of @p rows rows: 10 words of 5 letters a row. */
static const char *rows_of(int rows)
{
    static char text[2][700];
    static int which;
    char *t = text[which ^= 1];
    int n = 0;

    for (int i = 0; i < rows * 10 && n < 690; ++i) {
        n += snprintf(t + n, sizeof(text[0]) - (size_t)n, "%saaaaa",
                      i > 0 ? " " : "");
    }
    return t;
}

/* Notes that need more rows than there are page: the pager right above
 * the bottom line, a tap on the notes turns the page and wraps, a new set
 * of notes starts on page 1, and a room of one row shows no note. */
TEST_CASE(notes_that_need_more_rows_page)
{
    rig_start();
    CHECK_EQ(sprog_text_rows(&rig.f, k_three), 3);
    rig.note[0] = k_three;
    rig.note[1] = k_three;
    rig.notes = 2;
    rig.bottom = "All 32 registers read.";
    rig_view();
    /* 5 rows from y 356 to the bottom line at 468: 6 rows need 2 pages. */
    CHECK_EQ(sprog_note_pages(&rig.f), 2);
    const gfx_rect_t n = sprog_notes_rect(&rig.f);
    CHECK(same(n, 8, 356, 620, 100));
    rig_render();
    /* The pager "1/2" faint at the right of the row above the bottom line. */
    CHECK(count(gfx_rect_make(568, 448, 50, 20), UI_TEXT_FAINT) > 10);
    CHECK(count(gfx_rect_make(18, 356, 600, 60), UI_TEXT) > 200);
    CHECK_EQ(rig.f.note_page, 0);
    CHECK_EQ(rig_tap(n).kind, SPROG_R_NONE);
    CHECK_EQ(rig.f.note_page, 1);
    rig_render();
    CHECK_EQ(rig.f.note_page, 1);
    CHECK_EQ(rig_tap(n).kind, SPROG_R_NONE);
    CHECK_EQ(rig.f.note_page, 0);
    (void)rig_tap(n);
    /* Released off the notes: no turn. */
    (void)rig_down(n, 1);
    (void)rig_ev(TOUCH_EVENT_UP, 300, 140, 1);
    CHECK_EQ(rig.f.note_page, 1);
    /* Another set of notes starts on page 1. */
    rig.note[1] = "One row.";
    rig.note[2] = k_three;
    rig.notes = 3;
    rig_render();
    CHECK_EQ(rig.f.note_page, 0);
    /* A bottom line of 5 rows leaves a room of 1 row: no note shows. */
    rig.note[0] = k_three;
    rig.notes = 1;
    rig.bottom = rows_of(5);
    rig_render();
    CHECK_EQ(sprog_note_pages(&rig.f), 1);
    CHECK_EQ(sprog_notes_rect(&rig.f).w, 0);
    CHECK_EQ(count(gfx_rect_make(18, 356, 600, 32), UI_TEXT), 0);
    CHECK(count(gfx_rect_make(18, 388, 600, 20), UI_TEXT) > 50);
    /* A bottom line taller than the room under the rows shows its first
     * rows from 8px under them, and none past the foot. */
    rig.notes = 0;
    rig.bottom = rows_of(7);
    rig_render();
    CHECK(count(gfx_rect_make(18, 356, 600, 20), UI_TEXT) > 50);
    CHECK(count(gfx_rect_make(18, 456, 600, 20), UI_TEXT) > 50);
    CHECK_EQ(count(gfx_rect_make(18, 476, 600, 12), UI_TEXT), 0);
}

/* A list of 2 to 5 entries stands one to a line under the title, of 6 or
 * 7 two to a line, a label wider than a column on a line of its own; the
 * last entry ends 8px above CANCEL or higher. */
TEST_CASE(lists_of_2_to_7_entries_take_their_lines)
{
    static const char *labels[] = { "x1", "x2", "x4", "x8", "x16", "x32",
                                    "x64" };
    rig_start();
    const gfx_rect_t cancel = sprog_list_cancel_rect(&rig.f);

    CHECK(same(cancel, 468, 440, 150, 48));
    for (int n = 2; n <= 7; ++n) {
        sprog_list_open(&rig.f, 1, "STRETCH", false);
        for (int i = 0; i < n; ++i) {
            CHECK(sprog_list_add(&rig.f, labels[i], NULL, i));
        }
        for (int i = 0; i < n; ++i) {
            const gfx_rect_t r = sprog_list_rect(&rig.f, i);

            if (n <= 5) {
                CHECK(same(r, 18, 112 + 56 * i, 600, 48));
            } else {
                CHECK(same(r, i % 2 == 0 ? 18 : 323, 112 + 56 * (i / 2), 295,
                           48));
            }
            CHECK(r.y + r.h <= cancel.y - 8);
        }
        CHECK_EQ(sprog_list_rect(&rig.f, n).w, 0);
        CHECK_EQ(sprog_list_rect(&rig.f, -1).w, 0);
    }
    CHECK(!sprog_list_add(&rig.f, "x128", NULL, 7));
    CHECK_EQ(rig.f.list.count, SPROG_LIST_MAX);
    /* Drawn: the title, the current entry in the accent, CANCEL. */
    sprog_list_current(&rig.f, 3);
    rig_render();
    const gfx_rect_t cur = sprog_list_rect(&rig.f, 3);
    CHECK_EQ(rig_px(cur.x + 12, cur.y + 30), UI_ACCENT);
    const gfx_rect_t other = sprog_list_rect(&rig.f, 2);
    CHECK(rig_px(other.x + 12, other.y + 30) != UI_ACCENT);
    CHECK(count(gfx_rect_make(18, 82, 200, 20), UI_TEXT) > 20);
}

/* A files list: SAVE on a line of its own, five files two to a line, each
 * name padded to 16 cells before its type, "Show more files." in the last
 * cell, all of it clear of CANCEL. */
TEST_CASE(a_files_list_takes_two_columns_clear_of_cancel)
{
    rig_start();
    sprog_list_open(&rig.f, 2, "FILES 1 TO 5 OF 7", true);
    (void)sprog_list_add(&rig.f, "Save this servo's settings in a new file.",
                         NULL, -1);
    (void)sprog_list_add(&rig.f, "HAND", NULL, 0);
    (void)sprog_list_add(&rig.f, "KST005", "V6", 1);
    (void)sprog_list_add(&rig.f, "KST004", "V8", 2);
    (void)sprog_list_add(&rig.f, "KST003", "V6", 3);
    (void)sprog_list_add(&rig.f, "KST002", "V6", 4);
    (void)sprog_list_add(&rig.f, "Show more files.", NULL, -2);
    CHECK_STR_EQ(rig.f.list.label[1], "HAND");
    CHECK_STR_EQ(rig.f.list.label[2], "KST005           V6");
    CHECK(same(sprog_list_rect(&rig.f, 0), 18, 112, 600, 48));
    CHECK(same(sprog_list_rect(&rig.f, 1), 18, 168, 295, 48));
    CHECK(same(sprog_list_rect(&rig.f, 2), 323, 168, 295, 48));
    CHECK(same(sprog_list_rect(&rig.f, 5), 18, 280, 295, 48));
    CHECK(same(sprog_list_rect(&rig.f, 6), 323, 280, 295, 48));
    CHECK(sprog_list_rect(&rig.f, 6).y + 48 <= sprog_list_cancel_rect(&rig.f).y
                                                   - 8);
    /* A sub on a list that is no files list: two spaces between. */
    sprog_list_open(&rig.f, 3, "MORE", false);
    (void)sprog_list_add(&rig.f, "VOLTAGE", "5.0V", 0);
    CHECK_STR_EQ(rig.f.list.label[0], "VOLTAGE  5.0V");
    /* A wide entry after a narrow one starts a line of its own. */
    sprog_list_open(&rig.f, 4, "T", false);
    for (int i = 0; i < 6; ++i) {
        (void)sprog_list_add(&rig.f, i == 1 ? "A label wider than one column"
                                            : "N", NULL, i);
    }
    CHECK(same(sprog_list_rect(&rig.f, 0), 18, 112, 295, 48));
    CHECK(same(sprog_list_rect(&rig.f, 1), 18, 168, 600, 48));
    CHECK(same(sprog_list_rect(&rig.f, 2), 18, 224, 295, 48));
}

/* The rows' controls: a fill, a flat value, warn ink, the label's colours,
 * an off control dim, a SHOW value, the step's keys, a pair, a gap, and
 * the row hold's fade. */
TEST_CASE(the_rows_draw_their_controls)
{
    rig_start();
    rig_cell(R_READ, SPROG_K_ACT, SPROG_P_TAP)->fill = SPROG_F_ACCENT;
    rig_cell(R_TYPE, SPROG_K_ACT, SPROG_P_OFF);
    rig_cell(R_FILES, SPROG_K_VALUE, SPROG_P_TAP)->look =
        SPROG_L_FLAT | SPROG_L_INK_WARN | SPROG_L_LABEL_ACCENT;
    rig_cell(R_MORE, SPROG_K_SHOW, SPROG_P_OFF)->fill = SPROG_F_WARN;
    rig_cell(R_RESTORE, SPROG_K_GAP, SPROG_P_OFF);
    rig.cell[R_RESTORE].label = "GAP";
    rig_render();
    const gfx_rect_t rd = sprog_value_rect(&rig.f, R_READ);
    CHECK_EQ(rig_px(rd.x + 12, rd.y + 30), UI_ACCENT);
    const gfx_rect_t ty = sprog_row_rect(&rig.f, R_TYPE);
    CHECK(count(gfx_rect_make(ty.x, ty.y + 14, 100, 20), UI_TEXT_FAINT) > 10);
    const gfx_rect_t fl = sprog_value_rect(&rig.f, R_FILES);
    CHECK(count(fl, UI_WARN) > 20);
    const gfx_rect_t flr = sprog_row_rect(&rig.f, R_FILES);
    CHECK(count(gfx_rect_make(flr.x, flr.y + 14, 100, 20), UI_ACCENT) > 10);
    const gfx_rect_t mo = sprog_row_rect(&rig.f, R_MORE);
    CHECK(count(gfx_rect_make(mo.x, mo.y + 14, 100, 20), UI_TEXT_DIM) > 10);
    const gfx_rect_t re = sprog_row_rect(&rig.f, R_RESTORE);
    CHECK_EQ(count(re, UI_TEXT_DIM), 0);
    CHECK_EQ(count(re, UI_TEXT_FAINT), 0);
    /* A flat value pressed: the panel's fill; a button with warn ink. */
    rig_cell(R_FILES, SPROG_K_VALUE, SPROG_P_TAP)->look = SPROG_L_FLAT;
    rig_cell(R_READ, SPROG_K_ACT, SPROG_P_TAP)->look = SPROG_L_INK_WARN;
    (void)rig_down(fl, 1);
    rig_render();
    CHECK_EQ(rig_px(fl.x + 12, fl.y + 30), UI_PANEL);
    CHECK(count(rd, UI_WARN) > 20);
    (void)rig_up(fl, 1);
    /* The value tab: the step's keys and its value, a pair. */
    sprog_tab_set(&rig.f, 1);
    rig_cell(R_SPEED, SPROG_K_PAIR, SPROG_P_TAP)->fill2 = SPROG_F_WARN;
    rig.cell[R_SPEED].value = "LEFT END";
    rig.cell[R_SPEED].value2 = "RIGHT END";
    rig_render();
    const gfx_rect_t plus = sprog_step_rect(&rig.f, R_DEAD, true);
    CHECK_EQ(rig_px(plus.x + 12, plus.y + 30), UI_PANEL_HI);
    const gfx_rect_t k1 = sprog_pair_rect(&rig.f, R_SPEED, 1);
    CHECK_EQ(rig_px(k1.x + 12, k1.y + 30), UI_WARN);
    /* A step drawn without its keys: the value in its place, no key. */
    rig_cell(R_DEAD, SPROG_K_STEP, SPROG_P_OFF)->look = SPROG_L_NO_KEYS;
    rig.cell[R_DEAD].value = "--";
    rig_render();
    CHECK(rig_px(plus.x + 12, plus.y + 30) != UI_PANEL_HI);
    CHECK_EQ(rig_px(plus.x + 12, plus.y + 30), UI_PANEL);
    rig.cell[R_DEAD].set = false;
    rig.cell[R_DEAD].value = NULL;
    rig.cell[R_SPEED].value2 = NULL;
    (void)rig_down(plus, 1);
    rig_render();
    CHECK(rig_px(plus.x + 12, plus.y + 30) != UI_PANEL_HI);
    (void)rig_up(plus, 1);
    /* The row hold: the fill fades from its base while held. */
    sprog_tab_set(&rig.f, 0);
    const gfx_rect_t w = sprog_value_rect(&rig.f, R_WRITE);
    rig_render();
    const gfx_color_t base = rig_px(w.x + 12, w.y + 30);
    CHECK_EQ(base, UI_OK);
    (void)rig_down(w, 1);
    (void)rig_run(1.0f);
    rig_render();
    CHECK(rig_px(w.x + 12, w.y + 30) != base);
    CHECK(rig.f.hold.drawn_f != NULL);
}

/* The keypad and the keyboard draw alone, over the whole area. */
TEST_CASE(the_keypad_and_the_keyboard_draw_alone)
{
    rig_start();
    sprog_keypad(&rig.f, 1, "SPEED", "%", 50.0f, 0.0f, 100.0f, 0);
    rig_render();
    CHECK(count(sprog_value_rect(&rig.f, R_HEAD), UI_OK) < 5000);
    sprog_close_editors(&rig.f);
    sprog_name(&rig.f, "NAME", "HAND");
    rig_render();
    CHECK(count(sprog_value_rect(&rig.f, R_HEAD), UI_OK) < 5000);
    sprog_close_editors(&rig.f);
    rig_render();
    CHECK(count(sprog_value_rect(&rig.f, R_HEAD), UI_OK) > 5000);
}

/* The page: a 3px edge in its colour, the title, the lines at 28px while
 * they fit and closer when not, none under the buttons; the 380px hold of
 * a drop page; the hold's flash after it closed. */
TEST_CASE(the_page_draws_its_lines_at_the_widest_pitch)
{
    rig_start();
    sprog_page_open(&rig.f, 1, true, "RESTORE", "HOLD 2s TO RESTORE",
                    "CANCEL", -1, false);
    (void)sprog_page_line(&rig.f, "One.", SPROG_LV_TEXT);
    (void)sprog_page_line(&rig.f, "Two.", SPROG_LV_WARN);
    (void)sprog_page_line(&rig.f, "Three.", SPROG_LV_DANGER);
    rig_render();
    CHECK_EQ(rig_px(8, 200), UI_DANGER);
    CHECK_EQ(rig_px(10, 200), UI_DANGER);
    CHECK(rig_px(11, 200) != UI_DANGER);
    CHECK(count(gfx_rect_make(20, 132, 200, 20), UI_TEXT) > 10);
    CHECK(count(gfx_rect_make(20, 160, 200, 20), UI_WARN) > 10);
    CHECK(count(gfx_rect_make(20, 188, 200, 20), UI_DANGER) > 10);
    CHECK(same(sprog_apply_rect(&rig.f), 28, 432, 300, 56));
    CHECK(same(sprog_page_cancel_rect(&rig.f), 428, 432, 180, 56));
    /* 12 lines: 24px apart, the last above the buttons. */
    sprog_page_open(&rig.f, 2, false, "LEAVE", "HOLD", "BACK", -1, true);
    for (int i = 0; i < 12; ++i) {
        (void)sprog_page_line(&rig.f, "Line.", SPROG_LV_TEXT);
    }
    CHECK(same(sprog_apply_rect(&rig.f), 28, 432, 380, 56));
    rig_render();
    CHECK(count(gfx_rect_make(20, 156, 200, 20), UI_TEXT) > 10);
    CHECK(count(gfx_rect_make(20, 396, 200, 20), UI_TEXT) > 10);
    CHECK_EQ(count(gfx_rect_make(20, 416, 300, 16), UI_TEXT), 0);
    CHECK_EQ(rig_px(8, 200), UI_WARN);
    /* 8 lines: 28px apart still. */
    sprog_page_open(&rig.f, 3, false, "CHECK", "HOLD", "CANCEL", -1, false);
    for (int i = 0; i < 8; ++i) {
        (void)sprog_page_line(&rig.f, "Line.", SPROG_LV_TEXT);
    }
    rig_render();
    CHECK(count(gfx_rect_make(20, 132 + 7 * 28, 200, 20), UI_TEXT) > 10);
    /* The hold fires: the page closes and its button flashes where it was. */
    (void)rig_down(sprog_apply_rect(&rig.f), 1);
    sprog_result_t got = { 0 };
    for (int i = 0; i < 60 && got.kind == SPROG_R_NONE; ++i) {
        got = rig_tick(0.05f);
    }
    CHECK_EQ(got.kind, SPROG_R_PAGE_APPLY);
    rig_render();
    CHECK_EQ(rig_px(40, 460), GFX_WHITE);
}

/* A list whose wide entries would reach CANCEL takes no more: every entry
 * ends 8px above CANCEL (review of #996). */
TEST_CASE(a_list_of_wide_entries_stays_clear_of_cancel)
{
    rig_start();
    const gfx_rect_t cancel = sprog_list_cancel_rect(&rig.f);
    int n = 0;

    sprog_list_open(&rig.f, 1, "T", false);
    for (int i = 0; i < SPROG_LIST_MAX; ++i) {
        n += sprog_list_add(&rig.f, "A label wider than one column", NULL, i)
                 ? 1 : 0;
    }
    CHECK_EQ(n, 5);
    for (int i = 0; i < rig.f.list.count; ++i) {
        const gfx_rect_t r = sprog_list_rect(&rig.f, i);
        CHECK(r.y + r.h <= cancel.y - 8);
    }
    /* Narrow ones after them still fill the room left. */
    sprog_list_open(&rig.f, 1, "T", false);
    for (int i = 0; i < 4; ++i) {
        CHECK(sprog_list_add(&rig.f, "A label wider than one column", NULL, i));
    }
    CHECK(sprog_list_add(&rig.f, "N", NULL, 4));
    CHECK(sprog_list_add(&rig.f, "N", NULL, 5));
    CHECK(!sprog_list_add(&rig.f, "A label wider than one column", NULL, 6));
    CHECK_EQ(rig.f.list.count, 6);
}

/* A note taller than a page of notes goes on over the next pages: every
 * row of it shows on some page (review of #996). */
TEST_CASE(a_note_taller_than_a_page_goes_on_over_pages)
{
    rig_start();
    static char tall[800];
    int n = 0;

    for (int i = 0; i < 70; ++i) {
        n += snprintf(tall + n, sizeof(tall) - (size_t)n, "%sw%04d", i > 0 ? " " : "",
                      i);
    }
    CHECK_EQ(sprog_text_rows(&rig.f, tall), 7);
    rig.note[0] = tall;
    rig.notes = 1;
    rig.bottom = "All 32 registers read.";
    rig_view();
    /* 5 rows above the bottom line, 4 a page beside the pager: 2 pages. */
    CHECK_EQ(sprog_note_pages(&rig.f), 2);
    CHECK(sprog_notes_rect(&rig.f).w > 0);
    rig_render();
    CHECK(count(gfx_rect_make(18, 356, 600, 80), UI_TEXT) > 400);
    (void)rig_tap(sprog_notes_rect(&rig.f));
    CHECK_EQ(rig.f.note_page, 1);
    rig_render();
    /* Page 2: the last 3 rows from the top, the fourth row empty. */
    CHECK(count(gfx_rect_make(18, 356, 600, 60), UI_TEXT) > 300);
    CHECK_EQ(count(gfx_rect_make(18, 416, 600, 20), UI_TEXT), 0);
    /* A short note after it starts on the page its rows end on. */
    rig.note[1] = "Short.";
    rig.notes = 2;
    rig_view();
    CHECK_EQ(sprog_note_pages(&rig.f), 2);
    rig.f.note_page = 1;
    rig_render();
    CHECK(count(gfx_rect_make(18, 416, 600, 20), UI_TEXT) > 20);
}

int main(void)
{
    RUN(the_servo_tab_flows_in_table_order);
    RUN(a_value_tab_flows_and_its_step_has_keys);
    RUN(the_flow_rule_closes_up_and_stops_at_the_grid);
    RUN(the_head_draws_its_lines_and_button);
    RUN(the_notes_flow_from_the_top_and_the_bottom_line_ends_at_the_foot);
    RUN(notes_that_need_more_rows_page);
    RUN(lists_of_2_to_7_entries_take_their_lines);
    RUN(a_files_list_takes_two_columns_clear_of_cancel);
    RUN(the_rows_draw_their_controls);
    RUN(the_keypad_and_the_keyboard_draw_alone);
    RUN(the_page_draws_its_lines_at_the_widest_pitch);
    RUN(a_list_of_wide_entries_stays_clear_of_cancel);
    RUN(a_note_taller_than_a_page_goes_on_over_pages);
    return test_summary("sprog_draw");
}
