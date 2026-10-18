/*
 * The overlay of the programmer with no card (shared/ui/kst_panel.c): a write,
 * a restore and a release whose card is missing, put in late or pulled, and
 * the record of an interrupted write read from the card.  The bench is
 * kst_panel_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel_rig.h"

/* ------------------------------------------------------------- with no card */

/* The three lines of a write that goes without a file. */
static bool warns_no_file(void)
{
    return warns(TR(KP_NO_FILE_1)) && warns(TR(KP_NO_FILE_2))
        && warns(TR(KP_NO_FILE_3));
}

TEST_CASE(a_first_write_with_no_card_is_sent_after_its_page_says_so)
{
    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        card_out();
        enable();
        edits();
        /* A value past a soft limit, held for at the edit. */
        edit(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) + 2u));
        tap(val(PG_SERVO, 4, 0));
        /* The page opens; nothing is refused. */
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(msg() != KST_PROG_MSG_CARD_NONE);
        CHECK(warns_no_file());
        CHECK(warns(TR(KP_HORN_FREE)));
        CHECK(warns(TR(KP_WRITE_2)));
        CHECK(!warns(TR(KP_WRITE_BACKUP)));
        show();
        /* The one hold of the page, let go early: nothing is sent. */
        hold(1.9f);
        CHECK_EQ(kst_panel_editor(), 3);
        rig_frames(50);
        CHECK_EQ(g_rig.starts, 2);
        CHECK_EQ(g_rig.sim.n_writes, 0);
        CHECK(!kst_prog_no_file(M));
        CHECK_EQ(kst_prog_changes(M), 4);
        /* Held: the write goes, and no second hold is asked for. */
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 0);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK_EQ(kst_prog_changes(M), 0);
        CHECK_EQ(g_rig.fc.saves, 0);
        CHECK_EQ(g_rig.fc.n, 0);
        CHECK(kst_prog_no_file(M));
        CHECK(!kst_prog_backed_up(M));
        /* The SERVO tab's line. */
        tap(kst_panel_tab_rect(PG_SERVO));
        show();
        ui_text_set_language(UI_LANG_EN);
    }
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_FILE_1), TR(KP_NO_FILE_1))
          != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_FILE_2), TR(KP_NO_FILE_2))
          != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_FILE_3), TR(KP_NO_FILE_3))
          != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_BACKUP_FILE),
                 TR(KP_NO_BACKUP_FILE)) != 0);
}

TEST_CASE(a_card_put_in_between_two_writes_takes_the_file_at_the_second)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    connected();
    card_out();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK(warns_no_file());
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(kst_prog_no_file(M));

    card_in();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(!warns_no_file());
    CHECK(!warns(TR(KP_NO_FILE_1)));
    CHECK(warns(TR(KP_WRITE_BACKUP)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
    CHECK_STR_EQ(g_rig.fc.file[0].name, "KST001");
    /* The servo as the session found it, not as the first write left it. */
    CHECK(memcmp(g_rig.fc.file[0].img.r, sv.r, sizeof(sv.r)) == 0);
    CHECK(!kst_prog_no_file(M));
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");
    show();
}

TEST_CASE(a_card_pulled_under_the_write_page_brings_the_page_back)
{
    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_WRITE_BACKUP)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    /* The page promised a file; the card leaves before the hold ends. */
    card_out();
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns_no_file());
    CHECK(!warns(TR(KP_WRITE_BACKUP)));
    rig_frames(50);
    CHECK_EQ(g_rig.starts, 2);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(g_rig.fc.saves, 0);
    /* CANCEL sends nothing either. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
    /* The page's own hold does. */
    tap(val(PG_SERVO, 4, 0));
    CHECK(warns_no_file());
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 0);
    CHECK(kst_prog_no_file(M));
}

TEST_CASE(a_card_pulled_after_the_backup_file_leaves_the_next_write_alone)
{
    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);

    card_out();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    /* The file is saved: only the record of the write stays in memory. */
    CHECK(!warns(TR(KP_NO_FILE_1)));
    CHECK(warns(TR(KP_WRITE_NO_CARD)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
    CHECK(!kst_prog_no_file(M));
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");
}

TEST_CASE(a_restore_with_no_card_shows_its_warnings_and_the_missing_file)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t now;

    fresh();
    connected();
    card_out();
    enable();
    /* Max. Duty lowered: the session's backup raises it past a soft
     * limit. */
    edit(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 2u));
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 2, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    /* The soft limit's page first, as with a card. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_CHECK_1)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    hold(2.1f);
    /* Then the restore's, with the lines of the missing file. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    CHECK(warns_no_file());
    show();
    hold(1.9f);
    CHECK_EQ(kst_panel_editor(), 3);
    rig_frames(50);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) != 0);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) == 0);
    CHECK_EQ(g_rig.fc.saves, 0);
    CHECK(kst_prog_no_file(M));
}

TEST_CASE(a_restore_and_a_release_whose_card_is_pulled_come_back_too)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t now;

    /* RELEASE PAIRING as the session's first write. */
    fresh();
    connected();
    enable();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_RELEASE_1)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    card_out();
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_RELEASE_1)));
    CHECK(warns_no_file());
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    show();
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_EQ(g_rig.fc.saves, 0);
    CHECK(kst_prog_no_file(M));

    /* The session's restore after a write that had no card, the card
     * back in for its page and out again under the hold. */
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    card_in();
    tap(val(PG_SERVO, 2, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    card_out();
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    CHECK(warns_no_file());
    rig_frames(50);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) != 0);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) == 0);
}

static void other_20k(kst_image_t *img)
{
    CHECK(kst_field_set(img, KST_F_20K_SEL,
                        1u - kst_field_get(img, KST_F_20K_SEL)));
}

/* The record's lines name the GPIO the write went through; a record of
 * version 1, one with no pin and one whose pin no IOmcu has name the
 * port.  In every language, in the message and in the lines that stand. */
TEST_CASE(the_record_names_its_pin_and_its_port_without_one)
{
#define REC_IMAGES \
    "journal=00F5F5144877046541A40D805307140096960001010000000000000A00100000\n" \
    "target=00C8C8144877046541A40D805307140096960001010000000000000A00100000\n"
    /* Each check value is from an independent bitwise CRC-16/CCITT-FALSE. */
    static const struct {
        const char *text;
        bool pin;
    } k[] = {
        { "RCBENCH KST 2\n" REC_IMAGES "port=0\npin=0D\ncheck=AA87\n", true },
        { "RCBENCH KST 1\n" REC_IMAGES "port=0\ncheck=C56D\n", false },
        { "RCBENCH KST 2\n" REC_IMAGES "port=0\npin=30\ncheck=374A\n", false },
    };
#undef REC_IMAGES

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
            char want[KST_PANEL_LINE];

            fresh();
            ui_text_set_language((ui_lang_t)lang);
            g_rig.card = true;
            CHECK(kst_panel_file_load(k[i].text, strlen(k[i].text)));
            CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
            if (k[i].pin) {
                snprintf(want, sizeof(want), TR(KP_M_JOURNAL_2_PIN), 13u);
                CHECK(strstr(want, "13") != NULL);
            } else {
                snprintf(want, sizeof(want), TR(KP_M_JOURNAL_2), 1u);
            }
            CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
            CHECK_STR_EQ(t[0], TR(KP_M_JOURNAL));
            CHECK_STR_EQ(t[1], want);
            CHECK_STR_EQ(kst_panel_standing_line(0), TR(KP_M_JOURNAL));
            CHECK_STR_EQ(kst_panel_standing_line(1), want);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_servo_that_is_not_the_one_of_the_interrupted_write_is_named)
{
    char file[KST_PROG_FILE_MAX];
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char second[KST_PANEL_LINE];
    size_t len;
    kst_image_t img;

    fresh();
    g_rig.card = true;
    cut_write();
    len = g_rig.file_len;
    memcpy(file, g_rig.file, len + 1u);

    /* A panel that starts later: the message names the write's pin. */
    fresh();
    g_rig.card = true;
    CHECK(kst_panel_file_load(file, len));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
    snprintf(second, sizeof(second), TR(KP_M_JOURNAL_2_PIN), (unsigned)RIG_PIN);
    CHECK_STR_EQ(t[1], second);
    /* The band's second line holds the record's own line first. */
    CHECK_STR_EQ(kst_panel_standing_line(1), second);
    img = rig_servo();
    other_20k(&img);
    memcpy(g_rig.sim.reg, img.r, sizeof(img.r));
    link_up();
    tap(val(PG_SERVO, 0, 0));           /* CONNECT */
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK_EQ(kst_prog_port(M), RIG_PORT);

    /* Another servo is on it. */
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_STR_EQ(said(), TR(KP_M_JOURNAL_OTHER));
    CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
    snprintf(second, sizeof(second), TR(KP_M_JOURNAL_OTHER_2_PIN),
             (unsigned)RIG_PIN);
    CHECK_STR_EQ(t[1], second);
    /* The band keeps the two lines after the message has gone, in each
     * language. */
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        snprintf(second, sizeof(second), TR(KP_M_JOURNAL_OTHER_2_PIN),
                 (unsigned)RIG_PIN);
        CHECK_STR_EQ(kst_panel_standing_line(0), TR(KP_M_JOURNAL_OTHER));
        CHECK_STR_EQ(kst_panel_standing_line(1), second);
    }
    ui_text_set_language(UI_LANG_EN);
    show();
    /* Nothing is edited, and the switch does not open a write. */
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    tap(kst_panel_tab_rect(PG_SERVO));
    /* RESTORE holds the one entry that discards the record. */
    tap(val(PG_SERVO, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 1);
    CHECK(choice(TR(KP_FORGET)) == 0);
    tap(kst_panel_choice_rect(0));
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(!M->journal);
    CHECK(kst_prog_editable(M));
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

/* A servo that holds what the record says the servo held before the write
 * may be a second one with the same settings: the record stays, the two
 * lines ask with the pin's number, and nothing is written until the record
 * is discarded from RESTORE. */
TEST_CASE(a_servo_that_fits_the_record_is_asked_about)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        char file[KST_PROG_FILE_MAX];
        char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
        char first[KST_PANEL_LINE];
        size_t len;

        ui_text_set_language(UI_LANG_EN);
        fresh();
        g_rig.card = true;
        cut_write();
        len = g_rig.file_len;
        memcpy(file, g_rig.file, len + 1u);

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.card = true;
        CHECK(kst_panel_file_load(file, len));
        /* The twin: the servo as the record's image has it. */
        memcpy(g_rig.sim.reg, M->journal_img.r, sizeof(M->journal_img.r));
        link_up();
        tap(val(PG_SERVO, 0, 0));           /* CONNECT */
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_SAME);
        CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
        snprintf(first, sizeof(first), TR(KP_M_JOURNAL_ASK_PIN),
                 (unsigned)RIG_PIN);
        CHECK_STR_EQ(t[0], first);
        CHECK_STR_EQ(t[1], TR(KP_M_JOURNAL_SAME));
        /* The band keeps the two lines after the message has gone. */
        CHECK_STR_EQ(kst_panel_standing_line(0), first);
        CHECK_STR_EQ(kst_panel_standing_line(1), TR(KP_M_JOURNAL_SAME));
        CHECK(M->journal);
        show();
        /* The switch does not open a write. */
        tap(val(PG_SERVO, 3, 0));
        hold(2.1f);
        CHECK(!kst_prog_write_enabled(M));
        CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_SAME);
        tap(kst_panel_tab_rect(PG_SERVO));
        /* RESTORE holds the one entry that discards the record. */
        tap(val(PG_SERVO, 2, 0));
        CHECK_EQ(kst_panel_editor(), 2);
        CHECK_EQ(choices(), 1);
        CHECK(choice(TR(KP_FORGET)) == 0);
        tap(kst_panel_choice_rect(0));
        CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
        CHECK(!M->journal);
        rig_frames(50);
        CHECK_EQ(g_rig.sim.n_writes, 0);
        enable();

        /* The same with the write's target on the servo. */
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.card = true;
        CHECK(kst_panel_file_load(file, len));
        memcpy(g_rig.sim.reg, M->journal_target.r,
               sizeof(M->journal_target.r));
        link_up();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DONE);
        CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
        CHECK_STR_EQ(t[0], first);
        CHECK_STR_EQ(t[1], TR(KP_M_JOURNAL_DONE));
        CHECK_STR_EQ(kst_panel_standing_line(0), first);
        CHECK_STR_EQ(kst_panel_standing_line(1), TR(KP_M_JOURNAL_DONE));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A card put in after the start: its record is read then, and a record
 * the model holds is not replaced by it. */
TEST_CASE(the_record_of_a_card_put_in_late_is_read)
{
    char file[KST_PROG_FILE_MAX];
    size_t len;

    fresh();
    g_rig.card = true;
    cut_write();
    len = g_rig.file_len;
    memcpy(file, g_rig.file, len + 1u);

    fresh();
    CHECK(!kst_prog_journal(M));
    CHECK(kst_panel_file_late(file, len));
    CHECK(kst_prog_journal(M));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_STR_EQ(said(), TR(KP_M_JOURNAL));
    /* The record stands: the same text again is not taken, and neither
     * is one that cannot be read, with nothing said. */
    CHECK(!kst_panel_file_late(file, len));
    CHECK(!kst_panel_file_late("KST 1\n00=zz\n", 12));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK(kst_prog_journal(M));

    /* No record held, a file that is not readable: said. */
    fresh();
    CHECK(!kst_panel_file_late("KST 1\n00=zz\n", 12));
    CHECK_EQ(msg(), KST_PROG_MSG_FILE_BAD);
}

/* The read after an interrupted write closes the record's prompt and keeps
 * its values for RESTORE: a card put in then, with another record or with
 * none, does not replace or clear them. */
TEST_CASE(a_late_card_does_not_replace_a_record_that_was_read_back)
{
    char other[KST_PROG_FILE_MAX];
    char none[KST_PROG_FILE_MAX];
    size_t other_len;
    size_t none_len;
    kst_image_t before;
    kst_image_t target;
    int at;

    /* The file of another interrupted write, and one with no record: what
     * FORGET leaves on the card. */
    fresh();
    g_rig.card = true;
    cut_write();
    other_len = g_rig.file_len;
    memcpy(other, g_rig.file, other_len + 1u);
    come_back();
    tap(val(PG_SERVO, 1, 0));
    settle();
    tap(val(PG_SERVO, 2, 0));
    at = choice(TR(KP_FORGET));
    CHECK(at >= 0);
    tap(kst_panel_choice_rect(at));
    rig_frames(50);
    none_len = g_rig.file_len;
    memcpy(none, g_rig.file, none_len + 1u);
    CHECK(none_len > 0u);
    CHECK(none_len != other_len || memcmp(none, other, none_len) != 0);

    fresh();
    cut_write();
    come_back();
    tap(val(PG_SERVO, 1, 0));           /* READ ALL */
    settle();
    CHECK(!kst_prog_journal(M));
    CHECK(kst_prog_journal_held(M));
    before = M->journal_img;
    target = M->journal_target;

    CHECK(!kst_panel_file_late(other, other_len));
    CHECK(!kst_panel_file_late(none, none_len));
    CHECK(!kst_panel_file_late("KST 1\n00=zz\n", 12));
    CHECK(msg() != KST_PROG_MSG_FILE_BAD);
    CHECK(kst_prog_journal_held(M));
    CHECK(!kst_prog_journal(M));
    CHECK(memcmp(&before, &M->journal_img, sizeof(before)) == 0);
    CHECK(memcmp(&target, &M->journal_target, sizeof(target)) == 0);
    /* RESTORE still offers the values before the write. */
    tap(val(PG_SERVO, 2, 0));
    CHECK(choice(TR(KP_FROM_JOURNAL)) >= 0);
    tap(kst_panel_choice_cancel_rect());

    /* The same file with no record held is taken: the guard is what
     * kept it out. */
    fresh();
    CHECK(kst_panel_file_late(none, none_len));
    CHECK(!kst_prog_journal_held(M));
}

/* Servo A read, servo B put on the port in the session: READ ALL says
 * that A's backup is not B's, in two lines, and RESTORE does not hold
 * it. */
TEST_CASE(the_backup_of_the_session_is_not_offered_for_another_servo)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];

    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        enable();
        g_rig.sim.reg[0x10] ^= 0x01u;
        tap(val(PG_SERVO, 1, 0));           /* READ ALL */
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_BACKUP_OTHER);
        CHECK_STR_EQ(said(), TR(KP_M_BACKUP_OTHER));
        CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
        CHECK_STR_EQ(t[1], TR(KP_M_BACKUP_OTHER_2));
        CHECK(!kst_prog_write_enabled(M));
        show();
        enable();
        tap(val(PG_SERVO, 2, 0));           /* RESTORE */
        for (int i = 0; i < choices(); ++i) {
            CHECK(strcmp(kst_panel_choice_label(i), TR(KP_FROM_SESSION))
                  != 0);
        }
        rig_frames(50);
        CHECK_EQ(g_rig.sim.n_writes, 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_that_is_not_readable_is_said_and_not_used)
{
    fresh();
    CHECK(!kst_panel_file_load("KST 1\n00=zz\n", 12));
    CHECK_EQ(msg(), KST_PROG_MSG_FILE_BAD);
    CHECK_STR_EQ(said(), TR(KP_M_FILE_BAD));
}

TEST_CASE(more_holds_verify_discard_and_release_pairing)
{
    fresh();
    connected();
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 3);
    /* VERIFY. */
    tap(kst_panel_choice_rect(0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);
    CHECK_STR_EQ(said(), TR(KP_M_VERIFIED));
    /* A servo that changed under the session. */
    g_rig.sim.reg[0x03] = (uint8_t)(g_rig.sim.reg[0x03] - 1u);
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(0));
    settle();
    CHECK(msg() != KST_PROG_MSG_VERIFIED);
    CHECK(said()[0] != '\0');
    tap(val(PG_SERVO, 1, 0));
    settle();
    /* DISCARD EDITS. */
    edits();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(1));
    CHECK_EQ(kst_prog_changes(M), 0);
    /* RELEASE PAIRING needs the switch. */
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(2));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    enable();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(2));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_RELEASE_1)));
    tap(kst_panel_cancel_rect());
    CHECK(g_rig.sim.reg[KST_REG_NODE_ADDR] != 0u);
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(2));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_STR_EQ(said(), TR(KP_M_RELEASED));
    CHECK_EQ(g_rig.sim.reg[KST_REG_NODE_ADDR], 0);
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
}

int main(void)
{
    RUN(a_first_write_with_no_card_is_sent_after_its_page_says_so);
    RUN(a_card_put_in_between_two_writes_takes_the_file_at_the_second);
    RUN(a_card_pulled_under_the_write_page_brings_the_page_back);
    RUN(a_card_pulled_after_the_backup_file_leaves_the_next_write_alone);
    RUN(a_restore_with_no_card_shows_its_warnings_and_the_missing_file);
    RUN(a_restore_and_a_release_whose_card_is_pulled_come_back_too);
    RUN(a_servo_that_is_not_the_one_of_the_interrupted_write_is_named);
    RUN(a_servo_that_fits_the_record_is_asked_about);
    RUN(the_record_names_its_pin_and_its_port_without_one);
    RUN(the_record_of_a_card_put_in_late_is_read);
    RUN(a_late_card_does_not_replace_a_record_that_was_read_back);
    RUN(the_backup_of_the_session_is_not_offered_for_another_servo);
    RUN(a_file_that_is_not_readable_is_said_and_not_used);
    RUN(more_holds_verify_discard_and_release_pairing);
    return test_summary("kst_panel_nocard");
}
