/**
 * @file test_servo_lists_rows.c
 * @brief The rows of SERVO's SETTINGS lists as the list reads them: the
 *        hub's profile keys and FRAME RATE's list, RESET, RUDDER's ENC
 *        CENTER and ANGLE SOURCE, RUDDER AUTO's SERVO and SERVO PULSES
 *        with the reason each gives while it takes no press, LIMIT RUN's
 *        and RUDDER AUTO's CURRENT (SUPPLY's SET A) and PROG WAIT, and a
 *        list refused at the hub.
 *
 * Cases:
 * - the_hubs_keys_step_center_max_travel_and_hv
 * - pulse_min_steps_10_us_and_stops_50_us_under_center
 * - the_rate_list_marks_the_rate_in_force
 * - reset_on_the_hub_puts_the_profile_back
 * - enc_center_takes_the_live_count_of_an_as5600_only
 * - angle_source_names_the_source_in_force
 * - servo_family_is_picked_on_the_programming_port_only
 * - limit_run_picks_its_family_apart_from_rudder_auto
 * - servo_pulses_name_their_reason_until_a_pd_mini_feeds
 * - a_finger_on_a_programmer_control_keeps_the_hubs_list_shut
 * - the_servos_name_takes_the_name_keyboard
 * - current_shows_and_types_set_a_where_a_pd_mini_feeds
 * - current_is_locked_without_a_pd_mini_and_under_a_held_supply
 * - rudder_auto_holds_current_and_prog_wait
 * - set_a_reads_in_two_or_three_decimals_and_joins_a_rows_value
 * - set_a_applies_on_test_and_where_a_pd_mini_feeds_a_programmer_run
 * - set_a_typed_under_a_port_that_took_the_supply_is_refused
 * - set_a_names_the_esc_wire_session_that_holds_the_supply
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#include "esc_wire_int.h"
#include "kst_panel.h"
#include "servo_limrun.h"
#include "servo_rudder_auto.h"

/* servo_screen_int.h's current-limit helpers, which this suite does not
 * take in whole. */
bool rcur_set_a(void);
void rcur_amps(char *b, size_t n, float a);
void rcur_join(char *b, size_t n, const char *head, bool set_a);
void rcur_seq(char *b, size_t n, bool set_a);
const char *rcur_why(void);
void rcur_edit(void);

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
 * step, the keys of TRAVEL and PULSE CENTER and MAX at 5 degrees and 10us,
 * and the two switches flip. */
TEST_CASE(the_hubs_keys_step_center_max_travel_and_hv)
{
    fresh();
    open_settings();
    CHECK_STR_EQ(value_of(HL_CENTER), "1500us");
    sl_step(HL_CENTER, +1);
    CHECK_STR_EQ(value_of(HL_CENTER), "1510us");
    sl_step(HL_CENTER, -1);
    sl_step(HL_CENTER, -1);
    CHECK_STR_EQ(value_of(HL_CENTER), "1490us");

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
 * CENTER, as its keypad's range does. */
TEST_CASE(pulse_min_steps_10_us_and_stops_50_us_under_center)
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
    sl_step(HL_CENTER, +1);
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
    CHECK_STR_EQ(value_of(HL_CENTER), "1500us");
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

/* RUDDER's ENC CENTER shows the stored count.  With no reading, or with the
 * IMU as the source, it names why it takes no tap and a tap keeps the
 * count; with an AS5600 reading on show a tap stores that count. */
TEST_CASE(enc_center_takes_the_live_count_of_an_as5600_only)
{
    enum { ENC = 1 };
    fresh();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTER, 2048.0f);
    encoder(-1);
    go(SERVO_FN_RUDDER);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK_STR_EQ(value_of(ENC), "2048");
    CHECK_STR_EQ(why_of(ENC), ui_tr(TX_SVL_ENC_NO_READING));
    sl_tap(ENC);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 2048);

    encoder(2100);
    CHECK_STR_EQ(why_of(ENC), "");
    sl_tap(ENC);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 2100);
    CHECK_STR_EQ(value_of(ENC), "2100");

    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    encoder(2200);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 2100);
    sl_tap(ENC);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 2100);
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

/* RUDDER AUTO's SERVO row opens its three families as a choice list, and
 * only while the target is a programming port; off the port it names why
 * and a tap opens nothing.  A pick is RUDDER AUTO's alone: LIMIT RUN keeps
 * its own.  With HITEC D SERIES, D RECEIVE (the HITEC page's setting)
 * follows SERVO; with CLASSIC and KST/CHASERVO it does not. */
TEST_CASE(servo_family_is_picked_on_the_programming_port_only)
{
    fresh();
    go(SERVO_FN_RAUTO);
    tap_settings();
    CHECK(servo_screen_list()->open);
    const int n = ui_setlist_count(servo_screen_list());
    CHECK_STR_EQ(value_of(RAL_SERVO), ui_tr(TX_SV_LIM_SERVO_KST));
    CHECK_STR_EQ(why_of(RAL_SERVO), ui_tr(TX_SVL_PORT_ONLY));
    CHECK_EQ(hooks(RAL_SERVO)->choices(), 3);
    CHECK_STR_EQ(hooks(RAL_SERVO)->choice(1), "HITEC D SERIES");
    sl_choose(RAL_SERVO, 1);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_KST);

    servo_screen_set_drive(1u, 13u, true);
    CHECK_STR_EQ(why_of(RAL_SERVO), "");
    sl_choose(RAL_SERVO, 1);
    CHECK_STR_EQ(value_of(RAL_SERVO), "HITEC D SERIES");
    CHECK_EQ(hooks(RAL_SERVO)->chosen(), 1);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_D);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    CHECK_EQ(ui_setlist_count(servo_screen_list()), n + 1);
    const ui_setlist_row_t rx = servo_screen_list()->host->row(RAL_RX);
    CHECK_EQ(rx.kind, (uint8_t)UI_SL_SETTING);
    CHECK_EQ(rx.id, SET_HITEC_RX);
    sl_choose(RAL_SERVO, 2);
    CHECK_STR_EQ(value_of(RAL_SERVO), "HITEC CLASSIC");
    CHECK_EQ(ui_setlist_count(servo_screen_list()), n);
    sl_choose(RAL_SERVO, 0);
    CHECK_STR_EQ(value_of(RAL_SERVO), ui_tr(TX_SV_LIM_SERVO_KST));

    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    sl_choose(RAL_SERVO, 2);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_KST);
    settings_cancel_save();
}

/* LIMIT RUN's SERVO row is LIMIT RUN's own: a pick there leaves RUDDER
 * AUTO's, and D RECEIVE follows it with HITEC D SERIES. */
TEST_CASE(limit_run_picks_its_family_apart_from_rudder_auto)
{
    fresh();
    servo_screen_set_drive(1u, 13u, true);
    settings_set(SET_RAUTO_FAMILY, (float)SERVO_RAUTO_FAM_HITEC_C);
    go(SERVO_FN_LIMRUN);
    tap_settings();
    CHECK(servo_screen_list()->open);
    const int n = ui_setlist_count(servo_screen_list());
    sl_choose(LL_SERVO, 1);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_HITEC_D);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_C);
    CHECK_EQ(ui_setlist_count(servo_screen_list()), n + 1);
    CHECK_EQ(servo_screen_list()->host->row(LL_RX).id, SET_HITEC_RX);
    settings_cancel_save();
}

/* RUDDER AUTO's SERVO PULSES row names a port to be on, then a KST or
 * Chaservo servo, then the supply that cuts power, and flips only once a
 * PD mini feeds the servo. */
TEST_CASE(servo_pulses_name_their_reason_until_a_pd_mini_feeds)
{
    enum { PULSES = 5 };
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

    /* HITEC CLASSIC has no pulses to set (and no D RECEIVE row above). */
    sl_choose(RAL_SERVO, SERVO_RAUTO_FAM_HITEC_C);
    CHECK_STR_EQ(why_of(PULSES), ui_tr(TX_SVL_KST_ONLY));
    sl_step(PULSES, +1);
    CHECK_STR_EQ(value_of(PULSES), keep);
    settings_cancel_save();
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

/** @brief A PD mini answering with its output off, the servo rail not
 *         read: it feeds the servo, and START would switch it on. */
static void pd_answers(void)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.samples = 1u;
    servo_screen_supply(&st);
}

/** @brief CURRENT's value for SET A @p a, as the row shows it.
 *  @param a the limit, A @return the text, in a buffer of the next call */
static const char *set_a_text(float a)
{
    static char buf[24];
    snprintf(buf, sizeof(buf), "SET A %.2fA", (double)a);
    return buf;
}

/* With a PD mini feeding the servo, LIMIT RUN's CURRENT reads SUPPLY's SET
 * A and takes no lock; a tap opens SUPPLY's keypad over the left column,
 * CURRENT LIMIT in A from the caps' lowest to their highest, and OK sets
 * SET A, SET V kept.  A value past the caps is refused and SET A stays.
 * PROG I is no row of the list: no run takes it. */
TEST_CASE(current_shows_and_types_set_a_where_a_pd_mini_feeds)
{
    fresh();
    pd_answers();
    go(SERVO_FN_LIMRUN);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK_EQ(servo_screen_list()->host->row(LL_CURRENT).custom->label,
             TX_SV_CURRENT);
    CHECK_STR_EQ(value_of(LL_CURRENT), set_a_text(supply_screen_set_i()));
    CHECK_STR_EQ(why_of(LL_CURRENT), "");
    const float v0 = supply_screen_set_v();
    const float i0 = supply_screen_set_i();
    const supply_caps_t caps = supply_screen_caps();
    sl_tap(LL_CURRENT);
    const ui_keypad_t *kp = servo_screen_keypad();
    CHECK(kp->open);
    CHECK_STR_EQ(kp->title, TR(SUP_CURRENT_LIMIT));
    CHECK_STR_EQ(kp->unit, "A");
    CHECK(fabsf(kp->min - caps.i_min) < 0.001f);
    CHECK(fabsf(kp->max - caps.i_max) < 0.001f);
    char past[16];
    snprintf(past, sizeof(past), "%.0f", (double)(caps.i_max + 1.0f));
    keys(past);
    CHECK(kp->open);                    /* refused: past the caps */
    CHECK(fabsf(supply_screen_set_i() - i0) < 0.001f);
    key(UI_KEY_CLR);
    keys("1.25");
    CHECK(!kp->open);
    CHECK(fabsf(supply_screen_set_i() - 1.25f) < 0.001f);
    CHECK(fabsf(supply_screen_set_v() - v0) < 0.001f);
    CHECK(servo_screen_list()->open);
    CHECK_STR_EQ(value_of(LL_CURRENT), "SET A 1.25A");
    settings_cancel_save();
}

/* Without a PD mini no run sets a current limit: CURRENT reads `--`,
 * locked with the reason, and a tap opens no keypad.  A programming port
 * that holds the supply locks it with SUPPLY's own reason. */
TEST_CASE(current_is_locked_without_a_pd_mini_and_under_a_held_supply)
{
    fresh();
    go(SERVO_FN_LIMRUN);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK_STR_EQ(value_of(LL_CURRENT), "--");
    CHECK_STR_EQ(why_of(LL_CURRENT), TR(SVL_CURRENT_NO_PD));
    sl_tap(LL_CURRENT);
    CHECK(!servo_screen_keypad()->open);

    pd_answers();
    CHECK_STR_EQ(why_of(LL_CURRENT), "");
    supply_screen_set_port_held(true);
    CHECK_STR_EQ(why_of(LL_CURRENT), TR(SUP_HELD_REFUSED));
    const float i0 = supply_screen_set_i();
    sl_tap(LL_CURRENT);
    CHECK(!servo_screen_keypad()->open);
    CHECK(fabsf(supply_screen_set_i() - i0) < 0.001f);
    supply_screen_set_port_held(false);
    settings_cancel_save();
}

/* RUDDER AUTO's list holds CURRENT and PROG WAIT after ANGLE SOURCE: SET A
 * as LIMIT RUN's, and the KST programmer's wait before PWM its sessions'
 * LEAVE sends. */
TEST_CASE(rudder_auto_holds_current_and_prog_wait)
{
    fresh();
    go(SERVO_FN_RAUTO);
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK_EQ(ui_setlist_count(sl), RAL_ROWS);
    CHECK_EQ(sl->host->row(RAL_CURRENT).custom->label, TX_SV_CURRENT);
    CHECK_EQ(sl->host->row(RAL_WAIT).kind, (uint8_t)UI_SL_SETTING);
    CHECK_EQ(sl->host->row(RAL_WAIT).id, SET_SERVO_PROG_WAIT);
    CHECK_STR_EQ(value_of(RAL_CURRENT), "--");
    CHECK_STR_EQ(why_of(RAL_CURRENT), TR(SVL_CURRENT_NO_PD));
    pd_answers();
    CHECK_STR_EQ(value_of(RAL_CURRENT), set_a_text(supply_screen_set_i()));
    sl_tap(RAL_CURRENT);
    CHECK(servo_screen_keypad()->open);
    keys("0.5");
    CHECK(fabsf(supply_screen_set_i() - 0.5f) < 0.001f);
    CHECK_STR_EQ(value_of(RAL_CURRENT), "SET A 0.50A");
    settings_cancel_save();
}

/* A current limit shows as `2.00A`, and `0.125A` where its mA are no
 * multiple of 10; it rounds to the mA, and a limit of 0 or less reads
 * 0.00A.  A row's value takes SET A after its head, and the head alone
 * without it; an empty head leaves the amps with no space before them; the
 * amber supply row reads `ON AT START` with the amps, or RDY_SEQ_ON's text
 * without them. */
TEST_CASE(set_a_reads_in_two_or_three_decimals_and_joins_a_rows_value)
{
    char b[32];
    rcur_amps(b, sizeof(b), 2.0f);
    CHECK_STR_EQ(b, "2.00A");
    rcur_amps(b, sizeof(b), 1.5f);
    CHECK_STR_EQ(b, "1.50A");
    rcur_amps(b, sizeof(b), 0.125f);
    CHECK_STR_EQ(b, "0.125A");
    rcur_amps(b, sizeof(b), 12.345f);
    CHECK_STR_EQ(b, "12.345A");
    rcur_amps(b, sizeof(b), 0.1234f);        /* rounds to 123mA */
    CHECK_STR_EQ(b, "0.123A");
    rcur_amps(b, sizeof(b), 0.0004f);        /* under half a mA */
    CHECK_STR_EQ(b, "0.00A");
    rcur_amps(b, sizeof(b), 0.0f);
    CHECK_STR_EQ(b, "0.00A");
    rcur_amps(b, sizeof(b), -1.0f);
    CHECK_STR_EQ(b, "0.00A");

    fresh();
    supply_screen_adopt(5.0f, 1.75f);
    CHECK(fabsf(supply_screen_set_i() - 1.75f) < 0.001f);
    rcur_join(b, sizeof(b), "5.0V", true);
    CHECK_STR_EQ(b, "5.0V 1.75A");
    rcur_join(b, sizeof(b), "ON", true);
    CHECK_STR_EQ(b, "ON 1.75A");
    rcur_join(b, sizeof(b), "", true);
    CHECK_STR_EQ(b, "1.75A");
    rcur_join(b, sizeof(b), "5.0V", false);
    CHECK_STR_EQ(b, "5.0V");
    rcur_join(b, sizeof(b), "", false);
    CHECK_STR_EQ(b, "");
    rcur_seq(b, sizeof(b), true);
    char want[40];
    snprintf(want, sizeof(want), TR(RDY_SEQ_ON_AT), "1.75A");
    CHECK_STR_EQ(b, want);
    rcur_seq(b, sizeof(b), false);
    CHECK_STR_EQ(b, TR(RDY_SEQ_ON));
}

/* TEST's steps always set SET A; LIMIT RUN's and RUDDER AUTO's sessions
 * only where a PD mini feeds the servo; no other function has a run.  Each
 * says why it takes no edit: no PD mini on the two runs and on every other
 * function; none while it applies. */
TEST_CASE(set_a_applies_on_test_and_where_a_pd_mini_feeds_a_programmer_run)
{
    fresh();
    go(SERVO_FN_TEST);
    CHECK(rcur_set_a());
    CHECK(rcur_why() == NULL);
    const servo_fn_t others[] = { SERVO_FN_LIVE, SERVO_FN_KST };
    for (unsigned k = 0; k < sizeof(others) / sizeof(others[0]); ++k) {
        go(others[k]);
        CHECK(!rcur_set_a());
        CHECK_STR_EQ(rcur_why(), TR(SVL_CURRENT_NO_PD));
    }
    const servo_fn_t runs[] = { SERVO_FN_LIMRUN, SERVO_FN_RAUTO };
    for (unsigned k = 0; k < sizeof(runs) / sizeof(runs[0]); ++k) {
        fresh();
        go(runs[k]);
        CHECK(!rcur_set_a());
        CHECK_STR_EQ(rcur_why(), TR(SVL_CURRENT_NO_PD));
        pd_answers();
        CHECK(rcur_set_a());
        CHECK(rcur_why() == NULL);
    }
}

/* A programming port that takes the supply while SET A's keypad stands:
 * OK is refused with SUPPLY's reason, the keypad shuts and SET A stays.  A
 * value that snaps to SET A as it is shuts the keypad and changes nothing. */
TEST_CASE(set_a_typed_under_a_port_that_took_the_supply_is_refused)
{
    fresh();
    pd_answers();
    go(SERVO_FN_LIMRUN);
    supply_screen_adopt(5.0f, 1.5f);
    const float v0 = supply_screen_set_v();
    rcur_edit();
    CHECK(servo_screen_keypad()->open);
    keys("1.5");
    CHECK(!servo_screen_keypad()->open);        /* the same value: no change */
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 0.001f);

    rcur_edit();
    CHECK(servo_screen_keypad()->open);
    supply_screen_set_port_held(true);
    keys("0.5");
    CHECK(!servo_screen_keypad()->open);
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 0.001f);
    CHECK(fabsf(supply_screen_set_v() - v0) < 0.001f);
    supply_screen_set_port_held(false);
}

/* The ESC signal-wire session holds the supply on the carrier as its own
 * word: SET A's row is locked with SUPPLY's ESC reason, a tap opens nothing,
 * and a value typed under a keypad that stood when the session took the
 * supply is refused.  The KST and Hitec ports give SUPPLY's other reason. */
TEST_CASE(set_a_names_the_esc_wire_session_that_holds_the_supply)
{
    fresh();
    pd_answers();
    go(SERVO_FN_RAUTO);
    supply_screen_adopt(5.0f, 1.5f);
    CHECK(rcur_why() == NULL);
    rcur_edit();
    CHECK(servo_screen_keypad()->open);

    const esc_link_readout_t rd = ewp_s.prog.f.rd;
    ewp_s.prog.f.rd.known = true;
    ewp_s.prog.f.rd.held = true;     /* the session has the pin */
    CHECK(esc_wire_panel_holds_port());
    supply_screen_set_ports_own(true);
    supply_screen_set_page_held(true);
    CHECK(supply_screen_port_held());
    CHECK_STR_EQ(rcur_why(), TR(SUP_HELD_REFUSED_ESC));
    keys("0.5");
    CHECK(!servo_screen_keypad()->open);
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 0.001f);

    rcur_edit();
    CHECK(!servo_screen_keypad()->open);
    supply_screen_set_page_held(false);
    supply_screen_set_ports_own(false);
    ewp_s.prog.f.rd = rd;
    CHECK(!esc_wire_panel_holds_port());
    supply_screen_set_port_held(true);
    CHECK_STR_EQ(rcur_why(), TR(SUP_HELD_REFUSED));
    supply_screen_set_port_held(false);
    CHECK(rcur_why() == NULL);
}

int main(void)
{
    RUN(the_hubs_keys_step_center_max_travel_and_hv);
    RUN(pulse_min_steps_10_us_and_stops_50_us_under_center);
    RUN(the_rate_list_marks_the_rate_in_force);
    RUN(reset_on_the_hub_puts_the_profile_back);
    RUN(enc_center_takes_the_live_count_of_an_as5600_only);
    RUN(angle_source_names_the_source_in_force);
    RUN(servo_family_is_picked_on_the_programming_port_only);
    RUN(limit_run_picks_its_family_apart_from_rudder_auto);
    RUN(servo_pulses_name_their_reason_until_a_pd_mini_feeds);
    RUN(a_finger_on_a_programmer_control_keeps_the_hubs_list_shut);
    RUN(the_servos_name_takes_the_name_keyboard);
    RUN(current_shows_and_types_set_a_where_a_pd_mini_feeds);
    RUN(current_is_locked_without_a_pd_mini_and_under_a_held_supply);
    RUN(rudder_auto_holds_current_and_prog_wait);
    RUN(set_a_reads_in_two_or_three_decimals_and_joins_a_rows_value);
    RUN(set_a_applies_on_test_and_where_a_pd_mini_feeds_a_programmer_run);
    RUN(set_a_typed_under_a_port_that_took_the_supply_is_refused);
    RUN(set_a_names_the_esc_wire_session_that_holds_the_supply);
    return test_summary("servo_lists_rows");
}
