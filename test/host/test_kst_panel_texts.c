/*
 * The programmer overlay's texts: a line for every message in English and
 * German, every state drawn in both, and the rows a wrapped text takes.
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/* ---------------------------------------------------------------- the texts */

static bool lines_ok(unsigned id, unsigned arg, uint8_t reg)
{
    const kst_prog_msg_t m = { .id = (uint8_t)id, .level = KST_PROG_WARN,
                               .reg = reg, .arg = (uint16_t)arg, .seq = 1 };
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    const int n = kst_panel_message_lines(&m, t);
    bool ok = n >= 1 && n <= KST_PANEL_MSG_LINES;

    for (int i = 0; i < n; ++i) {
        /* A line wraps onto two rows of the card's 58 cells at most. */
        ok = ok && t[i][0] != '\0' && kst_panel_text_rows(t[i]) <= 2;
        ok = ok && strchr(t[i], '%') == NULL;
    }
    return ok;
}

/* Every id of @p from .. @p from + n - 1 has a text of its own kind. */
static bool family_ok(unsigned from, unsigned n)
{
    bool ok = true;

    for (unsigned i = 0; i < n; ++i) {
        char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
        char unknown[KST_PANEL_LINE];
        const kst_prog_msg_t m = { .id = (uint8_t)(from + i), .reg = 0xFFu };

        snprintf(unknown, sizeof(unknown), TR(KP_M_UNKNOWN), from + i);
        ok = ok && lines_ok(from + i, 3, 0xFFu) && lines_ok(from + i, 3, 0x1Fu);
        (void)kst_panel_message_lines(&m, t);
        ok = ok && strcmp(t[0], unknown) != 0;
    }
    return ok;
}

static void texts_in(ui_lang_t lang)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    kst_prog_msg_t m = { .id = KST_PROG_MSG_NONE };

    ui_text_set_language(lang);
    CHECK_EQ(kst_panel_message_lines(&m, t), 0);
    /* The fixed messages, the answers of start, the outcome kinds, the
     * port's refusals and the session's results. */
    CHECK(family_ok(1u, (unsigned)KST_PROG_MSG_NEED_DISARM));
    CHECK(family_ok(KST_PROG_MSG_START, (unsigned)KST_LINK_START_NO_OP + 1u));
    CHECK(family_ok(KST_PROG_MSG_OUT, (unsigned)KST_LINK_OUT_DROPPED + 1u));
    CHECK(family_ok(KST_PROG_MSG_REF, (unsigned)LINK_KST_REF_PORT + 1u));
    CHECK(family_ok(KST_PROG_MSG_REF + LINK_KST_REF_UNSAFE,
                    (unsigned)LINK_KST_REF_SUPPLY_LOW + 1u
                        - (unsigned)LINK_KST_REF_UNSAFE));
    m.id = (uint16_t)(KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_LOW);
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK_STR_EQ(t[0], TR(KP_R_SUPPLY_LOW));
    /* A leave the port refuses for a servo in doubt names the way out. */
    m.id = (uint16_t)(KST_PROG_MSG_REF + LINK_KST_REF_LOCKED);
    m.reg = 0xFFu;
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[0], TR(KP_R_LOCKED));
    CHECK_STR_EQ(t[1], TR(KP_M_LEAVE_SUPPLY_2));
    m.id = (uint16_t)(KST_PROG_MSG_REF + LINK_KST_REF_LEAVE_MS);
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK_STR_EQ(t[0], TR(KP_R_LEAVE_MS));
    m.id = (uint16_t)(KST_PROG_MSG_START + KST_LINK_START_NO_OP);
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK_STR_EQ(t[0], TR(KP_ST_NO_OP));
    /* 3 and 4 are refusals no port gives: they are a line all the same. */
    for (unsigned gap = 3u; gap < (unsigned)LINK_KST_REF_UNSAFE; ++gap) {
        m.id = (uint8_t)(KST_PROG_MSG_REF + gap);
        CHECK_EQ(kst_panel_message_lines(&m, t), 1);
        CHECK(t[0][0] != '\0');
    }
    /* The supply's result and the planner's two. */
    CHECK(family_ok(KST_PROG_MSG_SES + LINK_KST_RESULT_SUPPLY, 3u));
    m.id = (uint16_t)(KST_PROG_MSG_SES + LINK_KST_RESULT_NO_PATH);
    m.reg = 0xFFu;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK_STR_EQ(t[0], TR(KP_R_PLAN_NO_PATH));
    m.id = (uint16_t)(KST_PROG_MSG_SES + LINK_KST_RESULT_TOO_COMPLEX);
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[0], TR(KP_S_TOO_COMPLEX));
    CHECK_STR_EQ(t[1], TR(KP_S_TOO_COMPLEX_2));
    CHECK(family_ok(KST_PROG_MSG_SES, (unsigned)KST_SES_ERR_ABORTED + 1u));
    /* A hard limit's message is the rule's line. */
    for (unsigned rule = 0; rule < (unsigned)KST_RULE_COUNT; ++rule) {
        m.id = KST_PROG_MSG_EDIT_HARD;
        m.arg = (uint16_t)rule;
        CHECK(kst_panel_rule_text(rule)[0] != '\0');
        CHECK(kst_panel_text_rows(kst_panel_rule_text(rule)) <= 2);
        CHECK_EQ(kst_panel_message_lines(&m, t), 1);
        CHECK_STR_EQ(t[0], kst_panel_rule_text(rule));
        m.id = KST_PROG_MSG_RESTORE_REFUSED;
        CHECK_EQ(kst_panel_message_lines(&m, t), 2);
        CHECK_STR_EQ(t[1], kst_panel_rule_text(rule));
    }
    CHECK_STR_EQ(kst_panel_rule_text((unsigned)KST_RULE_COUNT),
                 TR(KP_M_OUTSIDE));
    /* A session the port ended says what to do next. */
    m.id = KST_PROG_MSG_SESSION_ENDED;
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[0], TR(KP_M_SESSION_ENDED));
    CHECK_STR_EQ(t[1], TR(KP_M_SESSION_ENDED_2));
    /* An IOmcu that started again: the servo is in its mode still. */
    m.id = KST_PROG_MSG_OUT + KST_LINK_OUT_RESTARTED;
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[0], TR(KP_O_RESTARTED));
    CHECK_STR_EQ(t[1], TR(KP_O_RESTARTED_2));
    /* A code of the wire this panel has no text for, with its number. */
    m.id = KST_PROG_MSG_UNKNOWN;
    m.arg = 255;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK(strstr(t[0], "255") != NULL);
    CHECK(strchr(t[0], '%') == NULL);
    /* An id nobody gave a text is still a line. */
    m.id = 0x3Fu;
    m.arg = 0;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK(strstr(t[0], "63") != NULL);
    m.id = KST_PROG_MSG_END + 5u;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    /* A servo nobody should use is said with the result. */
    m.id = KST_PROG_MSG_SES + KST_SES_ERR_TORN;
    m.reg = 0x03u;
    m.arg = 2;
    CHECK_EQ(kst_panel_message_lines(&m, t), 3);
    CHECK_STR_EQ(t[1], TR(KP_M_DO_NOT_USE));
    CHECK(strstr(t[2], "03") != NULL);
    m.id = KST_PROG_MSG_SES + KST_SES_ERR_LOCKED;
    m.reg = 0xFFu;
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[1], TR(KP_S_LOCKED_2));
    /* Locked values that differ from a backup are counted. */
    m.id = KST_PROG_MSG_NO_RESTORE;
    m.arg = 2;
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK(strstr(t[1], "2") != NULL);
    m.arg = 0;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
}

TEST_CASE(every_message_refusal_result_and_rule_has_a_line_in_english)
{
    fresh();
    texts_in(UI_LANG_EN);
}

TEST_CASE(every_message_refusal_result_and_rule_has_a_line_in_german)
{
    fresh();
    texts_in(UI_LANG_DE);
    /* And it is German. */
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_M_READ),
                 ui_tr_in(UI_LANG_EN, TX_KP_M_READ)) != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_HORN_FREE),
                 ui_tr_in(UI_LANG_EN, TX_KP_HORN_FREE)) != 0);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(every_state_is_drawn_in_both_languages)
{
    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.card = true;
        cut_write();
        come_back();
        for (int page = PG_SERVO; page <= PG_LOCKED; ++page) {
            tap(kst_panel_tab_rect(page));
        }
        tap(kst_panel_tab_rect(PG_SERVO));
        tap(val(PG_SERVO, 1, 0));
        settle();
        tap(val(PG_SERVO, 1, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_LOCKED);
        rig_stop(true);
        rig_frames(5);
        show();
        rig_stop(false);
        rig_frames(5);
        tap(val(PG_SERVO, 2, 0));
        show();
        tap(kst_panel_choice_cancel_rect());
    }
    ui_text_set_language(UI_LANG_EN);
}

/* What the last message says goes when it no longer holds: with the switch
 * that it said was off, with an edit after a write, with DISCARD EDITS. */
TEST_CASE(a_message_goes_when_what_it_says_no_longer_holds)
{
    fresh();
    connected();
    /* RELEASE PAIRING with the switch off says so; the switch on ends it. */
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_STR_EQ(said(), TR(KP_M_NEED_ENABLE));
    enable();
    CHECK_STR_EQ(said(), "");
    /* "written and verified" does not stand beside a new edit. */
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    settle();
    CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 2, 1));
    if (kst_panel_editor() == 3) {
        hold(2.1f);
    }
    CHECK_EQ(kst_prog_changes(M), 1);
    CHECK_STR_EQ(said(), "");
    /* DISCARD EDITS leaves no line of the edits behind. */
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_DISCARD))));
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_STR_EQ(said(), "");
}

/* A note, a message's line or a warning page's line is wrapped onto two
 * rows of the card: ui_text.def gives it 116 cells, the two rows' 58, and
 * every language's text takes no more than the two rows at a space. */
TEST_CASE(every_wrapped_text_takes_two_rows_at_most)
{
    fresh();
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (unsigned id = 0; id < (unsigned)TX_COUNT; ++id) {
            const char *t = ui_tr_in((ui_lang_t)lang, (ui_text_id_t)id);

            if (strncmp(ui_text_name((ui_text_id_t)id), "KP_", 3) != 0
                || ui_text_cells((ui_text_id_t)id) != 116) {
                continue;
            }
            if (kst_panel_text_rows(t) > 2) {
                T_FAIL("%s in language %d takes %d rows: \"%s\"",
                       ui_text_name((ui_text_id_t)id), lang,
                       kst_panel_text_rows(t), t);
            }
        }
    }
}

/* One decimal convention in the German texts: the point the values on the
 * screens have. */
TEST_CASE(no_german_text_writes_a_number_with_a_comma)
{
    ui_text_set_language(UI_LANG_DE);
    for (unsigned id = 0; id < (unsigned)TX_COUNT; ++id) {
        const char *t = ui_tr((ui_text_id_t)id);

        for (size_t i = 1; t[i] != '\0' && t[i + 1] != '\0'; ++i) {
            /* A digit or a number's place on each side of the comma. */
            const bool before = (t[i - 1] >= '0' && t[i - 1] <= '9')
                             || (i >= 2 && t[i - 2] == '%'
                                 && (t[i - 1] == 'u' || t[i - 1] == 'd'));
            const bool after = (t[i + 1] >= '0' && t[i + 1] <= '9')
                            || t[i + 1] == '%';

            if (t[i] == ',' && before && after) {
                T_FAIL("text %u: %s", id, t);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(every_message_refusal_result_and_rule_has_a_line_in_english);
    RUN(every_message_refusal_result_and_rule_has_a_line_in_german);
    RUN(every_state_is_drawn_in_both_languages);
    RUN(a_message_goes_when_what_it_says_no_longer_holds);
    RUN(every_wrapped_text_takes_two_rows_at_most);
    RUN(no_german_text_writes_a_number_with_a_comma);
    return test_summary("kst_panel_texts");
}
