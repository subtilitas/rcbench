/**
 * @file test_servo_profile_live.c
 * @brief A SERVO function screen's lists as the bench goes live under them
 *        (servo_lists.c, servo_settings_panel.c): the profile a function
 *        opens -- from LIVE's type pill, the RANGE row of LIMIT RUN's and
 *        RUDDER AUTO's READY cards, TEST's OUTPUT row -- closes with every
 *        editor over it, its keypad, its choice list and its warning, and
 *        a value OK'd in the frame the bench arms is refused.  LIMIT RUN's
 *        CURRENT keypad closes with its list, TEST's list's own keyboard
 *        and keypad with TEST's.  The hub drives nothing: its profile
 *        takes an OK on an armed bench.  The bench is servo_bench_rig.h.
 *
 * Cases:
 * - a_profile_keypad_closes_with_the_profile_as_the_bench_arms
 * - a_profile_keypad_ok_on_a_live_bench_changes_nothing
 * - a_profile_choice_closes_or_refuses_on_a_live_bench
 * - a_profile_warning_held_as_the_bench_arms_applies_nothing
 * - the_current_keypad_closes_with_its_list_as_the_bench_arms
 * - tests_own_editors_close_with_its_list_as_the_bench_arms
 * - the_hubs_profile_keypad_takes_ok_on_an_armed_bench
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
#include "ui_textkey.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

#include "servo_ready_int.h"

/* servo_screen_int.h's calls this suite reads, which it does not take in
 * whole. */
bool svs_profile_shown(void);
void svs_profile_open(int row);

/** @brief The hub list's rows: OUTPUT to HV SERVO, DUT, NAME and REPORT. */
#define HL_ROWS (HL_REPORT + 1)

/** @brief The functions that open the profile in their list's place. */
static const servo_fn_t k_openers[] = {
    SERVO_FN_LIVE, SERVO_FN_LIMRUN, SERVO_FN_RAUTO, SERVO_FN_TEST,
};
/** @brief Their count. */
#define OPENERS ((int)(sizeof(k_openers) / sizeof(k_openers[0])))

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

/** @brief Whether the open list is the profile: its rows, its name.
 *  @return true then */
static bool profile_open(void)
{
    const ui_setlist_t *sl = servo_screen_list();
    return sl->open && svs_profile_shown()
           && sl->host->count() == HL_ROWS
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

/** @brief A SAFE bench on @p fn with the profile open from the function's
 *         own opener: LIVE's type pill, the RANGE row of LIMIT RUN's and
 *         RUDDER AUTO's READY cards, TEST's OUTPUT row.
 *  @param fn the function screen */
static void profile_on(servo_fn_t fn)
{
    safe_on(fn);
    if (fn == SERVO_FN_LIMRUN) {
        tap(SV_RDY_X, SV_RDY_ROW_Y(LRR_RANGE));
    } else if (fn == SERVO_FN_RAUTO) {
        tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_RANGE));
    } else if (fn == SERVO_FN_TEST) {
        tap_settings();
        sl_tap(TL_OUTPUT);
    } else {
        const gfx_rect_t r = drive_tag_rect();
        tap(r.x + r.w / 2, r.y + r.h / 2);
    }
}

/** @brief PULSE MIN in force, read off the profile opened again on a SAFE
 *         bench.  @return the row's value, "" with no profile open */
static const char *pulse_min_now(void)
{
    servo_screen_set_armed(false);
    bench_frames(40u);
    svs_profile_open(HL_MIN);
    return profile_open() ? value_of(HL_MIN) : "";
}

/* PULSE MIN's keypad over the profile a function opened goes with the
 * profile as the bench arms: an OK typed after it reaches no row, and
 * PULSE MIN stays 1000us. */
TEST_CASE(a_profile_keypad_closes_with_the_profile_as_the_bench_arms)
{
    for (int k = 0; k < OPENERS; ++k) {
        profile_on(k_openers[k]);
        CHECK(profile_open());
        sl_tap(HL_MIN);
        CHECK(servo_screen_keypad()->open);
        servo_screen_set_armed(true);
        bench_frames(40u);
        CHECK(!servo_screen_list()->open);
        CHECK(!servo_screen_keypad()->open);
        keys("1100");
        CHECK_STR_EQ(pulse_min_now(), "1000us");
    }
}

/* An OK in the frame the bench arms, before the profile closes, is
 * refused: the keypad closes and PULSE MIN stays 1000us. */
TEST_CASE(a_profile_keypad_ok_on_a_live_bench_changes_nothing)
{
    for (int k = 0; k < OPENERS; ++k) {
        profile_on(k_openers[k]);
        sl_tap(HL_MIN);
        CHECK(servo_screen_keypad()->open);
        servo_screen_set_armed(true);
        keys("1100");
        CHECK(!servo_screen_keypad()->open);
        CHECK_STR_EQ(pulse_min_now(), "1000us");
    }
}

/* TYPE's choice list over the profile goes with it as the bench arms; a
 * pick in the frame the bench arms is refused.  STANDARD PWM stays. */
TEST_CASE(a_profile_choice_closes_or_refuses_on_a_live_bench)
{
    for (int k = 0; k < OPENERS; ++k) {
        profile_on(k_openers[k]);
        sl_tap(HL_TYPE);
        servo_screen_set_armed(true);
        bench_frames(40u);
        CHECK(!servo_screen_list()->open);
        tap(CH_X(1), CH_Y(1));                  /* NARROW 760 */
        CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

        profile_on(k_openers[k]);
        sl_tap(HL_TYPE);
        servo_screen_set_armed(true);
        tap(CH_X(1), CH_Y(1));
        CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    }
}

/* HELI CYCLIC's warning, its HOLD TO APPLY down as the bench arms: the
 * warning goes with the profile and the hold applies nothing. */
TEST_CASE(a_profile_warning_held_as_the_bench_arms_applies_nothing)
{
    for (int k = 0; k < OPENERS; ++k) {
        profile_on(k_openers[k]);
        sl_tap(HL_TYPE);
        tap(CH_X(3), CH_Y(3));                  /* HELI CYCLIC: warned */
        CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
        ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
        servo_screen_set_armed(true);
        for (int i = 0; i < 90; ++i) {          /* 2.3s, past the hold */
            scr->tick(1.0f / 39.0f);
        }
        ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
        CHECK(!servo_screen_list()->open);
        CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
        CHECK_EQ(servo_screen_frame_hz(), 50u);
    }
}

/* CURRENT's keypad over LIMIT RUN's list goes with the list as the bench
 * arms: an OK typed after it leaves SET A as it was. */
TEST_CASE(the_current_keypad_closes_with_its_list_as_the_bench_arms)
{
    safe_on(SERVO_FN_LIMRUN);
    tap_settings();
    CHECK(servo_screen_list()->open);
    const float i0 = supply_screen_set_i();
    sl_tap(LL_CURRENT);
    CHECK(servo_screen_keypad()->open);
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(!servo_screen_list()->open);
    CHECK(!servo_screen_keypad()->open);
    keys("0.5");
    CHECK(fabsf(supply_screen_set_i() - i0) < 0.001f);
    settings_cancel_save();
}

/* TEST's list's own editors -- DUT NAME's keyboard, DWELL TIME's keypad --
 * go with the list as the bench arms: nothing typed is taken. */
TEST_CASE(tests_own_editors_close_with_its_list_as_the_bench_arms)
{
    safe_on(SERVO_FN_TEST);
    settings_set_text(SET_TEXT_DUT_NAME, "SV12");
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    sl_tap(TL_NAME);
    CHECK(sl->tk.open);
    sl_text_key(UI_TK_DEL);
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(!sl->open);
    CHECK(!sl->tk.open);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SV12");

    safe_on(SERVO_FN_TEST);
    tap_settings();
    const int dwell = settings_get_int(SET_SERVO_DWELL_MS);
    sl_part(TL_DWELL, UI_SL_PART_VALUE);
    CHECK(sl->kp.open);
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(!sl->open);
    CHECK(!sl->kp.open);
    CHECK_EQ(settings_get_int(SET_SERVO_DWELL_MS), dwell);
    settings_cancel_save();
}

/* The hub drives nothing: its profile stays open on an armed bench and
 * its keypad takes an OK, as its list opens at any time. */
TEST_CASE(the_hubs_profile_keypad_takes_ok_on_an_armed_bench)
{
    safe_on(SERVO_FN_HUB);
    tap_settings();
    CHECK(profile_open());
    sl_tap(HL_MIN);
    CHECK(servo_screen_keypad()->open);
    servo_screen_set_armed(true);
    bench_frames(40u);
    CHECK(servo_screen_list()->open);
    CHECK(servo_screen_keypad()->open);
    keys("1100");
    CHECK_STR_EQ(value_of(HL_MIN), "1100us");
    settings_cancel_save();
}

int main(void)
{
    RUN(a_profile_keypad_closes_with_the_profile_as_the_bench_arms);
    RUN(a_profile_keypad_ok_on_a_live_bench_changes_nothing);
    RUN(a_profile_choice_closes_or_refuses_on_a_live_bench);
    RUN(a_profile_warning_held_as_the_bench_arms_applies_nothing);
    RUN(the_current_keypad_closes_with_its_list_as_the_bench_arms);
    RUN(tests_own_editors_close_with_its_list_as_the_bench_arms);
    RUN(the_hubs_profile_keypad_takes_ok_on_an_armed_bench);
    return test_summary("servo_profile_live");
}
