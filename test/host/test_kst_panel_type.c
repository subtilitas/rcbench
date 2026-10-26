/**
 * @file test_kst_panel_type.c
 * @brief The programmer overlay's TYPE row and the supply cycle as the
 * overlay shows it: the type asked before a load, the lines under the
 * rows, the notes after a write and the page of LEAVE.
 *
 * Cases:
 * - a_load_with_the_type_unset_asks_for_it_and_loads_after_the_choice
 * - a_cancelled_type_list_loads_nothing_and_leaves_no_load_behind
 * - the_other_type_chosen_before_a_load_refuses_the_file
 * - the_model_s_need_type_has_its_line_in_both_languages
 * - a_save_states_the_type_the_operator_set
 * - the_type_row_takes_no_tap_while_a_card_operation_runs
 * - with_the_pd_mini_on_connect_says_it_cycles_and_leave_cycles_after_a_write
 * - without_a_pd_mini_a_write_and_leave_note_the_power_cycle
 * - a_leave_without_a_write_cycles_nothing
 * - the_new_messages_have_their_lines
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/** @brief CONNECT and the read, the type left unset. */
static void read_untyped(void)
{
    link_up();
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(M));
    CHECK_EQ(kst_prog_type(M), 0u);
}

/**
 * @brief The entry of the open list that names file @p name.
 * @param name the file's name
 * @return the entry, -1 for none
 */
static int file_entry(const char *name)
{
    for (int i = 0; kst_panel_choice_label(i) != NULL; ++i) {
        if (strncmp(kst_panel_choice_label(i), name, strlen(name)) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief FILES and a tap on file @p name; nothing settled after it.
 * @param name the file's name
 */
static void tap_file(const char *name)
{
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK(file_entry(name) > 0);
    tap(kst_panel_choice_rect(file_entry(name)));
}

/**
 * @brief A file of the bench servo with boost one step lower, of the type
 *        20k_sel suggests.
 * @param name the file's name
 */
static void put_wider(const char *name)
{
    kst_image_t img = rig_servo();

    kst_field_set(&img, KST_F_BOOST,
                  (uint16_t)(kst_field_get(&img, KST_F_BOOST) - 1u));
    (void)fake_card_put(&g_rig.fc, name, &img);
}

TEST_CASE(a_load_with_the_type_unset_asks_for_it_and_loads_after_the_choice)
{
    uint16_t boost;

    fresh();
    g_rig.card = true;
    read_untyped();
    boost = servo(KST_F_BOOST);
    put_wider("W");
    tap_file("W");
    /* The TYPE list, under its own title; no load yet. */
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_STR_EQ(kst_panel_choice_title(), TR(KP_TYPE_FIRST));
    CHECK_EQ(choices(), 2);
    CHECK_EQ(g_rig.fc.loads, 0u);
    tap(kst_panel_choice_rect(kst_prog_type_guess(M) == 6u ? 0 : 1));
    settle();
    CHECK_EQ(kst_prog_type(M), kst_prog_type_guess(M));
    CHECK_EQ(g_rig.fc.loads, 1u);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(draft(KST_F_BOOST), (uint16_t)(boost - 1u));
}

TEST_CASE(a_cancelled_type_list_loads_nothing_and_leaves_no_load_behind)
{
    fresh();
    g_rig.card = true;
    read_untyped();
    put_wider("W");
    tap_file("W");
    CHECK_STR_EQ(kst_panel_choice_title(), TR(KP_TYPE_FIRST));
    tap(kst_panel_choice_cancel_rect());
    settle();
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_type(M), 0u);
    CHECK_EQ(g_rig.fc.loads, 0u);
    /* TYPE later sets the type and loads nothing. */
    tap(val(PG_SERVO, 1, 1));
    CHECK_STR_EQ(kst_panel_choice_title(), TR(KP_TYPE_TITLE));
    tap(kst_panel_choice_rect(1));
    settle();
    CHECK_EQ(kst_prog_type(M), 8u);
    CHECK_EQ(g_rig.fc.loads, 0u);
    CHECK_EQ(kst_prog_changes(M), 0u);
    /* With the type set, a file loads at its tap. */
    tap_file("W");
    CHECK_EQ(kst_panel_editor(), 0);    /* typed now: no list, a load */
    settle();
    CHECK_EQ(g_rig.fc.loads, 1u);
}

TEST_CASE(the_other_type_chosen_before_a_load_refuses_the_file)
{
    char want[KST_PANEL_LINE];

    fresh();
    g_rig.card = true;
    read_untyped();
    put_wider("W");
    tap_file("W");
    /* The type that is not the file's. */
    tap(kst_panel_choice_rect(kst_prog_type_guess(M) == 6u ? 1 : 0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_TYPE);
    snprintf(want, sizeof(want), TR(KP_M_LOAD_TYPE), kst_prog_type_guess(M),
             kst_prog_type(M));
    CHECK_STR_EQ(said(), want);
    CHECK_EQ(kst_prog_changes(M), 0u);
}

TEST_CASE(the_model_s_need_type_has_its_line_in_both_languages)
{
    fresh();
    g_rig.card = true;
    read_untyped();
    put_wider("W");
    files();
    tap(kst_panel_choice_cancel_rect());
    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_EQ(kst_prog_load(M, "W"), KST_PROG_MSG_NEED_TYPE);
        CHECK_STR_EQ(said(), TR(KP_M_NEED_TYPE));
    }
    CHECK(strcmp(ui_tr_in(UI_LANG_EN, TX_KP_M_NEED_TYPE),
                 ui_tr_in(UI_LANG_DE, TX_KP_M_NEED_TYPE)) != 0);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_save_states_the_type_the_operator_set)
{
    const fake_file_t *f;

    fresh();
    g_rig.card = true;
    read_untyped();
    /* The type that is not the guess. */
    tap(val(PG_SERVO, 1, 1));
    tap(kst_panel_choice_rect(kst_prog_type_guess(M) == 6u ? 1 : 0));
    save_entry();
    tkey(UI_TK_OK);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    f = fake_card_find(&g_rig.fc, kst_prog_card_name(M));
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    CHECK_EQ(f->type, kst_prog_type(M));
    CHECK(f->type != kst_prog_type_guess(M));
    /* 20k_sel in the file is the servo's. */
    CHECK_EQ(kst_field_get(&f->img, KST_F_20K_SEL), servo(KST_F_20K_SEL));
}

TEST_CASE(the_type_row_takes_no_tap_while_a_card_operation_runs)
{
    fresh();
    g_rig.card = true;
    read_untyped();
    CHECK(kst_panel_row_live(PG_SERVO, 1, 1));
    g_rig.fc.silent = true;
    files();
    CHECK(kst_prog_card_busy(M));
    CHECK(!kst_panel_row_live(PG_SERVO, 1, 1));
    tap(val(PG_SERVO, 1, 1));
    CHECK_EQ(kst_panel_editor(), 0);
    /* The model refuses it the same. */
    CHECK_EQ(kst_prog_type_set(M, 6u),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK_EQ(kst_prog_type(M), 0u);
}

/**
 * @brief Whether a line of the message shown reads @p s.
 * @param s the line
 * @return true when one does
 */
static bool message_has(const char *s)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    const int n = lines(t);

    for (int i = 0; i < n; ++i) {
        if (strcmp(t[i], s) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief Three edits written through ENABLE, the page and its hold. */
static void write_edits(void)
{
    enable();
    edits();
    tap(val(PG_SERVO, 4, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
}

TEST_CASE(with_the_pd_mini_on_connect_says_it_cycles_and_leave_cycles_after_a_write)
{
    unsigned sets;

    fresh();
    g_rig.rail = true;
    g_rig.sup_on = true;
    g_rig.sup_mv = 5000u;
    g_rig.sup_ma = 1000u;
    link_up();
    CHECK_STR_EQ(kst_panel_status_line(1), TR(KP_CONNECT_CYCLE));
    sets = g_rig.sup_sets;
    connected();
    /* Off, then on: two switches. */
    CHECK_EQ(g_rig.sup_sets, sets + 2u);
    CHECK(g_rig.sup_on);
    write_edits();
    CHECK(message_has(TR(KP_M_ACT_AT_LEAVE)));
    CHECK(!message_has(TR(KP_M_LEFT_NOTE)));
    /* LEAVE's page names the cycle. */
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_LEAVE_CYCLE)));
    CHECK(!warns(TR(KP_LEAVE_1)));
    sets = g_rig.sup_sets;
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_CYCLED);
    CHECK_EQ(kst_prog_message(M)->level, KST_PROG_INFO);
    CHECK(message_has(TR(KP_M_LEFT)));
    CHECK(message_has(TR(KP_M_LEFT_CYCLED)));
    CHECK_EQ(g_rig.sup_sets, sets + 2u);
    CHECK(g_rig.sup_on);
}

TEST_CASE(without_a_pd_mini_a_write_and_leave_note_the_power_cycle)
{
    fresh();
    link_up();
    CHECK_STR_EQ(kst_panel_status_line(1), TR(KP_NOPD_ENTRY));
    connected();
    write_edits();
    CHECK(message_has(TR(KP_M_LEFT_NOTE)));
    CHECK(!message_has(TR(KP_M_ACT_AT_LEAVE)));
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_LEAVE_1)));
    CHECK(warns(TR(KP_M_LEFT_NOTE)));
    CHECK(!warns(TR(KP_LEAVE_CYCLE)));
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_NOTE);
    CHECK_EQ(kst_prog_message(M)->level, KST_PROG_WARN);
    CHECK(message_has(TR(KP_M_LEFT_NOTE)));
    CHECK_EQ(g_rig.sup_sets, 0u);
}

TEST_CASE(a_leave_without_a_write_cycles_nothing)
{
    unsigned sets;

    fresh();
    g_rig.rail = true;
    g_rig.sup_on = true;
    g_rig.sup_mv = 5000u;
    g_rig.sup_ma = 1000u;
    connected();
    tap(val(PG_SERVO, 0, 0));
    CHECK(warns(TR(KP_LEAVE_1)));
    CHECK(!warns(TR(KP_LEAVE_CYCLE)));
    CHECK(!warns(TR(KP_M_LEFT_NOTE)));
    sets = g_rig.sup_sets;
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(g_rig.sup_sets, sets);
    CHECK(g_rig.sup_on);
}

TEST_CASE(the_new_messages_have_their_lines)
{
    static const struct {
        unsigned id;
        int first, second;
    } k_want[] = {
        { KST_PROG_MSG_NEED_TYPE, TX_KP_M_NEED_TYPE, -1 },
        { KST_PROG_MSG_CYCLE_CUT, TX_KP_M_CYCLE_CUT, -1 },
        { KST_PROG_MSG_LEFT_CYCLED, TX_KP_M_LEFT, TX_KP_M_LEFT_CYCLED },
        { KST_PROG_MSG_LEFT_NOTE, TX_KP_M_LEFT, TX_KP_M_LEFT_NOTE },
    };
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];

    fresh();
    for (size_t i = 0; i < sizeof(k_want) / sizeof(k_want[0]); ++i) {
        const kst_prog_msg_t m = { .id = (uint8_t)k_want[i].id };
        const int n = kst_panel_message_lines(&m, t);

        CHECK_EQ(n, k_want[i].second < 0 ? 1 : 2);
        CHECK_STR_EQ(t[0], ui_tr((ui_text_id_t)k_want[i].first));
        if (k_want[i].second >= 0) {
            CHECK_STR_EQ(t[1], ui_tr((ui_text_id_t)k_want[i].second));
        }
    }
}

int main(void)
{
    RUN(a_load_with_the_type_unset_asks_for_it_and_loads_after_the_choice);
    RUN(a_cancelled_type_list_loads_nothing_and_leaves_no_load_behind);
    RUN(the_other_type_chosen_before_a_load_refuses_the_file);
    RUN(the_model_s_need_type_has_its_line_in_both_languages);
    RUN(a_save_states_the_type_the_operator_set);
    RUN(the_type_row_takes_no_tap_while_a_card_operation_runs);
    RUN(with_the_pd_mini_on_connect_says_it_cycles_and_leave_cycles_after_a_write);
    RUN(without_a_pd_mini_a_write_and_leave_note_the_power_cycle);
    RUN(a_leave_without_a_write_cycles_nothing);
    RUN(the_new_messages_have_their_lines);
    return test_summary("kst_panel_type");
}
