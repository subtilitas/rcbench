/**
 * @file test_files_view.c
 * @brief The text viewer FILES hosts: a file read through the card task
 *        into the viewer, shown from its first row; its bench words in
 *        their classes in English and German; the faces, the page keys,
 *        the drag and the scroll bar at their ends; a line wrapped with
 *        its mark; and every file the viewer refuses at its limit and one
 *        past it.
 *
 * Cases:
 * - a_report_opens_read_only_from_its_first_row
 * - the_bench_words_draw_in_their_classes_in_both_languages
 * - a_servo_file_and_a_profile_take_their_classes
 * - small_switches_the_face_and_keeps_the_top_line
 * - the_page_keys_stop_at_the_first_and_last_screen
 * - a_long_line_wraps_onto_rows_with_the_mark
 * - a_drag_scrolls_a_row_a_pitch_and_the_bar_jumps
 * - the_bar_reaches_the_ends_inside_a_wrapped_line
 * - a_press_that_slides_off_or_a_second_finger_does_nothing
 * - the_size_limit_holds_at_1mib_and_refuses_one_byte_more
 * - a_nul_in_the_first_4kib_and_too_many_lines_are_refused
 * - an_empty_file_and_the_records_the_viewer_does_not_open
 * - closing_while_the_card_reads_drops_that_read
 * - a_card_swapped_or_gone_fails_the_read
 * - mixed_line_ends_are_shown_and_warned
 * - nothing_is_drawn_under_the_alert_band
 *
 * SPDX-License-Identifier: MIT
 */
#include "files_rig.h"

#include "files_int.h"
#include "servo_str.h"
#include "text_view.h"

/* ---------------------------------------------------------- the card --- */

/** @brief Put @p text at @p path on the card. */
static void put(const char *path, const char *text)
{
    fk_file(&g_h.card, path, (const uint8_t *)text, strlen(text));
}

/** @brief A report as servo_report.c lays one out, in English. */
static const char k_report[] =
    "RCBENCH SERVO TEST REPORT\n"
    "Result:         PASS\n"
    "Device:         MG90S\n"
    "Supply:         PD mini, 6.00V\n"
    "\n"
    "SETTINGS IN FORCE\n"
    "Limits:         idle 0.10A, holding 0.50A, travel 400ms\n"
    "Brown-out:      not run\n"
    "Idle current     highest 0.012A, limit 0.10A: FAIL\n";

/**
 * @brief A text of @p lines numbered lines, "line 1" up, each with a line
 *        feed, into a static buffer.
 * @param lines  how many, up to 400
 * @return the text
 */
static const char *numbered(int lines)
{
    static char buf[8192];
    size_t at = 0u;
    buf[0] = '\0';
    for (int i = 1; i <= lines && at + 16u < sizeof(buf); ++i) {
        at += (size_t)snprintf(buf + at, sizeof(buf) - at, "line %d\n", i);
    }
    return buf;
}

/**
 * @brief FILES over the default card, @p text as USER/LOGS/VIEW.TXT, and
 *        OPEN on it: the viewer shows it, or says why not.
 * @param text  the file's bytes, NUL-terminated
 */
static void view_text(const char *text)
{
    fresh_bare();
    card_default();
    put("USER/LOGS/VIEW.TXT", text);
    screen()->enter();
    settle();
    CHECK(pick(1, "VIEW.TXT"));
    tap_foot(FS_SPOT_OPEN);
}

/** @brief Row @p k on screen as drawn, "" for none. */
static const char *row(int k)
{
    static char out[512];
    if (!text_view_row_text(k, out, sizeof(out))) {
        out[0] = '\0';
    }
    return out;
}

/** @brief The top line on screen, its row in @p r. */
static size_t top(size_t *r)
{
    return text_view_top(r);
}

/** @brief Pixels of @p col in the rectangle (@p x, @p y, @p w, @p h). */
static int ink(int x, int y, int w, int h, gfx_color_t col)
{
    files_screen_invalidate();
    draw();
    int n = 0;
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            n += (gfx_pixel_get(&s_c, xx, yy) == col) ? 1 : 0;
        }
    }
    return n;
}

/** @brief A drag of contact 1 from (@p x, @p y0) to (@p x, @p y1). */
static void drag(int x, int y0, int y1)
{
    send(TOUCH_EVENT_DOWN, x, y0);
    send(TOUCH_EVENT_MOVE, x, (y0 + y1) / 2);
    send(TOUCH_EVENT_MOVE, x, y1);
    send(TOUCH_EVENT_UP, x, y1);
}

/** @brief A tap on PAGE DOWN, PAGE UP or SMALL. */
static void page_down(void)
{
    tap(FS_SPOT_TV_PGDN_CX, FS_SPOT_TV_FOOT_CY);
}

static void page_up(void)
{
    tap(FS_SPOT_TV_PGUP_CX, FS_SPOT_TV_FOOT_CY);
}

static void small(void)
{
    tap(FS_SPOT_TV_SMALL_CX, FS_SPOT_TV_FOOT_CY);
}

/** @brief Whether the viewer shows @p name of @p dir. */
static bool showing(const char *dir, const char *name)
{
    return files_screen_view() == FILES_VIEW_TEXT
           && text_view_state() == TEXT_VIEW_SHOWN
           && strcmp(text_view_dir(), dir) == 0
           && strcmp(text_view_name(), name) == 0;
}

/* ---------------------------------------------------------- the cases --- */

TEST_CASE(a_report_opens_read_only_from_its_first_row)
{
    view_text(k_report);
    CHECK(showing("USER/LOGS", "VIEW.TXT"));
    CHECK_EQ(text_view_lines(), 9u);
    CHECK_EQ(text_view_rows(), TEXT_VIEW_ROWS_NORMAL);
    CHECK_STR_EQ(row(0), "RCBENCH SERVO TEST REPORT");
    CHECK_STR_EQ(row(1), "Result:         PASS");
    CHECK_STR_EQ(row(4), "");
    CHECK_STR_EQ(row(8), "Idle current     highest 0.012A, limit 0.10A: FAIL");
    CHECK_STR_EQ(row(9), "");                   /* past the text */
    /* The whole text shows: neither page key moves it. */
    CHECK(text_view_at_start());
    CHECK(text_view_at_end());
    page_down();
    CHECK_EQ(top(NULL), 0u);
    /* The header: READ-ONLY and the line count, drawn. */
    CHECK(ink(8, 4, 1008, 48, UI_TEXT_DIM) > 0);
    /* The home tag closes it onto the panes, the file still selected. */
    CHECK(home());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_EQ(text_view_state(), TEXT_VIEW_CLOSED);
    CHECK_EQ(row_of(1, "VIEW.TXT"), files_screen_pane(1)->selected);
    /* A second tap on the selected row opens it, as OPEN does. */
    tap_row(1, row_of(1, "VIEW.TXT"));
    CHECK(showing("USER/LOGS", "VIEW.TXT"));
    /* Leaving FILES closes it; FILES comes back on its panes. */
    screen()->leave();
    CHECK_EQ(text_view_state(), TEXT_VIEW_CLOSED);
    screen()->enter();
    settle();
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
}

TEST_CASE(the_bench_words_draw_in_their_classes_in_both_languages)
{
    view_text(k_report);
    CHECK_EQ(text_view_cell_class(0, 0), TEXT_HL_HEADING);
    CHECK_EQ(text_view_cell_class(1, 0), TEXT_HL_LABEL);
    CHECK_EQ(text_view_cell_class(1, 16), TEXT_HL_OK);
    CHECK_EQ(text_view_cell_class(3, 26), TEXT_HL_VOLT);      /* 6.00V */
    CHECK_EQ(text_view_cell_class(5, 0), TEXT_HL_HEADING);
    CHECK_EQ(text_view_cell_class(6, 21), TEXT_HL_CURR);      /* 0.10A */
    CHECK_EQ(text_view_cell_class(6, 50), TEXT_HL_UNIT);      /* 400ms */
    CHECK_EQ(text_view_cell_class(7, 16), TEXT_HL_WARN);      /* not run */
    CHECK_EQ(text_view_cell_class(8, 46), TEXT_HL_FAIL);
    /* Drawn in the theme's colours: PASS green on row 1, FAIL red. */
    CHECK(ink(FS_SPOT_TV_TEXT_X, FS_SPOT_TV_TEXT_Y + FS_SPOT_TV_PITCH,
              900, FS_SPOT_TV_PITCH, UI_OK) > 0);
    CHECK(ink(FS_SPOT_TV_TEXT_X, FS_SPOT_TV_TEXT_Y + 8 * FS_SPOT_TV_PITCH,
              900, FS_SPOT_TV_PITCH, UI_DANGER) > 0);
    /* The words of every language, on a panel showing English: a report
     * written in German classes the same. */
    const ui_language_t *de = ui_text_table(UI_LANG_DE);
    const char *pass = servo_str_in(de->servo, SERVO_STR_PASS);
    const char *fail = servo_str_in(de->servo, SERVO_STR_FAIL);
    char text[256];
    snprintf(text, sizeof(text),
             "RCBENCH SERVOTESTBERICHT\nErgebnis:       %s\n"
             "Strom:          %s\n", pass, fail);
    view_text(text);
    CHECK_EQ(text_view_cell_class(1, 16), TEXT_HL_OK);
    CHECK_EQ(text_view_cell_class(2, 16), TEXT_HL_FAIL);
    /* The lists: PASS, FAIL and the 7 WARN words in each language, and
     * the writers' two marks. */
    const text_hl_words_t *w = text_view_words();
    CHECK_EQ(w->ok_n, (size_t)UI_LANG_COUNT);
    CHECK_EQ(w->fail_n, (size_t)UI_LANG_COUNT);
    CHECK_EQ(w->warn_n, 7u * (size_t)UI_LANG_COUNT + 2u);
    CHECK_STR_EQ(w->ok[0], servo_str(SERVO_STR_PASS));
    CHECK_STR_EQ(w->ok[1], pass);
    CHECK_STR_EQ(w->warn[w->warn_n - 1u], "[not loaded]");
}

TEST_CASE(a_servo_file_and_a_profile_take_their_classes)
{
    view_text("KST servo configuration, format 1\n"
              "Type: V6\n"
              "Neutral = 1500.0 us\n"
              "# set by hand\n"
              "00: 1A 2B 3C 4D\n"
              "checksum 9F3A77C1\n");
    CHECK_EQ(text_view_cell_class(0, 0), TEXT_HL_HEADING);
    CHECK_EQ(text_view_cell_class(1, 0), TEXT_HL_LABEL);
    CHECK_EQ(text_view_cell_class(2, 0), TEXT_HL_LABEL);
    CHECK_EQ(text_view_cell_class(2, 10), TEXT_HL_UNIT);      /* the neutral and its unit */
    CHECK_EQ(text_view_cell_class(3, 0), TEXT_HL_COMMENT);
    CHECK_EQ(text_view_cell_class(4, 6), TEXT_HL_HEX);
    CHECK_EQ(text_view_cell_class(5, 0), TEXT_HL_HEX);
    /* A profile, by its name: JSON's classes. */
    fresh_bare();
    card_default();
    put("BENCH/ESC/CONF/YGE.json", "{\n  \"cells\": 12,\n  \"on\": true\n}\n");
    screen()->enter();
    settle();
    CHECK(open_dir(0, "BENCH"));
    CHECK(open_dir(0, "ESC"));
    CHECK(open_dir(0, "CONF"));
    CHECK(pick(0, "YGE.json"));
    tap_foot(FS_SPOT_OPEN);
    CHECK(showing("BENCH/ESC/CONF", "YGE.json"));
    CHECK_EQ(text_view_cell_class(0, 0), TEXT_HL_PUNCT);
    CHECK_EQ(text_view_cell_class(1, 3), TEXT_HL_KEY);
    CHECK_EQ(text_view_cell_class(1, 12), TEXT_HL_NUMBER);
    CHECK_EQ(text_view_cell_class(2, 8), TEXT_HL_LITERAL);
}

TEST_CASE(small_switches_the_face_and_keeps_the_top_line)
{
    view_text(numbered(100));
    page_down();
    CHECK_EQ(top(NULL), 18u);
    small();
    CHECK(text_view_small());
    CHECK_EQ(text_view_rows(), TEXT_VIEW_ROWS_SMALL);
    CHECK_EQ(text_view_cells(), (unsigned)TEXT_VIEW_CELLS_SMALL);
    CHECK_EQ(top(NULL), 18u);
    CHECK_STR_EQ(row(0), "line 19");
    CHECK_STR_EQ(row(TEXT_VIEW_ROWS_SMALL - 1), "line 40");
    page_down();
    CHECK_EQ(top(NULL), 40u);
    /* The rows of the 8 x 16 face: the 22nd row inked, nothing under. */
    const int y21 = FS_SPOT_TV_TEXT_Y + 21 * FS_SPOT_TV_PITCH_SMALL;
    CHECK(ink(FS_SPOT_TV_TEXT_X, y21, 300, FS_SPOT_TV_PITCH_SMALL, UI_TEXT)
          > 0);
    CHECK_EQ(ink(FS_SPOT_TV_TEXT_X, y21 + FS_SPOT_TV_PITCH_SMALL, 300,
                 FS_SPOT_TV_PITCH_SMALL, UI_TEXT), 0);
    /* The choice outlives the file and the screen; a start ends it. */
    CHECK(home());
    CHECK(pick(1, "VIEW.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK(text_view_small());
    small();                                   /* NORMAL */
    CHECK(!text_view_small());
    CHECK_EQ(text_view_rows(), TEXT_VIEW_ROWS_NORMAL);
    small();
    ui_router_init();
    CHECK(!text_view_small());
}

TEST_CASE(the_page_keys_stop_at_the_first_and_last_screen)
{
    view_text(numbered(100));
    page_up();                                 /* at the start: nothing */
    CHECK_EQ(top(NULL), 0u);
    for (int k = 1; k <= 4; ++k) {
        page_down();
        CHECK_EQ(top(NULL), (size_t)(18 * k));
    }
    page_down();                               /* 90 to the last screen */
    CHECK_EQ(top(NULL), 82u);
    CHECK(text_view_at_end());
    CHECK_STR_EQ(row(17), "line 100");
    page_down();
    CHECK_EQ(top(NULL), 82u);
    page_up();
    CHECK_EQ(top(NULL), 64u);
    /* Exactly a screen, its line end at the end starting no line: both
     * keys still. */
    view_text(numbered(18));
    CHECK_EQ(text_view_lines(), 18u);
    CHECK(text_view_at_start() && text_view_at_end());
    page_down();
    CHECK_EQ(top(NULL), 0u);
    /* One line more: a page moves one line. */
    view_text(numbered(19));
    page_down();
    CHECK_EQ(top(NULL), 1u);
    CHECK(text_view_at_end());
}

TEST_CASE(a_long_line_wraps_onto_rows_with_the_mark)
{
    static char text[512];
    /* 92 cells fill a row; 93 start a second one. */
    memset(text, 'x', 92);
    snprintf(text + 92, sizeof(text) - 92, "\n%s", "");
    view_text(text);
    CHECK_EQ(strlen(row(0)), 92u);
    CHECK_STR_EQ(row(1), "");
    static char longer[1024];
    memset(longer, 'x', 200);
    snprintf(longer + 200, sizeof(longer) - 200, "\nnext\n%s", numbered(40));
    view_text(longer);
    CHECK_EQ(strlen(row(0)), 92u);
    CHECK_EQ(strlen(row(1)), 92u);
    CHECK_EQ(strlen(row(2)), 16u);
    CHECK_STR_EQ(row(3), "next");
    /* The mark at the left of each row a line continues. */
    CHECK(ink(8, FS_SPOT_TV_TEXT_Y + FS_SPOT_TV_PITCH, 24, FS_SPOT_TV_PITCH,
              UI_TEXT_FAINT) > 0);
    CHECK_EQ(ink(8, FS_SPOT_TV_TEXT_Y, 24, FS_SPOT_TV_PITCH, UI_TEXT_FAINT),
             0);
    /* The scroll steps through the rows of a line. */
    drag(400, 300, 300 - FS_SPOT_TV_PITCH);
    size_t r = 9u;
    CHECK_EQ(top(&r), 0u);
    CHECK_EQ(r, 1u);
    /* A tab reaches the next stop of 8 cells; at a row's end it ends
     * there, and the next unit starts the next row. */
    view_text("a\tb\n");
    CHECK_STR_EQ(row(0), "a       b");
    memset(text, 'x', 91);
    snprintf(text + 91, sizeof(text) - 91, "\ty\n");
    view_text(text);
    CHECK_EQ(strlen(row(0)), 92u);
    CHECK_EQ(row(0)[91], ' ');
    CHECK_STR_EQ(row(1), "y");
    /* A byte that starts no UTF-8 sequence and a control draw as "?"; an
     * umlaut draws as itself. */
    view_text("a\xff" "b\x01" "c \xc3\xa4\n");
    CHECK_STR_EQ(row(0), "a?b?c \xc3\xa4");
}

TEST_CASE(a_drag_scrolls_a_row_a_pitch_and_the_bar_jumps)
{
    view_text(numbered(100));
    drag(400, 300, 300 - 5 * FS_SPOT_TV_PITCH);
    CHECK_EQ(top(NULL), 5u);
    CHECK(showing("USER/LOGS", "VIEW.TXT"));     /* a drag is no tap */
    drag(400, 200, 200 + 2 * FS_SPOT_TV_PITCH);
    CHECK_EQ(top(NULL), 3u);
    /* Inside the slop: no scroll. */
    drag(400, 200, 192);
    CHECK_EQ(top(NULL), 3u);
    /* Far past either end: held to the first and last screen. */
    drag(400, 420, -4000);
    CHECK_EQ(top(NULL), 82u);
    drag(400, 70, 4000);
    CHECK_EQ(top(NULL), 0u);
    /* The bar: its foot is the last screen, its top the first. */
    tap(FS_SPOT_TV_BAR_CX, FS_SPOT_TV_BAR_TOP + FS_SPOT_TV_BAR_H - 2);
    CHECK_EQ(top(NULL), 82u);
    tap(FS_SPOT_TV_BAR_CX, FS_SPOT_TV_BAR_TOP + 2);
    CHECK_EQ(top(NULL), 0u);
    /* The thumb taken and dragged half way. */
    drag(FS_SPOT_TV_BAR_CX, FS_SPOT_TV_BAR_TOP + 10,
         FS_SPOT_TV_BAR_TOP + FS_SPOT_TV_BAR_H / 2);
    CHECK(top(NULL) > 20u && top(NULL) < 62u);
    /* A text that fits does not scroll. */
    view_text(numbered(10));
    drag(400, 300, 100);
    tap(FS_SPOT_TV_BAR_CX, FS_SPOT_TV_BAR_TOP + FS_SPOT_TV_BAR_H - 2);
    CHECK_EQ(top(NULL), 0u);
}

/**
 * @brief @p n letters with no space, a line wrapped at every row's last
 *        cell, at @p out.
 * @return the byte after them
 */
static char *letters(char *out, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        out[i] = (char)('a' + (int)(i % 26u));
    }
    return out + n;
}

/**
 * @brief The last screen as PAGE DOWN reaches it, and back to the first.
 * @param row  takes its row
 * @return its line
 */
static size_t page_end(size_t *row)
{
    for (int i = 0; i < 40 && !text_view_at_end(); ++i) {
        page_down();
    }
    const size_t line = top(row);
    for (int i = 0; i < 40 && !text_view_at_start(); ++i) {
        page_up();
    }
    return line;
}

/** @brief The thumb, taken 10px under its top at @p from, let go at @p to. */
static void thumb_drag(int from, int to)
{
    drag(FS_SPOT_TV_BAR_CX, from + 10, to + 10);
}

/*
 * The bar spans rows inside a wrapped line: a single line of 100 rows, and
 * a text whose last screen starts inside a wrapped line, scroll to the same
 * ends by the thumb as by PAGE DOWN and PAGE UP.
 */
TEST_CASE(the_bar_reaches_the_ends_inside_a_wrapped_line)
{
    static char text[16384];
    const int foot = FS_SPOT_TV_BAR_TOP + FS_SPOT_TV_BAR_H;
    /* One line of 9,200 letters: 100 rows at 92 cells. */
    *letters(text, 9200u) = '\0';
    view_text(text);
    CHECK_EQ(text_view_lines(), 1u);
    size_t er = 0u;
    CHECK_EQ(page_end(&er), 0u);
    CHECK_EQ(er, 82u);
    size_t r = 99u;
    thumb_drag(FS_SPOT_TV_BAR_TOP, foot + 200);
    CHECK_EQ(top(&r), 0u);
    CHECK_EQ(r, 82u);
    CHECK(text_view_at_end());
    /* Half the travel is half the rows: the thumb is 65px of 364px. */
    thumb_drag(foot - 65, FS_SPOT_TV_BAR_TOP + (FS_SPOT_TV_BAR_H - 65) / 2);
    CHECK_EQ(top(&r), 0u);
    CHECK(r >= 39u && r <= 43u);
    thumb_drag(FS_SPOT_TV_BAR_TOP + (FS_SPOT_TV_BAR_H - 65) / 2,
               FS_SPOT_TV_BAR_TOP - 200);
    CHECK_EQ(top(&r), 0u);
    CHECK_EQ(r, 0u);
    CHECK(text_view_at_start());

    /* 40 short lines, a line of 25 rows, 5 short lines: the last screen
     * starts on row 12 of the long line, line 40. */
    char *at = text;
    at += snprintf(at, 512u, "%s", numbered(40));
    at = letters(at, 25u * 92u);
    *at++ = '\n';
    for (int i = 0; i < 5; ++i) {
        at += snprintf(at, 16u, "end %d\n", i);
    }
    *at = '\0';
    view_text(text);
    CHECK_EQ(text_view_lines(), 46u);
    const size_t el = page_end(&er);
    CHECK_EQ(el, 40u);
    CHECK_EQ(er, 12u);
    thumb_drag(FS_SPOT_TV_BAR_TOP, foot + 200);
    CHECK_EQ(top(&r), el);
    CHECK_EQ(r, er);
    CHECK(text_view_at_end());
    /* The middle of the travel: the line half way to line 40, row 12. */
    tap(FS_SPOT_TV_BAR_CX, FS_SPOT_TV_BAR_TOP + FS_SPOT_TV_BAR_H / 2);
    CHECK(top(NULL) >= 18u && top(NULL) <= 22u);
    CHECK(!text_view_at_start() && !text_view_at_end());
    tap(FS_SPOT_TV_BAR_CX, FS_SPOT_TV_BAR_TOP + 2);
    CHECK_EQ(top(&r), 0u);
    CHECK_EQ(r, 0u);
    /* PAGE DOWN from the bar's end does nothing more. */
    thumb_drag(FS_SPOT_TV_BAR_TOP, foot + 200);
    page_down();
    CHECK_EQ(top(&r), el);
    CHECK_EQ(r, er);
}

TEST_CASE(a_press_that_slides_off_or_a_second_finger_does_nothing)
{
    view_text(numbered(100));
    send(TOUCH_EVENT_DOWN, FS_SPOT_TV_PGDN_CX, FS_SPOT_TV_FOOT_CY);
    send(TOUCH_EVENT_MOVE, FS_SPOT_TV_PGDN_CX - 200, FS_SPOT_TV_FOOT_CY);
    send(TOUCH_EVENT_UP, FS_SPOT_TV_PGDN_CX - 200, FS_SPOT_TV_FOOT_CY);
    CHECK_EQ(top(NULL), 0u);
    /* A second contact while one holds PAGE DOWN presses nothing; the
     * first one's release acts. */
    send_id(TOUCH_EVENT_DOWN, 1, FS_SPOT_TV_PGDN_CX, FS_SPOT_TV_FOOT_CY);
    send_id(TOUCH_EVENT_DOWN, 2, FS_SPOT_TV_SMALL_CX, FS_SPOT_TV_FOOT_CY);
    send_id(TOUCH_EVENT_UP, 2, FS_SPOT_TV_SMALL_CX, FS_SPOT_TV_FOOT_CY);
    CHECK(!text_view_small());
    send_id(TOUCH_EVENT_UP, 1, FS_SPOT_TV_PGDN_CX, FS_SPOT_TV_FOOT_CY);
    CHECK_EQ(top(NULL), 18u);
    /* A lost event drops the press: its release does nothing. */
    send(TOUCH_EVENT_DOWN, FS_SPOT_TV_PGDN_CX, FS_SPOT_TV_FOOT_CY);
    screen()->cancel();
    send(TOUCH_EVENT_UP, FS_SPOT_TV_PGDN_CX, FS_SPOT_TV_FOOT_CY);
    CHECK_EQ(top(NULL), 18u);
}

TEST_CASE(the_size_limit_holds_at_1mib_and_refuses_one_byte_more)
{
    static uint8_t big[TEXT_BUF_FILE_MAX + 1u];
    for (size_t i = 0; i < sizeof(big); ++i) {
        big[i] = (i % 80u == 79u) ? '\n' : 'a';
    }
    fresh_bare();
    card_default();
    fk_file(&g_h.card, "USER/LOGS/BIG.TXT", big, TEXT_BUF_FILE_MAX);
    fk_file(&g_h.card, "USER/LOGS/HUGE.TXT", big, sizeof(big));
    screen()->enter();
    settle();
    CHECK(pick(1, "BIG.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK(showing("USER/LOGS", "BIG.TXT"));
    CHECK_EQ(text_view_lines(), (size_t)(TEXT_BUF_FILE_MAX / 80u + 1u));
    /* The last screen of 13,108 lines, reached by the bar. */
    tap(FS_SPOT_TV_BAR_CX, FS_SPOT_TV_BAR_TOP + FS_SPOT_TV_BAR_H - 2);
    CHECK(text_view_at_end());
    CHECK_EQ(top(NULL), text_view_lines() - 18u);
    CHECK(home());
    /* One byte more: OPEN is shut and says why; nothing is read. */
    CHECK(pick(1, "HUGE.TXT"));
    fm_err_t why = FM_OK;
    CHECK(!fs_foot_live(FS_OPEN, &why));
    CHECK_EQ(why, FM_ERR_TOO_BIG);
    const unsigned submits = g_h.submits;
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_EQ(g_h.submits, submits);
    CHECK_STR_EQ(files_screen_message(), TR(ED_TOO_LARGE));
    CHECK_EQ(files_screen_open_text("USER/LOGS", "HUGE.TXT", g_h.card.mount,
                                    sizeof(big), false),
             FM_ERR_TOO_BIG);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* A file grown past the limit since it was listed: the read refuses
     * it, and the viewer says why. */
    CHECK_EQ(files_screen_open_text("USER/LOGS", "HUGE.TXT", g_h.card.mount,
                                    10u, false),
             FM_OK);
    settle();
    CHECK_EQ(text_view_state(), TEXT_VIEW_FAILED);
    CHECK_EQ(text_view_why(NULL), FM_ERR_TOO_BIG);
    CHECK(home());
}

TEST_CASE(a_nul_in_the_first_4kib_and_too_many_lines_are_refused)
{
    static char text[TEXT_BUF_PROBE + 16u];
    memset(text, 'a', sizeof(text));
    text[sizeof(text) - 1u] = '\0';
    fresh_bare();
    card_default();
    text[TEXT_BUF_PROBE - 1u] = '\0';         /* the probe's last byte */
    fk_file(&g_h.card, "USER/LOGS/NUL1.TXT", (const uint8_t *)text,
            sizeof(text) - 1u);
    text[TEXT_BUF_PROBE - 1u] = 'a';
    text[TEXT_BUF_PROBE] = '\0';              /* the first byte past it */
    fk_file(&g_h.card, "USER/LOGS/NUL2.TXT", (const uint8_t *)text,
            sizeof(text) - 1u);
    screen()->enter();
    settle();
    CHECK(pick(1, "NUL1.TXT"));
    tap_foot(FS_SPOT_OPEN);
    text_buf_err_t tb = TEXT_BUF_OK;
    CHECK_EQ(text_view_state(), TEXT_VIEW_FAILED);
    CHECK_EQ(text_view_why(&tb), FM_OK);
    CHECK_EQ(tb, TEXT_BUF_NOT_TEXT);
    CHECK(ink(FS_SPOT_TV_TEXT_X, FS_SPOT_TV_TEXT_Y, 900, 40, UI_DANGER) > 0);
    CHECK(home());
    CHECK(pick(1, "NUL2.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK(showing("USER/LOGS", "NUL2.TXT"));
    /* The NUL past the probe is kept and drawn as "?". */
    text_view_scroll(1000);
    CHECK(text_view_at_end());
    CHECK(strstr(row(TEXT_VIEW_ROWS_NORMAL - 1), "a?a") != NULL);
    CHECK(home());
    /* 65,536 line ends open; one more is refused. */
    static char ends[TEXT_BUF_ENDS_MAX + 2u];
    memset(ends, '\n', TEXT_BUF_ENDS_MAX + 1u);
    ends[TEXT_BUF_ENDS_MAX + 1u] = '\0';
    fk_file(&g_h.card, "USER/LOGS/ENDS1.TXT", (const uint8_t *)ends,
            TEXT_BUF_ENDS_MAX);
    fk_file(&g_h.card, "USER/LOGS/ENDS2.TXT", (const uint8_t *)ends,
            TEXT_BUF_ENDS_MAX + 1u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(1, "ENDS1.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK(showing("USER/LOGS", "ENDS1.TXT"));
    CHECK_EQ(text_view_lines(), (size_t)TEXT_BUF_ENDS_MAX);
    CHECK(home());
    CHECK(pick(1, "ENDS2.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(text_view_state(), TEXT_VIEW_FAILED);
    CHECK_EQ(text_view_why(&tb), FM_OK);
    CHECK_EQ(tb, TEXT_BUF_TOO_MANY);
    CHECK(home());
}

TEST_CASE(an_empty_file_and_the_records_the_viewer_does_not_open)
{
    view_text("");
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    CHECK_EQ(text_view_lines(), 0u);
    CHECK_STR_EQ(row(0), "");
    CHECK(ink(FS_SPOT_TV_TEXT_X, FS_SPOT_TV_TEXT_Y, 900, 40, UI_TEXT_DIM)
          > 0);
    page_down();
    drag(400, 300, 100);
    CHECK_EQ(top(NULL), 0u);
    CHECK(home());
    /* The servo programmer's record: OPEN is shut. */
    CHECK(open_dir(0, "BENCH"));
    CHECK(open_dir(0, "KST"));
    CHECK(open_dir(0, "CONF"));
    CHECK(pick(0, "KST.TXT"));
    fm_err_t why = FM_OK;
    CHECK(!fs_foot_live(FS_OPEN, &why));
    CHECK_EQ(why, FM_ERR_LOCKED);
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* A servo file beside it opens. */
    CHECK(pick(0, "MG90S.TXT"));
    CHECK(fs_foot_live(FS_OPEN, &why));
}

TEST_CASE(closing_while_the_card_reads_drops_that_read)
{
    fresh_bare();
    card_default();
    put("USER/LOGS/A.TXT", "first file\n");
    put("USER/LOGS/B.TXT", "second file\n");
    screen()->enter();
    settle();
    CHECK(pick(1, "A.TXT"));
    g_h.defer = true;
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(text_view_state(), TEXT_VIEW_LOADING);
    CHECK(ink(FS_SPOT_TV_TEXT_X, FS_SPOT_TV_TEXT_Y, 900, 40, UI_TEXT_DIM)
          > 0);
    /* The home tag: the read is asked to stop, the panes show. */
    const unsigned cancels = g_h.cancels;
    CHECK(home());
    CHECK_EQ(g_h.cancels, cancels + 1u);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* OPEN waits while a request is out, as every button does. */
    CHECK(pick(1, "B.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* B opened from LOGS while A's read is still out: it waits for that
     * answer. */
    CHECK_EQ(files_screen_open_text("USER/LOGS", "B.TXT", g_h.card.mount,
                                    12u, false),
             FM_OK);
    CHECK_EQ(text_view_state(), TEXT_VIEW_LOADING);
    CHECK(fh_run(&g_h));                     /* A's answer: dropped */
    settle();
    CHECK_EQ(text_view_state(), TEXT_VIEW_LOADING);
    CHECK_STR_EQ(text_view_name(), "B.TXT");
    g_h.defer = false;
    CHECK(fh_run(&g_h));                     /* B's read */
    settle();
    CHECK(showing("USER/LOGS", "B.TXT"));
    CHECK_STR_EQ(row(0), "second file");
}

TEST_CASE(a_card_swapped_or_gone_fails_the_read)
{
    view_text(k_report);
    CHECK(home());
    /* Listed from another card: the read runs on no card. */
    CHECK_EQ(files_screen_open_text("USER/LOGS", "VIEW.TXT",
                                    g_h.card.mount + 1u, 10u, false),
             FM_OK);
    settle();
    CHECK_EQ(text_view_state(), TEXT_VIEW_FAILED);
    CHECK_EQ(text_view_why(NULL), FM_ERR_CARD_GONE);
    char want[192];
    files_err_text(FM_ERR_CARD_GONE, "VIEW.TXT", want, sizeof(want));
    CHECK_STR_EQ(want, TR(FM_E_CARD_GONE));
    CHECK(home());
    /* A read that fails part way. */
    fk_fail(&g_h.card, FK_READ, 1, FM_ERR_IO);
    CHECK(pick(1, "VIEW.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(text_view_state(), TEXT_VIEW_FAILED);
    CHECK_EQ(text_view_why(NULL), FM_ERR_IO);
    CHECK(home());
    /* No card's task at all: the viewer says there is no card. */
    files_screen_set_io(NULL);
    CHECK_EQ(files_screen_open_text("USER/LOGS", "VIEW.TXT", 1u, 10u, false),
             FM_OK);
    CHECK_EQ(text_view_state(), TEXT_VIEW_FAILED);
    CHECK_EQ(text_view_why(NULL), FM_ERR_NO_CARD);
    CHECK(home());
}

TEST_CASE(mixed_line_ends_are_shown_and_warned)
{
    /* The warning's box, between SMALL and PAGE UP. */
    const int wx = 8 + 140 + 16;
    view_text("one\r\ntwo\nthree\rfour");
    CHECK_EQ(text_view_lines(), 4u);
    CHECK_STR_EQ(row(0), "one");
    CHECK_STR_EQ(row(2), "three");
    CHECK_STR_EQ(row(3), "four");
    CHECK(ink(wx, FS_SPOT_FOOT_Y, 550, 52, UI_WARN) > 0);
    view_text("one\r\ntwo\r\n");
    CHECK_EQ(text_view_lines(), 2u);
    CHECK_EQ(ink(wx, FS_SPOT_FOOT_Y, 550, 52, UI_WARN), 0);
    /* A BOM is no cell. */
    view_text("\xef\xbb\xbfRCBENCH\n");
    CHECK_STR_EQ(row(0), "RCBENCH");
}

/** @brief The views the floor check draws. */
static void at_report(void)
{
    view_text(k_report);
}

static void at_small(void)
{
    view_text(numbered(100));
    small();
}

static void at_reading(void)
{
    fresh_bare();
    card_default();
    screen()->enter();
    settle();
    g_h.defer = true;
    CHECK(pick(1, "ATEST005.TXT"));
    tap_foot(FS_SPOT_OPEN);
}

static void at_failed(void)
{
    view_text("a\x01");
    CHECK(home());
    fk_fail(&g_h.card, FK_READ, 1, FM_ERR_CARD_GONE);
    CHECK(pick(1, "VIEW.TXT"));
    tap_foot(FS_SPOT_OPEN);
}

/*
 * The router's alert band covers body y 496 to 539 while an alert shows.
 * Nothing of the viewer reaches 4px over it, in English and in German.
 */
TEST_CASE(nothing_is_drawn_under_the_alert_band)
{
    static const struct {
        const char *name;
        void (*view)(void);
    } views[] = {
        { "report", at_report }, { "small", at_small },
        { "reading", at_reading }, { "failed", at_failed },
    };
    const int floor = UI_ALERT_Y - UI_BAND_H - 4;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (size_t i = 0; i < sizeof(views) / sizeof(views[0]); ++i) {
            views[i].view();
            ui_text_set_language((ui_lang_t)lang);
            CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
            const int n = ink(0, floor, W, UI_BODY_H - floor, UI_BG);
            if (n != W * (UI_BODY_H - floor)) {
                T_FAIL("%s, language %d: drawn under the floor", views[i].name,
                       lang);
            }
            g_h.defer = false;
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_report_opens_read_only_from_its_first_row);
    RUN(the_bench_words_draw_in_their_classes_in_both_languages);
    RUN(a_servo_file_and_a_profile_take_their_classes);
    RUN(small_switches_the_face_and_keeps_the_top_line);
    RUN(the_page_keys_stop_at_the_first_and_last_screen);
    RUN(a_long_line_wraps_onto_rows_with_the_mark);
    RUN(a_drag_scrolls_a_row_a_pitch_and_the_bar_jumps);
    RUN(the_bar_reaches_the_ends_inside_a_wrapped_line);
    RUN(a_press_that_slides_off_or_a_second_finger_does_nothing);
    RUN(the_size_limit_holds_at_1mib_and_refuses_one_byte_more);
    RUN(a_nul_in_the_first_4kib_and_too_many_lines_are_refused);
    RUN(an_empty_file_and_the_records_the_viewer_does_not_open);
    RUN(closing_while_the_card_reads_drops_that_read);
    RUN(a_card_swapped_or_gone_fails_the_read);
    RUN(mixed_line_ends_are_shown_and_warned);
    RUN(nothing_is_drawn_under_the_alert_band);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_view");
}
