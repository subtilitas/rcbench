/*
 * What the SERVO screen's automatic test decides: START TEST's
 * hold, the ways a run ends, its faces, and a run on the servo
 * rail's meter.
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
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_workbench.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* ------------------------------------------------ the automatic test */

/* START TEST on TEST's AUTOMATIC TEST card runs only from a two-second
 * hold on an armed bench: the run asks the first set point and the output
 * on and steps the servo to its centre with no slew, and the workbench
 * stays on TEST. */
TEST_CASE(start_test_is_a_two_second_hold_on_an_armed_bench)
{
    fresh();
    memset(&bench, 0, sizeof(bench));
    hold_start(2.3f);
    CHECK(!servo_screen_testing());               /* not armed */
    scr->render(&cv, 0);

    bench_fresh();
    open_test();
    tap(START_X, START_Y);                        /* a tap is not the hold */
    CHECK(!servo_screen_testing());
    hold_start(1.0f);
    CHECK(!servo_screen_testing());
    close_settings();                             /* a switch drops a hold */
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);    /* LIVE: not START */
    for (int i = 0; i < 120; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.on);
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    const servo_cmd_t sc = last_cmd();
    CHECK_EQ(sc.kind, SERVO_CMD_POSITION);
    CHECK_EQ(sc.value_us, 1500u);
    CHECK_EQ(sc.slew_per_s, 0u);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
}

/* START TEST reads STOP while a run is under way, and a release ends the
 * run however it came about: here the tracker's, for a contact that
 * jumps. */
TEST_CASE(a_run_stops_on_a_release_the_tracker_makes_for_a_jump)
{
    bench_fresh();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    feed_reset();
    feed_to_screen(scr);
    finger(FEED_LONE, START_X, START_Y + UI_BAND_H);
    CHECK(servo_screen_testing());
    finger(FEED_LONE, START_X, START_Y + UI_BAND_H - 200);
    CHECK_EQ(feed_ups, 1);
    CHECK(!servo_screen_testing());
    lift(FEED_LONE);
}

/* Opening the programmer from PROGRAM's list ends a run: a run left
 * going would step the rail and the position of the port that CONNECT
 * takes out of PWM.  The tab alone ends nothing. */
TEST_CASE(program_ends_a_run)
{
    bench_fresh();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    open_program();
    CHECK(servo_screen_testing());
    tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));         /* KST and Chaservo */
    CHECK(!servo_screen_testing());
    CHECK(kst_panel_is_open());
    close_kst();
    CHECK(!kst_panel_is_open());
    CHECK(!servo_screen_testing());
}

/* A run through the screen: it passes, its files are handed over and
 * ended, the output goes off and the set points come back to what they
 * were. */
TEST_CASE(a_run_through_the_screen_ends_and_restores_the_set_points)
{
    bench_fresh();
    short_runs();
    supply_screen_put(5.5f, 1.5f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(bench.stepped);
    CHECK(bench.released);
    CHECK(!bench.out);
    CHECK_EQ(bench.opens, 1u);
    CHECK_EQ(bench.ends, 1u);
    CHECK(bench.csv > 50u);
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    CHECK(strstr(bench.report, "Supply:         PD mini") != NULL);
    CHECK(bench.set_v_max <= 6.0f + 1e-4f);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 1e-4f);
    /* The result on the left card, and the files once the card says. */
    servo_screen_test_files(12, true);
    settle_drawn();
    two_buffers();
    CHECK(both_whole());
    servo_screen_test_files(-1, false);
    CHECK(both_whole());
    /* CLOSE on LIVE's box puts it away. */
    close_settings();
    CHECK(both_whole());
    tap(BOXBTN_X, BOXBTN_Y);
    CHECK(both_whole());
}

/* HV SERVO adds 7.4 and 8.4 V, and a run that has them starts only through
 * HV SERVOS ONLY held for two seconds: START TEST is then a tap that opens
 * it, and a tap on APPLY does nothing. */
TEST_CASE(a_run_above_6_v_starts_through_the_hv_hold)
{
    bench_fresh();
    short_runs();
    open_run_settings(0);
    tap(ROW_R_X, HV_ROW_Y);                       /* HV SERVO on */
    CHECK(home_tag());                            /* back to the cards */
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 120; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);      /* the warning, not a run */
    CHECK(!servo_screen_testing());
    scr->render(&cv, 0);
    CHECK_EQ(fb[6 * W + 6], ui_theme_color(UI_C_DANGER));
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(!servo_screen_testing());
    hold_apply(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 200 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(bench.set_v_max > 8.3f);
    CHECK(strstr(bench.report, "HV servo:       ON") != NULL);
    CHECK(strstr(bench.report, " 8.40 V") != NULL);
}

/* Every way out of a run ends it: the output asked off, the servo let go,
 * and the report says ABORTED. */
TEST_CASE(every_way_out_of_a_run_switches_off_and_lets_go)
{
    for (int way = 0; way < 11; ++way) {
        bench_fresh();
        servo_screen_set_link(true);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        bench_frames(3000u);
        CHECK(bench.out);
        bench.released = false;
        switch (way) {
        case 0: servo_screen_cancel_arm(); break;              /* STOP */
        case 1: servo_screen_set_armed(false); break;
        case 2: servo_screen_set_link(false); break;
        case 3: scr->leave(); break;
        case 4: scr->cancel(); break;                          /* touch */
        case 5: {                                              /* the dial */
            close_settings();
            int x, y;
            dial_at(30.0f, ARC_R - 20, &x, &y);
            tap(x, y);
            break;
        }
        case 6:                                     /* LIVE's STOP TEST */
            close_settings();
            tap(BOXBTN_X, BOXBTN_Y);
            break;
        case 7:                                     /* the card's STOP TEST */
            open_test();
            tap(START_X, START_Y);
            break;
        case 8:                                     /* the profile changed */
            open_settings();
            tap(TAB_X(0), TAB_Y);
            tap(ROW_R_X, ROW_Y(4));                 /* REVERSE */
            break;
        case 9: bench.online = false; break;            /* the supply is gone */
        default:                                    /* OUTPUT OFF */
            tap(SUP_OUT_X, SUP_ROW_Y);
            break;
        }
        bench_frames(2000u);
        CHECK(!servo_screen_testing());
        CHECK(!bench.out);
        /* Let go, or -- a finger on the dial -- where the finger put it. */
        CHECK(bench.released || (way == 5 && bench.cmd != 1100u && bench.cmd != 1900u));
        CHECK_EQ(bench.ends, 1u);
        CHECK(strstr(bench.report, "Result:         ABORTED") != NULL);
    }
}

/*
 * A run ended by a disarm, by a stop, which disarms, or by leaving the
 * screen, which does too: no step of the run is said to the disarmed bench,
 * only the release or the disarm and the OFF are, and the value shown stays
 * where the run last drove the servo.  The arm after it starts at the rest.
 */
static void a_run_ends_disarmed(int way)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 2000 && bench.cmd == 1500u; ++i) {
        bench_frames(20u);
    }
    CHECK(bench.cmd != 1500u);
    const uint16_t driven = servo_screen_commanded();
    CHECK_EQ(driven, bench.cmd);

    if (way == 1) {
        servo_screen_cancel_arm();
        CHECK_EQ(servo_screen_commanded(), driven);
    } else if (way == 2) {
        scr->leave();
        CHECK_EQ(servo_screen_commanded(), driven);
    }
    servo_screen_set_armed(false);
    int positions = 0, releases = 0, disarms = 0;
    for (int i = 0; i < 100; ++i) {
        bench.now += 20u;
        servo_screen_clock(bench.now);
        scr->tick(0.02f);
        servo_screen_service();
        servo_cmd_t c;
        while (servo_screen_take(&c)) {
            if (c.kind == SERVO_CMD_RELEASE) {
                ++releases;
            } else if (c.kind == SERVO_CMD_DISARM) {
                ++disarms;
            } else {
                ++positions;
            }
        }
    }
    CHECK(!servo_screen_testing());
    CHECK_EQ(positions, 0);
    /* Leaving posts its disarm over the run's release; the disarm lets go. */
    CHECK_EQ(releases, (way == 2) ? 0 : 1);
    CHECK_EQ(disarms, (way == 2) ? 1 : 0);
    CHECK_EQ(servo_screen_commanded(), driven);

    servo_screen_set_armed(true);
    CHECK_EQ(servo_screen_commanded(), 1500);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/*
 * DISARM tapped during a run: the run ends at the tap, marked as a disarm
 * ends it, and until the bench answers -- it reads armed here meanwhile --
 * no step of the run is posted, START TEST is refused, and the value shown
 * stays where the run last drove the servo.
 */
TEST_CASE(a_disarm_tap_ends_a_run_at_the_tap)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 2000 && bench.cmd == 1500u; ++i) {
        bench_frames(20u);
    }
    CHECK(bench.cmd != 1500u);
    const uint16_t driven = servo_screen_commanded();

    tap(ARM_X + 40, ARM_Y + 16);
    CHECK(!servo_screen_testing());
    CHECK_EQ(servo_screen_commanded(), driven);
    int positions = 0, disarms = 0;
    for (int i = 0; i < 100; ++i) {
        bench.now += 20u;
        servo_screen_clock(bench.now);
        servo_screen_set_armed(true);           /* not yet answered */
        scr->tick(0.02f);
        servo_screen_service();
        servo_cmd_t c;
        while (servo_screen_take(&c)) {
            if (c.kind == SERVO_CMD_DISARM) {
                ++disarms;
            } else if (c.kind != SERVO_CMD_RELEASE) {
                ++positions;
            }
        }
    }
    CHECK_EQ(disarms, 1);
    CHECK_EQ(positions, 0);
    CHECK_EQ(servo_screen_commanded(), driven);
    hold_start(2.3f);
    CHECK(!servo_screen_testing());
    close_settings();
    bench_drain();
    CHECK(strstr(bench.report, "ABORTED") != NULL);

    servo_screen_set_armed(false);
    CHECK_EQ(servo_screen_commanded(), driven);
    servo_screen_set_armed(true);
    CHECK_EQ(servo_screen_commanded(), 1500);
}

/*
 * The run's row of the value-state table (test_value_state.c).
 *
 * A run starts only in LIVE: START TEST's hold starts nothing on a bench
 * that is OFF, ARMING, or LEAVING by a DISARM waiting, a DISARM taken or a
 * STOP.  A run under way ends on every edge out of LIVE -- the DISARM tap,
 * a STOP, the bench's report, leaving -- at the edge and not at the bench's
 * answer: no step is posted after it, the release it ends with moves no
 * value, and the value shown stays where the run last drove the servo
 * until the next arm sets the rest.
 */
TEST_CASE(a_run_follows_the_value_state_at_every_edge)
{
    for (int st = 0; st < 5; ++st) {
        bench_fresh();
        short_runs();
        switch (st) {
        case 0:                                  /* OFF */
            servo_screen_set_armed(false);
            break;
        case 1:                                  /* ARMING */
            servo_screen_set_armed(false);
            arm_press();
            hold_for(2.3f);
            arm_release();
            CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
            break;
        case 2:                                  /* LEAVING: DISARM waiting */
            tap(ARM_X + 40, ARM_Y + 16);
            break;
        case 3:                                  /* LEAVING: DISARM taken */
            tap(ARM_X + 40, ARM_Y + 16);
            CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
            servo_screen_set_armed(true);
            break;
        default:                                 /* LEAVING: a STOP */
            servo_screen_cancel_arm();
            servo_screen_set_armed(true);
            break;
        }
        hold_start(2.3f);
        CHECK(!servo_screen_testing());
        close_settings();
    }

    for (int edge = 0; edge < 4; ++edge) {
        bench_fresh();
        short_runs();
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        for (int i = 0; i < 2000 && bench.cmd == 1500u; ++i) {
            bench_frames(20u);
        }
        CHECK(bench.cmd != 1500u);
        const uint16_t driven = servo_screen_commanded();
        switch (edge) {
        case 0:  tap(ARM_X + 40, ARM_Y + 16); break;
        case 1:  servo_screen_cancel_arm(); break;
        case 2:  servo_screen_set_armed(false); break;
        default: scr->leave(); break;
        }
        CHECK(!servo_screen_testing());
        CHECK_EQ(servo_screen_commanded(), driven);
        int drives = 0;
        for (int i = 0; i < 100; ++i) {
            bench.now += 20u;
            servo_screen_clock(bench.now);
            if (edge != 2) {
                servo_screen_set_armed(true);   /* not yet answered */
            }
            scr->tick(0.02f);
            servo_screen_service();
            servo_cmd_t c;
            while (servo_screen_take(&c)) {
                if (c.kind != SERVO_CMD_DISARM
                    && c.kind != SERVO_CMD_RELEASE) {
                    ++drives;
                }
            }
        }
        CHECK_EQ(drives, 0);
        CHECK_EQ(servo_screen_commanded(), driven);
        hold_start(2.3f);
        CHECK(!servo_screen_testing());
        close_settings();
        servo_screen_set_armed(false);
        CHECK_EQ(servo_screen_commanded(), driven);
        servo_screen_set_armed(true);
        CHECK_EQ(servo_screen_commanded(), 1500);
    }
}

TEST_CASE(a_run_ended_by_a_disarm_leaves_no_position_to_send)
{
    for (int way = 0; way < 3; ++way) {
        a_run_ends_disarmed(way);
    }
}

/*
 * A run that ends on an armed bench releases the servo, and the value shown
 * is the rest the pins go to: 1500 us for STANDARD PWM whatever the trim,
 * 760 us for NARROW 760.  Ended by its last step, or by STOP TEST on the
 * left card while it is driven off the centre.
 */
static void a_run_ends_armed(int profile, bool by_operator)
{
    bench_fresh();
    short_runs();
    uint16_t rest = 1500u;
    if (profile == 1) {
        choose_type(1);                 /* NARROW 760 */
        bench.sv.cfg.stop_lo_us = 660u;
        bench.sv.cfg.stop_hi_us = 860u;
        bench.sv.position_us = 760.0f;
        bench.cmd = 760u;
        rest  = 760u;
    } else if (profile == 2) {
        open_settings();
        for (int i = 0; i < 4; ++i) {
            tap(TRIM_UP_X, ROW_Y(3));   /* +20 us */
        }
        close_settings();
    }
    (void)last_cmd();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    if (by_operator) {
        const uint16_t first = (uint16_t)(rest + ((profile == 2) ? 20u : 0u));
        bench.cmd = first;
        for (int i = 0; i < 2000 && bench.cmd == first; ++i) {
            bench_frames(20u);
        }
        CHECK(bench.cmd != first);
        CHECK_EQ(servo_screen_commanded(), bench.cmd);
        close_settings();
        tap(BOXBTN_X, BOXBTN_Y);        /* LIVE's STOP TEST */
        CHECK(!servo_screen_testing());
        CHECK_EQ(servo_screen_commanded(), rest);
        CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
    } else {
        for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
            bench_frames(500u);
        }
        CHECK(!servo_screen_testing());
        CHECK(bench.released);
        CHECK_EQ(servo_screen_commanded(), rest);
    }
    bench_frames(200u);
    CHECK_EQ(servo_screen_commanded(), rest);
    CHECK_EQ(servo_screen_drawn(), rest);
    /* The first 1 % of the knob goes on from the rest, on LIVE: a run
     * that ends by itself leaves the workbench on TEST, where the knob
     * does not move the servo. */
    close_settings();
    servo_screen_knob_frame();
    servo_screen_knob(0.01f);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, (profile == 1) ? 762u : 1510u);
}

TEST_CASE(a_run_that_ends_on_an_armed_bench_leaves_the_value_at_the_rest)
{
    for (int profile = 0; profile < 3; ++profile) {
        a_run_ends_armed(profile, true);
    }
    a_run_ends_armed(0, false);
    a_run_ends_armed(2, false);
}

/* START TEST's hold repaints only its button, a finger that leaves it or a
 * STOP mid-hold starts nothing, and a run's faces draw as a full redraw
 * would. */
TEST_CASE(the_runs_faces_draw_as_a_full_redraw_would)
{
    bench_fresh();
    short_runs();
    settle_drawn();
    two_buffers();
    open_test();
    CHECK(both_whole());
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(0.02f);
    }
    CHECK(both_whole());
    ev(START_X, 10, TOUCH_EVENT_MOVE, 1);             /* off the button */
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, 10, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
    CHECK(both_whole());

    /* Running, phase by phase: on TEST's card and on LIVE's box. */
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 12; ++i) {
        if (i == 6) {
            close_settings();
        }
        bench_frames(700u);
        settle_drawn();
        CHECK(both_whole_in(16, 48, 270, 132));           /* the box */
        CHECK(both_whole_in(496, 0, W - 496, H));          /* the right card */
        CHECK(both_whole());
    }
    open_test();
    CHECK(both_whole());                              /* RUNNING, STOP TEST */

    /* A STOP mid-hold starts nothing. */
    bench_fresh();
    open_test();
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 50; ++i) {
        scr->tick(0.02f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
}

/* A run that cannot start says why on the TEST page; one whose report is
 * still on its way to the card waits for it. */
TEST_CASE(a_refused_start_says_why)
{
    bench_fresh();
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    settings_set(SET_SERVO_BROWNOUT, 0.0f);
    hold_start(2.3f);
    CHECK(!servo_screen_testing());
    settle_drawn();
    two_buffers();
    CHECK(both_whole());

    bench_fresh();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.samples = 99u;                 /* the supply not answering */
    servo_screen_supply(&st);
    hold_start(2.3f);
    CHECK(!servo_screen_testing());

    /* A run whose report has not been taken: the next waits. */
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    servo_screen_cancel_arm();
    servo_screen_set_armed(false);      /* the stop's disarm, and a new arm */
    servo_screen_set_armed(true);
    scr->tick(0.02f);
    hold_start(2.3f);
    CHECK(!servo_screen_testing());
    bench_drain();
    close_settings();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
}

/* ------------------------------------- the run on the servo rail's meter */

static void run_out_bench(void)
{
    for (int i = 0; i < 200 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
}

/* Pixels of @p col in the rows @p y0 to @p y0 + 15 of the left card. */
static unsigned ink_in_row(int y0, ui_color_id_t col)
{
    scr->render(&cv, 0);
    unsigned n = 0u;
    for (int y = y0; y < y0 + 16; ++y) {
        for (int x = 10; x < 495; ++x) {
            n += (fb[(size_t)y * W + (size_t)x] == ui_theme_color(col)) ? 1u
                                                                         : 0u;
        }
    }
    return n;
}

/* With the INA3221 as the rail's meter at START TEST the run reads its
 * windows: one row a window, the report naming it and the shunt SETUP
 * holds, and the supply named as the supply. */
TEST_CASE(a_run_started_under_the_ina3221_reads_its_windows)
{
    bench_on_ina();
    settings_set(SET_INA3221_MOHM, 50.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    const unsigned wins0 = bench.wins;
    /* A window without a stamp of its own is stamped where the screen has
     * it, and is the run's reading as any other. */
    bench_frames(1000u);
    const unsigned rows0 = bench.csv;
    sense_link_win_t unstamped = rail_win(120, 4800u);
    unstamped.number = (uint16_t)(bench.win_number + 1u);
    ++bench.win_number;
    window(&unstamped);
    ++bench.wins;
    bench_frames(20u);
    CHECK(bench.csv >= rows0 + 1u);
    CHECK(servo_screen_testing());
    run_out_bench();
    CHECK(bench.released);
    CHECK(!bench.out);
    CHECK_EQ(bench.opens, 1u);
    CHECK_EQ(bench.ends, 1u);
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    CHECK(strstr(bench.report, "Supply:         PD mini\n") != NULL);
    CHECK(strstr(bench.report, "Current:        INA3221 CH1, shunt 50.0 mOhm, "
                           "range 3.276 A\n") != NULL);
    CHECK(strstr(bench.report, "Voltage:        INA3221 CH1, load side of the "
                           "shunt\n") != NULL);
    CHECK(strstr(bench.report, "INA3221:        not used") == NULL);
    CHECK(strstr(bench.csv_head, ";travel (ms);meter;window;current max (A);")
          != NULL);
    CHECK(strstr(bench.csv_last, ";INA3221;") != NULL);
    /* Every window that arrived while it ran is a row, and no supply
     * reading is: 20 rows a second, not 10 or 30. */
    CHECK(bench.csv - 1u <= bench.wins - wins0);
    CHECK(bench.csv - 1u + 40u >= bench.wins - wins0);
    /* The default STALL AT, 2.00 A, is at the supply's 2.00 A limit and
     * under this shunt's range. */
    CHECK(strstr(bench.report, "STALL AT 2.00 A cannot be reached: current "
                           "limit 2.00 A\n") != NULL);
    CHECK(strstr(bench.report, "INA3221 range") == NULL);
}

/* A window stamped on tick 0 is stamped: its row sits where tick 0 is on the
 * run's clock, not where the window arrived.  Before the run began the row
 * is at 0.000 s; after a wrap it is the stretch from the run's start. */
TEST_CASE(an_ina3221_window_stamped_on_tick_zero_keeps_its_stamp)
{
    static const uint32_t k_base[] = { 5000u, 0xFFFFFFFFu - 2500u };
    for (size_t k = 0; k < sizeof(k_base) / sizeof(k_base[0]); ++k) {
        bench_fresh();
        bench.now = k_base[k];
        bench.next = bench.now;
        servo_screen_clock(bench.now);
        settings_set(SET_INA3221_EN, 1.0f);
        short_runs();
        meter(SERVO_SOURCE_PDMINI);
        meter(SERVO_SOURCE_INA3221);
        bench.ina = true;
        bench.win_next = bench.now;
        bench_frames(200u);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        /* Run the clock past tick 0 when it started before it. */
        while ((int32_t)bench.now < 1000) {
            bench_frames(100u);
        }
        bench_frames(1000u);
        bench.csv_last[0] = '\0';
        sense_link_win_t w = rail_win(120, 4800u);
        w.number    = (uint16_t)(bench.win_number + 1u);
        ++bench.win_number;
        w.taken_ms  = 0u;
        w.stamped   = true;
        window(&w);
        bench_drain();
        CHECK(servo_screen_testing());
        if (k == 0u) {
            CHECK(strncmp(bench.csv_last, "0.000;", 6) == 0);
        } else {
            /* Tick 0 is under 0.5 s after the run began and over 1 s
             * before the clock reads what it does now. */
            CHECK(strncmp(bench.csv_last, "0.", 2) == 0);
        }
        run_out_bench();
    }
}

/* Each condition that takes the INA3221 off the rail ends a run that
 * reads it, on whichever of the three paths the change arrives first: the
 * frame's meter, a window, a supply sample.  The run is ABORTED with the
 * condition, the output goes off, the servo is let go, and it does not
 * carry on with the PD mini's readings, which arrive throughout. */
TEST_CASE(the_meter_changing_ends_a_run_on_the_ina3221)
{
    static const struct {
        servo_source_why_t why;
        const char        *text;
    } k_case[] = {
        { SERVO_SOURCE_WHY_OFF,       "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_OLD,       "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_NOT_HELD,  "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_SILENT,    "INA3221 not answering" },
        { SERVO_SOURCE_WHY_NO_WINDOW, "INA3221 window without current" },
        { SERVO_SOURCE_WHY_RESET,     "INA3221 reset itself" },
        { SERVO_SOURCE_WHY_NO_LINK,   "link lost" },
        { SERVO_SOURCE_WHY_SETTLING,  "INA3221 no longer the meter" },
    };
    for (size_t k = 0; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        for (int path = 0; path < 3; ++path) {
            bench_on_ina();
            hold_start(2.3f);
            CHECK(servo_screen_testing());
            bench_frames(3000u);
            CHECK(servo_screen_testing());
            CHECK(bench.out);
            /* A drop from before the run is not this run's. */
            servo_screen_source_dropped(SERVO_SOURCE_WHY_RESET,
                                        meter_changes - 1u);
            meter(SERVO_SOURCE_INA3221);
            CHECK(servo_screen_testing());
            bench.ina = false;
            const unsigned rows = bench.csv;
            const servo_source_id_t to =
                (k_case[k].why == SERVO_SOURCE_WHY_NO_LINK)
                    ? SERVO_SOURCE_MODEL : SERVO_SOURCE_PDMINI;
            servo_screen_source_dropped(k_case[k].why, meter_changes + 1u);
            if (path == 0) {
                meter(to);
            } else if (path == 1) {
                meter_id = to;
                ++meter_changes;
                const sense_link_win_t w = rail_win(100, 4800u);
                window(&w);
            } else {
                meter_id = to;
                ++meter_changes;
                supply_state_t st;
                memset(&st, 0, sizeof(st));
                st.online = true;
                st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
                st.output = true;
                st.mode = SUPPLY_MODE_CV;
                st.set_v = supply_screen_set_v();
                st.samples = ++bench.samples;
                st.taken_ms = bench.now;
                st.stamped = true;
                servo_screen_supply_at(&st, meter_id, meter_changes);
            }
            CHECK(!servo_screen_testing());
            bench_frames(2000u);
            CHECK(!bench.out);
            CHECK(bench.released);
            char want[96];
            snprintf(want, sizeof(want), "Result:         ABORTED - %s\n",
                     k_case[k].text);
            CHECK(strstr(bench.report, want) != NULL);
            /* No row after the end, and none of the PD mini's. */
            CHECK(bench.csv <= rows + 1u);
            CHECK(strstr(bench.csv_last, ";INA3221;") != NULL);
            CHECK_EQ(bench.ends, 1u);
        }
    }
}

/* A run that started on the PD mini reads it to its end: the INA3221
 * becoming the rail's meter under it, its windows arriving, changes
 * nothing.  With the INA3221 on in SETUP the report says why it was not
 * read. */
TEST_CASE(a_run_on_the_pd_mini_goes_on_when_the_ina3221_becomes_the_meter)
{
    bench_fresh();
    settings_set(SET_INA3221_EN, 1.0f);
    short_runs();
    meter(SERVO_SOURCE_PDMINI);
    servo_screen_source_why(SERVO_SOURCE_WHY_SETTLING);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(2000u);
    meter(SERVO_SOURCE_INA3221);
    bench.ina = true;
    bench.win_next = bench.now;
    bench_frames(1000u);
    CHECK(servo_screen_testing());
    /* And it stopping again, with a reason. */
    servo_screen_source_dropped(SERVO_SOURCE_WHY_SILENT, meter_changes + 1u);
    meter(SERVO_SOURCE_PDMINI);
    CHECK(servo_screen_testing());
    run_out_bench();
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    CHECK(strstr(bench.report, "Current:        PD mini\n") != NULL);
    CHECK(strstr(bench.report, "INA3221:        not used: it has worked for "
                           "less than 1 s\n") != NULL);
    CHECK(strstr(bench.csv_last, ";PDMINI;;;;;") != NULL);
    /* About ten rows a second for the run, not the windows' twenty. */
    CHECK(bench.csv < bench.samples);

    /* The INA3221 off in SETUP: the line is not written. */
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    run_out_bench();
    CHECK(strstr(bench.report, "INA3221:") == NULL);

    /* Each condition the panel names for the INA3221 not being the meter,
     * as the report of a run on the PD mini states it; the ones that are
     * no reason of the part's -- it is off, or no IOmcu answers --
     * write no line. */
    static const struct {
        servo_source_why_t why;
        const char        *text;
    } k_why[] = {
        { SERVO_SOURCE_WHY_OLD, "the IOmcu is older than link "
                                "protocol 4.11" },
        { SERVO_SOURCE_WHY_NOT_HELD, "the IOmcu does not hold its "
                                     "set-up" },
        { SERVO_SOURCE_WHY_SILENT, "it does not answer" },
        { SERVO_SOURCE_WHY_NO_WINDOW, "no window with current in the last "
                                      "200 ms" },
        { SERVO_SOURCE_WHY_RESET, "it reset itself" },
        { SERVO_SOURCE_WHY_OFF, NULL },
        { SERVO_SOURCE_WHY_NO_LINK, NULL },
        { SERVO_SOURCE_WHY_NONE, NULL },
    };
    for (size_t k = 0; k < sizeof(k_why) / sizeof(k_why[0]); ++k) {
        bench_fresh();
        settings_set(SET_INA3221_EN, 1.0f);
        short_runs();
        meter(SERVO_SOURCE_PDMINI);
        servo_screen_source_why(k_why[k].why);
        /* A drop without a condition says nothing. */
        servo_screen_source_dropped(SERVO_SOURCE_WHY_NONE, meter_changes);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        run_out_bench();
        CHECK(strstr(bench.report, "Current:        PD mini\n") != NULL);
        if (k_why[k].text == NULL) {
            CHECK(strstr(bench.report, "INA3221:") == NULL);
        } else {
            char want[128];
            snprintf(want, sizeof(want), "INA3221:        not used: %s\n",
                     k_why[k].text);
            CHECK(strstr(bench.report, want) != NULL);
        }
    }
}

/* The supply is the panel's model: the run reads the model whatever the
 * rail's meter is, and says why the INA3221 was not read. */
TEST_CASE(a_run_on_the_modelled_supply_reads_the_model)
{
    bench_on_ina();
    supply_screen_set_model(true);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    run_out_bench();
    supply_screen_set_model(false);
    CHECK(strstr(bench.report, "Supply:         the panel's model: every "
                           "reading is simulated\n") != NULL);
    CHECK(strstr(bench.report, "Current:        the panel's model\n") != NULL);
    CHECK(strstr(bench.report, "INA3221:        not used: the supply is the "
                           "panel's model\n") != NULL);
    CHECK(strstr(bench.csv_last, ";MODEL;;;;;") != NULL);
}

/* The windows stopping while the meter is still named the INA3221: the
 * run ends 500 ms after the last one. */
TEST_CASE(a_run_on_the_ina3221_ends_when_its_windows_stop)
{
    bench_on_ina();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(3000u);
    /* The last window at most 40 ms back: 160 ms more is under 200 ms
     * without one, 60 ms after that is past it. */
    bench.ina = false;
    bench_frames(160u);
    CHECK(servo_screen_testing());
    bench_frames(60u);
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(!bench.out);
    CHECK(bench.released);
    CHECK(strstr(bench.report, "Result:         ABORTED - no INA3221 window "
                           "for 0.2 s\n") != NULL);
}

/* A servo on a stop against a supply at its limit, through the screen
 * with the shipped LIMITS page: STALL AT 2.00 A at a current limit of
 * 2.00 A.  The supply reads constant current and the run ends on that, on
 * either meter. */
TEST_CASE(a_stalled_servo_at_the_shipped_limits_ends_the_run)
{
    for (int ina = 0; ina < 2; ++ina) {
        if (ina) {
            bench_on_ina();
        } else {
            bench_fresh();
            short_runs();
        }
        bench.sv.cfg.stop_hi_us = 1850u;
        bench.limits = true;
        CHECK_NEAR(settings_get(SET_SERVO_STALL_A), 2.0f, 1e-6f);
        CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-6f);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        uint32_t ran = 0u;
        for (; ran < 30000u && servo_screen_testing(); ran += 100u) {
            bench_frames(100u);
        }
        CHECK(!servo_screen_testing());
        CHECK(ran < 12000u);
        bench_frames(1000u);
        CHECK(!bench.out);
        CHECK(bench.released);
        CHECK(strstr(bench.report, "Result:         ABORTED - constant current "
                               "for 1 s\n") != NULL);
        CHECK(strstr(bench.report, "STALL AT 2.00 A cannot be reached: current "
                               "limit 2.00 A\n") != NULL);
    }
}

/* Under START TEST: a STALL AT that no reading can pass is said, in the
 * warning colour, and START TEST stays open.  It goes with the edit that
 * answers it and comes with the meter whose range is under STALL AT. */
TEST_CASE(a_stall_at_out_of_reach_is_said_under_start_test)
{
    bench_fresh();
    open_test();
    /* The AUTOMATIC TEST card's lines, mirrored from servo_test_panel.c:
     * the card at y 46, its state line 98 px down and the note 36 px under
     * that. */
    const int y = 46 + 98 + 36;
    /* As shipped: 2.00 A at a limit of 2.00 A. */
    CHECK(ink_in_row(y, UI_C_WARN) > 100u);
    /* One step of STALL AT's 0.05 A under the limit. */
    settings_set(SET_SERVO_STALL_A, 1.95f);
    servo_invalidate();
    CHECK_EQ(ink_in_row(y, UI_C_WARN), 0u);
    /* The INA3221 on 0.1 Ohm reads to 1.638 A: 1.95 A is out of its
     * reach, 1.60 A is in it and 1.65 A is not. */
    meter(SERVO_SOURCE_PDMINI);
    meter(SERVO_SOURCE_INA3221);
    CHECK(ink_in_row(y, UI_C_WARN) > 100u);
    settings_set(SET_SERVO_STALL_A, 1.60f);
    servo_invalidate();
    CHECK_EQ(ink_in_row(y, UI_C_WARN), 0u);
    settings_set(SET_SERVO_STALL_A, 1.65f);
    servo_invalidate();
    CHECK(ink_in_row(y, UI_C_WARN) > 100u);
    /* And the meter going takes the note with it. */
    meter(SERVO_SOURCE_PDMINI);
    CHECK_EQ(ink_in_row(y, UI_C_WARN), 0u);
    /* Said, not refused: the run starts. */
    close_settings();
    settings_set(SET_SERVO_STALL_A, 2.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
}

/* What servo_screen_on_testing() was told, and how often. */
static struct {
    bool     running;
    unsigned starts, ends;
} told;

static void told_testing(bool running)
{
    told.running = running;
    if (running) {
        ++told.starts;
    } else {
        ++told.ends;
    }
}

/* A run's start and its end are told inside the call that makes them, not
 * a frame later: the hold that starts it, and each thing that ends it --
 * the meter changing with a window or a supply sample, a supply reading
 * that does not answer, the windows stopping at a tick, STOP TEST, the
 * link, a disarm, and a reset of the screen. */
TEST_CASE(a_runs_start_and_end_are_told_where_they_happen)
{
    for (int how = 0; how < 8; ++how) {
        bench_on_ina();
        memset(&told, 0, sizeof(told));
        servo_screen_on_testing(told_testing);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        /* Told by the event that completed the hold: no frame since. */
        CHECK(told.running);
        CHECK_EQ(told.starts, 1u);
        bench_frames(3000u);
        CHECK(told.running);
        CHECK_EQ(told.ends, 0u);
        bench.ina = false;
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        st.output = true;
        st.mode = SUPPLY_MODE_CV;
        st.set_v = supply_screen_set_v();
        st.samples = ++bench.samples;
        st.taken_ms = bench.now;
        st.stamped = true;
        switch (how) {
        case 0: {                       /* a window under another meter */
            servo_screen_source_dropped(SERVO_SOURCE_WHY_SILENT,
                                        meter_changes + 1u);
            meter_id = SERVO_SOURCE_PDMINI;
            ++meter_changes;
            const sense_link_win_t w = rail_win(100, 4800u);
            window(&w);
            break;
        }
        case 1:                         /* a supply sample under another */
            meter_id = SERVO_SOURCE_PDMINI;
            ++meter_changes;
            servo_screen_supply_at(&st, meter_id, meter_changes);
            break;
        case 2:                         /* the supply not answering */
            st.online = false;
            servo_screen_supply(&st);
            break;
        case 3:                         /* no window for 500 ms: a tick */
            bench.now += 600u;
            servo_screen_clock(bench.now);
            st.taken_ms = bench.now;
            st.stamped = true;
            servo_screen_supply(&st);
            CHECK(told.running);
            scr->tick(0.02f);
            break;
        case 4:                         /* the link */
            servo_screen_set_link(false);
            break;
        case 5:                         /* a disarm */
            servo_screen_set_armed(false);
            break;
        case 6:                         /* the screen left */
            scr->leave();
            break;
        default:                        /* the screen reset under a run */
            scr->reset();
            break;
        }
        CHECK(!servo_screen_testing());
        CHECK(!told.running);
        CHECK_EQ(told.starts, 1u);
        CHECK_EQ(told.ends, 1u);
        /* And nothing more is told while no run starts. */
        bench_frames(1000u);
        CHECK_EQ(told.starts, 1u);
        CHECK_EQ(told.ends, 1u);
        servo_screen_on_testing(NULL);
        servo_screen_set_link(true);
    }
    /* With nobody to tell, a run starts and ends as it does. */
    bench_on_ina();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    servo_screen_set_armed(false);
    CHECK(!servo_screen_testing());
}


/* Line @p i of the run's box on LIVE's left card: its text at the box's
 * left edge on the box's own fill, compared across the box. */
#define TBOX_X 16
#define TBOX_Y 48
static bool test_box_reads(int i, const char *text, ui_color_id_t col)
{
    close_settings();
    scr->render(&cv, 0);
    want_fresh();
    const int y = TBOX_Y + 8 + 20 * i;
    const gfx_color_t bg = fb[(size_t)y * W + TBOX_X + 4];
    gfx_fill_rect(&want_cv, TBOX_X + 2, (int16_t)y, 264, 16, bg);
    gfx_text(&want_cv, TBOX_X + 10, y, text, &gfx_font_8x16,
             ui_theme_color(col), 1);
    return same_in(fb, want_fb, TBOX_X + 2, y, 264, 16);
}

/*
 * USER LOGS off on SETUP > SD CARD: a run hands the logger nothing -- no
 * OPEN, so no number is taken and no card alert can follow -- and the box
 * names USER LOGS OFF where the files would be, whatever number the last
 * run's answer still carries.  Switched on again, the next run's files go
 * out as before, an aborted one's with its ABORTED report.
 */
TEST_CASE(user_logs_off_sends_no_file_and_the_box_says_so)
{
    bench_fresh();
    short_runs();
    settings_set(SET_LOG_USER, 0.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK_EQ(bench.opens, 0u);
    CHECK_EQ(bench.opens_txt, 0u);
    CHECK_EQ(bench.csv, 0u);
    CHECK_EQ(bench.txt, 0u);
    CHECK_EQ(bench.ends, 0u);
    CHECK(test_box_reads(2, TR(SV_USER_LOGS_OFF), UI_C_TEXT_DIM));
    /* The logger's answer to the run before stays that run's. */
    servo_screen_test_files(12, true);
    CHECK(test_box_reads(2, TR(SV_USER_LOGS_OFF), UI_C_TEXT_DIM));

    /* Switched on mid-way changes nothing for the run that started off;
     * the next run, aborted by STOP, writes its files and says ABORTED. */
    settings_set(SET_LOG_USER, 1.0f);
    CHECK(test_box_reads(2, TR(SV_USER_LOGS_OFF), UI_C_TEXT_DIM));
    servo_screen_set_link(true);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(3000u);
    servo_screen_cancel_arm();
    bench_frames(2000u);
    CHECK(!servo_screen_testing());
    CHECK_EQ(bench.opens, 1u);
    CHECK_EQ(bench.ends, 1u);
    CHECK(strstr(bench.report, "Result:         ABORTED") != NULL);
    servo_screen_test_files(13, true);
    char want[48];
    snprintf(want, sizeof(want), "ATEST013.CSV + .TXT");
    CHECK(test_box_reads(2, want, UI_C_TEXT_DIM)
          || test_box_reads(3, want, UI_C_TEXT_DIM));

    /* Off at the start of a run that is then aborted: still nothing. */
    bench_fresh();
    settings_set(SET_LOG_USER, 0.0f);
    servo_screen_set_link(true);
    hold_start(2.3f);
    bench_frames(3000u);
    servo_screen_cancel_arm();
    bench_frames(2000u);
    CHECK(!servo_screen_testing());
    CHECK_EQ(bench.opens, 0u);
    CHECK_EQ(bench.ends, 0u);
}

int main(void)
{
    RUN(start_test_is_a_two_second_hold_on_an_armed_bench);
    RUN(a_run_stops_on_a_release_the_tracker_makes_for_a_jump);
    RUN(program_ends_a_run);
    RUN(a_run_through_the_screen_ends_and_restores_the_set_points);
    RUN(a_run_above_6_v_starts_through_the_hv_hold);
    RUN(every_way_out_of_a_run_switches_off_and_lets_go);
    RUN(a_disarm_tap_ends_a_run_at_the_tap);
    RUN(a_run_follows_the_value_state_at_every_edge);
    RUN(a_run_ended_by_a_disarm_leaves_no_position_to_send);
    RUN(a_run_that_ends_on_an_armed_bench_leaves_the_value_at_the_rest);
    RUN(the_runs_faces_draw_as_a_full_redraw_would);
    RUN(a_refused_start_says_why);
    RUN(a_run_started_under_the_ina3221_reads_its_windows);
    RUN(an_ina3221_window_stamped_on_tick_zero_keeps_its_stamp);
    RUN(the_meter_changing_ends_a_run_on_the_ina3221);
    RUN(a_run_on_the_pd_mini_goes_on_when_the_ina3221_becomes_the_meter);
    RUN(a_run_on_the_modelled_supply_reads_the_model);
    RUN(a_run_on_the_ina3221_ends_when_its_windows_stop);
    RUN(a_stalled_servo_at_the_shipped_limits_ends_the_run);
    RUN(a_stall_at_out_of_reach_is_said_under_start_test);
    RUN(a_runs_start_and_end_are_told_where_they_happen);
    RUN(user_logs_off_sends_no_file_and_the_box_says_so);
    return test_summary("servo_test_panel");
}
