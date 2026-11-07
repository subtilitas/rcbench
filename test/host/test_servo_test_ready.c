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
 *        back on DISCARD.  The bench is servo_bench_rig.h.
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
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

#include "servo_ready_int.h"

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
    CHECK_STR_EQ(svs_seq_failure(), want);
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
    CHECK_STR_EQ(why, ui_servo_str(SERVO_STR_START_NO_STEPS));
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
    CHECK_STR_EQ(ready_refusal(got, sizeof(got)), want);
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
    CHECK_EQ(ui_setlist_count(servo_screen_list()), 21);
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
    return test_summary("servo_test_ready");
}
