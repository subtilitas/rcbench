/**
 * @file test_kst_panel_leave.c
 * @brief The programmer overlay's current limit, the supply it asks for, and the
 * port it gives back: connect, the wait before PWM and what a leave, a STOP
 * and a lost link leave behind.
 *
 * Cases:
 * - the_current_limit_is_typed_inside_50_to_3000_ma
 * - a_supply_that_cannot_give_the_voltage_is_refused_by_name
 * - a_supply_that_is_on_under_4_8_v_is_refused_by_name
 * - a_supply_that_does_not_switch_on_ends_the_connect_with_its_line
 * - a_leave_gives_the_port_back_and_switches_no_supply
 * - a_leave_with_a_pd_mini_keeps_the_supply_on
 * - the_tester_has_no_port_until_the_wait_ends_and_abort_keeps_it
 * - a_coprocessor_without_leave_gets_a_stated_message
 * - stop_and_a_lost_link_while_leaving_keep_the_port_from_the_tester
 * - the_wait_before_pwm_is_a_row_with_limits_and_is_kept
 * - a_count_of_1_is_said_in_the_singular
 * - connect_reads_the_servo_and_keeps_the_overlay_while_the_port_is_held
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

TEST_CASE(the_current_limit_is_typed_inside_50_to_3000_ma)
{
    char want[KST_PANEL_LINE];

    fresh();
    g_rig.rail = true;
    link_up();
    more_input(1);
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_STR_EQ(kst_panel_keypad()->title, TR(KP_PROG_CURRENT));
    CHECK_NEAR(kst_panel_keypad()->min, (float)PDMINI_I_MIN_MA, 0.001f);
    CHECK_NEAR(kst_panel_keypad()->max, (float)PDMINI_I_MAX_MA, 0.001f);
    type_text("49");
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK(kst_panel_keypad()->refused);
    type_text("3001");
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_EQ(kst_prog_supply_ma(M), KST_PROG_SUPPLY_MA);
    type_text("50");
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_ma(M), 50u);
    more_input(1);
    type_text("3000");
    CHECK_EQ(kst_prog_supply_ma(M), 3000u);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    snprintf(want, sizeof(want), TR(KP_STATE_PWM), RIG_PORT + 1u, 5u, 0u,
             3000u);
    CHECK_STR_EQ(status(0), want);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(g_rig.sup_ma, 3000u);
    CHECK_EQ(g_rig.sup_mv, KST_PROG_SUPPLY_MV);
}

TEST_CASE(a_supply_that_cannot_give_the_voltage_is_refused_by_name)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        /* A 5.5V source: 5.0V out is 200mV over its cap, 4.8V. */
        g_rig.vin_mv = 4800u + PDMINI_CAP_MV;
        link_up();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_RANGE);
        CHECK_STR_EQ(said(), TR(KP_R_SUPPLY_RANGE));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_EQ(g_rig.sup_sets, 0);
        /* 4.8V is what it can give. */
        more_input(0);
        type_text("4.8");
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK_EQ(g_rig.sup_mv, 4800u);
    }
}

/* An output that is on under 4.8V is not programmed at: CONNECT ends with
 * the line that says so, the port stays in PWM and the output as it is. */
TEST_CASE(a_supply_that_is_on_under_4_8_v_is_refused_by_name)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        g_rig.sup_on = true;
        g_rig.sup_mv = KST_PORT_SUPPLY_MIN_MV - 100u;
        g_rig.sup_ma = 800u;
        link_up();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_LOW);
        CHECK_STR_EQ(said(), TR(KP_R_SUPPLY_LOW));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
        CHECK_EQ(g_rig.sup_sets, 0);
        CHECK(g_rig.sup_on);
        CHECK_EQ(g_rig.sup_mv, KST_PORT_SUPPLY_MIN_MV - 100u);

        /* At 4.8V and 3A the session runs at 4.8V and the staged
         * 1000mA: the lower one of each. */
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        g_rig.sup_on = true;
        g_rig.sup_mv = KST_PORT_SUPPLY_MIN_MV;
        g_rig.sup_ma = 3000u;
        link_up();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(g_rig.sup_on);
        CHECK_EQ(g_rig.sup_mv, KST_PORT_SUPPLY_MIN_MV);
        CHECK_EQ(g_rig.sup_ma, 1000u);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_supply_that_does_not_switch_on_ends_the_connect_with_its_line)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        g_rig.rail_deaf = true;
        link_up();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_SES + LINK_KST_RESULT_SUPPLY);
        snprintf(want, sizeof(want), TR(KP_S_SUPPLY),
                 (unsigned)KST_PORT_SUPPLY_MS);
        CHECK_STR_EQ(said(), want);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
    }
}

TEST_CASE(a_leave_gives_the_port_back_and_switches_no_supply)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        int base;

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        base = g_rig.starts;
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 0, 0), TR(KP_LEAVE));
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        /* The page says where the port goes and that the servo may move. */
        CHECK(warns(TR(KP_LEAVE_1)));
        CHECK(warns(TR(KP_LEAVE_2)));
        /* CANCEL and a short hold leave nothing. */
        hold(1.9f);
        if (kst_panel_editor() == 3) {
            tap(kst_panel_cancel_rect());
        }
        settle();
        CHECK_EQ(g_rig.starts, base);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 0);
        /* Until the port reports PWM the tester has no port and the line
         * says that no pulse runs. */
        CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
        CHECK(kst_panel_holds_port());
        CHECK_STR_EQ(status(0), TR(KP_STATE_LEAVING));
        /* No other row starts anything under it. */
        tap(val(PG_SERVO, 1, 0));
        CHECK_EQ(kst_panel_editor(), 0);
        settle();
        CHECK_EQ(g_rig.starts, base + 1);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
        CHECK_STR_EQ(said(), TR(KP_M_LEFT));
        CHECK(!kst_prog_have_servo(M));
        CHECK(!kst_panel_holds_port());
        CHECK_EQ(g_rig.sup_sets, 0);
    }
}

TEST_CASE(a_leave_with_a_pd_mini_keeps_the_supply_on)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        int base, sets;

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        connected();
        CHECK(g_rig.sup_on);
        base = g_rig.starts;
        sets = (int)g_rig.sup_sets;
        /* The same page as without a PD mini. */
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(warns(TR(KP_LEAVE_1)));
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 0);
        settle();
        CHECK_EQ(g_rig.starts, base + 1);
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
        CHECK_STR_EQ(said(), TR(KP_M_LEFT));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
        /* No supply command, and the output is on. */
        CHECK_EQ((int)g_rig.sup_sets, sets);
        CHECK(g_rig.sup_on);
    }
}

TEST_CASE(the_tester_has_no_port_until_the_wait_ends_and_abort_keeps_it)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        connected();
        CHECK(kst_prog_wait_set(M, 5000u));
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        rig_frames(1000);
        show();
        /* The wait runs: the port is the programmer's, the line says that
         * no pulse runs and the button aborts. */
        CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
        CHECK_EQ(M->f.rd.port, LINK_KST_LEAVING);
        CHECK(kst_panel_holds_port());
        CHECK_STR_EQ(status(0), TR(KP_STATE_LEAVING));
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 0, 0), TR(KP_ABORT));
        /* CLOSE does not give the screen back to the tester. */
        tap(kst_panel_close_rect());
        CHECK(kst_panel_is_open());
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_CUT);
        CHECK_STR_EQ(said(), TR(KP_M_LEAVE_CUT));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(kst_prog_have_servo(M));
        CHECK(!kst_prog_in_doubt(M));
        CHECK(kst_panel_holds_port());
        CHECK(g_rig.sup_on);
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 0, 0), TR(KP_LEAVE));
        /* The whole wait, then PWM. */
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        rig_frames(4900);
        CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
        CHECK(kst_panel_holds_port());
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
        CHECK(g_rig.sup_on);
    }
}

TEST_CASE(a_coprocessor_without_leave_gets_a_stated_message)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
        int base, sets;

        /* Link 4.14 with a PD mini: nothing is sent that would cycle the
         * supply, and the lines name the update and STOP. */
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        CHECK(rig_link_up((uint16_t)(LINK_MINOR_KST_LEAVE - 1u)));
        show();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(g_rig.sup_on);
        base = g_rig.starts;
        sets = (int)g_rig.sup_sets;
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_SUPPLY);
        CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
        CHECK_STR_EQ(t[0], TR(KP_M_LEAVE_SUPPLY));
        CHECK_STR_EQ(t[1], TR(KP_M_LEAVE_SUPPLY_2));
        CHECK_EQ(g_rig.starts, base + 1);
        CHECK_EQ((int)g_rig.sup_sets, sets);
        CHECK(g_rig.sup_on);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(kst_prog_have_servo(M));
        CHECK(kst_panel_holds_port());
        /* STOP ends the session, as the line says: the panel's STOP also
         * writes the supply off, and the port finds it off. */
        rig_stop(true);
        g_rig.sup_on = false;
        rig_frames(KST_PORT_RAIL_OFF_MS + KST_PORT_RAIL_WAIT_MS + 500u);
        settle();
        CHECK(!g_rig.sup_on);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_prog_have_servo(M));
        CHECK(!kst_panel_holds_port());

        /* The same link with no PD mini: the port is back in PWM, and the
         * lines say that no wait was applied. */
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        CHECK(rig_link_up((uint16_t)(LINK_MINOR_KST_LEAVE - 1u)));
        show();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT_NO_WAIT);
        CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
        CHECK_STR_EQ(t[0], TR(KP_M_LEFT));
        CHECK_STR_EQ(t[1], TR(KP_M_LEFT_NO_WAIT));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
    }
}

TEST_CASE(stop_and_a_lost_link_while_leaving_keep_the_port_from_the_tester)
{
    /* STOP with the leave on its way. */
    fresh();
    connected();
    tap(val(PG_SERVO, 0, 0));
    hold(2.1f);
    CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
    rig_stop(true);
    CHECK(kst_panel_holds_port());
    settle();
    /* No PD mini answers: the port stays out of PWM and says so. */
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_panel_holds_port());
    CHECK(msg() == KST_PROG_MSG_LEAVE_CUT
          || msg() == KST_PROG_MSG_REF + (unsigned)LINK_KST_REF_MUST_READ);
    CHECK(kst_prog_in_doubt(M));
    /* The leave of a port that asks for a read applies no wait. */
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_DOUBT);
    CHECK_STR_EQ(said(), TR(KP_M_LEFT));
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(M));
    /* Stopped, the row says why it does nothing. */
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);

    /* The link goes with the leave on its way. */
    fresh();
    connected();
    tap(val(PG_SERVO, 0, 0));
    hold(2.1f);
    CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
    rig_link_lost();
    CHECK_EQ(kst_prog_state(M), KST_PROG_DOWN);
    CHECK_STR_EQ(status(0), TR(KP_ST_DOWN));
    CHECK_EQ(kst_panel_editor(), 0);
    /* No row asks anything of a link that is down. */
    tap(val(PG_SERVO, 0, 0));
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 0);
}

TEST_CASE(the_wait_before_pwm_is_a_row_with_limits_and_is_kept)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        link_up();
        CHECK_EQ(kst_prog_wait_ms(M), 100u);
        /* The list names it beside the voltage. */
        tap(val(PG_SERVO, 2, 1));
        snprintf(want, sizeof(want), TR(KP_PWM_WAIT), 100u);
        CHECK_STR_EQ(kst_panel_choice_label(2), want);
        tap(kst_panel_choice_rect(2));
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK_STR_EQ(kst_panel_keypad()->title, TR(KP_PWM_WAIT_TITLE));
        CHECK_NEAR(kst_panel_keypad()->min, 0.0f, 0.001f);
        CHECK_NEAR(kst_panel_keypad()->max, (float)KST_PROG_WAIT_MAX_MS,
                   0.001f);
        /* The limits are taken. */
        type_text("0");
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(kst_prog_wait_ms(M), 0u);
        CHECK_EQ(settings_get_int(SET_SERVO_PROG_WAIT), 0);
        more_input(2);
        type_text("10000");
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(kst_prog_wait_ms(M), 10000u);
        CHECK_EQ(settings_get_int(SET_SERVO_PROG_WAIT), 10000);
        CHECK(settings_save_asked());
        /* 1ms above is refused: the value stays. */
        more_input(2);
        type_text("10001");
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK(kst_panel_keypad()->refused);
        CHECK_EQ(kst_prog_wait_ms(M), 10000u);
        CHECK_EQ(settings_get_int(SET_SERVO_PROG_WAIT), 10000);
        /* A panel that starts again has the value and the voltage. */
        settings_set(SET_SERVO_PROG_WAIT, 2500.0f);
        settings_set(SET_SERVO_PROG_V, 5.5f);
        settings_set(SET_SERVO_PROG_I, 0.8f);
        kst_panel_init(AREA);
        kst_panel_open();
        CHECK_EQ(kst_prog_wait_ms(M), 2500u);
        CHECK_EQ(kst_prog_supply_mv(M), 5500u);
        CHECK_EQ(kst_prog_supply_ma(M), 800u);
        /* A stored voltage above a standard servo's is not loaded. */
        settings_set(SET_SERVO_PROG_V, 7.4f);
        kst_panel_init(AREA);
        kst_panel_screen_entry();
        CHECK_EQ(kst_prog_supply_mv(M), KST_PANEL_STD_MV);
        settings_init();
    }
}

TEST_CASE(a_count_of_1_is_said_in_the_singular)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        enable();
        edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
        show();
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 4, 1), TR(KP_WRITE_ONE));
        tap(val(PG_SERVO, 4, 1));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
        /* Two are a count. */
        edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 1u));
        edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
        show();
        snprintf(want, sizeof(want), TR(KP_WRITE_N), 2u);
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 4, 1), want);
    }
}

TEST_CASE(connect_reads_the_servo_and_keeps_the_overlay_while_the_port_is_held)
{
    fresh();
    connected();
    /* Two requests: into the mode, then the read. */
    CHECK_EQ(g_rig.starts, 2);
    CHECK_EQ(kst_prog_port(M), RIG_PORT);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_STR_EQ(said(), TR(KP_M_READ));
    CHECK(kst_panel_holds_port());
    CHECK(!kst_prog_write_enabled(M));
    CHECK_STR_EQ(kst_panel_button(PG_SERVO, 0, 0), TR(KP_LEAVE));
    /* CLOSE asks for the way back to PWM; CANCEL stays. */
    tap(kst_panel_close_rect());
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_LEAVE_1)));
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
}

int main(void)
{
    RUN(the_current_limit_is_typed_inside_50_to_3000_ma);
    RUN(a_supply_that_cannot_give_the_voltage_is_refused_by_name);
    RUN(a_supply_that_is_on_under_4_8_v_is_refused_by_name);
    RUN(a_supply_that_does_not_switch_on_ends_the_connect_with_its_line);
    RUN(a_leave_gives_the_port_back_and_switches_no_supply);
    RUN(a_leave_with_a_pd_mini_keeps_the_supply_on);
    RUN(the_tester_has_no_port_until_the_wait_ends_and_abort_keeps_it);
    RUN(a_coprocessor_without_leave_gets_a_stated_message);
    RUN(stop_and_a_lost_link_while_leaving_keep_the_port_from_the_tester);
    RUN(the_wait_before_pwm_is_a_row_with_limits_and_is_kept);
    RUN(a_count_of_1_is_said_in_the_singular);
    RUN(connect_reads_the_servo_and_keeps_the_overlay_while_the_port_is_held);
    return test_summary("kst_panel_leave");
}
