/**
 * @file test_servo_lists_rows.c
 * @brief The rows of SERVO's SETTINGS lists as the list reads them: the
 *        hub's profile keys and FRAME RATE's list, RESET, RUDDER's ENC
 *        CENTRE and ANGLE SOURCE, RUDDER AUTO's SERVO and SERVO PULSES
 *        with the reason each gives while it takes no press, and a list
 *        refused at the hub.
 *
 * Cases:
 * - the_hubs_keys_step_centre_max_travel_and_hv
 * - pulse_min_steps_10_us_and_stops_50_us_under_centre
 * - the_rate_list_marks_the_rate_in_force
 * - reset_on_the_hub_puts_the_profile_back
 * - enc_centre_takes_the_live_count_of_an_as5600_only
 * - angle_source_names_the_source_in_force
 * - servo_family_steps_on_the_programming_port_only
 * - servo_pulses_name_their_reason_until_a_pd_mini_feeds
 * - a_finger_on_a_programmer_control_keeps_the_hubs_list_shut
 * - the_servos_name_takes_the_name_keyboard
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#include "kst_panel.h"

/** @brief The open list's row @p i as the list reads it.
 *  @param i the row @return its hooks */
static const ui_setlist_custom_t *hooks(int i)
{
    const ui_setlist_t *sl = servo_screen_list();
    return sl->host->row(i).custom;
}

/** @brief The text row @p i shows as its value.
 *  @param i the row @return the text, in a buffer of the next call */
static const char *value_of(int i)
{
    static char buf[48];
    buf[0] = '\0';
    hooks(i)->value_text(buf, sizeof(buf));
    return buf;
}

/** @brief The reason row @p i takes no press, "" for none.
 *  @param i the row @return the reason */
static const char *why_of(int i)
{
    const char *w = hooks(i)->locked();
    return w != NULL ? w : "";
}

/* One step on each key of the profile's list moves its value by the row's
 * step, the keys of TRAVEL and PULSE CENTRE and MAX at 5 degrees and 10us,
 * and the two switches flip. */
TEST_CASE(the_hubs_keys_step_centre_max_travel_and_hv)
{
    fresh();
    open_settings();
    CHECK_STR_EQ(value_of(HL_CENTRE), "1500us");
    sl_step(HL_CENTRE, +1);
    CHECK_STR_EQ(value_of(HL_CENTRE), "1510us");
    sl_step(HL_CENTRE, -1);
    sl_step(HL_CENTRE, -1);
    CHECK_STR_EQ(value_of(HL_CENTRE), "1490us");

    CHECK_STR_EQ(value_of(HL_MAX), "2000us");
    sl_step(HL_MAX, -1);
    CHECK_STR_EQ(value_of(HL_MAX), "1990us");

    CHECK_STR_EQ(value_of(HL_TRAVEL), "+/-90");
    sl_step(HL_TRAVEL, -1);
    CHECK_STR_EQ(value_of(HL_TRAVEL), "+/-85");

    char hv0[48];
    snprintf(hv0, sizeof(hv0), "%s", value_of(HL_HV));
    sl_step(HL_HV, +1);
    CHECK(strcmp(value_of(HL_HV), hv0) != 0);
    sl_step(HL_HV, +1);
    CHECK_STR_EQ(value_of(HL_HV), hv0);
    settings_cancel_save();
}

/* PULSE MIN's key steps 10us either way and stops 50us under PULSE
 * CENTRE, as its keypad's range does. */
TEST_CASE(pulse_min_steps_10_us_and_stops_50_us_under_centre)
{
    fresh();
    open_settings();
    CHECK_STR_EQ(value_of(HL_MIN), "1000us");
    sl_step(HL_MIN, -1);
    CHECK_STR_EQ(value_of(HL_MIN), "990us");
    sl_step(HL_MIN, +1);
    sl_step(HL_MIN, +1);
    CHECK_STR_EQ(value_of(HL_MIN), "1010us");
    for (int i = 0; i < 60; ++i) {
        sl_step(HL_MIN, +1);
    }
    CHECK_STR_EQ(value_of(HL_MIN), "1450us");
    settings_cancel_save();
}

/** @brief The fill of list key @p i in buffer 0, left of its label.
 *  @param i the key @return its colour */
static gfx_color_t choice_fill(int i)
{
    scr->tick(0.025f);
    scr->render(&cv, 0);
    return fb[(size_t)CH_Y(i) * W + CH_X(i) - 120];
}

/* FRAME RATE's list draws the rate in force in the accent and the others
 * on the sunk grey.  A key is taken on its release, a finger that moved
 * on it included, and the accent moves to it. */
TEST_CASE(the_rate_list_marks_the_rate_in_force)
{
    fresh();
    open_settings();
    sl_tap(HL_RATE);                           /* 50Hz in force */
    CHECK_EQ(choice_fill(0), ui_theme_color(UI_C_ACCENT));
    CHECK_EQ(choice_fill(1), ui_theme_color(UI_C_PANEL_SUNK));
    ev(CH_X(1), CH_Y(1), TOUCH_EVENT_DOWN, 1);  /* 60Hz */
    ev(CH_X(1) + 6, CH_Y(1), TOUCH_EVENT_MOVE, 1);
    CHECK_EQ(servo_screen_frame_hz(), 50);      /* the release takes it */
    ev(CH_X(1) + 6, CH_Y(1), TOUCH_EVENT_UP, 1);
    CHECK_EQ(servo_screen_frame_hz(), 60);
    sl_tap(HL_RATE);
    CHECK_EQ(choice_fill(0), ui_theme_color(UI_C_PANEL_SUNK));
    CHECK_EQ(choice_fill(1), ui_theme_color(UI_C_ACCENT));
    tap(CH_CANCEL_X, CH_CANCEL_Y);
    CHECK_EQ(servo_screen_frame_hz(), 60);
}

/* RESET on the hub's list sets the profile back to STANDARD PWM at 50Hz,
 * TRIM 0, TRAVEL 90deg, and REVERSE and HV SERVO off. */
TEST_CASE(reset_on_the_hub_puts_the_profile_back)
{
    fresh();
    open_settings();
    char type0[48];
    snprintf(type0, sizeof(type0), "%s", value_of(HL_TYPE));
    char rate0[48];
    snprintf(rate0, sizeof(rate0), "%s", value_of(HL_RATE));
    char rev0[48];
    snprintf(rev0, sizeof(rev0), "%s", value_of(HL_REVERSE));
    char hv0[48];
    snprintf(hv0, sizeof(hv0), "%s", value_of(HL_HV));
    sl_step(HL_TRIM, +1);
    sl_step(HL_TRAVEL, -1);
    sl_step(HL_REVERSE, +1);
    sl_step(HL_HV, +1);
    sl_step(HL_CENTRE, +1);
    CHECK_STR_EQ(value_of(HL_TRIM), "+5");
    CHECK_STR_EQ(value_of(HL_TRAVEL), "+/-85");
    CHECK(strcmp(value_of(HL_REVERSE), rev0) != 0);
    CHECK(strcmp(value_of(HL_HV), hv0) != 0);

    const gfx_rect_t reset = ui_setlist_strip_rect(1);
    tap(reset.x + reset.w / 2, reset.y + reset.h / 2);
    CHECK_STR_EQ(value_of(HL_TYPE), type0);
    CHECK_STR_EQ(value_of(HL_RATE), rate0);
    CHECK_STR_EQ(value_of(HL_TRIM), "+0");
    CHECK_STR_EQ(value_of(HL_TRAVEL), "+/-90");
    CHECK_STR_EQ(value_of(HL_REVERSE), rev0);
    CHECK_STR_EQ(value_of(HL_HV), hv0);
    CHECK_STR_EQ(value_of(HL_CENTRE), "1500us");
    settings_cancel_save();
}

/** @brief The link up, and a reading of @p raw counts when @p raw is not
 *         negative.
 *  @param raw the count, or -1 for none */
static void encoder(int raw)
{
    servo_screen_set_link(true);
    if (raw >= 0) {
        const servo_test_enc_t e = { .valid = true, .raw = (uint16_t)raw,
                                     .taken_ms = 1000u };
        servo_screen_encoder(&e);
    }
}

/* RUDDER's ENC CENTRE shows the stored count.  With no reading, or with the
 * IMU as the source, it names why it takes no tap and a tap keeps the
 * count; with an AS5600 reading on show a tap stores that count. */
TEST_CASE(enc_centre_takes_the_live_count_of_an_as5600_only)
{
    enum { ENC = 1 };
    fresh();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 2048.0f);
    encoder(-1);
    go(SERVO_FN_RUDDER);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK_STR_EQ(value_of(ENC), "2048");
    CHECK_STR_EQ(why_of(ENC), ui_tr(TX_SVL_ENC_NO_READING));
    sl_tap(ENC);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2048);

    encoder(2100);
    CHECK_STR_EQ(why_of(ENC), "");
    sl_tap(ENC);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2100);
    CHECK_STR_EQ(value_of(ENC), "2100");

    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    encoder(2200);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2100);
    sl_tap(ENC);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2100);
    settings_cancel_save();
}

/* ANGLE SOURCE reads the source in force: the AS5600, or the IMU with its
 * axis and sign. */
TEST_CASE(angle_source_names_the_source_in_force)
{
    enum { SRC = 2 };
    fresh();
    go(SERVO_FN_RUDDER);
    tap_settings();
    CHECK_STR_EQ(value_of(SRC), "AS5600");
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    CHECK(strncmp(value_of(SRC), "IMU", 3) == 0);
    settings_cancel_save();
}

/* RUDDER AUTO's SERVO row steps between KST/CHASERVO and HITEC for a run
 * started after it, and only while the target is a programming port; off
 * the port it names why and a key keeps the family. */
TEST_CASE(servo_family_steps_on_the_programming_port_only)
{
    enum { FAM = 4 };
    fresh();
    go(SERVO_FN_RAUTO);
    tap_settings();
    CHECK(servo_screen_list()->open);
    char kst[48];
    snprintf(kst, sizeof(kst), "%s", value_of(FAM));
    CHECK_STR_EQ(kst, ui_tr(TX_SV_LIM_SERVO_KST));
    CHECK_STR_EQ(why_of(FAM), ui_tr(TX_SVL_PORT_ONLY));

    servo_screen_set_drive(1u, 13u, true);
    CHECK_STR_EQ(why_of(FAM), "");
    sl_step(FAM, +1);
    CHECK_STR_EQ(value_of(FAM), ui_tr(TX_SV_LIM_SERVO_HITEC));
    sl_step(FAM, +1);
    CHECK_STR_EQ(value_of(FAM), kst);

    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    sl_step(FAM, +1);
    CHECK_STR_EQ(value_of(FAM), kst);
}

/* RUDDER AUTO's SERVO PULSES row names a port to be on, then a KST or
 * Chaservo servo, then the supply that cuts power, and flips only once a
 * PD mini feeds the servo. */
TEST_CASE(servo_pulses_name_their_reason_until_a_pd_mini_feeds)
{
    enum { FAM = 4, PULSES = 5 };
    fresh();
    go(SERVO_FN_RAUTO);
    tap_settings();
    char keep[48];
    snprintf(keep, sizeof(keep), "%s", value_of(PULSES));
    CHECK_STR_EQ(keep, ui_tr(TX_SV_LIM_PULSES_KEEP));
    CHECK_STR_EQ(why_of(PULSES), ui_tr(TX_SVL_PORT_ONLY));

    servo_screen_set_drive(1u, 13u, true);
    CHECK_STR_EQ(why_of(PULSES), ui_tr(TX_SV_LIM_PROG_NO_PD));
    sl_step(PULSES, +1);
    CHECK_STR_EQ(value_of(PULSES), keep);

    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.samples = 1u;
    servo_screen_supply(&st);
    CHECK_STR_EQ(why_of(PULSES), "");
    sl_step(PULSES, +1);
    CHECK_STR_EQ(value_of(PULSES), ui_tr(TX_SV_LIM_PULSES_STD));
    sl_step(PULSES, +1);
    CHECK_STR_EQ(value_of(PULSES), keep);

    sl_step(FAM, +1);                       /* HITEC has no pulses to set */
    CHECK_STR_EQ(why_of(PULSES), ui_tr(TX_SVL_KST_ONLY));
    sl_step(PULSES, +1);
    CHECK_STR_EQ(value_of(PULSES), keep);
}

/* A finger on a programmer's control keeps the hub's list shut as it keeps
 * a function's: the tap on SETTINGS opens nothing until the finger lifts. */
TEST_CASE(a_finger_on_a_programmer_control_keeps_the_hubs_list_shut)
{
    fresh();
    go(SERVO_FN_KST);
    const gfx_rect_t r = kst_panel_tab_rect(0);
    ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    CHECK(kst_panel_pressed());
    go(SERVO_FN_HUB);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!servo_screen_list()->open);
    go(SERVO_FN_KST);
    ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
    CHECK(!kst_panel_pressed());
    go(SERVO_FN_HUB);
    tap_settings();
    CHECK(servo_screen_list()->open);
}

/* The servo's name types on the name keyboard, not the card folder's: a
 * name with a space and more than eight characters is taken on OK. */
TEST_CASE(the_servos_name_takes_the_name_keyboard)
{
    fresh();
    settings_set_text(SET_TEXT_DUT_NAME, "MY SERVO 12");
    open_settings();
    sl_tap(HL_NAME);
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->tk.open);
    CHECK(!sl->tk.folder);
    const gfx_rect_t ok = ui_textkey_key_rect(&sl->tk, UI_TK_OK);
    tap(ok.x + ok.w / 2, ok.y + ok.h / 2);
    CHECK(!sl->tk.open);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "MY SERVO 12");
}

int main(void)
{
    RUN(the_hubs_keys_step_centre_max_travel_and_hv);
    RUN(pulse_min_steps_10_us_and_stops_50_us_under_centre);
    RUN(the_rate_list_marks_the_rate_in_force);
    RUN(reset_on_the_hub_puts_the_profile_back);
    RUN(enc_centre_takes_the_live_count_of_an_as5600_only);
    RUN(angle_source_names_the_source_in_force);
    RUN(servo_family_steps_on_the_programming_port_only);
    RUN(servo_pulses_name_their_reason_until_a_pd_mini_feeds);
    RUN(a_finger_on_a_programmer_control_keeps_the_hubs_list_shut);
    RUN(the_servos_name_takes_the_name_keyboard);
    return test_summary("servo_lists_rows");
}
