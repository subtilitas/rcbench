/**
 * @file test_kst_panel_notes.c
 * @brief The programmer overlay's notes: their flow under the rows, the
 * value tabs' hint, pages, the standing lines and the message.
 *
 * Cases:
 * - the_notes_flow_from_under_the_rows
 * - value_tabs_end_with_the_common_hint
 * - every_message_fits_whole_under_the_servo_tab
 * - a_servo_in_doubt_keeps_every_line_of_its_message
 * - the_notes_start_on_their_first_page_again
 * - a_text_before_the_area_is_set_still_ends
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/* The @p i-th note on the page shown, "" past the last. */
static const char *shown(int i)
{
    const char *s = kst_panel_shown_note(i);

    return s != NULL ? s : "";
}

/* The SERVO tab's notes flow from 8px under its last row, the state line
 * first; the last message is pinned to the foot, 8px above the area's
 * bottom. */
TEST_CASE(the_notes_flow_from_under_the_rows)
{
    fresh();
    connected();
    show();
    {
        const gfx_rect_t last = val(PG_SERVO, 3, 1);
        const gfx_rect_t drawn = kst_panel_notes_drawn();

        CHECK_STR_EQ(shown(0), kst_panel_status_line(0));
        CHECK_EQ(drawn.y, last.y + last.h + 8);
        CHECK(kst_panel_shown_message_lines() > 0);
        CHECK_EQ(drawn.y + drawn.h, AREA.y + AREA.h - 8);
    }
}

/* A value tab's note, then the hint every servo programmer shares; the
 * LOCKED tab takes no edit and has no hint. */
TEST_CASE(value_tabs_end_with_the_common_hint)
{
    fresh();
    settings_set(SET_KST_EXPERT, 1.0f);
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK_STR_EQ(shown(0), TR(SPROG_EDIT_NOTE));
    CHECK_STR_EQ(shown(1), "");
    tap(kst_panel_tab_rect(PG_PULSE));
    CHECK_STR_EQ(shown(0), TR(KP_ANGLE_NOTE));
    CHECK_STR_EQ(shown(1), TR(SPROG_EDIT_NOTE));
    tap(kst_panel_tab_rect(PG_FAILSAFE));
    CHECK_STR_EQ(shown(0), TR(KP_FS_NOTE_1));
    CHECK_STR_EQ(shown(1), TR(KP_FS_NOTE_2));
    CHECK_STR_EQ(shown(2), TR(SPROG_EDIT_NOTE));
    tap(kst_panel_tab_rect(PG_LOCKED));
    CHECK_STR_EQ(shown(0), TR(KP_LOCKED_NOTE));
    for (int i = 0; kst_panel_shown_note(i) != NULL; ++i) {
        CHECK(strcmp(shown(i), TR(SPROG_EDIT_NOTE)) != 0);
    }
    settings_set(SET_KST_EXPERT, 0.0f);
}

/* Whether @p s is a note on the page shown. */
static bool on_page(const char *s)
{
    for (int i = 0; kst_panel_shown_note(i) != NULL; ++i) {
        if (strcmp(kst_panel_shown_note(i), s) == 0) {
            return true;
        }
    }
    return false;
}

/* The notes' foot: 4px above the alert band, in body pixels. */
#define NOTES_FOOT (UI_ALERT_Y - UI_BAND_H - 4)

/* Both standing lines of the SERVO tab on the page shown, and the notes
 * and the message between the tab's last row and 4px above the alert
 * band. */
static void standing_shown(void)
{
    const gfx_rect_t last = val(PG_SERVO, 3, 1);
    const gfx_rect_t drawn = kst_panel_notes_drawn();

    CHECK(kst_panel_standing_line(0) != NULL);
    CHECK(kst_panel_standing_line(1) != NULL);
    CHECK_EQ(kst_panel_note_page(), 0);
    CHECK(on_page(kst_panel_standing_line(0)));
    CHECK(on_page(kst_panel_standing_line(1)));
    CHECK(drawn.h > 0);
    CHECK(drawn.y >= last.y + last.h);
    CHECK(drawn.y + drawn.h <= NOTES_FOOT);
}

/* The rows the message @p m takes. */
static int message_rows(const kst_prog_msg_t *m)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    const int n = kst_panel_message_lines(m, t);
    int rows = 0;

    for (int i = 0; i < n; ++i) {
        rows += kst_panel_text_rows(t[i]);
    }
    return rows;
}

/* The rows of a journal line with a port or pin number of 3 digits. */
static int journal_rows(ui_text_id_t id)
{
    char line[KST_PANEL_LINE];

    snprintf(line, sizeof(line), ui_tr(id), 255u);
    return kst_panel_text_rows(line);
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

/* The standing lines take up to two rows each and every message up to
 * three lines.  Between the SERVO tab's last row and 4px above the alert
 * band there are 10 rows: the tallest message fits whole in them, and the
 * tallest pair beside it, in English (7 rows) and German (9 rows). */
TEST_CASE(every_message_fits_whole_under_the_servo_tab)
{
    static const unsigned args[] = { 0u, 1u, 2u, 9u, 10u, 99u, 255u, 256u,
                                     0x0101u, 0x0F0Fu, 0xFFFFu };
    static const uint8_t regs[] = { 0x00u, 0x10u, 0xFFu };

    fresh();
    for (int lang = 0; lang < 2; ++lang) {
        const gfx_rect_t last = val(PG_SERVO, 3, 1);
        const int fit = (NOTES_FOOT - (last.y + last.h + 4)) / 20;
        int held = 0;
        int text = 0;

        ui_text_set_language(lang != 0 ? UI_LANG_DE : UI_LANG_EN);
        held = max_int(held, kst_panel_text_rows(TR(KP_M_STOPPED)));
        held = max_int(held, kst_panel_text_rows(TR(KP_WRITING)));
        held = max_int(held, kst_panel_text_rows(TR(KP_JOURNAL_KEPT)));
        held = max_int(held, max_int(journal_rows(TX_KP_M_JOURNAL_ASK),
                                     journal_rows(TX_KP_M_JOURNAL_ASK_PIN))
                             + max_int(kst_panel_text_rows(
                                           TR(KP_M_JOURNAL_SAME)),
                                       kst_panel_text_rows(
                                           TR(KP_M_JOURNAL_DONE))));
        held = max_int(held, kst_panel_text_rows(TR(KP_M_JOURNAL))
                             + max_int(journal_rows(TX_KP_M_JOURNAL_2),
                                       journal_rows(TX_KP_M_JOURNAL_2_PIN)));
        held = max_int(held,
                       kst_panel_text_rows(TR(KP_M_JOURNAL_OTHER))
                       + max_int(journal_rows(TX_KP_M_JOURNAL_OTHER_2),
                                 journal_rows(TX_KP_M_JOURNAL_OTHER_2_PIN)));
        held = max_int(held, kst_panel_text_rows(TR(KP_S_LOCKED))
                             + kst_panel_text_rows(TR(KP_S_LOCKED_2)));
        held = max_int(held, kst_panel_text_rows(TR(KP_DOUBT))
                             + kst_panel_text_rows(TR(KP_M_NEED_READ)));
        held = max_int(held, kst_panel_text_rows(TR(KP_M_READ_ONLY))
                             + kst_panel_text_rows(TR(KP_M_READ_ONLY_2)));
        for (unsigned id = 1u; id < 256u; ++id) {
            for (size_t a = 0; a < sizeof(args) / sizeof(args[0]); ++a) {
                for (size_t r = 0; r < sizeof(regs); ++r) {
                    const kst_prog_msg_t m = { .id = (uint8_t)id,
                                               .reg = regs[r],
                                               .arg = (uint16_t)args[a] };

                    text = max_int(text, message_rows(&m));
                }
            }
        }
        CHECK(held > 0);
        CHECK(text > 0);
        CHECK_EQ(fit, 10);
        CHECK(text <= fit);
        CHECK(held + text <= fit);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* An area 524px tall, a left card down to the body's foot: 10 rows of
 * notes under the SERVO tab.  SERVO's KST screen hands the session 428px,
 * the card over the footer: 6 rows, where the longest message, 5 rows, is
 * drawn whole. */
static const gfx_rect_t k_tall = { UI_MARGIN, UI_MARGIN, 620,
                                   UI_BODY_H - 2 * UI_MARGIN };

/* Every line of the message shown is drawn, and nothing passes 4px above
 * the alert band. */
static void message_whole(void)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    const gfx_rect_t drawn = kst_panel_notes_drawn();
    const gfx_rect_t last = val(PG_SERVO, 3, 1);

    CHECK_EQ(kst_panel_shown_message_lines(),
             kst_panel_message_lines(kst_prog_message(M), t));
    CHECK(drawn.h > 0);
    CHECK(drawn.y >= last.y + last.h);
    CHECK(drawn.y + drawn.h <= NOTES_FOOT);
}

/* A servo in doubt, in German, after READ ALL, in the 524px area: the
 * standing pair (4 rows)
 * and the message (3 rows) leave no row for the count, so the pair yields:
 * its lines page with the other notes, first, and the message stays
 * whole.  The tallest message, 5 rows, beside the pair: the pair's lines,
 * 2 rows each, wait, and every line of the message is drawn.  With no
 * message every note fits. */
TEST_CASE(a_servo_in_doubt_keeps_every_line_of_its_message)
{
    fresh();
    kst_panel_init(k_tall);
    kst_panel_open();
    ui_text_set_language(UI_LANG_DE);
    g_rig.card = true;
    cut_write();
    come_back();
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 1, 0));          /* READ ALL */
    settle();
    CHECK(kst_panel_standing_line(1) != NULL);
    message_whole();
    CHECK(kst_panel_note_pages() > 1);
    CHECK(on_page(kst_panel_standing_line(0)));
    tap(kst_panel_notes_rect());
    CHECK_EQ(kst_panel_note_page(), 1);
    CHECK(on_page(kst_panel_standing_line(1)));
    message_whole();

    {
        const kst_prog_msg_t four = { .id = KST_PROG_MSG_BACKUP_OTHER,
                                      .level = KST_PROG_DANGER,
                                      .reg = 0xFFu,
                                      .seq = M->msg.seq + 1u };
        const kst_prog_msg_t five = { .id = KST_PROG_MSG_SES + 0x0Fu,
                                      .level = KST_PROG_DANGER,
                                      .reg = 0x00u,
                                      .seq = M->msg.seq + 2u };

        CHECK_EQ(message_rows(&four), 4);
        M->msg = four;
        show();
        message_whole();
        CHECK_EQ(message_rows(&five), 5);
        M->msg = five;
        show();
        message_whole();
        CHECK(!on_page(kst_panel_standing_line(0)));
        CHECK(!on_page(kst_panel_standing_line(1)));
    }

    /* No message: on SERVO's left card every note fits on one page. */
    M->msg = (kst_prog_msg_t){ .id = KST_PROG_MSG_NONE,
                               .seq = M->msg.seq + 3u };
    show();
    CHECK_EQ(kst_panel_note_pages(), 1);
    standing_shown();
    CHECK(on_page(kst_panel_status_line(0)));
    ui_text_set_language(UI_LANG_EN);
}

/* The notes of a servo in doubt after READ ALL, in German, on SERVO's
 * left card: the standing pair and the message take two pages. */
static void doubt_paged(void)
{
    fresh();
    kst_panel_open();
    ui_text_set_language(UI_LANG_DE);
    g_rig.card = true;
    cut_write();
    come_back();
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 1, 0));          /* READ ALL */
    settle();
    show();
}

TEST_CASE(the_notes_start_on_their_first_page_again)
{
    doubt_paged();
    CHECK(kst_panel_note_pages() > 1);

    /* Leaving the programmer and opening it again. */
    tap(kst_panel_notes_rect());
    CHECK_EQ(kst_panel_note_page(), 1);
    kst_panel_leave();
    kst_panel_open();
    show();
    CHECK_EQ(kst_panel_note_page(), 0);

    /* Another set of notes: a message of another height. */
    tap(kst_panel_notes_rect());
    CHECK_EQ(kst_panel_note_page(), 1);
    {
        const kst_prog_msg_t two = { .id = KST_PROG_MSG_STOPPED,
                                     .level = KST_PROG_WARN, .reg = 0xFFu,
                                     .seq = M->msg.seq + 1u };

        M->msg = two;
        show();
    }
    CHECK_EQ(kst_panel_note_page(), 0);

    /* The same set paged again stays on its page. */
    if (kst_panel_note_pages() > 1) {
        tap(kst_panel_notes_rect());
        show();
        CHECK_EQ(kst_panel_note_page(), 1);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_text_before_the_area_is_set_still_ends)
{
    kst_panel_init((gfx_rect_t){ 0, 0, 0, 0 });
    CHECK_EQ(kst_panel_text_rows("abc"), 3);
    CHECK_EQ(kst_panel_text_rows(""), 0);
    kst_panel_init((gfx_rect_t){ 0, 0, 20, 0 });
    CHECK_EQ(kst_panel_text_rows("ab c"), 3);
    kst_panel_init(AREA);
}

int main(void)
{
    RUN(the_notes_flow_from_under_the_rows);
    RUN(value_tabs_end_with_the_common_hint);
    RUN(every_message_fits_whole_under_the_servo_tab);
    RUN(a_servo_in_doubt_keeps_every_line_of_its_message);
    RUN(the_notes_start_on_their_first_page_again);
    RUN(a_text_before_the_area_is_set_still_ends);
    return test_summary("kst_panel_notes");
}
