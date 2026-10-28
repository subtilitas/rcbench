/*
 * The SERVO screen's automatic test ends on an error with the bench SAFE.
 *
 * Under test, through the screen and its bench rig: a run the supply's
 * loss, the INA3221's silence or a stalled servo ends posts one DISARM
 * besides the output off, and no release ahead of it; a run the operator
 * ends -- OUTPUT OFF, the dial, a profile changed, STOP TEST, a touch loss
 * -- posts no DISARM and lets the servo go to the rest on an ARMED bench;
 * a run a disarm ended posts nothing more.
 *
 * The screen and its fixtures are servo_rig.h, the bench
 * servo_bench_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "kst_panel.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_sensors.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_workbench.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* A run under way for 3s on an ARMED bench with the output on. */
static void run_under_way(bool ina)
{
    if (ina) {
        bench_on_ina();
    } else {
        bench_fresh();
    }
    servo_screen_set_link(true);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(3000u);
    CHECK(bench.out);
    bench.released = false;
    bench.disarms = 0u;
}

/* The supply stops answering: the run ends ABORTED, the output is asked
 * off and the bench SAFE, once. */
TEST_CASE(the_supply_lost_ends_a_run_with_the_bench_safe)
{
    run_under_way(false);
    bench.online = false;
    bench_frames(2000u);
    CHECK(!servo_screen_testing());
    CHECK(!bench.out);
    CHECK_EQ(bench.disarms, 1u);
    CHECK_EQ(bench.ends, 1u);
    CHECK(strstr(bench.report, "Result:         ABORTED") != NULL);
}

/* The INA3221's windows stop: 200ms without one ends the run, SAFE. */
TEST_CASE(the_meters_silence_ends_a_run_with_the_bench_safe)
{
    run_under_way(true);
    bench.ina = false;
    bench_frames(160u);
    CHECK(servo_screen_testing());
    CHECK_EQ(bench.disarms, 0u);
    bench_frames(60u);
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(!bench.out);
    CHECK_EQ(bench.disarms, 1u);
    CHECK(strstr(bench.report, "Result:         ABORTED - no INA3221 window "
                               "for 0.2s\n") != NULL);
}

/* A servo on a stop at the supply's current limit: constant current for
 * 1s ends the run, SAFE. */
TEST_CASE(a_stalled_servo_ends_a_run_with_the_bench_safe)
{
    bench_fresh();
    short_runs();
    bench.sv.cfg.stop_hi_us = 1850u;
    bench.limits = true;
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (uint32_t ran = 0u; ran < 30000u && servo_screen_testing();
         ran += 100u) {
        bench_frames(100u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(!bench.out);
    CHECK_EQ(bench.disarms, 1u);
    CHECK(strstr(bench.report, "Result:         ABORTED - constant current "
                               "for 1s\n") != NULL);
}

/* The operator's ends: the servo let go at the rest, the bench left ARMED,
 * no DISARM posted. */
TEST_CASE(the_operators_end_leaves_the_bench_armed)
{
    for (int way = 0; way < 5; ++way) {
        run_under_way(false);
        switch (way) {
        case 0:                                     /* OUTPUT OFF */
            tap(SUP_OUT_X, SUP_ROW_Y);
            break;
        case 1: {                                   /* the dial */
            close_settings();
            int x, y;
            dial_at(30.0f, ARC_R - 20, &x, &y);
            tap(x, y);
            break;
        }
        case 2:                                     /* the profile changed */
            open_settings();
            tap(TAB_X(0), TAB_Y);
            tap(ROW_R_X, ROW_Y(4));                 /* REVERSE */
            break;
        case 3:                                     /* LIVE's STOP TEST */
            close_settings();
            tap(BOXBTN_X, BOXBTN_Y);
            break;
        default: scr->cancel(); break;              /* touch events lost */
        }
        bench_frames(2000u);
        CHECK(!servo_screen_testing());
        CHECK(!bench.out);
        CHECK_EQ(bench.disarms, 0u);
        CHECK(bench.released || way == 1);
    }
}

/* A run a disarm ended: the bench is SAFE already, and the run posts no
 * DISARM of its own. */
TEST_CASE(a_run_a_disarm_ended_posts_no_disarm)
{
    run_under_way(false);
    servo_screen_set_armed(false);
    bench_frames(2000u);
    CHECK(!servo_screen_testing());
    CHECK(!bench.out);
    CHECK_EQ(bench.disarms, 0u);
}

int main(void)
{
    RUN(the_supply_lost_ends_a_run_with_the_bench_safe);
    RUN(the_meters_silence_ends_a_run_with_the_bench_safe);
    RUN(a_stalled_servo_ends_a_run_with_the_bench_safe);
    RUN(the_operators_end_leaves_the_bench_armed);
    RUN(a_run_a_disarm_ended_posts_no_disarm);
    return test_summary("servo_error_safe");
}
