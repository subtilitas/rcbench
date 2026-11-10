/**
 * @file test_servo_test_ready.c
 * @brief TEST's HOLD 2s: ARM + START and its READY card
 *        (servo_test_ready.c, servo_ready.c, servo_start_seq.c): every
 *        text of TEST's gate belongs to exactly one row; from SAFE one
 *        hold arms the bench and starts the run; ARMED has 500ms and a
 *        refused ARM ends with every output off, naming ARM; a press on the
 *        hold during the steps ends them; the run takes the settings the
 *        hold fired on; a second finger opens nothing under the hold; a
 *        refused hold names its row; a PD mini off on a
 *        rail fed from elsewhere refuses the hold, naming PD MINI; a step
 *        past 6.0V starts from SAFE only through the HV warning; and
 *        TEST's SETTINGS list opens on SAFE only, closes as the bench
 *        arms, locks the rows that do not apply and puts the stored values
 *        back on DISCARD.  A row outside the card has no state; the METER
 *        and PD MINI rows name the panel's model; PD MINI shows SUPPLY's
 *        SET A and a tap types it through SUPPLY's keypad and question,
 *        never under the steps, a run or a held supply; a port's PORT PWM
 *        off
 *        reads amber on SAFE and red on ARMED; ARM reads ARMING while the
 *        steps wait; a tap on a row takes its fix (the port list, the
 *        list at STEPS, the HV warning) or names it in the band, and does
 *        nothing under a run or the steps; a row that turns red under the
 *        hold drops it and under the steps ends them, every output off; a
 *        press outside the hold or beside another is not the hold's.  The
 *        bench is servo_bench_rig.h.
 *
 * Cases:
 * - every_gate_text_belongs_to_exactly_one_row
 * - from_safe_one_hold_arms_and_starts
 * - armed_has_500ms_and_a_refused_arm_ends_all_off
 * - a_press_on_the_hold_during_the_steps_ends_them_safe
 * - the_run_takes_the_settings_the_hold_fired_on
 * - a_second_finger_opens_nothing_under_the_hold
 * - a_refused_hold_names_its_row
 * - a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold
 * - a_step_past_6v_from_safe_starts_through_the_hv_warning
 * - the_list_opens_on_safe_only_and_closes_as_the_bench_arms
 * - the_list_locks_the_rows_that_do_not_apply
 * - discard_puts_the_stored_values_back
 * - a_row_outside_the_card_has_no_state
 * - the_meter_and_pd_rows_name_the_modelled_supply
 * - a_port_without_its_pulse_reads_amber_on_safe_and_red_armed
 * - the_arm_row_reads_arming_while_the_steps_wait
 * - a_tap_on_a_row_does_nothing_under_the_steps_or_a_run
 * - a_tap_on_port_opens_the_port_list
 * - a_tap_on_a_red_steps_row_opens_the_list_unless_it_is_refused
 * - a_tap_on_an_amber_steps_row_opens_the_hv_warning
 * - a_tap_on_a_red_row_names_it_and_on_an_amber_row_names_nothing
 * - a_row_that_turns_red_under_the_hold_drops_it
 * - a_row_that_turns_red_under_the_steps_ends_them_all_off
 * - a_press_outside_the_hold_or_beside_another_is_not_the_holds
 * - the_pd_row_shows_set_a_and_a_tap_types_it
 * - set_a_typed_on_a_live_output_waits_for_supplys_question
 * - set_a_takes_no_tap_under_the_steps_a_run_or_a_held_supply
 * - set_a_names_the_test_run_under_the_steps_and_the_run
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "servo_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_footer.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

#include "servo_ready_int.h"

/* servo_screen_int.h's press and list guards and current-limit helpers,
 * which this suite does not take in whole. */
bool svs_press_held(void);
const char *svs_list_refused(void);
bool rcur_set_a(void);
const char *rcur_why(void);
void rcur_edit(void);

/** @brief A text TEST's gate returns, and the row it belongs to. */
typedef struct {
    int        kind;  /**< 0 a TX_ text, 1 a SERVO_STR_ text */
    int        id;    /**< its ID                             */
    test_row_t row;   /**< its row                            */
} gate_text_t;

/** @brief Every text test_why() can return, listed here on its own, with
 *         the row it belongs to. */
static const gate_text_t k_gate[] = {
    { 1, SERVO_STR_START_PROG_STEPS, TRR_PORT },
    { 0, TX_OUT_PORT_HELD, TRR_PORT },
    { 0, TX_SV_LIM_PWM_SAFE, TRR_PORT },
    { 0, TX_SV_LIM_PWM_OFF, TRR_PORT },
    { 0, TX_SV_LIM_PWM_OFF_ARMED, TRR_PORT },
    { 1, SERVO_STR_START_PROG_5V, TRR_PORT },
    { 1, SERVO_STR_START_NO_SUPPLY, TRR_PD },
    { 0, TX_TST_RAIL_FED, TRR_PD },
    { 1, SERVO_STR_START_NO_STEPS, TRR_STEPS },
    { 1, SERVO_STR_START_ABOVE_CAP, TRR_STEPS },
    { 1, SERVO_STR_START_BAD_ENDS, TRR_RANGE },
    { 1, SERVO_STR_START_NOT_ARMED, TRR_ARM },
    { 1, SERVO_STR_START_LIMIT_RUNS, TRR_ARM },
    { 1, SERVO_STR_START_AUTO_RUNS, TRR_ARM },
    { 1, SERVO_STR_START_BUSY, TRR_ARM },
};
#define N_GATE ((int)(sizeof(k_gate) / sizeof(k_gate[0])))

/** @brief Gate text @p g in the language showing. */
static const char *gate_str(const gate_text_t *g)
{
    return g->kind == 0 ? ui_tr((ui_text_id_t)g->id)
                        : ui_servo_str((servo_str_t)g->id);
}

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief A fresh bench on TEST, SAFE, short runs, the band clear. */
static void safe_test(void)
{
    bench_fresh();
    short_runs();
    open_test();
    servo_screen_set_armed(false);
    bench_frames(100u);
    ui_router_set_alert(NULL);
}

/** @brief ARM + START pressed and held until it fires, with the bench's
 *         frames under the finger, then let go. */
static void hold_fire(void)
{
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(2100u);
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
}

/* Each of the gate's texts is one row's, the same row in English and in
 * German; a text the gate does not return is no row's. */
TEST_CASE(every_gate_text_belongs_to_exactly_one_row)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int i = 0; i < N_GATE; ++i) {
            CHECK_EQ(test_row_of(gate_str(&k_gate[i])), k_gate[i].row);
        }
        CHECK_EQ(test_row_of(TR(RA_RANGE)), TRR_COUNT);
        CHECK_EQ(test_row_of(NULL), TRR_COUNT);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* SAFE on TEST: ARM reads amber, ARM + START is offered, and one hold
 * asks the ARM, takes ARMED and starts the run. */
TEST_CASE(from_safe_one_hold_arms_and_starts)
{
    safe_test();
    CHECK(test_hold_blocked(NULL) == NULL);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    hold_fire();
    bench_frames(100u);
    CHECK(servo_screen_testing());
    CHECK(svs_seq_failure() == NULL);
    CHECK(test_ready_row(TRR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
    tap(START_X, START_Y);                       /* STOP TEST */
    bench_frames(500u);
    CHECK(!servo_screen_testing());
    CHECK(bench.disarms >= 1u);
}

/* ARMED has 500ms: an ARM the IOmcu leaves unanswered fails the steps at
 * ARM, in the alert band, the supply's output off and the bench SAFE. */
TEST_CASE(armed_has_500ms_and_a_refused_arm_ends_all_off)
{
    char want[128], cause[64];
    safe_test();
    bench.no_arm = true;
    bench.out = true;
    supply_screen_set_output(true);
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(2060u);
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    CHECK(svs_seq_busy());
    bench_frames(300u);
    CHECK(svs_seq_busy());
    bench_frames(300u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_testing());
    snprintf(cause, sizeof(cause), TR(SQ_NOT_ARMED), 500u);
    snprintf(want, sizeof(want), TR(SQ_FAILED), "ARM", cause);
    const char *failed = svs_seq_failure();
    CHECK_STR_EQ(failed != NULL ? failed : "", want);
    CHECK_STR_EQ(alert(), want);
    bench_frames(200u);
    CHECK(!bench.out);
    CHECK(!servo_screen_arm_asked());
}

/* A press on the hold while the steps wait for ARMED ends them at once:
 * no run, no ARM left asked, the bench SAFE. */
TEST_CASE(a_press_on_the_hold_during_the_steps_ends_them_safe)
{
    safe_test();
    bench.no_arm = true;
    hold_fire();
    CHECK(svs_seq_busy());
    tap(START_X, START_Y);
    CHECK(!svs_seq_busy());
    bench_frames(200u);
    CHECK(!servo_screen_testing());
    CHECK(!servo_screen_arm_asked());
    bench.no_arm = false;
    bench_frames(1000u);
    CHECK(!servo_screen_testing());
}

/* The run starts on the settings as they read when the hold fired: steps
 * changed while the bench arms do not reach it.  6.0V alone at the hold,
 * 4.8V alone after it: the run steps 6.0V. */
TEST_CASE(the_run_takes_the_settings_the_hold_fired_on)
{
    safe_test();
    settings_set(SET_SERVO_BROWNOUT, 0.0f);
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 1.0f);
    bench.no_arm = true;
    hold_fire();
    CHECK(svs_seq_busy());
    settings_set(SET_SERVO_STEP_48, 1.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    bench.no_arm = false;
    servo_screen_set_armed(true);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    CHECK_NEAR(bench.set_v_max, 6.0f, 1e-3f);
}

/* While a finger holds ARM + START, SETTINGS with a second finger opens
 * nothing, and the hold still fires. */
TEST_CASE(a_second_finger_opens_nothing_under_the_hold)
{
    safe_test();
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(400u);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!servo_screen_list()->open);
    bench_frames(1700u);
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    CHECK(!servo_screen_list()->open);
    tap(START_X, START_Y);                       /* STOP TEST */
}

/* No step chosen: STEPS reads red with its reason, a 2.1s hold starts
 * nothing, and the refusal a tap hands the band names the row. */
TEST_CASE(a_refused_hold_names_its_row)
{
    char want[160];
    safe_test();
    settings_set(SET_SERVO_BROWNOUT, 0.0f);
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    bench_frames(40u);
    test_row_t row = TRR_COUNT;
    const char *why = test_hold_blocked(&row);
    CHECK(why == ui_servo_str(SERVO_STR_START_NO_STEPS));
    CHECK_EQ(row, TRR_STEPS);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_STEPS, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK(r.why == why);
    hold_fire();
    CHECK(!servo_screen_testing());
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_arm_asked());
    /* A tap on the grey hold hands ready_refusal()'s line, the row and
     * its reason, to the band as a notice (ui_action_refused()). */
    char got[160];
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(TST_STEPS), why);
    const char *line = ready_refusal(got, sizeof(got));
    CHECK_STR_EQ(line != NULL ? line : "", want);
    tap(START_X, START_Y);
    CHECK(!svs_seq_busy());
}

/* The PD mini online with its output off, the INA3221 reading 6.0V on the
 * rail: something else feeds the servo.  PD MINI reads red with the
 * reason and the hold is refused; a dead rail takes the refusal away. */
TEST_CASE(a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold)
{
    bench_on_ina();
    servo_screen_set_armed(false);
    open_test();
    bench.out = false;
    bench.rail_set = true;
    bench.rail_mv = 6000u;
    bench_frames(400u);
    test_row_t row = TRR_COUNT;
    const char *why = test_hold_blocked(&row);
    CHECK(why != NULL && why == TR(TST_RAIL_FED));
    CHECK_EQ(row, TRR_PD);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_PD, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    hold_fire();
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_testing());
    bench.rail_mv = 0u;
    bench_frames(400u);
    CHECK(test_hold_blocked(NULL) == NULL);
}

/* HV SERVO on with 7.4V and 8.4V chosen, on a SAFE bench: STEPS names the
 * HV warning, the hold is a tap that opens it, and the warning's 2s hold
 * arms the bench and starts the run. */
TEST_CASE(a_step_past_6v_from_safe_starts_through_the_hv_warning)
{
    safe_test();
    test_hv_on();
    servo_screen_set_armed(false);
    bench_frames(100u);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_STEPS, &r));
    CHECK(r.why == TR(SV_TAP_HV));
    tap(START_X, START_Y);
    CHECK(!svs_seq_busy());
    scr->render(&cv, 0);
    CHECK_EQ(fb[UI_MARGIN * W + UI_MARGIN], ui_theme_color(UI_C_DANGER));
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(2300u);
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    tap(START_X, START_Y);                       /* STOP TEST */
}

/* SETTINGS opens TEST's list on a SAFE bench only, and the list closes as
 * the bench arms under it. */
TEST_CASE(the_list_opens_on_safe_only_and_closes_as_the_bench_arms)
{
    safe_test();
    servo_screen_set_armed(true);
    bench_frames(40u);
    tap_settings();
    CHECK(!servo_screen_list()->open);
    servo_screen_set_armed(false);
    bench_frames(40u);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK_EQ(ui_setlist_count(servo_screen_list()), TL_ROWS);
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(!servo_screen_list()->open);
}

/* 7.4V and 8.4V take no press with HV SERVO off; TEST TIME none while the
 * length goes by moves, MOVEMENTS none while it goes by time. */
TEST_CASE(the_list_locks_the_rows_that_do_not_apply)
{
    safe_test();
    tap_settings();
    CHECK(servo_screen_list()->open);
    const bool s74 = settings_get_bool(SET_SERVO_STEP_74);
    sl_step(TL_STEP_74, +1);
    CHECK_EQ(settings_get_bool(SET_SERVO_STEP_74), s74);
    sl_step(TL_HV, +1);
    sl_step(TL_STEP_74, +1);
    CHECK(settings_get_bool(SET_SERVO_STEP_74) != s74);

    CHECK_EQ(settings_get_int(SET_SERVO_LEN_BY), 1);      /* by moves */
    const int len = settings_get_int(SET_SERVO_LEN_S);
    sl_step(TL_LEN_S, +1);
    CHECK_EQ(settings_get_int(SET_SERVO_LEN_S), len);
    const int moves = settings_get_int(SET_SERVO_LEN_MOVES);
    sl_step(TL_MOVES, +1);
    CHECK(settings_get_int(SET_SERVO_LEN_MOVES) != moves);
}

/* A change not saved holds the list's leave: DISCARD puts the stored value
 * back and writes nothing. */
TEST_CASE(discard_puts_the_stored_values_back)
{
    safe_test();
    tap_settings();
    CHECK(servo_screen_list()->open);
    const float stall = settings_get(SET_SERVO_STALL_A);
    sl_step(TL_STALL, +1);
    CHECK(settings_get(SET_SERVO_STALL_A) != stall);
    CHECK(!settings_save_asked());
    CHECK(home_tag());
    CHECK(servo_screen_list()->warn);
    const gfx_rect_t d = ui_setlist_strip_rect(1);       /* DISCARD */
    tap(d.x + d.w / 2, d.y + d.h / 2);
    CHECK_NEAR(settings_get(SET_SERVO_STALL_A), stall, 1e-6f);
    CHECK(!settings_save_asked());
}

/* A tap on row @p i of the READY card, in the screen's coordinates. */
static void tap_row(int i)
{
    tap(SV_RDY_X, SV_RDY_ROW_Y(i));
}

/* Rows outside the card have no state, and a tap on one does nothing. */
TEST_CASE(a_row_outside_the_card_has_no_state)
{
    safe_test();
    rdy_row_t r;
    CHECK(!test_ready_row(-1, &r));
    CHECK(!test_ready_row((int)TRR_COUNT, &r));
    CHECK(!test_ready_row(TRR_PORT, NULL));
    test_ready_tap(-1);
    test_ready_tap((int)TRR_COUNT);
    CHECK_STR_EQ(alert(), "");
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK(!servo_screen_list()->open);
    CHECK(!svs_seq_busy());
}

/* With the panel's model as the supply, METER and PD MINI read the model
 * and are met; a run that began on it keeps METER on the model whatever
 * the supply is switched to after. */
TEST_CASE(the_meter_and_pd_rows_name_the_modelled_supply)
{
    bench_on_ina();
    supply_screen_set_model(true);
    open_test();
    bench_frames(200u);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_METER, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(r.value, TR(SV_METER_MODEL));
    CHECK(test_ready_row(TRR_PD, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    char want[40];
    snprintf(want, sizeof(want), "%s %.2fA", TR(SV_METER_MODEL),
             (double)supply_screen_set_i());
    CHECK_STR_EQ(r.value, want);
    CHECK(test_hold_blocked(NULL) == NULL);
    hold_fire();
    bench_frames(100u);
    CHECK(servo_screen_testing());
    supply_screen_set_model(false);
    CHECK(test_ready_row(TRR_METER, &r));
    CHECK_STR_EQ(r.value, TR(SV_METER_MODEL));
    tap(START_X, START_Y);                       /* STOP TEST */
    bench_frames(500u);
    CHECK(!servo_screen_testing());
}

/* A port whose pulse is not out: PORT reads amber, START SWITCHES IT ON,
 * on a SAFE bench, whether PORT PWM waits for SAFE or is off; on an ARMED
 * bench the same texts are red and refuse the hold on PORT. */
TEST_CASE(a_port_without_its_pulse_reads_amber_on_safe_and_red_armed)
{
    safe_test();
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    servo_screen_set_drive(1u, 13u, false);
    bench_frames(100u);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_PORT, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK(r.why == TR(SV_LIM_PWM_SAFE));
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ON));
    CHECK(r.fix);
    CHECK(test_hold_blocked(NULL) == NULL);

    settings_set(SET_SERVO_PORT_PWM, 0.0f);
    CHECK(test_ready_row(TRR_PORT, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK(r.why == TR(SV_LIM_PWM_OFF));
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ON));
    CHECK(test_hold_blocked(NULL) == NULL);

    servo_screen_set_armed(true);
    bench_frames(40u);
    test_row_t row = TRR_COUNT;
    CHECK(test_ready_row(TRR_PORT, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK(r.why == TR(SV_LIM_PWM_OFF_ARMED));
    CHECK_STR_EQ(r.value, TR(RDY_TAG_PWM_DISARM));     /* its tag */
    CHECK(test_hold_blocked(&row) == TR(SV_LIM_PWM_OFF_ARMED));
    CHECK_EQ(row, TRR_PORT);

    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    CHECK(test_ready_row(TRR_PORT, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK(r.why == TR(SV_LIM_PWM_SAFE));
    CHECK(test_hold_blocked(&row) == TR(SV_LIM_PWM_SAFE));
    CHECK_EQ(row, TRR_PORT);
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
}

/* The hold fired and ARM not answered: ARM reads ARMING, the hold's slot
 * names the step in amber, and the hold's frame is not the idle one. */
TEST_CASE(the_arm_row_reads_arming_while_the_steps_wait)
{
    safe_test();
    rdy_row_t r;
    CHECK(test_ready_row(TRR_ARM, &r));
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ARM));
    scr->render(&cv, 0);
    const gfx_rect_t hold = ui_footer_rect(UI_FOOTER_HOLD);
    const gfx_color_t idle = fb[(size_t)(hold.y + hold.h / 2) * W
                                + (size_t)hold.x + 6u];
    bench.no_arm = true;
    hold_fire();
    CHECK(svs_seq_busy());
    CHECK(servo_screen_arm_asked());
    CHECK(test_ready_row(TRR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_ARMING));
    CHECK_STR_EQ(r.label, "ARM");
    char label[40];
    CHECK(strlen(svs_seq_label(label, sizeof(label))) > 0u);
    scr->render(&cv, 0);
    const gfx_color_t waiting = fb[(size_t)(hold.y + hold.h / 2) * W
                                   + (size_t)hold.x + 6u];
    CHECK(waiting != idle);
    tap(START_X, START_Y);                       /* a press ends the steps */
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_arm_asked());
}

/* The rows as the steps or the run started: a tap on PORT opens no list
 * and a tap on a red STEPS opens none, whichever of the two is under
 * way. */
TEST_CASE(a_tap_on_a_row_does_nothing_under_the_steps_or_a_run)
{
    safe_test();
    bench.no_arm = true;
    hold_fire();
    CHECK(svs_seq_busy());
    tap_row(TRR_PORT);
    CHECK_EQ(servo_screen_port_choices(), -1);
    tap_row(TRR_STEPS);
    CHECK(!servo_screen_list()->open);
    tap(START_X, START_Y);                       /* ends the steps */
    CHECK(!svs_seq_busy());

    bench.no_arm = false;
    servo_screen_set_armed(true);
    hold_fire();
    bench_frames(100u);
    CHECK(servo_screen_testing());
    tap_row(TRR_PORT);
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK(servo_screen_testing());
    tap(START_X, START_Y);                       /* STOP TEST */
    bench_frames(500u);
    CHECK(!servo_screen_testing());
}

/* A tap on PORT opens the choice of OUTPUTS and the four PROG PORTs. */
TEST_CASE(a_tap_on_port_opens_the_port_list)
{
    safe_test();
    CHECK_EQ(servo_screen_port_choices(), -1);
    tap_row(TRR_PORT);
    CHECK_EQ(servo_screen_port_choices(), 5);
}

/* A tap on STEPS with no step chosen opens TEST's list at its steps; with
 * a finger on the hold it names the held press, and on an ARMED bench it
 * names the SAFE the list needs.  Neither opens the list. */
TEST_CASE(a_tap_on_a_red_steps_row_opens_the_list_unless_it_is_refused)
{
    safe_test();
    settings_set(SET_SERVO_BROWNOUT, 0.0f);
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    bench_frames(40u);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_STEPS, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK(r.fix);

    /* A finger on the hold: nothing opens. */
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    test_ready_tap(TRR_STEPS);
    CHECK(!servo_screen_list()->open);
    CHECK(svs_press_held());
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);

    /* ARMED: the list is for SAFE. */
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(svs_list_refused() == TR(FOOT_SETTINGS_SAFE));
    tap_row(TRR_STEPS);
    CHECK(!servo_screen_list()->open);

    servo_screen_set_armed(false);
    bench_frames(40u);
    CHECK(svs_list_refused() == NULL);
    tap_row(TRR_STEPS);
    CHECK(servo_screen_list()->open);
}

/* A step past 6.0V turns STEPS amber, TAP: HV WARNING; a tap on it opens
 * the warning, whose 2s hold arms the bench and starts the run. */
TEST_CASE(a_tap_on_an_amber_steps_row_opens_the_hv_warning)
{
    safe_test();
    test_hv_on();
    servo_screen_set_armed(false);
    bench_frames(100u);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_STEPS, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(SV_TAP_HV));
    CHECK(r.fix);
    tap_row(TRR_STEPS);
    CHECK(!svs_seq_busy());
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(2300u);
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    tap(START_X, START_Y);                       /* STOP TEST */
}

/* A tap on a red row that is not STEPS names the row and its reason in
 * the band; a tap on a row the hold does itself names nothing. */
TEST_CASE(a_tap_on_a_red_row_names_it_and_on_an_amber_row_names_nothing)
{
    char want[160];
    bench_on_ina();
    servo_screen_set_armed(false);
    open_test();
    bench.out = false;
    bench.rail_set = true;
    bench.rail_mv = 6000u;
    bench_frames(400u);
    ui_router_set_alert(NULL);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_PD, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    tap_row(TRR_PD);
    snprintf(want, sizeof(want), TR(RDY_BAND), "PD MINI", TR(TST_RAIL_FED));
    CHECK_STR_EQ(alert(), want);

    ui_router_set_alert(NULL);
    CHECK(test_ready_row(TRR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    tap_row(TRR_ARM);
    CHECK_STR_EQ(alert(), "");
    tap_row(TRR_METER);                          /* met: nothing to name */
    CHECK_STR_EQ(alert(), "");
}

/* A row that turns red while the finger holds ARM + START drops the hold:
 * it does not fire, not at 2.1s either, and no ARM is asked. */
TEST_CASE(a_row_that_turns_red_under_the_hold_drops_it)
{
    safe_test();
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(1000u);
    CHECK(!svs_seq_busy());
    settings_set(SET_SERVO_BROWNOUT, 0.0f);
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    bench_frames(1200u);
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    bench_frames(100u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_testing());
    CHECK(!servo_screen_arm_asked());
    CHECK_EQ(bench.arms, 0u);
}

/* A row that turns red while the steps wait for ARMED ends them: the
 * failure names the reason in the band, the bench is SAFE and the
 * supply's output is off. */
TEST_CASE(a_row_that_turns_red_under_the_steps_ends_them_all_off)
{
    safe_test();
    bench.no_arm = true;
    bench.out = true;
    supply_screen_set_output(true);
    hold_fire();
    CHECK(svs_seq_busy());
    settings_set(SET_SERVO_BROWNOUT, 0.0f);
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    bench_frames(100u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_testing());
    const char *failed = svs_seq_failure();
    CHECK(failed != NULL
          && strstr(failed, ui_servo_str(SERVO_STR_START_NO_STEPS)) != NULL);
    CHECK_STR_EQ(alert(), failed != NULL ? failed : "");
    bench_frames(200u);
    CHECK(!bench.out);
    CHECK(!servo_screen_arm_asked());
}

/* A press away from the hold is not the hold's, and a second finger on
 * the hold while the first is down starts nothing of its own: the first
 * finger's hold fires once. */
TEST_CASE(a_press_outside_the_hold_or_beside_another_is_not_the_holds)
{
    safe_test();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 2, .y = 2, .strength = 40 } };
    CHECK(!test_hold_down(NULL));
    CHECK(!test_hold_down(&e));
    CHECK(!svs_seq_busy());

    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(400u);
    e.point.id = 2;
    e.point.x = START_X;
    e.point.y = START_Y;
    CHECK(test_hold_down(&e));
    bench_frames(1700u);
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    CHECK_EQ(bench.arms, 1u);
    tap(START_X, START_Y);                       /* STOP TEST */
}

/** @brief The amber PD MINI row's text for SET A @p a in the language
 *         showing.  @param a the limit, A
 *  @return the text, in a buffer of the next call */
static const char *seq_text(float a)
{
    static char buf[40];
    char amps[16];
    snprintf(amps, sizeof(amps), "%.2fA", (double)a);
    snprintf(buf, sizeof(buf), TR(RDY_SEQ_ON_AT), amps);
    return buf;
}

/* PD MINI shows SUPPLY's SET A, the current limit the steps set: amber
 * `ON AT START 2.00A` while the output is off (in German `EIN BEI START`),
 * `ON 1.50A` with the output on; 20 cells at most in both languages.  A tap
 * opens SUPPLY's keypad over the left column, CURRENT LIMIT in A within
 * SUPPLY's caps, and OK sets SET A, SET V kept, and the row follows. */
TEST_CASE(the_pd_row_shows_set_a_and_a_tap_types_it)
{
    safe_test();
    rdy_row_t r;
    CHECK(test_ready_row(TRR_PD, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, seq_text(supply_screen_set_i()));
    CHECK(r.fix);
    const float v0 = supply_screen_set_v();
    const supply_caps_t caps = supply_screen_caps();
    tap_row(TRR_PD);
    const ui_keypad_t *kp = servo_screen_keypad();
    CHECK(kp->open);
    CHECK_STR_EQ(kp->title, TR(SUP_CURRENT_LIMIT));
    CHECK(fabsf(kp->min - caps.i_min) < 0.001f);
    CHECK(fabsf(kp->max - caps.i_max) < 0.001f);
    keys("1.5");
    CHECK(!kp->open);
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 0.001f);
    CHECK(fabsf(supply_screen_set_v() - v0) < 0.001f);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(test_ready_row(TRR_PD, &r));
        CHECK_STR_EQ(r.value, seq_text(1.5f));
        CHECK(gfx_text_cells(r.value) <= 20);
    }
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(r.value, "EIN BEI START 1.50A");
    ui_text_set_language(UI_LANG_EN);
    bench.out = true;
    bench_frames(300u);
    CHECK(test_ready_row(TRR_PD, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
    CHECK_STR_EQ(r.value, "ON 1.50A");
    CHECK(r.fix);
}

/* SET A typed while the output is on waits for SUPPLY's question, as on
 * SUPPLY with Confirm keypad on: APPLY sets it; CANCEL, and
 * an output gone off under the question, leave SET A as it was. */
TEST_CASE(set_a_typed_on_a_live_output_waits_for_supplys_question)
{
    safe_test();
    settings_set(SET_SUPPLY_CONFIRM_KEYS, 1.0f);
    bench.out = true;
    bench_frames(300u);
    const float i0 = supply_screen_set_i();
    tap_row(TRR_PD);
    keys("1.1");
    CHECK(servo_screen_output_held());          /* the question stands */
    CHECK(fabsf(supply_screen_set_i() - i0) < 0.001f);
    scr->render(&cv, 0);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(!servo_screen_output_held());
    CHECK(fabsf(supply_screen_set_i() - 1.1f) < 0.001f);

    tap_row(TRR_PD);
    keys("0.9");
    CHECK(servo_screen_output_held());
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK(!servo_screen_output_held());
    CHECK(fabsf(supply_screen_set_i() - 1.1f) < 0.001f);

    tap_row(TRR_PD);
    keys("0.9");
    CHECK(servo_screen_output_held());
    bench.out = false;
    bench_frames(300u);
    CHECK(!servo_screen_output_held());
    CHECK(fabsf(supply_screen_set_i() - 1.1f) < 0.001f);
}

/* Under the steps and under a run PD MINI opens no keypad, as TEST's other
 * rows do nothing then; the run's row shows the limit it started with.  A
 * programming port that holds the supply refuses it too. */
TEST_CASE(set_a_takes_no_tap_under_the_steps_a_run_or_a_held_supply)
{
    safe_test();
    bench.no_arm = true;
    hold_fire();
    CHECK(svs_seq_busy());
    tap_row(TRR_PD);
    CHECK(!servo_screen_keypad()->open);
    tap(START_X, START_Y);                       /* ends the steps */
    CHECK(!svs_seq_busy());

    bench.no_arm = false;
    servo_screen_set_armed(true);
    const float i_run = supply_screen_set_i();
    hold_fire();
    bench_frames(100u);
    CHECK(servo_screen_testing());
    rdy_row_t r;
    CHECK(test_ready_row(TRR_PD, &r));
    char want[24];
    snprintf(want, sizeof(want), "%.2fA", (double)i_run);
    CHECK(strstr(r.value, want) != NULL);
    tap_row(TRR_PD);
    CHECK(!servo_screen_keypad()->open);
    tap(START_X, START_Y);                       /* STOP TEST */
    bench_frames(500u);
    CHECK(!servo_screen_testing());

    servo_screen_set_armed(false);
    bench_frames(100u);
    supply_screen_set_port_held(true);
    tap_row(TRR_PD);
    CHECK(!servo_screen_keypad()->open);
    CHECK(fabsf(supply_screen_set_i() - i_run) < 0.001f);
    supply_screen_set_port_held(false);
}

/* rcur_why() and rcur_edit() on TEST: under the steps and under the run
 * SET A is refused with the run's reason, and the edit opens no keypad and
 * leaves SET A as it is; with neither, nothing is refused and the edit
 * opens the keypad. */
TEST_CASE(set_a_names_the_test_run_under_the_steps_and_the_run)
{
    safe_test();
    CHECK(rcur_why() == NULL);
    const float i0 = supply_screen_set_i();

    bench.no_arm = true;
    hold_fire();
    CHECK(svs_seq_busy());
    CHECK(!servo_screen_testing());
    CHECK_STR_EQ(rcur_why(), ui_servo_str(SERVO_STR_START_TEST_RUNS));
    ui_router_set_alert(NULL);
    rcur_edit();
    CHECK(!servo_screen_keypad()->open);
    tap(START_X, START_Y);                       /* ends the steps */
    CHECK(!svs_seq_busy());
    CHECK(rcur_why() == NULL);

    bench.no_arm = false;
    servo_screen_set_armed(true);
    hold_fire();
    bench_frames(100u);
    CHECK(servo_screen_testing());
    CHECK_STR_EQ(rcur_why(), ui_servo_str(SERVO_STR_START_TEST_RUNS));
    rcur_edit();
    CHECK(!servo_screen_keypad()->open);
    CHECK(fabsf(supply_screen_set_i() - i0) < 0.001f);
    tap(START_X, START_Y);                       /* STOP TEST */
    bench_frames(500u);
    CHECK(!servo_screen_testing());

    servo_screen_set_armed(false);
    bench_frames(100u);
    CHECK(rcur_why() == NULL);
    rcur_edit();
    CHECK(servo_screen_keypad()->open);
}

int main(void)
{
    RUN(every_gate_text_belongs_to_exactly_one_row);
    RUN(from_safe_one_hold_arms_and_starts);
    RUN(armed_has_500ms_and_a_refused_arm_ends_all_off);
    RUN(a_press_on_the_hold_during_the_steps_ends_them_safe);
    RUN(the_run_takes_the_settings_the_hold_fired_on);
    RUN(a_second_finger_opens_nothing_under_the_hold);
    RUN(a_refused_hold_names_its_row);
    RUN(a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold);
    RUN(a_step_past_6v_from_safe_starts_through_the_hv_warning);
    RUN(the_list_opens_on_safe_only_and_closes_as_the_bench_arms);
    RUN(the_list_locks_the_rows_that_do_not_apply);
    RUN(discard_puts_the_stored_values_back);
    RUN(a_row_outside_the_card_has_no_state);
    RUN(the_meter_and_pd_rows_name_the_modelled_supply);
    RUN(a_port_without_its_pulse_reads_amber_on_safe_and_red_armed);
    RUN(the_arm_row_reads_arming_while_the_steps_wait);
    RUN(a_tap_on_a_row_does_nothing_under_the_steps_or_a_run);
    RUN(a_tap_on_port_opens_the_port_list);
    RUN(a_tap_on_a_red_steps_row_opens_the_list_unless_it_is_refused);
    RUN(a_tap_on_an_amber_steps_row_opens_the_hv_warning);
    RUN(a_tap_on_a_red_row_names_it_and_on_an_amber_row_names_nothing);
    RUN(a_row_that_turns_red_under_the_hold_drops_it);
    RUN(a_row_that_turns_red_under_the_steps_ends_them_all_off);
    RUN(a_press_outside_the_hold_or_beside_another_is_not_the_holds);
    RUN(the_pd_row_shows_set_a_and_a_tap_types_it);
    RUN(set_a_typed_on_a_live_output_waits_for_supplys_question);
    RUN(set_a_takes_no_tap_under_the_steps_a_run_or_a_held_supply);
    RUN(set_a_names_the_test_run_under_the_steps_and_the_run);
    return test_summary("servo_test_ready");
}
