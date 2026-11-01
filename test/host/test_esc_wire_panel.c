/*
 * The ESC signal-wire session on the whole bench of esc_wire_rig.h: the connect page
 * with SUPPLY's values and the output it names, the 2s holds, the keypad on a number
 * and the list on a choice, a draft and its count, the warning that names each
 * second-look field, the backup before the first write and the question without a
 * card, the record, a file loaded, RESTORE on an erased AM32, read-only pages, LEAVE,
 * ABORT on the press, and how ESCape32's leave is said.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"

#include "esc_wire_rig.h"

/** @brief The session's voltage. */
static unsigned ewp_s_mv(void)
{
    return ewp_s.mv;
}

/** @brief The session's limit. */
static unsigned ewp_s_ma(void)
{
    return ewp_s.ma;
}

TEST_CASE(connect_shows_what_it_will_drive_and_takes_a_hold)
{
    char s[40];

    session_up(up_am_rig);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK(!esc_wire_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, "SLOT 3 GPIO 5");
    /* A release before 2s connects nothing. */
    ew_hold(1.0f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK(!esc_wire_panel_holds_port());
    connected();
    CHECK(esc_wire_panel_holds_port());
    CHECK(!esc_wire_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, "SLOT 3 GPIO 5");
    CHECK_EQ(esc_wire_pages.pin(), 5u);
}

TEST_CASE(no_target_says_so_and_connects_nothing)
{
    char s[40];

    session_up(up_am_rig);
    esc_wire_panel_set_targets(NULL, 0u, true);
    CHECK(esc_wire_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, TR(EW_NO_PIN));
    ew_hold(2.2f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_NO_TARGET));
    esc_wire_panel_set_targets(NULL, 0u, false);
    CHECK(esc_wire_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, TR(EW_NOT_READ_YET));
}

TEST_CASE(the_session_uses_supplys_values_until_typed_and_no_default)
{
    session_up(up_am_rig);
    CHECK_EQ(ewp_s_mv(), 7400u);
    CHECK_EQ(ewp_s_ma(), 1000u);
    supply_screen_put(5.0f, 0.5f);
    bench_run(5u);
    /* Follows SUPPLY: never above what the operator set. */
    CHECK_EQ(ewp_s_mv(), 5000u);
    CHECK_EQ(ewp_s_ma(), 500u);
    /* A tap on a number opens the keypad. */
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK(esc_wire_panel_keypad()->open);
    kp_type("6");
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK_EQ(ewp_s_mv(), 6000u);
    supply_screen_put(5.0f, 0.5f);
    bench_run(5u);
    CHECK_EQ(ewp_s_mv(), 6000u);
    connected();
    CHECK_EQ(g.on_mv, 6000u);
    CHECK_EQ(g.on_ma, 500u);
}

TEST_CASE(no_voltage_refuses_connect_with_its_reason)
{
    session_up(up_am_rig);
    supply_screen_put(0.0f, 0.0f);
    bench_run(5u);
    if (ewp_s_mv() >= LINK_ESC_SUPPLY_MIN_MV) {
        return;     /* The caps snap it up: nothing below the lowest to try. */
    }
    ew_hold(2.2f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_RANGE));
    CHECK_EQ(g.ons, 0);
}

TEST_CASE(a_number_opens_the_keypad_and_a_choice_the_list)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(1));
    CHECK_STR_EQ(esc_wire_panel_row_name(0), "POWER RAMP");
    ew_tap(esc_wire_panel_value_rect(0));
    CHECK(esc_wire_panel_keypad()->open);
    kp_type("99");
    CHECK_STR_EQ(esc_wire_panel_row_value(0), "99");
    CHECK_EQ(ewp_changed_count(), 1);
    /* REVERSED, ON as read: OFF and ON. */
    CHECK_STR_EQ(esc_wire_panel_row_name(2), "REVERSED");
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK_STR_EQ(esc_wire_panel_choice_label(1), "ON");
    CHECK(esc_wire_panel_choice_label(2) == NULL);
    ew_tap(esc_wire_panel_choice_rect(0));
    CHECK_STR_EQ(esc_wire_panel_row_value(2), "OFF");
    CHECK_EQ(ewp_changed_count(), 2);
    /* A scaled number goes through its unit: TIMING 30deg, stored 42. */
    CHECK_STR_EQ(esc_wire_panel_row_name(7), "TIMING");
    ew_tap(esc_wire_panel_value_rect(7));
    kp_type("30");
    CHECK_EQ(ewp_s.draft[0x17], 42u);
    /* A value out of its range stays on the keypad. */
    ew_tap(esc_wire_panel_value_rect(1));
    CHECK(esc_wire_panel_keypad()->open);
    kp_type("99");
    CHECK(esc_wire_panel_keypad()->open);
    ew_tap(ui_keypad_key_rect(esc_wire_panel_keypad(), UI_KEY_CANCEL));
    CHECK(!esc_wire_panel_keypad()->open);
}

TEST_CASE(a_number_with_an_off_offers_off_and_the_keypad)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(4));
    CHECK_STR_EQ(esc_wire_panel_row_name(3), "TEMPERATURE LIMIT");
    ew_tap(esc_wire_panel_value_rect(3));
    CHECK_STR_EQ(esc_wire_panel_choice_label(0), "OFF");
    CHECK_STR_EQ(esc_wire_panel_choice_label(1), TR(EW_TYPE_NUMBER));
    ew_tap(esc_wire_panel_choice_rect(1));
    CHECK(esc_wire_panel_keypad()->open);
    kp_type("100");
    CHECK_EQ(ewp_s.draft[0x2B], 100u);
    ew_tap(esc_wire_panel_value_rect(3));
    ew_tap(esc_wire_panel_choice_rect(0));
    CHECK_EQ(ewp_s.draft[0x2B], 255u);
}

/** @brief The draft of a read AM32: REVERSED changed and STUCK ROTOR, second-look. */
static void two_changes(void)
{
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(2));
    ew_tap(esc_wire_panel_choice_rect(0));
    ew_tap(esc_wire_panel_tab_rect(4));
    CHECK_STR_EQ(esc_wire_panel_row_name(8), "STUCK ROTOR");
    ew_tap(esc_wire_panel_value_rect(8));
    ew_tap(esc_wire_panel_choice_rect(0));
    CHECK_EQ(ewp_changed_count(), 2);
}

/** @brief Runs the bench until no request is out, at most 10s. */
static void wait_idle(void)
{
    for (int i = 0; i < 2000 && esc_wire_panel_stage() != ESC_WIRE_SESSION; ++i) {
        bench_run(5u);
    }
}

TEST_CASE(the_warning_names_each_second_look_field_with_its_value)
{
    session_up(up_am_rig);
    connected();
    two_changes();
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_WRITE);
    /* The second-look field first, with its check word and new value. */
    CHECK(strstr(esc_wire_panel_page_line(0), "CHECK STUCK ROTOR") != NULL);
    CHECK(strstr(esc_wire_panel_page_line(0), "ON -> OFF") != NULL);
    CHECK(strstr(esc_wire_panel_page_line(1), "REVERSED") != NULL);
    CHECK(strncmp(esc_wire_panel_page_line(1), "CHECK", 5u) != 0);
    /* CANCEL writes nothing. */
    ew_tap(esc_wire_panel_cancel_rect());
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    CHECK_EQ(g_card_requests, 0);
    CHECK_EQ(esc.am[0x11], 1u);
    CHECK_EQ(ewp_changed_count(), 2);
}

TEST_CASE(a_write_saves_the_backup_writes_the_record_then_goes)
{
    session_up(up_am_rig);
    connected();
    two_changes();
    ew_tap(esc_wire_panel_action_rect(2));
    /* Not before the hold. */
    ew_hold(1.0f);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_WRITE);
    CHECK_EQ(g_card_requests, 0);
    ew_hold(2.2f);
    bench_run(50u);
    wait_idle();
    /* One backup of the ESC before the write, then the record. */
    CHECK_EQ(g_card_files, 1);
    CHECK_STR_EQ(g_names[0], "ESCW001.TXT");
    CHECK(strncmp(g_texts[0], "ESC settings backup, format ", 28u) == 0);
    CHECK(strstr(g_record, "RCBENCH ESCW RECORD 1\n") == g_record);
    CHECK(strstr(g_record, "REVERSED: ON -> OFF\n") != NULL);
    CHECK(strstr(g_record, "\nbefore\nESC settings backup, format ") != NULL);
    CHECK_EQ(esc.am[0x11], 0u);
    CHECK_EQ(esc.am[0x16], 0u);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_WRITE_DONE));
    CHECK_EQ(ewp_changed_count(), 0);
    /* A second write of the session: no second backup. */
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(0));
    kp_type("77");
    ew_tap(esc_wire_panel_action_rect(2));
    ew_hold(2.2f);
    bench_run(50u);
    wait_idle();
    CHECK_EQ(g_card_files, 1);
    CHECK_EQ(esc.am[0x05], 77u);
    CHECK(strstr(g_record, "POWER RAMP") != NULL);
}

TEST_CASE(without_a_card_it_asks_and_goes_only_on_a_hold)
{
    session_up(up_am_rig);
    connected();
    g_card_in = false;
    two_changes();
    ew_tap(esc_wire_panel_action_rect(2));
    ew_hold(2.2f);
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NO_BACKUP);
    CHECK_STR_EQ(esc_wire_panel_page_line(0), TR(EW_CARD_NONE));
    CHECK_EQ(esc.am[0x16], 1u);
    ew_hold(2.2f);
    bench_run(50u);
    wait_idle();
    CHECK_EQ(g_card_files, 0);
    CHECK_EQ(g_record_len, 0u);
    CHECK_EQ(esc.am[0x16], 0u);
}

TEST_CASE(a_card_that_refuses_the_record_stops_the_write_and_asks)
{
    session_up(up_am_rig);
    connected();
    two_changes();
    /* The backup's save refused: the question. */
    g_card_refuses = true;
    ew_tap(esc_wire_panel_action_rect(2));
    ew_hold(2.2f);
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NO_BACKUP);
    CHECK_STR_EQ(esc_wire_panel_page_line(0), TR(EW_CARD_DENIED));
    CHECK_EQ(esc.am[0x16], 1u);
    ew_tap(esc_wire_panel_cancel_rect());
    /* Backup taken, record refused: the same. */
    g_card_refuses = false;
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(10u);
    ew_tap(esc_wire_panel_choice_rect(0));
    bench_run(10u);
    CHECK_EQ(g_card_files, 1);
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_SETTLED);
    g_card_refuses = true;
    ew_tap(esc_wire_panel_action_rect(2));
    ew_hold(2.2f);
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NO_BACKUP);
    CHECK_EQ(esc.am[0x16], 1u);
}

TEST_CASE(a_file_loaded_goes_into_the_draft_and_writes_nothing)
{
    session_up(up_am_rig);
    connected();
    /* A person's file on the card. */
    snprintf(g_names[0], sizeof(g_names[0]), "ESCW005.TXT");
    snprintf(g_texts[0], sizeof(g_texts[0]),
             "ESC settings backup, format 1\nFirmware: AM32\nLayout: 4\n"
             "POWER RAMP = 123\nedited\n");
    g_card_files = 1;
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(10u);
    CHECK_STR_EQ(esc_wire_panel_choice_label(0), TR(EW_FILE_SAVE));
    CHECK_STR_EQ(esc_wire_panel_choice_label(1), "ESCW005");
    ew_tap(esc_wire_panel_choice_rect(1));
    bench_run(10u);
    CHECK_EQ(ewp_s.draft[0x05], 123u);
    CHECK_EQ(ewp_changed_count(), 1);
    CHECK(esc.am[0x05] != 123u);
    char line[EWP_LINE];

    snprintf(line, sizeof(line), TR(EW_MSG_LOADED), 1u);
    CHECK_STR_EQ(esc_wire_panel_message(), line);
}

TEST_CASE(an_erased_am32_offers_only_the_restore_of_its_own_file)
{
    static uint8_t s_page[192];
    static char s_file[EW_FILE_MAX];

    session_up(up_am_rig);
    connected();
    /* The session's backup, as the card would have it. */
    size_t n = esc_prog_backup_text(esc_wire_panel_model(), s_file, sizeof(s_file));

    CHECK(n > 0u);
    memcpy(s_page, esc.am, sizeof(s_page));
    ew_tap(esc_wire_panel_back_rect());
    ew_hold(2.2f);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    memset(esc.am, 0xFF, sizeof(esc.am));
    snprintf(g_names[0], sizeof(g_names[0]), "ESCW001.TXT");
    memcpy(g_texts[0], s_file, n);
    g_texts[0][n] = '\0';
    g_card_files = 1;
    connected();
    CHECK((esc_wire_panel_model()->f.rd.flags & LINK_EW_F_ERASED) != 0u);
    /* READ and WRITE are not offered: the one action is the restore. */
    ew_tap(esc_wire_panel_action_rect(0));
    bench_run(10u);
    CHECK_STR_EQ(esc_wire_panel_choice_label(0), "ESCW001");
    CHECK(esc_wire_panel_choice_label(1) == NULL);
    ew_tap(esc_wire_panel_choice_rect(0));
    bench_run(10u);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_RESTORE);
    CHECK_EQ(esc.am[0], 0xFFu);
    ew_hold(2.2f);
    bench_run(50u);
    wait_idle();
    CHECK_EQ(esc.am[0], s_page[0]);
    CHECK_EQ(esc.am[0x19], s_page[0x19]);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_RESTORE_DONE));
}

TEST_CASE(leave_takes_a_hold_and_gives_the_pin_back)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_back_rect());
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_LEAVE);
    CHECK_STR_EQ(esc_wire_panel_page_line(0), TR(EW_LEAVE_1));
    CHECK(esc_wire_panel_holds_port());
    ew_hold(2.2f);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK(!esc_wire_panel_holds_port());
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_LEFT));
    CHECK(!g.v.asked_on);
}

TEST_CASE(abort_acts_on_the_press)
{
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(4));
    kp_type("5");
    ew_hold(2.2f);
    bench_run(10u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_ENTERING);
    const gfx_rect_t a = esc_wire_panel_action_rect(0);

    CHECK(esc_wire_pages.safe_at(a.x + 5, a.y + 5));
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(a.x + 5),
                                   .y = (int16_t)(a.y + 5), .strength = 40 } };

    esc_wire_pages.event(&e);
    bench_run(100u);
    CHECK(!g.v.asked_on);
    CHECK_EQ(esc_port_state(&g_p) == LINK_ESC_OPEN
             || esc_port_state(&g_p) == LINK_ESC_FREE, true);
}

TEST_CASE(back_on_a_session_under_way_stays_and_says_why)
{
    session_up(up_am_rig);
    ew_hold(2.2f);
    bench_run(10u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_ENTERING);
    esc_wire_pages.back();
    CHECK(esc_wire_pages.is_open());
    /* BACK names the button that ends it. */
    CHECK(strstr(esc_wire_panel_message(), TR(EW_ABORT)) != NULL);
}

/**
 * @brief The session fed as a read ESC, with no bench.
 * @param fw     The firmware.
 * @param layout The layout.
 * @param flags  LINK_EW_F_* beside HAVE_READ.
 * @param why    Why it is not written.
 */
static void fed_as(uint8_t fw, uint8_t layout, uint16_t flags, uint8_t why)
{
    session_up(up_am_rig);
    memset(&g_f, 0, sizeof(g_f));
    g_f.rd.phase = ESC_LINK_READY;
    g_f.rd.known = true;
    g_f.rd.held = true;
    g_f.rd.ours = true;
    g_f.rd.state = LINK_ESC_READY;
    g_f.rd.gpio = 5u;
    g_f.rd.fw = fw;
    g_f.rd.layout = layout;
    g_f.rd.why = why;
    g_f.rd.flags = (uint16_t)(flags | LINK_EW_F_HAVE_READ);
    g_f.image.gen = 1u;
    g_f.image.len = ew_map_len(fw);
    g_hand = true;
    esc_wire_panel_feed(&g_f);
}

TEST_CASE(a_page_not_written_is_dimmed_and_has_no_write)
{
    char why[EWP_LINE];

    fed_as(EW_FW_BJ, 204u, 0u, EW_WHY_OLD_LAYOUT);
    ew_tap(esc_wire_panel_tab_rect(1));
    /* A tap on a row opens nothing. */
    ew_tap(esc_wire_panel_value_rect(0));
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK(esc_wire_panel_choice_label(0) == NULL);
    /* WRITE is absent: the place takes no tap. */
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    ewp_why_text(EW_WHY_OLD_LAYOUT, why, sizeof(why));
    CHECK(strstr(why, "204") != NULL);
}

/* An ESCape32 protection key is a second look only when it turns the protection off. */
TEST_CASE(a_protection_key_warns_only_on_its_way_to_off)
{
    size_t slot = 0u;

    fed_as(EW_FW_E32, 17u, LINK_EW_F_WRITABLE, 0u);
    CHECK(ew_key_find("prot_temp", 9u, &slot) != NULL);
    ew_e32_put(ewp_s.prog.f.image.page, slot, 60);
    ew_e32_put(ewp_s.draft, slot, 80);
    ewp_page_write();
    CHECK(strstr(esc_wire_panel_page_line(0), "prot_temp") != NULL);
    CHECK(strncmp(esc_wire_panel_page_line(0), "CHECK", 5u) != 0);
    esc_wire_pages.back();
    ew_e32_put(ewp_s.draft, slot, 0);
    ewp_page_write();
    CHECK(strncmp(esc_wire_panel_page_line(0), "CHECK prot_temp", 15u) == 0);
}

TEST_CASE(escape32s_leave_says_a_power_cycle_ends_it)
{
    fed_as(EW_FW_E32, 17u, LINK_EW_F_WRITABLE, 0u);
    ew_tap(esc_wire_panel_back_rect());
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_LEAVE);
    CHECK_STR_EQ(esc_wire_panel_page_line(0), TR(EW_LEAVE_E32));
    CHECK_STR_EQ(esc_wire_panel_page_line(1), TR(EW_LEAVE_E32_SWITCHED));
    esc_wire_pages.back();
    /* BY HAND: disconnect it. */
    ewp_s.power = LINK_ESC_BY_HAND;
    ew_tap(esc_wire_panel_back_rect());
    CHECK_STR_EQ(esc_wire_panel_page_line(1), TR(EW_LEAVE_E32_HAND));
    /* The other firmwares' leave says none of it. */
    fed_as(EW_FW_AM, 4u, LINK_EW_F_WRITABLE, 0u);
    ew_tap(esc_wire_panel_back_rect());
    CHECK_STR_EQ(esc_wire_panel_page_line(0), TR(EW_LEAVE_1));
}

TEST_CASE(a_screen_says_nothing_that_implies_simonk_works)
{
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(strstr(TR(PL_ESC_WIRE), "SimonK") == NULL);
        CHECK(strstr(TR(EW_SES_SILENT), "SimonK") == NULL);
        CHECK(strstr(TR(EW_WARN_SIMONK), "SimonK") != NULL);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_steady_session_moves_no_revision)
{
    session_up(up_am_rig);
    connected();
    bench_run(200u);
    const uint32_t r = esc_wire_pages.revision();

    bench_run(2000u);
    CHECK_EQ(esc_wire_pages.revision(), r);
}

int main(void)
{
    RUN(connect_shows_what_it_will_drive_and_takes_a_hold);
    RUN(no_target_says_so_and_connects_nothing);
    RUN(the_session_uses_supplys_values_until_typed_and_no_default);
    RUN(no_voltage_refuses_connect_with_its_reason);
    RUN(a_number_opens_the_keypad_and_a_choice_the_list);
    RUN(a_number_with_an_off_offers_off_and_the_keypad);
    RUN(the_warning_names_each_second_look_field_with_its_value);
    RUN(a_write_saves_the_backup_writes_the_record_then_goes);
    RUN(without_a_card_it_asks_and_goes_only_on_a_hold);
    RUN(a_card_that_refuses_the_record_stops_the_write_and_asks);
    RUN(a_file_loaded_goes_into_the_draft_and_writes_nothing);
    RUN(an_erased_am32_offers_only_the_restore_of_its_own_file);
    RUN(leave_takes_a_hold_and_gives_the_pin_back);
    RUN(abort_acts_on_the_press);
    RUN(back_on_a_session_under_way_stays_and_says_why);
    RUN(a_page_not_written_is_dimmed_and_has_no_write);
    RUN(escape32s_leave_says_a_power_cycle_ends_it);
    RUN(a_protection_key_warns_only_on_its_way_to_off);
    RUN(a_screen_says_nothing_that_implies_simonk_works);
    RUN(a_steady_session_moves_no_revision);
    return test_summary("esc_wire_panel");
}
