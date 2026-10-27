/**
 * @file test_kst_panel_notes.c
 * @brief The programmer overlay's notes: pages, the standing lines and what the
 * first page holds.
 *
 * Cases:
 * - notes_that_do_not_fit_take_pages_and_none_is_lost
 * - standing_notes_are_on_the_first_page_whole
 * - every_message_fits_whole_under_the_servo_tab
 * - a_servo_in_doubt_keeps_every_line_of_its_message
 * - the_notes_start_on_their_first_page_again
 * - a_text_before_the_area_is_set_still_ends
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/* Notes that fit take one page.  Notes that do not take pages that keep
 * their last row for the count: no page holds more than room - 1 rows,
 * no note is split, and every note is on a page. */
TEST_CASE(notes_that_do_not_fit_take_pages_and_none_is_lost)
{
    int page[5];
    const int fit[3] = { 2, 1, 2 };
    const bool none[5] = { false, false, false, false, false };
    const int many[5] = { 2, 2, 1, 2, 1 };

    CHECK(kst_panel_pack_notes(fit, none, 3, 5, page) == 1);
    CHECK(page[0] == 0 && page[1] == 0 && page[2] == 0);
    for (int room = 3; room <= 7; ++room) {
        const int pages = kst_panel_pack_notes(many, none, 5, room, page);
        int used[5] = { 0 };

        CHECK(pages >= 2);
        for (int i = 0; i < 5; ++i) {
            CHECK(page[i] >= 0 && page[i] < pages);
            CHECK(i == 0 || page[i] >= page[i - 1]);
            used[page[i]] += many[i];
        }
        for (int pg = 0; pg < pages; ++pg) {
            CHECK(used[pg] > 0 && used[pg] <= room - 1);
        }
    }
}

/* State, type, the standing pair and the backup: the SERVO tab's notes
 * at their widest in German. */
TEST_CASE(standing_notes_are_on_the_first_page_whole)
{
    const int rows[5] = { 1, 1, 2, 2, 1 };
    const bool held[5] = { false, false, true, true, false };
    int page[5];

    /* Room for all: one page. */
    CHECK(kst_panel_pack_notes(rows, held, 5, 7, page) == 1);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(page[i], 0);
    }
    /* The standing pair and the count row fill page 0; the others page. */
    CHECK(kst_panel_pack_notes(rows, held, 5, 5, page) == 2);
    CHECK(page[0] == 1 && page[1] == 1 && page[2] == 0 && page[3] == 0
          && page[4] == 1);
    /* One row more: the state goes on page 0 with the pair. */
    CHECK(kst_panel_pack_notes(rows, held, 5, 6, page) == 2);
    CHECK(page[0] == 0 && page[1] == 1 && page[2] == 0 && page[3] == 0
          && page[4] == 1);
    /* No row beside the pair for a count: the pair yields, every note
     * pages in pages of room - 1 rows, the pair first. */
    CHECK(kst_panel_pack_notes(rows, held, 5, 4, page) == 3);
    CHECK(page[2] == 0 && page[3] == 1 && page[0] == 1 && page[1] == 2
          && page[4] == 2);
    CHECK(kst_panel_pack_notes(rows, held, 5, 3, page) == 4);
    CHECK(page[2] == 0 && page[3] == 1 && page[0] == 2 && page[1] == 2
          && page[4] == 3);
    /* A note taller than a page waits for the room to grow. */
    CHECK(kst_panel_pack_notes(rows, held, 5, 2, page) == 3);
    CHECK(page[2] == -1 && page[3] == -1 && page[0] == 0 && page[1] == 1
          && page[4] == 2);
    CHECK(kst_panel_pack_notes(rows, held, 5, 1, page) == 1);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(page[i], -1);
    }
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
    const gfx_rect_t last = val(PG_SERVO, 4, 1);
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
 * band there are 7 rows: the tallest message fits whole in them in both
 * languages, and in English the tallest pair fits beside it.  In German
 * they take 9; the pair yields to the message (kst_panel_pack_notes()). */
TEST_CASE(every_message_fits_whole_under_the_servo_tab)
{
    static const unsigned args[] = { 0u, 1u, 2u, 9u, 10u, 99u, 255u, 256u,
                                     0x0101u, 0x0F0Fu, 0xFFFFu };
    static const uint8_t regs[] = { 0x00u, 0x10u, 0xFFu };

    fresh();
    for (int lang = 0; lang < 2; ++lang) {
        const gfx_rect_t last = val(PG_SERVO, 4, 1);
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
        CHECK_EQ(fit, 7);
        CHECK(text <= fit);
        if (lang == 0) {
            CHECK(held + text <= fit);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* An area 460px tall: 6 rows of notes under the SERVO tab. */
static const gfx_rect_t k_short = { UI_MARGIN, UI_MARGIN, 620, 460 };

/* Every line of the message shown is drawn, and nothing passes 4px above
 * the alert band. */
static void message_whole(void)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    const gfx_rect_t drawn = kst_panel_notes_drawn();
    const gfx_rect_t last = val(PG_SERVO, 4, 1);

    CHECK_EQ(kst_panel_shown_message_lines(),
             kst_panel_message_lines(kst_prog_message(M), t));
    CHECK(drawn.h > 0);
    CHECK(drawn.y >= last.y + last.h);
    CHECK(drawn.y + drawn.h <= NOTES_FOOT);
}

/* A servo in doubt, in German, after READ ALL: the standing pair (4 rows)
 * and the message (3 rows) leave no row for the count, so the pair yields:
 * its lines page with the other notes, first, and the message stays
 * whole.  The tallest message, 5 rows, beside the pair: the pair's lines,
 * 2 rows each, wait, and every line of the message is drawn.  With no
 * message every note fits. */
TEST_CASE(a_servo_in_doubt_keeps_every_line_of_its_message)
{
    fresh();
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

/* The notes of a servo in doubt with no message, in German, in the area
 * 460px tall: two pages. */
static void doubt_no_message(void)
{
    fresh();
    kst_panel_init(k_short);
    kst_panel_open();
    ui_text_set_language(UI_LANG_DE);
    g_rig.card = true;
    cut_write();
    come_back();
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 1, 0));          /* READ ALL */
    settle();
    M->msg = (kst_prog_msg_t){ .id = KST_PROG_MSG_NONE,
                               .seq = M->msg.seq + 1u };
    show();
}

TEST_CASE(the_notes_start_on_their_first_page_again)
{
    doubt_no_message();
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
    RUN(notes_that_do_not_fit_take_pages_and_none_is_lost);
    RUN(standing_notes_are_on_the_first_page_whole);
    RUN(every_message_fits_whole_under_the_servo_tab);
    RUN(a_servo_in_doubt_keeps_every_line_of_its_message);
    RUN(the_notes_start_on_their_first_page_again);
    RUN(a_text_before_the_area_is_set_still_ends);
    return test_summary("kst_panel_notes");
}
