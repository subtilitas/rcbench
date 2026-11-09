/**
 * @file test_servo_settings_rows.c
 * @brief The settings each SERVO function screen sets on itself
 *        (servo_lists.c, servo_test_list.c, servo_test_ready.c,
 *        servo_drive_ready.c): the profile opens in place from the type
 *        pill of LIVE, SWEEP and RUDDER, from the RANGE row of LIMIT RUN
 *        and RUDDER AUTO, from TEST's OUTPUT row and from LIVE's SETTINGS,
 *        on the row the tap names and on a SAFE bench only; TEST's RANGE
 *        row opens TEST's list on RANGE, the setting SWEEP's list sets;
 *        DWELL TIME is one setting on SWEEP and TEST; DUT NAME and REPORT
 *        are the hub's on TEST, DUT NAME on LIMIT RUN and RUDDER AUTO;
 *        TEST's LOG row switches USER LOGS and is refused under a run, and
 *        the lists of LIMIT RUN and RUDDER AUTO set the same setting.  The
 *        bench is servo_bench_rig.h.
 *
 * Cases:
 * - the_type_pill_opens_the_profile_on_type
 * - the_type_pill_names_safe_on_an_armed_bench
 * - lives_settings_opens_the_profile
 * - a_range_row_opens_the_profile_on_pulse_min
 * - a_pulse_set_from_a_function_is_the_hubs
 * - the_profile_closes_as_the_bench_arms
 * - tests_range_row_opens_its_list_on_range_shared_with_sweep
 * - tests_output_row_opens_the_profile_on_pulse_min
 * - dwell_is_one_setting_on_sweep_and_test
 * - report_and_dut_name_are_the_hubs_on_test
 * - dut_name_is_typed_on_limit_run_and_rudder_auto
 * - the_log_row_switches_user_logs_and_saves
 * - the_log_row_is_refused_under_a_run
 * - user_logs_is_a_list_row_on_limit_run_and_rudder_auto
 *
 * SPDX-License-Identifier: MIT
 */
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
#include "ui_textkey.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

#include "servo_ready_int.h"

/* servo_screen_int.h's calls this suite reads, which it does not take in
 * whole. */
bool svs_profile_shown(void);
const char *svs_list_refused(void);

/** @brief The hub list's rows: OUTPUT to HV SERVO, DUT, NAME and REPORT. */
#define HL_ROWS (HL_REPORT + 1)

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief A fresh bench on @p fn, SAFE, the band clear.
 *  @param fn the function screen */
static void safe_on(servo_fn_t fn)
{
    bench_fresh();
    short_runs();
    go(fn);
    servo_screen_set_armed(false);
    bench_frames(100u);
    ui_router_set_alert(NULL);
}

/** @brief A tap on the type pill under LIVE's, SWEEP's or RUDDER's
 *         rows. */
static void tap_pill(void)
{
    const gfx_rect_t r = drive_tag_rect();
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief A tap on row @p i of the READY card. */
static void tap_row(int i)
{
    tap(SV_RDY_X, SV_RDY_ROW_Y(i));
}

/** @brief Whether the open list shows row @p i whole, as it opened.
 *  @param i the row @return true then */
static bool row_in_view(int i)
{
    const ui_setlist_t *sl = servo_screen_list();
    const gfx_rect_t pane = ui_setlist_pane(sl);
    const gfx_rect_t r = ui_setlist_row_rect(sl, i);
    return r.h > 0 && r.y >= pane.y && r.y + r.h <= pane.y + pane.h;
}

/** @brief Whether the open list is the profile: its rows, its name.
 *  @return true then */
static bool profile_open(void)
{
    const ui_setlist_t *sl = servo_screen_list();
    return sl->open && svs_profile_shown()
           && sl->host->count() == HL_ROWS
           && sl->host->row(HL_TYPE).kind == (uint8_t)UI_SL_CUSTOM
           && strcmp(sl->host->name(), ui_tr(TX_SVH_SERVO)) == 0;
}

/** @brief The custom row @p i's value as the open list shows it.
 *  @param i the row @return the text, in a buffer of the next call */
static const char *value_of(int i)
{
    static char buf[48];
    buf[0] = '\0';
    servo_screen_list()->host->row(i).custom->value_text(buf, sizeof(buf));
    return buf;
}

/* A tap on the type pill of LIVE, SWEEP and RUDDER opens the profile in
 * the list's place with TYPE in view; the home tag closes it and the
 * function screen shows again. */
TEST_CASE(the_type_pill_opens_the_profile_on_type)
{
    const servo_fn_t fns[] = { SERVO_FN_LIVE, SERVO_FN_SWEEP,
                               SERVO_FN_RUDDER };
    for (size_t k = 0; k < sizeof(fns) / sizeof(fns[0]); ++k) {
        safe_on(fns[k]);
        settings_request_save();        /* no leave question on the tag */
        CHECK(!servo_screen_list()->open);
        tap_pill();
        CHECK(profile_open());
        CHECK(row_in_view(HL_TYPE));
        CHECK(home_tag());
        CHECK(!servo_screen_list()->open);
        CHECK(!svs_profile_shown());
        CHECK(drive_shown());
    }
}

/* On an ARMED bench the pill names the SAFE the list needs and opens
 * nothing; a press dragged off the pill opens nothing either. */
TEST_CASE(the_type_pill_names_safe_on_an_armed_bench)
{
    safe_on(SERVO_FN_LIVE);
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(svs_list_refused() == ui_tr(TX_FOOT_SETTINGS_SAFE));
    tap_pill();
    CHECK(!servo_screen_list()->open);

    servo_screen_set_armed(false);
    bench_frames(40u);
    const gfx_rect_t r = drive_tag_rect();
    ev(r.x + 10, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    ev(r.x + 10, r.y - 80, TOUCH_EVENT_MOVE, 1);
    ev(r.x + 10, r.y - 80, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_list()->open);
}

/* LIVE's SETTINGS is offered and opens the profile, as the hub's does; on
 * an ARMED bench it opens nothing. */
TEST_CASE(lives_settings_opens_the_profile)
{
    safe_on(SERVO_FN_LIVE);
    tap_settings();
    CHECK(profile_open());
    CHECK(row_in_view(HL_OUTPUT));
    settings_request_save();
    CHECK(home_tag());
    CHECK(!servo_screen_list()->open);

    servo_screen_set_armed(true);
    bench_frames(40u);
    tap_settings();
    CHECK(!servo_screen_list()->open);
}

/* The RANGE row of LIMIT RUN's and RUDDER AUTO's READY cards opens the
 * profile with PULSE MIN in view. */
TEST_CASE(a_range_row_opens_the_profile_on_pulse_min)
{
    safe_on(SERVO_FN_LIMRUN);
    rdy_row_t r;
    CHECK(lim_ready_row(LRR_RANGE, &r));
    CHECK(r.fix);
    tap_row(LRR_RANGE);
    CHECK(profile_open());
    CHECK(row_in_view(HL_MIN));

    safe_on(SERVO_FN_RAUTO);
    CHECK(rauto_ready_row(RAR_RANGE, &r));
    CHECK(r.fix);
    tap_row(RAR_RANGE);
    CHECK(profile_open());
    CHECK(row_in_view(HL_MIN));
}

/* PULSE MIN stepped in the profile LIMIT RUN opened is the profile the
 * hub's list and LIVE's read. */
TEST_CASE(a_pulse_set_from_a_function_is_the_hubs)
{
    safe_on(SERVO_FN_LIMRUN);
    tap_row(LRR_RANGE);
    CHECK_STR_EQ(value_of(HL_MIN), "1000us");
    sl_step(HL_MIN, +1);
    CHECK_STR_EQ(value_of(HL_MIN), "1010us");
    settings_request_save();
    CHECK(home_tag());
    go(SERVO_FN_LIVE);
    tap_pill();
    CHECK(profile_open());
    CHECK_STR_EQ(value_of(HL_MIN), "1010us");
    settings_request_save();
    CHECK(home_tag());
    go(SERVO_FN_HUB);
    tap_settings();
    CHECK_STR_EQ(value_of(HL_MIN), "1010us");
}

/* The profile opened on a function screen closes as the bench arms under
 * it, as the function's own list does. */
TEST_CASE(the_profile_closes_as_the_bench_arms)
{
    safe_on(SERVO_FN_SWEEP);
    tap_pill();
    CHECK(profile_open());
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(!servo_screen_list()->open);
    CHECK(!svs_profile_shown());
}

/* TEST's RANGE row opens TEST's own list with RANGE in view; RANGE there
 * is SWEEP's setting: a step on TEST's row is what SWEEP's list reads. */
TEST_CASE(tests_range_row_opens_its_list_on_range_shared_with_sweep)
{
    safe_on(SERVO_FN_TEST);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_RANGE, &r));
    CHECK(r.fix);
    tap_row(TRR_RANGE);
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK(!svs_profile_shown());
    CHECK_EQ(sl->host->count(), (int)TL_ROWS);
    CHECK_EQ(sl->host->row(TL_RANGE).id, SET_SERVO_TEST_RANGE);
    CHECK(row_in_view(TL_RANGE));
    const int before = settings_get_int(SET_SERVO_TEST_RANGE);
    sl_step(TL_RANGE, -1);
    CHECK_EQ(settings_get_int(SET_SERVO_TEST_RANGE), before - 5);
    /* The heading's line follows: the run's ends move in. */
    char line[32] = "";
    sl->host->row(TL_H_RANGE).custom->value_text(line, sizeof(line));
    CHECK(strstr(line, "us") != NULL);
    settings_request_save();
    CHECK(home_tag());

    go(SERVO_FN_SWEEP);
    tap_settings();
    CHECK_EQ(sl->host->count(), (int)SWL_ROWS);
    CHECK_EQ(sl->host->row(SWL_RANGE).id, SET_SERVO_TEST_RANGE);
    CHECK_EQ(settings_get_int(SET_SERVO_TEST_RANGE), before - 5);
}

/* TEST's OUTPUT row reads the profile as the type pill does and opens it
 * with PULSE MIN in view. */
TEST_CASE(tests_output_row_opens_the_profile_on_pulse_min)
{
    safe_on(SERVO_FN_TEST);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK(strstr(value_of(TL_OUTPUT), "50Hz  1000-2000") != NULL);
    sl_tap(TL_OUTPUT);
    CHECK(profile_open());
    CHECK(row_in_view(HL_MIN));
}

/* DWELL TIME is one setting: SWEEP's row steps what TEST's row and TEST's
 * run read. */
TEST_CASE(dwell_is_one_setting_on_sweep_and_test)
{
    safe_on(SERVO_FN_SWEEP);
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK_EQ(sl->host->row(SWL_DWELL).id, SET_SERVO_DWELL_MS);
    const int before = settings_get_int(SET_SERVO_DWELL_MS);
    sl_step(SWL_DWELL, +1);
    CHECK_EQ(settings_get_int(SET_SERVO_DWELL_MS), before + 10);
    settings_request_save();
    CHECK(home_tag());

    go(SERVO_FN_TEST);
    tap_settings();
    CHECK_EQ(sl->host->row(TL_DWELL).id, SET_SERVO_DWELL_MS);
    sl_step(TL_DWELL, -1);
    sl_step(TL_DWELL, -1);
    CHECK_EQ(settings_get_int(SET_SERVO_DWELL_MS), before - 10);
}

/* TEST's list holds the hub's REPORT and DUT NAME: REPORT switched there
 * is what the hub's row reads. */
TEST_CASE(report_and_dut_name_are_the_hubs_on_test)
{
    safe_on(SERVO_FN_TEST);
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    CHECK_EQ(sl->host->row(TL_REPORT).id, SET_SERVO_REPORT);
    CHECK_EQ(sl->host->row(TL_NAME).kind, (uint8_t)UI_SL_TEXT);
    CHECK_EQ(sl->host->row(TL_NAME).text, (int8_t)SET_TEXT_DUT_NAME);
    const bool before = settings_get_bool(SET_SERVO_REPORT);
    sl_step(TL_REPORT, +1);
    CHECK(settings_get_bool(SET_SERVO_REPORT) != before);
    settings_request_save();
    CHECK(home_tag());

    go(SERVO_FN_HUB);
    tap_settings();
    CHECK_EQ(sl->host->row(HL_REPORT).id, SET_SERVO_REPORT);
    CHECK(settings_get_bool(SET_SERVO_REPORT) != before);
}

/* DUT NAME on LIMIT RUN's and RUDDER AUTO's lists types the one name: a
 * DEL and OK on LIMIT RUN's is the name RUDDER AUTO's row and the hub's
 * read. */
TEST_CASE(dut_name_is_typed_on_limit_run_and_rudder_auto)
{
    safe_on(SERVO_FN_LIMRUN);
    settings_set_text(SET_TEXT_DUT_NAME, "SV12");
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK_EQ(sl->host->row(LL_NAME).text, (int8_t)SET_TEXT_DUT_NAME);
    sl_tap(LL_NAME);
    CHECK(sl->tk.open);
    sl_text_key(UI_TK_DEL);
    sl_text_key(UI_TK_OK);
    CHECK(!sl->tk.open);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SV1");
    settings_request_save();
    CHECK(home_tag());

    go(SERVO_FN_RAUTO);
    tap_settings();
    CHECK(sl->open);
    CHECK_EQ(sl->host->row(RAL_NAME).text, (int8_t)SET_TEXT_DUT_NAME);
    sl_tap(RAL_NAME);
    CHECK(sl->tk.open);
    sl_text_key(UI_TK_DEL);
    sl_text_key(UI_TK_OK);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SV");
}

/* TEST's LOG row reads USER LOGS: ON green, OFF amber.  A tap switches it
 * and asks a save; LIMIT RUN's list then reads the same. */
TEST_CASE(the_log_row_switches_user_logs_and_saves)
{
    safe_on(SERVO_FN_TEST);
    CHECK(settings_get_bool(SET_LOG_USER));
    rdy_row_t r;
    CHECK(test_ready_row(TRR_LOG, &r));
    CHECK_STR_EQ(r.label, ui_tr(TX_RDY_LOG));
    CHECK_STR_EQ(r.value, ui_on_off(1));
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    settings_cancel_save();
    tap_row(TRR_LOG);
    CHECK(!settings_get_bool(SET_LOG_USER));
    CHECK(settings_save_asked());
    CHECK(test_ready_row(TRR_LOG, &r));
    CHECK_STR_EQ(r.value, ui_on_off(0));
    CHECK_EQ(r.st, (uint8_t)RDY_WARN);
    /* Never a refusal: the hold is still offered. */
    CHECK(test_hold_blocked(NULL) == NULL);

    go(SERVO_FN_LIMRUN);
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK_EQ(sl->host->row(LL_LOG).id, SET_LOG_USER);
    sl_step(LL_LOG, +1);
    CHECK(settings_get_bool(SET_LOG_USER));
    settings_request_save();
    CHECK(home_tag());
    go(SERVO_FN_TEST);
    CHECK(test_ready_row(TRR_LOG, &r));
    CHECK_STR_EQ(r.value, ui_on_off(1));
}

/* Under a run a tap on LOG is refused in the alert band and changes
 * nothing; the row reads the state the run started with. */
TEST_CASE(the_log_row_is_refused_under_a_run)
{
    safe_on(SERVO_FN_TEST);
    servo_screen_set_armed(true);
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames(2100u);
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    ui_router_set_alert(NULL);
    tap_row(TRR_LOG);
    CHECK(settings_get_bool(SET_LOG_USER));
    char want[128];
    snprintf(want, sizeof(want), ui_tr(TX_RDY_BAND), ui_tr(TX_RDY_LOG),
             ui_tr(TX_RDY_LOG_RUN));
    CHECK_STR_EQ(alert(), want);
    /* SETUP switches it under the run: the row keeps the run's. */
    settings_set(SET_LOG_USER, 0.0f);
    rdy_row_t r;
    CHECK(test_ready_row(TRR_LOG, &r));
    CHECK_STR_EQ(r.value, ui_on_off(1));
    tap(START_X, START_Y);                       /* STOP TEST */
    bench_frames(500u);
    CHECK(!servo_screen_testing());
}

/* LIMIT RUN's and RUDDER AUTO's READY cards are full: USER LOGS is the
 * last row of their lists, with D RECEIVE or without. */
TEST_CASE(user_logs_is_a_list_row_on_limit_run_and_rudder_auto)
{
    CHECK_EQ((int)LRR_COUNT, READY_ROWS_MAX);
    CHECK_EQ((int)RAR_COUNT, READY_ROWS_MAX);
    safe_on(SERVO_FN_RAUTO);
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK_EQ(sl->host->count(), (int)RAL_ROWS);
    CHECK_EQ(sl->host->row(RAL_LOG).id, SET_LOG_USER);
    sl_step(RAL_LOG, +1);
    CHECK(!settings_get_bool(SET_LOG_USER));

    servo_screen_set_drive(1u, 13u, true);
    sl_choose(RAL_SERVO, 1);                     /* HITEC D SERIES */
    CHECK_EQ(sl->host->count(), (int)RAL_ROWS + 1);
    CHECK_EQ(sl->host->row(RAL_LOG + 1).id, SET_LOG_USER);
    CHECK_EQ(sl->host->row(RAL_NAME + 1).text, (int8_t)SET_TEXT_DUT_NAME);
    settings_cancel_save();
}

int main(void)
{
    RUN(the_type_pill_opens_the_profile_on_type);
    RUN(the_type_pill_names_safe_on_an_armed_bench);
    RUN(lives_settings_opens_the_profile);
    RUN(a_range_row_opens_the_profile_on_pulse_min);
    RUN(a_pulse_set_from_a_function_is_the_hubs);
    RUN(the_profile_closes_as_the_bench_arms);
    RUN(tests_range_row_opens_its_list_on_range_shared_with_sweep);
    RUN(tests_output_row_opens_the_profile_on_pulse_min);
    RUN(dwell_is_one_setting_on_sweep_and_test);
    RUN(report_and_dut_name_are_the_hubs_on_test);
    RUN(dut_name_is_typed_on_limit_run_and_rudder_auto);
    RUN(the_log_row_switches_user_logs_and_saves);
    RUN(the_log_row_is_refused_under_a_run);
    RUN(user_logs_is_a_list_row_on_limit_run_and_rudder_auto);
    return test_summary("servo_settings_rows");
}
