/*
 * The servo files and backups in the Hitec programmer's overlay
 * (shared/ui/hitec_panel_file.c): FILES in place of SERIES while the port
 * is held, its pages of 5 and their turn back to the first, the keyboard of
 * a new file's name with the next proposed name and the names the card
 * refuses, a load into the draft, CHECK THIS FILE for a soft rule and for
 * another model, RESTORE of the session's backup with the count of cells
 * of no setting it writes back, FACTORY DEFAULT from MORE, both refused
 * with an edit standing, the backup file's lines on the write page and on the SERVO tab,
 * the state line while the backup file is saved, a list the card does not
 * give in 5000ms, FILES without a card, and every new page and message in
 * both languages inside the card.  The rig is hitec_panel_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include "card_state.h"
#include "hitec_card.h"
#include "ui_card_text.h"
#include "ui_textkey.h"

/* ---------------------------------------------------------------- helpers */

/* Whether a line of the open page is @p s. */
static bool page_has(const char *s)
{
    for (int i = 1; hitec_panel_page_line(i) != NULL; ++i) {
        if (strcmp(hitec_panel_page_line(i), s) == 0) {
            return true;
        }
    }
    return false;
}

/* Whether a note or the message of the tab shown is @p s. */
static bool note_has(const char *s)
{
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        if (strcmp(hitec_panel_note(i), s) == 0) {
            return true;
        }
    }
    return false;
}

/* The list's entry labelled @p label tapped. */
static void pick(const char *label)
{
    for (int i = 0; hitec_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(hitec_panel_choice_label(i), label) == 0) {
            tap(hitec_panel_choice_rect(i));
            return;
        }
    }
    T_FAIL("no entry %s", label);
}

/* The list's entries. */
static int choices(void)
{
    int n = 0;

    while (hitec_panel_choice_label(n) != NULL) {
        ++n;
    }
    return n;
}

/* The list's entry of the file @p name, -1 for none. */
static int file_choice(const char *name)
{
    const size_t len = strlen(name);

    for (int i = 0; hitec_panel_choice_label(i) != NULL; ++i) {
        const char *l = hitec_panel_choice_label(i);

        if (strncmp(l, name, len) == 0 && (l[len] == ' ' || l[len] == '\0')) {
            return i;
        }
    }
    return -1;
}

/* The card's answer: 3 frames, then drawn. */
static void settle(void)
{
    prun(60u);
    show();
}

/* FILES on the SERVO tab and the card's list. */
static void files(void)
{
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_SERVO, 1, 0), TR(KP_FILES));
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    settle();
}

/* A key of the keyboard. */
static void tkey(int key)
{
    tap(ui_textkey_key_rect(hitec_panel_textkey(), key));
}

/* The index of a character key of the keyboard. */
static int tk_key(char ch)
{
    static const char chars[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.";
    const char *p = strchr(chars, ch);

    return p != NULL ? (int)(p - chars) : -1;
}

/* Clear the name, type @p s and OK. */
static void type_name(const char *s)
{
    tkey(UI_TK_CLR);
    for (; *s != '\0'; ++s) {
        tkey(tk_key(*s));
    }
    tkey(UI_TK_OK);
}

/* A D485HW file with parameter @p id at @p v. */
static void d_file(const char *name, uint8_t id, int32_t v)
{
    hitec_image_t img;

    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, id, v), HITEC_SET_OK);
    files_put_image(name, &img);
}

/* The overlay on a D485HW read, a card in, in @p lang. */
static void d_open(ui_lang_t lang)
{
    open_at(SUPPLY_ON, lang);
    servo_d485();
    tap_connect();
    g_card = true;
    prun(20u);
    show();
}

/* The overlay on an HS-5645MG read, a card in. */
static void c_open(void)
{
    open_en();
    servo_hs5645();
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    g_card = true;
    prun(20u);
    show();
}

/* The line @p id formatted with @p a. */
static const char *fmt_s(ui_text_id_t id, const char *a)
{
    static char t[HITEC_PANEL_LINE];

    snprintf(t, sizeof(t), ui_tr(id), a);
    return t;
}

/* Whether every line of the open page lies above its button. */
static bool page_fits(void)
{
    int y = AREA.y + 64;   /* HP_PAGE_TOP */

    for (int i = 1; hitec_panel_page_line(i) != NULL; ++i) {
        y += hitec_panel_text_rows(hitec_panel_page_line(i)) * 20 + 8;
    }
    return y - 8 <= hitec_panel_apply_rect().y - 4
           && gfx_text_cells(hitec_panel_page_line(0)) * 20 + 24 <= AREA.w;
}

/* ------------------------------------------------------------------ FILES */

TEST_CASE(files_lists_the_card_in_pages_of_5_under_the_entry_that_saves)
{
    char name[16];
    char want[HITEC_PANEL_LINE];

    d_open(UI_LANG_EN);
    /* A tap on FILES asks the panel to look at the slot. */
    (void)hitec_panel_card_look();
    files();
    CHECK(hitec_panel_card_look());
    CHECK(!hitec_panel_card_look());
    /* An empty card: the entry that saves, and the message. */
    CHECK_EQ(hitec_panel_editor(), 2);
    CHECK_EQ(choices(), 1);
    CHECK_STR_EQ(hitec_panel_choice_label(0), TR(KP_FILE_SAVE));
    CHECK_STR_EQ(said(), TR(KP_M_NO_FILES));
    hitec_panel_back();
    CHECK_EQ(hitec_panel_editor(), 0);
    /* 7 files: 5 a page, the next page's entry. */
    for (unsigned i = 1; i <= 7u; ++i) {
        snprintf(name, sizeof(name), "F%u.TXT", i);
        d_file(name, HITEC_P_D_SPEED, 50);
    }
    files();
    CHECK_EQ(choices(), 7);
    snprintf(want, sizeof(want), TR(KP_FILES_TITLE_N), 1u, 5u, 7u);
    CHECK_STR_EQ(hitec_panel_choice_title(), want);
    snprintf(want, sizeof(want), "%-16s  %s", "F1", "D485HW");
    CHECK_STR_EQ(hitec_panel_choice_label(1), want);
    CHECK_STR_EQ(hitec_panel_choice_label(6), TR(KP_FILES_NEXT));
    pick(TR(KP_FILES_NEXT));
    settle();
    CHECK_EQ(choices(), 4);
    snprintf(want, sizeof(want), TR(KP_FILES_TITLE_N), 6u, 7u, 7u);
    CHECK_STR_EQ(hitec_panel_choice_title(), want);
    CHECK(file_choice("F7") > 0);
    /* After the last page: the first. */
    pick(TR(KP_FILES_NEXT));
    settle();
    CHECK(file_choice("F1") == 1);
    /* Nothing of this reached the servo. */
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(files_without_a_card_or_without_its_answer)
{
    d_open(UI_LANG_EN);
    g_card = false;
    prun(20u);
    files();
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_STR_EQ(said(), ui_card_text(CARD_STATE_NONE, UI_CARD_AT_KP_MSG));
    /* The list does not come within 5000ms. */
    g_card = true;
    g_files_slow = true;
    prun(20u);
    files();
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 1, 0));
    prun(HITEC_PROG_FILES_MS);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_CARD_READ));
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 1, 0));
    /* Its late answer opens nothing. */
    files_answer();
    show();
    CHECK_EQ(hitec_panel_editor(), 0);
}

TEST_CASE(a_save_takes_the_proposed_name_or_one_typed)
{
    hitec_image_t img;

    d_open(UI_LANG_EN);
    files();
    pick(TR(KP_FILE_SAVE));
    CHECK_EQ(hitec_panel_editor(), 4);
    CHECK_STR_EQ(hitec_panel_textkey()->title, TR(KP_NAME_TITLE));
    CHECK_STR_EQ(hitec_panel_textkey()->text, "HITEC001");
    tkey(UI_TK_OK);
    settle();
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_STR_EQ(said(), fmt_s(TX_KP_M_SAVED, "HITEC001"));
    CHECK(files_image("HITEC001.TXT", &img));
    /* The first save of the session is its backup file. */
    CHECK(note_has(fmt_s(TX_KP_BACKUP_FILE, "HITEC001")));
    /* A name typed; the keyboard takes 16 characters. */
    files();
    pick(TR(KP_FILE_SAVE));
    CHECK_STR_EQ(hitec_panel_textkey()->text, "HITEC002");
    tkey(UI_TK_CLR);
    for (unsigned i = 0; i < 20u; ++i) {
        tkey(tk_key('A'));
    }
    CHECK_EQ(strlen(hitec_panel_textkey()->text), 16u);
    type_name("WING-L_1");
    settle();
    CHECK_STR_EQ(said(), fmt_s(TX_KP_M_SAVED, "WING-L_1"));
    /* CANCEL saves nothing. */
    files();
    pick(TR(KP_FILE_SAVE));
    tkey(UI_TK_CANCEL);
    CHECK_EQ(hitec_panel_editor(), 0);
    settle();
    CHECK_EQ(g_files_saves, 2u);
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(a_name_the_card_refuses_comes_back_to_the_keyboard)
{
    d_open(UI_LANG_EN);
    d_file("WING.TXT", HITEC_P_D_SPEED, 50);
    files();
    pick(TR(KP_FILE_SAVE));
    type_name("WING");
    settle();
    CHECK_STR_EQ(said(), TR(KP_M_NAME_EXISTS));
    CHECK_EQ(hitec_panel_editor(), 4);
    CHECK_STR_EQ(hitec_panel_textkey()->title, TR(KP_NAME_EXISTS));
    CHECK_STR_EQ(hitec_panel_textkey()->text, "WING");
    tkey(tk_key('2'));
    tkey(UI_TK_OK);
    settle();
    CHECK_STR_EQ(said(), fmt_s(TX_KP_M_SAVED, "WING2"));
    /* A device name of a desktop computer, and the record's name. */
    files();
    pick(TR(KP_FILE_SAVE));
    type_name("CON");
    settle();
    CHECK_STR_EQ(said(), TR(KP_M_NAME_BAD));
    CHECK_EQ(hitec_panel_editor(), 4);
    CHECK_STR_EQ(hitec_panel_textkey()->title, TR(KP_NAME_BAD));
    type_name("HITEC");
    settle();
    CHECK_EQ(hitec_panel_editor(), 4);
    CHECK_STR_EQ(hitec_panel_textkey()->title, TR(KP_NAME_BAD));
    /* An empty name: refused at once. */
    type_name("");
    CHECK_EQ(hitec_panel_editor(), 4);
    tkey(UI_TK_CANCEL);
    CHECK_EQ(hitec_panel_editor(), 0);
}

/* ------------------------------------------------------------------ loads */

TEST_CASE(a_file_loads_into_the_draft_and_marks_its_values)
{
    hitec_image_t img;
    char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
    char want[HITEC_PANEL_LINE];

    d_open(UI_LANG_EN);
    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START, 60),
             HITEC_SET_OK);
    CHECK(hitec_image_put_reg(&img, 0x64, 7u));
    files_put_image("SOFT.TXT", &img);
    files();
    tap(hitec_panel_choice_rect(file_choice("SOFT")));
    settle();
    CHECK_EQ(hitec_panel_editor(), 0);
    snprintf(want, sizeof(want), TR(KP_M_LOADED_ONE), "SOFT");
    CHECK_STR_EQ(said(), want);
    CHECK_EQ(hitec_panel_message_lines(hitec_prog_message(model()), t), 2);
    CHECK_STR_EQ(t[1], TR(HP_LOAD_SKIPPED_ONE));
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_MOTION, 2, 0), "*60% (6)");
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_SERVO, 4, 1), "WRITE 1 VALUE");
    CHECK_EQ(g_drv.d_writes, 0u);
}

TEST_CASE(check_this_file_holds_a_soft_rule_and_another_model)
{
    hitec_image_t img;
    char want[HITEC_PANEL_LINE];

    d_open(UI_LANG_EN);
    d_file("END.TXT", HITEC_P_D_LEFT_END, 3300);
    files();
    tap(hitec_panel_choice_rect(file_choice("END")));
    settle();
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_FILE_TITLE));
    snprintf(want, sizeof(want), TR(KP_FILE_DIFF_ONE), "END");
    CHECK(page_has(want));
    CHECK(page_fits());
    CHECK_STR_EQ(said(), TR(KP_M_LOAD_SOFT));
    /* CANCEL: the draft as it was. */
    tap(hitec_panel_cancel_rect());
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK(!hitec_prog_load_waits(model()));
    /* The hold loads it. */
    files();
    tap(hitec_panel_choice_rect(file_choice("END")));
    settle();
    hold(2.1f);
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    hitec_prog_discard(model());
    /* Another model of the D series. */
    d485(&img);
    CHECK(hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34999u));
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
             HITEC_SET_OK);
    files_put_image("OTHER.TXT", &img);
    files();
    tap(hitec_panel_choice_rect(file_choice("OTHER")));
    settle();
    CHECK_EQ(hitec_panel_editor(), 3);
    snprintf(want, sizeof(want), TR(HP_LOAD_MODEL), "D series 34999",
             "D485HW");
    CHECK(page_has(want));
    CHECK(page_has(TR(HP_LOAD_MODEL_2)));
    CHECK(page_fits());
    /* BACK drops it. */
    hitec_panel_back();
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK(!hitec_prog_load_waits(model()));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

TEST_CASE(each_refusal_of_a_load_says_why)
{
    char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
    hitec_image_t img;
    char want[HITEC_PANEL_LINE];

    d_open(UI_LANG_EN);
    hs5645(&img);
    files_put_image("C.TXT", &img);
    files();
    tap(hitec_panel_choice_rect(file_choice("C")));
    settle();
    snprintf(want, sizeof(want), TR(HP_LOAD_SERIES), TR(HP_SERIES_C),
             TR(HP_SERIES_D));
    CHECK_STR_EQ(said(), want);
    /* A cut file. */
    files_put("CUT.TXT", "RCBENCH HITEC 1\nseries=D\n", 25u);
    files();
    tap(hitec_panel_choice_rect(file_choice("CUT")));
    settle();
    CHECK_EQ(hitec_panel_message_lines(hitec_prog_message(model()), t), 2);
    CHECK_STR_EQ(t[0], TR(HP_LOAD_END));
    CHECK_STR_EQ(t[1], TR(HP_LOAD_HAND));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

/* ---------------------------------------------------------------- backups */

TEST_CASE(the_write_page_and_the_servo_tab_name_the_backup_file)
{
    d_open(UI_LANG_EN);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK(page_has(TR(KP_WRITE_BACKUP)));
    CHECK(!page_has(TR(KP_NO_FILE_2)));
    g_files_slow = true;
    hold(2.1f);
    CHECK_EQ(pstate(), HITEC_PROG_BACKUP);
    show();
    CHECK(note_has(TR(HP_ST_BACKUP)));
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0), TR(KP_ABORT));
    files_answer();
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
    CHECK(note_has(fmt_s(TX_KP_BACKUP_FILE, "HITEC001")));
}

TEST_CASE(a_write_without_a_card_leaves_the_backup_in_ram)
{
    d_open(UI_LANG_EN);
    g_card = false;
    prun(20u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    CHECK(page_has(TR(KP_NO_FILE_1)));
    CHECK(page_has(TR(KP_NO_FILE_2)));
    CHECK(page_has(TR(KP_NO_FILE_3)));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK(note_has(ui_card_text(CARD_STATE_NONE, UI_CARD_AT_KP_NO_BACKUP)));
}

TEST_CASE(a_card_taken_out_after_the_backup_file_leaves_the_record_in_ram)
{
    d_open(UI_LANG_EN);
    card_in();
    g_card = false;
    prun(20u);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    CHECK(page_has(TR(KP_WRITE_NO_CARD)));
    CHECK(!page_has(TR(KP_NO_FILE_2)));
    CHECK(!page_has(TR(KP_WRITE_BACKUP)));
    tap(hitec_panel_cancel_rect());
}

TEST_CASE(restore_and_factory_default_with_an_edit_standing_say_why)
{
    char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];

    c_open();
    card_in();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    pick(TR(HP_MORE_FACTORY));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(msg(), HITEC_PROG_MSG_EDITS_PENDING);
    /* A write of the edit, then RESTORE with another edit standing. */
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 30),
             HITEC_PROG_EDIT_OK);
    tap(val(HITEC_PANEL_SERVO, 4, 0));
    pick(TR(KP_FROM_SESSION));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(msg(), HITEC_PROG_MSG_EDITS_PENDING);
    CHECK(hitec_panel_message_lines(hitec_prog_message(model()), t) >= 1);
    CHECK_EQ(g_drv.c_writes, 2u);
}

TEST_CASE(restore_after_factory_default_counts_the_cells_of_no_setting)
{
    char want[HITEC_PANEL_LINE];

    open_en();
    servo_hs5645();
    /* The factory block holds another cell 0x00, which no setting names. */
    g_drv.eeprom[0x30] = (uint8_t)(g_drv.eeprom[0x00] + 1u);
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    g_card = true;
    prun(20u);
    card_in();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    pick(TR(HP_MORE_FACTORY));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(HP_FACTORY_DONE));
    CHECK_EQ(g_drv.eeprom[0x00], g_drv.eeprom[0x30]);
    tap(val(HITEC_PANEL_SERVO, 4, 0));
    pick(TR(KP_FROM_SESSION));
    snprintf(want, sizeof(want), TR(HP_RESTORE_RAW), 1u);
    CHECK(page_has(want));
    CHECK(page_fits());
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_RESTORED));
    CHECK_EQ(g_drv.eeprom[0x00], fix_hs5645()[0x00]);
}

TEST_CASE(factory_default_counts_the_cells_of_no_setting_it_writes)
{
    char want[HITEC_PANEL_LINE];

    open_en();
    servo_hs5645();
    /* The factory block equals the settings but for cell 0x1D. */
    for (unsigned c = 0; c < 0x24u; ++c) {
        g_drv.eeprom[0x30u + c] = g_drv.eeprom[c];
    }
    g_drv.eeprom[0x30u + 0x1Du] ^= 0x01u;
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    g_card = true;
    prun(20u);
    card_in();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    pick(TR(HP_MORE_FACTORY));
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(HP_FACTORY));
    snprintf(want, sizeof(want), TR(HP_FACTORY_RAW), 1u);
    CHECK(page_has(want));
    CHECK(page_fits());
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(HP_FACTORY_DONE));
    CHECK_EQ(g_drv.eeprom[0x1D], g_drv.eeprom[0x4D]);
}

TEST_CASE(restore_writes_the_session_backup_back_from_its_page)
{
    d_open(UI_LANG_EN);
    card_in();
    /* Nothing written: RESTORE has nothing to offer. */
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 4, 0));
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_EQ(g_drv.dreg[0x60], 6u);
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 4, 0));
    tap(val(HITEC_PANEL_SERVO, 4, 0));
    CHECK_EQ(choices(), 1);
    CHECK_STR_EQ(hitec_panel_choice_label(0), TR(KP_FROM_SESSION));
    pick(TR(KP_FROM_SESSION));
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_RESTORE_TITLE));
    CHECK(page_has(TR(KP_FROM_SESSION)));
    CHECK(page_has(TR(KP_RESTORE_UNCHECKED)));
    CHECK(page_has("SOFT START: 60% -> 20%"));
    CHECK(page_fits());
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_RESTORED));
    CHECK_EQ(g_drv.dreg[0x60], fix_d485()[0x60 / 2]);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 4, 0));
}

TEST_CASE(factory_default_comes_from_more_for_a_classic_servo)
{
    c_open();
    card_in();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    pick(TR(HP_MORE_FACTORY));
    /* The switch's page first. */
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_ENABLE_TITLE));
    hold(2.1f);
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    pick(TR(HP_MORE_FACTORY));
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(HP_FACTORY));
    CHECK(page_has(TR(HP_FACTORY_1)));
    CHECK(page_has(TR(HP_FACTORY_2)));
    CHECK(page_has(TR(KP_HORN_FREE)));
    CHECK(page_fits());
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(HP_FACTORY_DONE));
    CHECK_EQ(g_drv.eeprom[0x13], 0x74u);
    /* The D series has none. */
    d_open(UI_LANG_EN);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    for (int i = 0; hitec_panel_choice_label(i) != NULL; ++i) {
        CHECK(strcmp(hitec_panel_choice_label(i), TR(HP_MORE_FACTORY)) != 0);
    }
}

/* ------------------------------------------------------- both languages */

TEST_CASE(every_file_message_has_its_lines_in_both_languages)
{
    static const ui_lang_t langs[] = { UI_LANG_EN, UI_LANG_DE };
    static const struct {
        uint8_t  id;
        uint16_t arg, reg;
    } k_msg[] = {
        { HITEC_PROG_MSG_SAVED, 0, 0 },
        { HITEC_PROG_MSG_CARD_NONE, CARD_STATE_NO_FAT, 0 },
        { HITEC_PROG_MSG_CARD_NONE, CARD_STATE_UNREADABLE, 0 },
        { HITEC_PROG_MSG_CARD_FULL, 0, 0 },
        { HITEC_PROG_MSG_CARD_WRITE, 0, 0 },
        { HITEC_PROG_MSG_CARD_READ, 0, 0 },
        { HITEC_PROG_MSG_CARD_DENIED, 0, 0 },
        { HITEC_PROG_MSG_NO_FILES, 0, 0 },
        { HITEC_PROG_MSG_NAME_BAD, 0, 0 },
        { HITEC_PROG_MSG_NAME_EXISTS, 0, 0 },
        { HITEC_PROG_MSG_LOAD_BAD, 0, 0 },
        { HITEC_PROG_MSG_LOAD_DAMAGED, HITEC_CARD_ERR_CHECK, 0 },
        { HITEC_PROG_MSG_LOAD_DAMAGED, HITEC_CARD_ERR_TRUNCATED, 0 },
        { HITEC_PROG_MSG_LOAD_LINE, 5, 0 },
        { HITEC_PROG_MSG_LOAD_SERIES, HITEC_CLASSIC, 0 },
        { HITEC_PROG_MSG_LOAD_LAYOUT, HITEC_LAYOUT_HS7, HITEC_LAYOUT_HS5 },
        { HITEC_PROG_MSG_LOAD_LAYOUT, HITEC_LAYOUT_D, HITEC_LAYOUT_HS7 },
        { HITEC_PROG_MSG_LOAD_SAME, 16, 0 },
        { HITEC_PROG_MSG_LOAD_CHECK, 0, 0 },
        { HITEC_PROG_MSG_LOADED, 12, 99 },
        { HITEC_PROG_MSG_LOADED, 1, 1 },
        { HITEC_PROG_MSG_BACKUP_RESTORED, 0, 0 },
        { HITEC_PROG_MSG_FACTORY_DONE, 0, 0 },
        { HITEC_PROG_MSG_LOAD_SAME, 0, 0 },
    };

    for (size_t k = 0; k < sizeof(langs) / sizeof(langs[0]); ++k) {
        d_open(langs[k]);
        /* The longest name a file has. */
        CHECK_EQ(hitec_prog_save(model(), "ABCDEFGHIJKLMNOP"),
                 HITEC_PROG_MSG_NONE);
        settle();
        for (size_t i = 0; i < sizeof(k_msg) / sizeof(k_msg[0]); ++i) {
            char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
            const hitec_prog_msg_t m = { k_msg[i].id, HITEC_PROG_INFO,
                                         k_msg[i].reg, k_msg[i].arg, 1u };
            const int n = hitec_panel_message_lines(&m, t);
            int rows = 0;

            CHECK(n >= 1);
            for (int l = 0; l < n; ++l) {
                CHECK(t[l][0] != '\0');
                CHECK(strchr(t[l], '%') == NULL);
                rows += hitec_panel_text_rows(t[l]);
            }
            /* The message band holds 4 rows. */
            CHECK(rows <= 4);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(every_new_page_fits_in_both_languages)
{
    static const ui_lang_t langs[] = { UI_LANG_EN, UI_LANG_DE };

    for (size_t k = 0; k < sizeof(langs) / sizeof(langs[0]); ++k) {
        hitec_image_t img;

        /* CHECK THIS FILE: another model and two rules. */
        d_open(langs[k]);
        d485(&img);
        CHECK(hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34999u));
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END,
                                 3300), HITEC_SET_OK);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_RIGHT_END,
                                 13000), HITEC_SET_OK);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
                 HITEC_SET_OK);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START,
                                 60), HITEC_SET_OK);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_ID, 3),
                 HITEC_SET_OK);
        files_put_image("ABCDEFGHIJKLMNOP.TXT", &img);
        files();
        tap(hitec_panel_choice_rect(file_choice("ABCDEFGHIJKLMNOP")));
        settle();
        CHECK_EQ(hitec_panel_editor(), 3);
        CHECK(page_fits());
        tap(hitec_panel_cancel_rect());
        /* The write page without a card, its three lines. */
        g_card = false;
        prun(20u);
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
                 HITEC_PROG_EDIT_OK);
        CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
        tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        tap(val(HITEC_PANEL_SERVO, 4, 1));
        CHECK(page_has(TR(KP_NO_FILE_2)));
        CHECK(page_fits());
        tap(hitec_panel_cancel_rect());
        /* RESTORE of the backup and FACTORY DEFAULT of a classic servo. */
        open_at(SUPPLY_ON, langs[k]);
        servo_hs5645();
        tap(val(HITEC_PANEL_SERVO, 1, 0));
        tap(hitec_panel_choice_rect(1));
        tap_connect();
        g_card = true;
        prun(20u);
        card_in();
        CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
        tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        tap(val(HITEC_PANEL_SERVO, 2, 0));
        pick(TR(HP_MORE_FACTORY));
        CHECK_STR_EQ(hitec_panel_page_line(0), TR(HP_FACTORY));
        CHECK(page_fits());
        hold(2.1f);
        until(HITEC_PROG_HELD);
        show();
        tap(val(HITEC_PANEL_SERVO, 4, 0));
        pick(TR(KP_FROM_SESSION));
        CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_RESTORE_TITLE));
        CHECK(page_fits());
        tap(hitec_panel_cancel_rect());
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The warning of each soft rule, as hitec_panel_page.c's k_rule. */
static const ui_text_id_t k_rule_text[HITEC_RULE_COUNT] = {
    TX_KP_W_SPD_SLOW, TX_KP_W_DEAD_BAND_NARROWED, TX_HP_SOFT_OVERLOAD,
    TX_KP_W_END_MOVED, TX_KP_W_NEUTRAL_MOVED, TX_KP_W_REVERSION_CHANGED,
    TX_KP_W_UNCONT_POS_FAR,
};

TEST_CASE(every_warning_of_check_this_file_stands_above_its_hold)
{
    static const ui_lang_t langs[] = { UI_LANG_EN, UI_LANG_DE };

    for (size_t k = 0; k < sizeof(langs) / sizeof(langs[0]); ++k) {
        static const struct {
            uint8_t id;
            int32_t v;
        } k_set[] = {
            { HITEC_P_D_SPEED, 10 },
            { HITEC_P_D_DEAD_BAND, 1 },
            { HITEC_P_D_OVERLOAD, HITEC_OVERLOAD_OFF },
            { HITEC_P_D_LEFT_END, 3000 },
            { HITEC_P_D_RIGHT_END, 13300 },
            { HITEC_P_D_NEUTRAL, 7000 },
            { HITEC_P_D_DIRECTION, HITEC_CCW },
            { HITEC_P_D_FAILSAFE, 2000 },
            { HITEC_P_D_SOFT_START, 60 },
            { HITEC_P_D_ID, 3 },
        };
        hitec_image_t img;
        bool other = false;
        uint16_t rules;
        uint16_t seen = 0;
        bool seen_model = false;
        int parts = 0;

        /* A servo with a dead band of 3 and overload protection at 30%,
         * and a file of another model that changes every value it can,
         * past all 7 rules, under the longest file name. */
        open_at(SUPPLY_ON, langs[k]);
        d485(&img);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND,
                                 3), HITEC_SET_OK);
        CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD,
                                 30), HITEC_SET_OK);
        for (unsigned i = 0; i < 128u; ++i) {
            g_drv.dreg[2u * i] = hitec_image_reg(&img, (uint8_t)(2u * i));
        }
        tap_connect();
        g_card = true;
        prun(20u);
        show();
        CHECK(hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34999u));
        for (size_t i = 0; i < sizeof(k_set) / sizeof(k_set[0]); ++i) {
            CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, k_set[i].id,
                                     k_set[i].v), HITEC_SET_OK);
        }
        files_put_image("ABCDEFGHIJKLMNOP.TXT", &img);
        files();
        tap(hitec_panel_choice_rect(file_choice("ABCDEFGHIJKLMNOP")));
        settle();
        CHECK_EQ(hitec_panel_editor(), 3);
        rules = hitec_prog_load_pending(model(), &other);
        CHECK(other);
        CHECK_EQ(rules, (1u << HITEC_RULE_COUNT) - 1u);
        /* Each part above its hold; HOLD 2s TO CONTINUE to the next, the
         * load waiting until the last part's HOLD 2s TO LOAD. */
        for (int part = 0; part < 4; ++part) {
            CHECK_EQ(hitec_panel_editor(), 3);
            CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_FILE_TITLE));
            CHECK(page_fits());
            if (page_has(TR(HP_LOAD_MODEL_2))) {
                seen_model = true;
            }
            for (unsigned r = 0; r < (unsigned)HITEC_RULE_COUNT; ++r) {
                if (page_has(ui_tr(k_rule_text[r]))) {
                    seen |= HITEC_RULE_BIT(r);
                }
            }
            if (strcmp(hitec_panel_apply_label(), TR(KP_HOLD_NEXT)) != 0) {
                break;
            }
            ++parts;
            hold(2.1f);
            CHECK(hitec_prog_load_waits(model()));
            CHECK_EQ(hitec_prog_changes(model()), 0u);
        }
        CHECK(parts >= (langs[k] == UI_LANG_DE ? 1 : 0));
        CHECK_STR_EQ(hitec_panel_apply_label(), TR(KP_HOLD_LOAD));
        CHECK_EQ(seen, rules);
        CHECK(seen_model);
        if (k == 0u) {
            hold(2.1f);
            CHECK_EQ(hitec_panel_editor(), 0);
            CHECK(hitec_prog_changes(model()) > 0u);
        } else {
            /* CANCEL on the last part drops the load. */
            tap(hitec_panel_cancel_rect());
            CHECK(!hitec_prog_load_waits(model()));
            CHECK_EQ(hitec_prog_changes(model()), 0u);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(files_lists_the_card_in_pages_of_5_under_the_entry_that_saves);
    RUN(files_without_a_card_or_without_its_answer);
    RUN(a_save_takes_the_proposed_name_or_one_typed);
    RUN(a_name_the_card_refuses_comes_back_to_the_keyboard);
    RUN(a_file_loads_into_the_draft_and_marks_its_values);
    RUN(check_this_file_holds_a_soft_rule_and_another_model);
    RUN(each_refusal_of_a_load_says_why);
    RUN(the_write_page_and_the_servo_tab_name_the_backup_file);
    RUN(a_write_without_a_card_leaves_the_backup_in_ram);
    RUN(a_card_taken_out_after_the_backup_file_leaves_the_record_in_ram);
    RUN(restore_and_factory_default_with_an_edit_standing_say_why);
    RUN(restore_after_factory_default_counts_the_cells_of_no_setting);
    RUN(factory_default_counts_the_cells_of_no_setting_it_writes);
    RUN(restore_writes_the_session_backup_back_from_its_page);
    RUN(factory_default_comes_from_more_for_a_classic_servo);
    RUN(every_file_message_has_its_lines_in_both_languages);
    RUN(every_new_page_fits_in_both_languages);
    RUN(every_warning_of_check_this_file_stands_above_its_hold);
    free(fb);
    return test_summary("hitec_panel_file");
}
