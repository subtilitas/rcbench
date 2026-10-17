/*
 * The servo files in the overlay of the programmer (shared/ui/kst_panel.c):
 * FILES lists the card, a save asks for a name, a load becomes the pending
 * values, and the first write saves the servo to a file.  The bench is
 * kst_panel_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel_rig.h"

/* ---------------------------------------------------------- the servo files */

/* The index of a character key of the text keyboard. */
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

/* The list's entry of the file @p name, -1 for none. */
static int file_choice(const char *name)
{
    const size_t len = strlen(name);

    for (int i = 0; kst_panel_choice_label(i) != NULL; ++i) {
        const char *l = kst_panel_choice_label(i);

        if (strncmp(l, name, len) == 0 && (l[len] == ' ' || l[len] == '\0')) {
            return i;
        }
    }
    return -1;
}

/* FILES, the file @p name, and what the card answers. */
static void pick(const char *name)
{
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK(file_choice(name) > 0);
    tap(kst_panel_choice_rect(file_choice(name)));
    settle();
}

/* A file whose image is the bench servo's with @p id at @p raw. */
/* The raw value a line of an edited file gives for @p raw: the displayed
 * value, read back as a keypad entry is. */
static uint16_t as_typed(kst_field_id_t id, uint16_t raw)
{
    int32_t shown = 0;
    uint16_t back = 0;

    CHECK(kst_field_display(id, raw, &shown));
    CHECK(kst_field_raw_from_display(id, shown, &back));
    return back;
}

static fake_file_t *file_with(const char *name, kst_field_id_t id,
                              uint16_t raw)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, id, raw));
    return fake_card_put(&g_rig.fc, name, &img);
}

/* The bench servo's image as the other servo type. */
static kst_image_t other_type(void)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_set(&img, KST_F_20K_SEL,
                        1u - kst_field_get(&img, KST_F_20K_SEL)));
    return img;
}

TEST_CASE(files_lists_the_card_under_the_entry_that_saves)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    char want[KST_PANEL_LINE];

    fresh();
    connected();
    CHECK_STR_EQ(kst_panel_label(PG_SERVO, 1, 1), TR(KP_FILES));
    /* An empty card: the entry that saves, and the message. */
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 1);
    CHECK_EQ(choice(TR(KP_FILE_SAVE)), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NO_FILES);
    CHECK_STR_EQ(said(), TR(KP_M_NO_FILES));
    CHECK_STR_EQ(kst_panel_choice_title(), TR(KP_FILES_TITLE));
    tap(kst_panel_choice_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);

    /* The names as stored, each with its servo type. */
    (void)fake_card_put(&g_rig.fc, "KST001", &sv);
    (void)fake_card_put(&g_rig.fc, "WING", &ot);
    fake_card_put(&g_rig.fc, "HAND", &sv)->type = 0;
    files();
    CHECK_EQ(choices(), 4);
    /* Sorted by name.  A file that names no type shows none. */
    CHECK_STR_EQ(kst_panel_choice_label(1), "HAND");
    snprintf(want, sizeof(want), "%-16s V%u", "KST001", kst_card_type(&sv));
    CHECK_STR_EQ(kst_panel_choice_label(2), want);
    snprintf(want, sizeof(want), "%-16s V%u", "WING", kst_card_type(&ot));
    CHECK_STR_EQ(kst_panel_choice_label(3), want);
    CHECK_EQ(kst_card_type(&sv) + kst_card_type(&ot), 14);
    tap(kst_panel_choice_cancel_rect());
    rig_frames(20);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(more_files_than_the_list_holds_are_shown_in_turn)
{
    const kst_image_t sv = sim_bench_image();
    char title[64];

    fresh();
    connected();
    for (unsigned i = 0; i < 7u; ++i) {
        char name[KST_PROG_NAME_MAX];

        snprintf(name, sizeof(name), "F%u", i);
        (void)fake_card_put(&g_rig.fc, name, &sv);
    }
    files();
    /* The entry that saves, five files, the entry for the next. */
    CHECK_EQ(choices(), 7);
    CHECK_EQ(file_choice("F0"), 1);
    CHECK_EQ(file_choice("F4"), 5);
    CHECK_EQ(choice(TR(KP_FILES_NEXT)), 6);
    /* The last entry ends above CANCEL. */
    CHECK(kst_panel_choice_rect(6).y + kst_panel_choice_rect(6).h
          <= kst_panel_choice_cancel_rect().y);
    tap(kst_panel_choice_rect(6));
    settle();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 4);
    CHECK_EQ(file_choice("F5"), 1);
    CHECK_EQ(file_choice("F6"), 2);
    snprintf(title, sizeof(title), TR(KP_FILES_TITLE_N), 6u, 7u, 7u);
    CHECK_STR_EQ(kst_panel_choice_title(), title);
    /* Behind the last file the list starts again. */
    tap(kst_panel_choice_rect(3));
    settle();
    CHECK_EQ(choices(), 7);
    CHECK_EQ(file_choice("F0"), 1);
    snprintf(title, sizeof(title), TR(KP_FILES_TITLE_N), 1u, 5u, 7u);
    CHECK_STR_EQ(kst_panel_choice_title(), title);
    /* A file of the second page loads. */
    g_rig.fc.file[6].img.r[KST_REG_DUTY]--;
    g_rig.fc.file[6].img.r[KST_REG_DUTY_COPY]--;
    tap(kst_panel_choice_rect(6));
    settle();
    tap(kst_panel_choice_rect(file_choice("F6")));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_changes(M), 1);
}

TEST_CASE(the_list_waits_for_the_card_and_opens_once)
{
    fresh();
    connected();
    g_rig.fc.silent = true;
    tap(val(PG_SERVO, 1, 1));
    rig_frames(5);
    tick(0.02f);
    show();
    CHECK(kst_prog_card_busy(M));
    CHECK_EQ(kst_panel_editor(), 0);
    /* A second tap asks for no second list. */
    tap(val(PG_SERVO, 1, 1));
    rig_frames(5);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.fc.lists, 1);
    /* A card that does not answer is given up. */
    rig_frames(KST_PROG_CARD_MS + 10u);
    tick(0.02f);
    show();
    CHECK(!kst_prog_card_busy(M));
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_STR_EQ(said(), TR(KP_M_CARD_READ));
    CHECK_EQ(kst_panel_editor(), 0);
    /* A list asked for as the link goes opens nothing. */
    g_rig.fc.silent = false;
    tap(val(PG_SERVO, 1, 1));
    kst_link_lost(&g_rig.link);
    settle();
    CHECK_EQ(kst_panel_editor(), 0);
}

TEST_CASE(a_save_asks_for_the_name_with_the_next_free_one_preset)
{
    char want[KST_PANEL_LINE];

    fresh();
    connected();
    save_entry();
    CHECK_STR_EQ(kst_panel_textkey()->title, TR(KP_NAME_TITLE));
    CHECK_STR_EQ(kst_panel_textkey()->text, "KST001");
    tkey(UI_TK_OK);
    CHECK_EQ(kst_panel_editor(), 0);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    snprintf(want, sizeof(want), TR(KP_M_SAVED), "KST001");
    CHECK_STR_EQ(said(), want);
    CHECK_EQ(g_rig.fc.n, 1);
    CHECK_STR_EQ(g_rig.fc.file[0].name, "KST001");
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");

    /* The next preset, and a name typed over it. */
    save_entry();
    CHECK_STR_EQ(kst_panel_textkey()->text, "KST002");
    type_name("WING-L_1");
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    snprintf(want, sizeof(want), TR(KP_M_SAVED), "WING-L_1");
    CHECK_STR_EQ(said(), want);
    CHECK_STR_EQ(g_rig.fc.file[1].name, "WING-L_1");

    /* The keyboard takes as many characters as a name has. */
    save_entry();
    tkey(UI_TK_CLR);
    for (unsigned i = 0; i < KST_PANEL_NAME_TYPED + 3u; ++i) {
        tkey(tk_key('A'));
    }
    CHECK_EQ(strlen(kst_panel_textkey()->text), KST_PANEL_NAME_TYPED);
    /* CANCEL saves nothing. */
    tkey(UI_TK_CANCEL);
    CHECK_EQ(kst_panel_editor(), 0);
    rig_frames(5);
    CHECK_EQ(g_rig.fc.n, 2);
    CHECK_EQ(g_rig.fc.saves, 2);
    /* Nothing of this reached the servo. */
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(a_name_the_card_refuses_comes_back_to_the_keyboard)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    connected();
    (void)fake_card_put(&g_rig.fc, "WING", &sv);
    /* A name a file has: no file is replaced, the name is changed. */
    save_entry();
    type_name("WING");
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_EXISTS);
    CHECK_STR_EQ(said(), TR(KP_M_NAME_EXISTS));
    CHECK_EQ(kst_panel_editor(), 4);
    CHECK_STR_EQ(kst_panel_textkey()->title, TR(KP_NAME_EXISTS));
    CHECK_STR_EQ(kst_panel_textkey()->text, "WING");
    CHECK_EQ(g_rig.fc.n, 1);
    tkey(tk_key('2'));
    tkey(UI_TK_OK);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_STR_EQ(g_rig.fc.file[1].name, "WING2");

    /* A name no file takes: a device name of a desktop computer. */
    save_entry();
    type_name("CON");
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    CHECK_STR_EQ(said(), TR(KP_M_NAME_BAD));
    CHECK_EQ(kst_panel_editor(), 4);
    CHECK_STR_EQ(kst_panel_textkey()->title, TR(KP_NAME_BAD));
    CHECK_STR_EQ(kst_panel_textkey()->text, "CON");
    tkey(UI_TK_CANCEL);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.fc.n, 2);
}

TEST_CASE(every_failure_of_the_card_has_its_line)
{
    static const struct {
        uint8_t result;
        unsigned msg;
        ui_text_id_t text;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE, TX_KP_M_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL, TX_KP_M_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE,
          TX_KP_M_CARD_WRITE },
        { KST_PROG_CARD_DENIED, KST_PROG_MSG_CARD_DENIED,
          TX_KP_FILE_DENIED },
    };
    const kst_image_t sv = sim_bench_image();

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        /* A save. */
        for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            g_rig.fc.fails = KST_PROG_CARD_OK;
            save_entry();
            g_rig.fc.fails = k[i].result;
            tkey(UI_TK_OK);
            settle();
            CHECK_EQ(msg(), k[i].msg);
            CHECK_STR_EQ(said(), ui_tr(k[i].text));
            CHECK_EQ(kst_panel_editor(), 0);
            CHECK(!kst_prog_backed_up(M));
        }
        /* The list. */
        g_rig.fc.fails = KST_PROG_CARD_READ_ERR;
        files();
        CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
        CHECK_STR_EQ(said(), TR(KP_M_CARD_READ));
        CHECK_EQ(kst_panel_editor(), 0);
        /* A file that went between the list and the tap. */
        g_rig.fc.fails = KST_PROG_CARD_OK;
        (void)fake_card_put(&g_rig.fc, "GONE", &sv);
        files();
        CHECK_EQ(kst_panel_editor(), 2);
        g_rig.fc.n = 0;
        tap(kst_panel_choice_rect(1));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
        /* No card in the slot. */
        g_rig.card = false;
        rig_frame();
        tap(val(PG_SERVO, 1, 1));
        CHECK_EQ(msg(), KST_PROG_MSG_CARD_NONE);
        CHECK_STR_EQ(said(), TR(KP_M_CARD_NONE));
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK(!kst_prog_card_busy(M));
        CHECK_EQ(g_rig.sim.n_writes, 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_becomes_the_pending_values_and_the_ordinary_write_sends_them)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];

    fresh();
    connected();
    (void)file_with("LOW", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 1u));
    pick("LOW");
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(lines(t), 2);
    snprintf(want, sizeof(want), TR(KP_M_LOADED_ONE), "LOW");
    CHECK_STR_EQ(t[0], want);
    CHECK_STR_EQ(t[1], TR(KP_EDIT_NOTE_2));
    CHECK_EQ(draft(KST_F_DUTY), servo(KST_F_DUTY) - 1u);
    CHECK_EQ(kst_prog_changed(M), KST_FIELD_BIT(KST_F_DUTY));
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    /* The values are written as edits are: the switch, the page, the
     * hold. */
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_ENABLE_1)));
    tap(kst_panel_cancel_rect());
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    enable();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_HORN_FREE)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo(KST_F_DUTY), draft(KST_F_DUTY));
    CHECK_EQ(servo(KST_F_DUTY), sim_bench_image().r[KST_REG_DUTY] - 1u);
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
}

TEST_CASE(an_edited_file_is_named_as_one_and_its_locked_values_are_counted)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];
    unsigned of = 0;

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        kst_image_t ot = other_type();

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        fake_file_t *f = file_with("ED", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 10u));
        f->kind = KST_PROG_FILE_EDITED;
        pick("ED");
        CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
        CHECK_EQ(lines(t), 2);
        /* How many of the 27 fields its lines give. */
        const unsigned named = kst_prog_load_named(M, &of);
        CHECK(named >= 1u && named <= 27u);
        CHECK_EQ(of, 27);
        snprintf(want, sizeof(want), TR(KP_FILE_EDITED_N), named, 27u);
        CHECK_STR_EQ(t[1], want);
        /* A card's side that counts none: the line without the count. */
        kst_prog_discard(M);
        g_rig.fc.uncounted = true;
        pick("ED");
        CHECK_EQ(lines(t), 2);
        CHECK_STR_EQ(t[1], TR(KP_FILE_EDITED));
        g_rig.fc.uncounted = false;
        kst_prog_discard(M);

        /* No type and another 20k value: loaded, the type stays the
         * servo's and is counted as locked, not written. */
        CHECK(kst_field_edit(&ot, KST_F_DUTY,
                             (uint16_t)(servo(KST_F_DUTY) - 20u)));
        f = fake_card_put(&g_rig.fc, "UN", &ot);
        f->kind = KST_PROG_FILE_UNTYPED;
        f->type = 0;
        pick("UN");
        CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
        CHECK_EQ(lines(t), 3);
        snprintf(want, sizeof(want), TR(KP_M_LOADED_ONE), "UN");
        CHECK_STR_EQ(t[0], want);
        snprintf(want, sizeof(want), TR(KP_FILE_UNTYPED_N),
                 kst_prog_load_named(M, NULL), 27u);
        CHECK_STR_EQ(t[1], want);
        snprintf(want, sizeof(want), "%s", TR(KP_FILE_STAY_ONE));
        CHECK_STR_EQ(t[2], want);
        CHECK_EQ(kst_prog_load_skipped(M), KST_FIELD_BIT(KST_F_20K_SEL));
        CHECK_EQ(draft(KST_F_20K_SEL), servo(KST_F_20K_SEL));
        CHECK_EQ(kst_prog_changed(M), KST_FIELD_BIT(KST_F_DUTY));
        /* The LOCKED tab draws with it. */
        tap(kst_panel_tab_rect(PG_LOCKED));
        show();
        tap(kst_panel_tab_rect(PG_SERVO));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The servo is of the other type when @p servo_other, the file of the type
 * that is not the servo's. */
static void type_mismatch(bool servo_other, ui_lang_t lang)
{
    char want[KST_PANEL_LINE];
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    const unsigned t_file = kst_card_type(servo_other ? &sv : &ot);
    const unsigned t_servo = kst_card_type(servo_other ? &ot : &sv);

    fresh();
    ui_text_set_language(lang);
    if (servo_other) {
        memcpy(g_rig.sim.reg, ot.r, sizeof(ot.r));
    }
    connected();
    (void)fake_card_put(&g_rig.fc, "X", servo_other ? &sv : &ot);
    pick("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_TYPE);
    snprintf(want, sizeof(want), TR(KP_M_LOAD_TYPE), t_file, t_servo);
    CHECK_STR_EQ(said(), want);
    CHECK_EQ(t_file + t_servo, 14);
    if (lang == UI_LANG_EN) {
        CHECK_STR_EQ(said(), t_file == 6u
                                 ? "File is V6, servo is V8. Not loaded."
                                 : "File is V8, servo is V6. Not loaded.");
    } else {
        CHECK(strstr(said(), t_file == 6u ? "V6" : "V8") != NULL);
        CHECK(strstr(said(), t_servo == 6u ? "V6" : "V8") != NULL);
        CHECK(strcmp(TR(KP_M_LOAD_TYPE),
                     ui_tr_in(UI_LANG_EN, TX_KP_M_LOAD_TYPE)) != 0);
    }
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_of_the_other_servo_type_is_denied_naming_both)
{
    type_mismatch(false, UI_LANG_EN);
    type_mismatch(true, UI_LANG_EN);
    type_mismatch(false, UI_LANG_DE);
    type_mismatch(true, UI_LANG_DE);
}

TEST_CASE(the_servo_type_is_shown_once_the_servo_is_read)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    char want[24];

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        for (int other = 0; other < 2; ++other) {
            const unsigned type = kst_card_type(other != 0 ? &ot : &sv);

            fresh();
            ui_text_set_language((ui_lang_t)lang);
            if (other != 0) {
                memcpy(g_rig.sim.reg, ot.r, sizeof(ot.r));
            }
            link_up();
            /* Nothing read: no type. */
            CHECK_STR_EQ(kst_panel_label(PG_SERVO, 1, 0), "SERVO");
            tap(val(PG_SERVO, 0, 0));
            settle();
            snprintf(want, sizeof(want), TR(KP_SERVO_V), type);
            CHECK_STR_EQ(kst_panel_label(PG_SERVO, 1, 0), want);
            CHECK(strstr(want, type == 6u ? "V6" : "V8") != NULL);
            /* The 20k field is the type, and takes no tap. */
            tap(kst_panel_tab_rect(PG_LOCKED));
            CHECK_STR_EQ(kst_panel_label(PG_LOCKED, 0, 0), TR(KP_TYPE));
            tap(val(PG_LOCKED, 0, 0));
            CHECK_EQ(kst_panel_editor(), 0);
            show();
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_that_does_not_read_says_why)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        fake_file_t *f = file_with("X", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 1u));
        f->result = KST_PROG_CARD_BAD;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
        CHECK_EQ(lines(t), 1);
        CHECK_STR_EQ(t[0], TR(KP_M_LOAD_BAD));
        f->line = 7;
        pick("X");
        snprintf(want, sizeof(want), TR(KP_M_LOAD_LINE), 7u);
        CHECK_STR_EQ(said(), want);
        /* A wrong checksum: damaged, and how it loads all the same. */
        f->result = KST_PROG_CARD_CHECKSUM;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_CHECKSUM);
        CHECK_EQ(lines(t), 2);
        CHECK_STR_EQ(t[0], TR(KP_M_LOAD_CHECKSUM));
        CHECK_STR_EQ(t[1], TR(KP_M_LOAD_CHECKSUM_2));
        f->result = KST_PROG_CARD_NEWER;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_NEWER);
        CHECK_STR_EQ(said(), TR(KP_M_LOAD_NEWER));
        /* A value past a hard limit: its line and its field. */
        f->result = KST_PROG_CARD_VALUE;
        f->field = KST_F_BOOST;
        f->line = 5;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_VALUE);
        snprintf(want, sizeof(want), TR(KP_M_LOAD_VALUE), 5u,
                 kst_panel_label(PG_TUNE, 1, 1));
        CHECK_EQ(lines(t), 1);
        CHECK_STR_EQ(t[0], want);
        CHECK(strstr(t[0], "BOOST") != NULL);
        f->field = 0xFFu;
        pick("X");
        snprintf(want, sizeof(want), TR(KP_M_LOAD_VALUE_ANY), 5u);
        CHECK_STR_EQ(said(), want);
        CHECK_EQ(kst_prog_changes(M), 0);
        CHECK_EQ(kst_panel_editor(), 0);

        /* A value the hard limits of the programmer refuse: the rule's
         * line. */
        (void)file_with("HARD", KST_F_DUTY, 0xFFu);
        pick("HARD");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_REFUSED);
        CHECK_EQ(lines(t), 2);
        CHECK_STR_EQ(t[0], TR(KP_M_LOAD_REFUSED));
        CHECK_STR_EQ(t[1], kst_panel_rule_text(KST_H_DUTY_HIGH));
        CHECK_EQ(kst_prog_changes(M), 0);
        rig_frames(50);
        CHECK_EQ(g_rig.sim.n_writes, 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_past_a_soft_limit_waits_for_a_2_s_hold)
{
    char want[KST_PANEL_LINE];
    fake_file_t *f;

    fresh();
    connected();
    /* 5 steps of 255: more than the 1 % an edited file's line states. */
    CHECK(as_typed(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) + 5u))
          > servo(KST_F_DUTY));
    f = file_with("UP", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) + 5u));
    f->kind = KST_PROG_FILE_EDITED;
    pick("UP");
    /* The page of a soft limit, with the file's name and kind. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SOFT);
    snprintf(want, sizeof(want), TR(KP_FILE_DIFF_ONE), "UP");
    CHECK_STR_EQ(kst_panel_warning_line(0), want);
    snprintf(want, sizeof(want), TR(KP_FILE_EDITED_N),
             kst_prog_load_named(M, NULL), 27u);
    CHECK(warns(want));
    CHECK(warns(kst_panel_rule_text(KST_S_DUTY_RAISED)));
    CHECK_EQ(kst_prog_changes(M), 0);
    /* Short of 2 s nothing is taken. */
    hold(1.9f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* CANCEL leaves the draft as it was. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!kst_prog_soft_is_load(M));
    CHECK_EQ(kst_prog_changes(M), 0);

    pick("UP");
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
    CHECK_EQ(draft(KST_F_DUTY),
             as_typed(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) + 5u)));
    /* Pending, not written: the write still needs its switch. */
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    tap(val(PG_SERVO, 4, 0));
    CHECK(warns(TR(KP_ENABLE_1)));
    tap(kst_panel_cancel_rect());
    CHECK(!kst_prog_write_enabled(M));

    /* The page goes with the link, and the file with it. */
    kst_prog_discard(M);
    pick("UP");
    CHECK_EQ(kst_panel_editor(), 3);
    rig_link_lost();
    tick(0.02f);
    show();
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!kst_prog_soft_is_load(M));
}

/* Values the bench servo's are above, far from, or other than. */
static void lower_many(kst_image_t *img)
{
    const uint16_t duty = (uint16_t)(kst_field_get(img, KST_F_DUTY) - 10u);

    CHECK(kst_field_set(img, KST_F_DUTY, duty));
    CHECK(kst_field_set(img, KST_F_DUTY_COPY, duty));
    CHECK(kst_field_set(img, KST_F_BOOST,
                        (uint16_t)(kst_field_get(img, KST_F_BOOST) - 4u)));
    CHECK(kst_field_set(img, KST_F_DEAD_BAND,
                        (uint16_t)(kst_field_get(img, KST_F_DEAD_BAND) + 4u)));
    CHECK(kst_field_set(img, KST_F_LEFT_ANGLE,
                        (uint16_t)(kst_field_get(img, KST_F_LEFT_ANGLE) - 5u)));
    CHECK(kst_field_set(img, KST_F_REVERSION,
                        1u - kst_field_get(img, KST_F_REVERSION)));
    CHECK(kst_field_set(img, KST_F_NEUTRAL,
                        (uint16_t)(kst_field_get(img, KST_F_NEUTRAL) + 300u)));
    CHECK(kst_field_set(img, KST_F_PULSE_LOWER,
                        (uint16_t)(kst_field_get(img, KST_F_PULSE_LOWER)
                                   + 1200u)));
}

TEST_CASE(a_file_with_more_warnings_than_lines_counts_the_rest)
{
    char more[KST_PANEL_LINE];
    kst_image_t sv = sim_bench_image();
    kst_image_t img = sv;
    kst_rules_t soft;
    unsigned texts = 0, n = 0, values;
    int16_t seen[KST_RULE_COUNT];

    fresh();
    lower_many(&img);
    /* Two switches more, each with a warning of its own. */
    CHECK(kst_field_set(&img, KST_F_SOFT_START, 1u));
    CHECK(kst_field_set(&sv, KST_F_SOFT_START, 0u));
    CHECK(kst_field_set(&img, KST_F_ALLOW_UNCONT, 0u));
    CHECK(kst_field_set(&sv, KST_F_ALLOW_UNCONT, 1u));
    memcpy(g_rig.sim.reg, img.r, sizeof(img.r));
    connected();
    (void)fake_card_put(&g_rig.fc, "MANY", &sv);
    pick("MANY");
    CHECK_EQ(kst_panel_editor(), 3);
    soft = kst_prog_soft_rules(M);
    /* The texts the rules have between them, two rules to a text where
     * the text is the same. */
    for (unsigned rule = 0; rule < (unsigned)KST_RULE_COUNT; ++rule) {
        bool twice = false;

        if ((soft & KST_RULE_BIT(rule)) == 0u) {
            continue;
        }
        for (unsigned i = 0; i < texts; ++i) {
            twice = twice || strcmp(kst_panel_rule_text((unsigned)seen[i]),
                                    kst_panel_rule_text(rule)) == 0;
        }
        if (!twice) {
            seen[texts++] = (int16_t)rule;
        }
    }
    CHECK(texts > 7u);
    while (kst_panel_warning_line((int)n) != NULL) {
        ++n;
    }
    /* The file; three values and the count of the rest; six warnings and
     * the count of the rest: the 12 lines the page has. */
    CHECK_EQ(n, 12);
    values = 0;
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        values += id != KST_F_DUTY_COPY
               && kst_field_get(&sv, (kst_field_id_t)id)
                      != kst_field_get(&img, (kst_field_id_t)id);
    }
    CHECK(values > 4u);
    snprintf(more, sizeof(more), TR(KP_VALUES_MORE), values - 3u);
    CHECK_STR_EQ(kst_panel_warning_line(4), more);
    /* The values come in the order of the tabs, the servo's value first;
     * one at fault for a warning is in the warning colour. */
    CHECK(strncmp(kst_panel_warning_line(1), "DEAD BAND: ", 11) == 0);
    CHECK_EQ(kst_panel_warning_level(1), KST_PROG_INFO);
    CHECK(strncmp(kst_panel_warning_line(2), "MAX. DUTY: ", 11) == 0);
    CHECK_EQ(kst_panel_warning_level(2), KST_PROG_WARN);
    snprintf(more, sizeof(more), TR(KP_CHECK_MORE), texts - 6u);
    CHECK_STR_EQ(kst_panel_warning_line(11), more);
    for (unsigned i = 0; i < 6u; ++i) {
        CHECK(warns(kst_panel_rule_text((unsigned)seen[i])));
    }
    CHECK(!warns(kst_panel_rule_text((unsigned)seen[6])));
    /* Every line is inside the page, above its buttons. */
    show();
    /* CANCEL leaves the servo and the draft as they are. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(a_file_that_holds_the_servos_values_changes_nothing)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];
    const kst_image_t sv = sim_bench_image();
    kst_image_t pair = sv;

    fresh();
    connected();
    (void)fake_card_put(&g_rig.fc, "SAME", &sv);
    pick("SAME");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(lines(t), 1);
    snprintf(want, sizeof(want), TR(KP_M_LOAD_SAME), "SAME");
    CHECK_STR_EQ(t[0], want);
    /* Only a locked value differs: the pairing value. */
    pair.r[KST_REG_NODE_ADDR] ^= 0x22u;
    (void)fake_card_put(&g_rig.fc, "PAIR", &pair);
    pick("PAIR");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(lines(t), 2);
    snprintf(want, sizeof(want), "%s", TR(KP_FILE_STAY_ONE));
    CHECK_STR_EQ(t[1], want);
    CHECK_EQ(kst_prog_changes(M), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(a_load_waits_for_a_servo_that_is_read)
{
    const kst_image_t sv = sim_bench_image();

    /* No servo connected: FILES takes no tap. */
    fresh();
    link_up();
    (void)fake_card_put(&g_rig.fc, "F", &sv);
    tap(val(PG_SERVO, 1, 1));
    rig_frames(5);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.fc.lists, 0);

    /* A servo that fails the fingerprint: listed, never loaded onto. */
    fresh();
    g_rig.sim.reg[0x06] = 0x05u;
    link_up();
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    (void)fake_card_put(&g_rig.fc, "F", &sv);
    pick("F");
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(g_rig.fc.loads, 0);
    CHECK_EQ(kst_prog_changes(M), 0);
}

/* ------------------------------------------- the file before the first write */

TEST_CASE(the_first_write_saves_the_servo_to_a_file_without_asking)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_WRITE_BACKUP)));
    hold(2.1f);
    /* No name is asked for. */
    CHECK_EQ(kst_panel_editor(), 0);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
    CHECK_EQ(g_rig.starts_at_save, 2);
    CHECK_STR_EQ(g_rig.fc.file[0].name, "KST001");
    CHECK(memcmp(g_rig.fc.file[0].img.r, sv.r, sizeof(sv.r)) == 0);
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");
    show();
    /* The second write of the session takes no file. */
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(!warns(TR(KP_WRITE_BACKUP)));
    CHECK(warns(TR(KP_WRITE_CARD)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
}

TEST_CASE(a_first_write_whose_file_fails_is_not_sent)
{
    static const struct {
        uint8_t result;
        unsigned msg;
        ui_text_id_t text;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE, TX_KP_M_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL, TX_KP_M_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE,
          TX_KP_M_CARD_WRITE },
        { KST_PROG_CARD_DENIED, KST_PROG_MSG_CARD_DENIED,
          TX_KP_FILE_DENIED },
    };
    const unsigned starts = 2u;

    fresh();
    connected();
    enable();
    edits();
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        g_rig.fc.fails = k[i].result;
        if (!kst_prog_write_enabled(M)) {
            enable();
        }
        tap(val(PG_SERVO, 4, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), k[i].msg);
        CHECK_STR_EQ(said(), ui_tr(k[i].text));
        CHECK_EQ(g_rig.sim.n_writes, 0);
        CHECK_EQ(g_rig.starts, starts);
        CHECK_EQ(kst_prog_changes(M), 3);
        CHECK(!kst_prog_journal(M));
    }
    /* The card takes the file: the same write goes. */
    g_rig.fc.fails = KST_PROG_CARD_OK;
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.n, 1);
}

int main(void)
{
    RUN(files_lists_the_card_under_the_entry_that_saves);
    RUN(more_files_than_the_list_holds_are_shown_in_turn);
    RUN(the_list_waits_for_the_card_and_opens_once);
    RUN(a_save_asks_for_the_name_with_the_next_free_one_preset);
    RUN(a_name_the_card_refuses_comes_back_to_the_keyboard);
    RUN(every_failure_of_the_card_has_its_line);
    RUN(a_file_becomes_the_pending_values_and_the_ordinary_write_sends_them);
    RUN(an_edited_file_is_named_as_one_and_its_locked_values_are_counted);
    RUN(a_file_of_the_other_servo_type_is_denied_naming_both);
    RUN(the_servo_type_is_shown_once_the_servo_is_read);
    RUN(a_file_that_does_not_read_says_why);
    RUN(a_file_past_a_soft_limit_waits_for_a_2_s_hold);
    RUN(a_file_with_more_warnings_than_lines_counts_the_rest);
    RUN(a_file_that_holds_the_servos_values_changes_nothing);
    RUN(a_load_waits_for_a_servo_that_is_read);
    RUN(the_first_write_saves_the_servo_to_a_file_without_asking);
    RUN(a_first_write_whose_file_fails_is_not_sent);
    return test_summary("kst_panel_files");
}
