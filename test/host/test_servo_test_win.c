/*
 * The automatic servo test on the INA3221's CH1 windows: the endings and
 * the files, by hand.
 *
 * Windows and supply readings are given at the millisecond a case names
 * (servo_test_win_rig.h).
 *
 * Under test: a run on the INA3221 ended, never moved to the PD mini, when
 * no window arrives for 200ms (at 199, 200 and 201ms), windows without
 * both quantities do not hold off that end, no supply reading arrives for
 * 1500ms, or an idle has no reading on the PD mini; STALL AT on the
 * window's mean at 1mA under, at and 1mA over, its abort at 999, 1000
 * and 1001ms and 20 windows in a row; constant current for 999 and 1000
 * ms on either meter, and a spell of it shorter than that counted and
 * failing nothing; STALL AT under, at and over the current limit and the
 * INA3221's range; every ending with the output asked off once, the servo
 * let go, the report marked, and nothing fed to the run afterwards
 * changing its verdict, its report or its file; every timer across the
 * 2^32ms tick wrap; each condition of servo_source.h that drops the
 * meter; the CSV read back by the viewer's parser, with and without the
 * encoder's columns, beside files of the 13-column format; and a bench log
 * recorded on 0.14.0, which holds 35% of its windows, ending a run at its
 * first gap.
 *
 * SPDX-License-Identifier: MIT
 */
#include "log_csv.h"
#include "servo_test_win_rig.h"

/* ------------------------------------------- no window, and no reading */

/* No window for 200ms ends a run on the INA3221: a pass 199ms after the
 * last window leaves it running, one at 200ms and one at 201ms end it.
 * The supply's readings go on, and so does nothing else.  At tick 5000 and
 * with the 200ms across the tick wrap. */
TEST_CASE(no_window_for_200_ms_ends_the_run)
{
    static const uint32_t k_after[] = { 199u, 200u, 201u };
    CHECK_EQ(SERVO_TEST_WIN_STALE_MS, 200u);
    servo_test_cfg_t c;
    h_cfg(&c, true);
    /* Where IDLE begins, from the start. */
    h_idle_by_hand(5000u, &c);
    const uint32_t rel = h.t.last_ms - 5000u;
    const uint32_t k_tick0[] = { 5000u, (uint32_t)(0u - 200u - rel) };
    for (size_t i = 0u; i < 2u; ++i) {
        for (size_t k = 0u; k < 3u; ++k) {
            h_idle_by_hand(k_tick0[i], &c);
            const uint32_t last = h.t.last_ms;
            if (i > 0u) {
                /* The 200ms end on the wrap: tick 0 is the 200th. */
                CHECK_EQ(last, 0u - 200u);
            }
            /* The supply answers throughout; passes run to 198ms. */
            for (uint32_t at = h.now + 1u; (int32_t)(at - last) < 199;
                 ++at) {
                if ((at - last) % 100u == 0u) {
                    h_pd_at(at);
                }
                h_step_at(at);
                h.now = at;
            }
            CHECK(servo_test_running(&h.t));
            h.now = last + k_after[k];
            h_step_at(h.now);
            if (k_after[k] < SERVO_TEST_WIN_STALE_MS) {
                CHECK(servo_test_running(&h.t));
                CHECK_EQ(h.offs, 0u);
                /* And a window then keeps it running past 200ms from
                 * the one before. */
                h_win_at(h.now, 20);
                h.now += 150u;
                h_step_at(h.now);
                CHECK(servo_test_running(&h.t));
                continue;
            }
            CHECK_EQ(h.t.end_ms, last + k_after[k]);
            h_check_ended(SERVO_TEST_AB_WIN_STALE);
            CHECK(strstr(h.out.report,
                         "Result:         ABORTED - no INA3221 window for "
                         "0.2s\n") != NULL);
        }
    }
    /* A run that never has a window ends 200ms after its start, here
     * across the wrap. */
    h_fresh(0u - 100u);
    h.win_on = false;
    h_start(&c);
    const uint32_t start = h.now;
    h_run_ms(199u);
    CHECK(servo_test_running(&h.t));
    h_ms();
    CHECK_EQ(h.t.end_ms, start + 200u);
    CHECK(start > 0xFFFF0000u && h.t.end_ms < 0x10000u);
    h_check_ended(SERVO_TEST_AB_WIN_STALE);
}

/* Windows that arrive without the rail voltage, or without a current, are
 * no window for the meter's silence either: with one every 50ms, the last
 * at the 200ms edge itself, the run still ends 200ms after the last
 * window that held both. */
TEST_CASE(windows_without_both_quantities_do_not_hold_off_the_200_ms_end)
{
    servo_test_cfg_t c;
    h_cfg(&c, true);
    for (int q = 0; q < 2; ++q) {
        h_idle_by_hand(5000u, &c);
        const uint32_t last = h.t.last_ms;
        const uint32_t readings = h.t.readings;
        for (uint32_t k = 1u; k <= 4u; ++k) {
            servo_test_win_t w;
            memset(&w, 0, sizeof(w));
            w.number   = ++h.win_number;
            w.current  = q != 0;
            w.voltage  = q == 0;
            w.mean_ma  = 20;
            w.mean_mv  = 6000u;
            w.min_mv   = 5800u;
            w.taken_ms = last + 50u * k;
            if (k == 4u) {
                h.now = last + 199u;
                h_step_at(h.now);
                CHECK(servo_test_running(&h.t));
            }
            servo_test_window(&h.t, &w, 0u);
            h.now = w.taken_ms;
            h_step_at(h.now);
        }
        CHECK_EQ(h.t.readings, readings);
        CHECK_EQ(h.t.end_ms, last + 200u);
        h_check_ended(SERVO_TEST_AB_WIN_STALE);
    }
}

/* The supply's own silence ends a run on the INA3221 as it ends one on the
 * PD mini: no new reading for more than 1500ms, the windows arriving
 * throughout.  Running at 1500ms, ended at 1501, and across the wrap. */
TEST_CASE(no_supply_reading_for_1500_ms_ends_a_run_on_the_ina3221)
{
    servo_test_cfg_t c;
    h_cfg(&c, true);
    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_IDLE));
    const uint32_t rel = h.t.sup_ms - 5000u;
    const uint32_t k_tick0[] = { 5000u, wrap_at(rel + 700u) };
    for (size_t i = 0u; i < 2u; ++i) {
        h_fresh(k_tick0[i]);
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_IDLE));
        h.pd_on = false;
        const uint32_t heard = h.t.sup_ms;
        while (h.now != heard + SERVO_TEST_STALE_MS) {
            h_ms();
        }
        CHECK(servo_test_running(&h.t));
        h_ms();
        CHECK_EQ(h.t.end_ms, heard + SERVO_TEST_STALE_MS + 1u);
        if (i > 0u) {
            CHECK(heard > 0xFFFFF000u && h.t.end_ms < 2000u);
        }
        h_check_ended(SERVO_TEST_AB_STALE);
    }
}

/* The supply's readings stopping as IDLE begins, on the PD mini: IDLE has
 * no reading to measure, and the run ends at IDLE's end, 1000ms on, before
 * the 1500ms without a reading are over. */
TEST_CASE(an_idle_without_a_reading_ends_a_run_on_the_pd_mini)
{
    servo_test_cfg_t c;
    h_cfg(&c, false);
    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_IDLE));
    h.pd_on = false;
    const uint32_t idle_from = h.t.phase_ms;
    CHECK_EQ(h.t.steps[0].idle.n, 0u);
    CHECK((uint32_t)(idle_from - h.t.sup_ms) < 500u);
    while ((uint32_t)(h.now - idle_from) < SERVO_TEST_IDLE_MS - 1u) {
        h_ms();
    }
    CHECK(servo_test_running(&h.t));
    h_ms();
    CHECK_EQ(h.t.end_ms, idle_from + SERVO_TEST_IDLE_MS);
    h_check_ended(SERVO_TEST_AB_STALE);
}

/* ------------------------------------------------------------- stall */

/* STALL AT is judged on the window's mean, in whole mA: a mean 1mA under
 * STALL AT and one at it are no stall, one 1mA over is, whatever the
 * window's highest sample.  Negative means count by their magnitude. */
TEST_CASE(stall_at_is_judged_on_the_windows_mean)
{
    static const struct {
        int  ma;
        bool stall;
    } k_case[] = {
        { 999, false }, { 1000, false }, { 1001, true },
        { -999, false }, { -1000, false }, { -1001, true },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        servo_test_cfg_t c;
        h_cfg(&c, true);
        h_idle_by_hand(5000u, &c);
        /* The highest sample of this window is 50mA above its mean: over
         * STALL AT in every case, and no part of the rule. */
        h_win_at(h.now + 50u, k_case[k].ma);
        CHECK_EQ(h.t.stalled, k_case[k].stall);
        CHECK_EQ(h.t.stalling, k_case[k].stall);
        CHECK_NEAR(h.t.stall_peak_a, (float)k_case[k].ma / 1000.0f, 1e-6f);
        CHECK(servo_test_running(&h.t));
        /* One window at or under it between two over it starts the second
         * over again. */
        h_win_at(h.now + 100u, 1000);
        CHECK(!h.t.stalling);
    }
    CHECK(!servo_test_over_a(1.0f, 1.0f));
    CHECK(servo_test_over_a(1.001f, 1.0f));
    CHECK(servo_test_over_a(-1.001f, 1.0f));
    CHECK(!servo_test_over_a(-1.0f, 1.0f));
    CHECK(!servo_test_over_a(NAN, 1.0f));
    CHECK(servo_test_over_a(INFINITY, 1.0f));
    CHECK(servo_test_over_a(-INFINITY, 1.0f));
    CHECK(servo_test_over_a(2.0e6f, 1.0f));
    CHECK(!servo_test_over_a(1.0f, INFINITY));
}

/* Windows over STALL AT end the run 1000ms after the first one began: a
 * window taken 999ms after that leaves it running, one at 1000 and one at
 * 1001ms end it, and at 50ms a window the twentieth in a row does.  At
 * tick 5000 and with the second across the wrap. */
TEST_CASE(a_second_over_stall_at_ends_the_run_on_the_windows)
{
    static const uint32_t k_last[] = { 999u, 1000u, 1001u };
    servo_test_cfg_t c;
    h_cfg(&c, true);
    h_idle_by_hand(5000u, &c);
    const uint32_t rel = h.now - 5000u;
    const uint32_t k_tick0[] = { 5000u, wrap_at(rel) };
    for (size_t i = 0u; i < 2u; ++i) {
        for (size_t k = 0u; k < 3u; ++k) {
            h_idle_by_hand(k_tick0[i], &c);
            /* The first window over it is taken at began + 50: it began at
             * began.  Nineteen in a row leave the run running. */
            const uint32_t began = h.now;
            for (uint32_t n = 1u; n <= 19u; ++n) {
                const uint32_t at = began + n * SERVO_TEST_WIN_MS;
                h_win_at(at, 1001);
                if (n % 2u == 0u) {
                    h_pd_at(at);
                }
                h_step_at(at);
            }
            CHECK(servo_test_running(&h.t));
            CHECK(h.t.stalled);
            CHECK_EQ(h.t.stall_since_ms, began);
            if (i > 0u) {
                CHECK(began > 0xFFFFFC00u);
            }
            /* The twentieth. */
            const uint32_t at = began + k_last[k];
            h_win_at(at, 1001);
            h.now = at;
            if (k_last[k] < SERVO_TEST_STALL_ABORT_MS) {
                CHECK(servo_test_running(&h.t));
                h_win_at(at + 1u, 1001);
                CHECK(!servo_test_running(&h.t));
                continue;
            }
            CHECK_EQ(h.t.end_ms, at);
            h_check_ended(SERVO_TEST_AB_STALL);
            CHECK(strstr(h.out.report, "Stall threshold  highest 1.001A, "
                                       "STALL AT 1.00A: FAIL\n") != NULL);
        }
    }
}

/* On the PD mini the second counts from the first reading over STALL AT:
 * a reading 999ms after it leaves the run running, one at 1000 and one at
 * 1001ms end it.  At tick 5000 and across the wrap. */
TEST_CASE(a_second_over_stall_at_ends_the_run_on_the_pd_mini)
{
    static const uint32_t k_last[] = { 999u, 1000u, 1001u };
    servo_test_cfg_t c;
    h_cfg(&c, false);
    h_idle_by_hand(5000u, &c);
    const uint32_t rel = h.now - 5000u;
    const uint32_t k_tick0[] = { 5000u, wrap_at(rel) };
    for (size_t i = 0u; i < 2u; ++i) {
        for (size_t k = 0u; k < 3u; ++k) {
            h_idle_by_hand(k_tick0[i], &c);
            h.pd_a = 1.001f;
            const uint32_t first = h.now + 10u;
            h_pd_at(first);
            for (uint32_t n = 1u; n <= 9u; ++n) {
                h_pd_at(first + n * 100u);
                h_step_at(first + n * 100u);
            }
            CHECK(servo_test_running(&h.t));
            CHECK(h.t.stalled);
            const uint32_t at = first + k_last[k];
            h_pd_at(at);
            h.now = at;
            if (k_last[k] < SERVO_TEST_STALL_ABORT_MS) {
                CHECK(servo_test_running(&h.t));
                continue;
            }
            CHECK_EQ(h.t.end_ms, at);
            h_check_ended(SERVO_TEST_AB_STALL);
        }
    }
    /* A reading at STALL AT is none over it, and one under it between two
     * over it starts the second again. */
    h_idle_by_hand(5000u, &c);
    h.pd_a = 1.0f;
    h_pd_at(h.now + 10u);
    CHECK(!h.t.stalled);
    CHECK(!h.t.stalling);
    h.pd_a = 1.2f;
    h_pd_at(h.now + 110u);
    h.pd_a = 0.9f;
    h_pd_at(h.now + 610u);
    h.pd_a = 1.2f;
    h_pd_at(h.now + 1110u);
    h_pd_at(h.now + 1210u);
    CHECK(servo_test_running(&h.t));
    CHECK(h.t.stalled);
}

/* ------------------------------------------------- constant current */

/* The supply in constant current for 1000ms ends a run, whichever meter
 * it reads and whatever STALL AT is: a reading 999ms after the first in
 * constant current leaves it running, one at 1000ms ends it.  At tick
 * 5000 and across the wrap. */
TEST_CASE(constant_current_for_a_second_ends_the_run)
{
    static const uint32_t k_last[] = { 999u, 1000u };
    for (unsigned ina = 0u; ina < 2u; ++ina) {
        servo_test_cfg_t c;
        h_cfg(&c, ina != 0u);
        c.stall_a = 5.0f;           /* over every reading */
        h_idle_by_hand(5000u, &c);
        const uint32_t rel = h.now - 5000u;
        const uint32_t k_tick0[] = { 5000u, wrap_at(rel) };
        for (size_t i = 0u; i < 2u; ++i) {
            for (size_t k = 0u; k < 2u; ++k) {
                h_idle_by_hand(k_tick0[i], &c);
                h.cc = true;
                const uint32_t first = h.now + 10u;
                for (uint32_t ms = 0u; ms <= 900u; ms += 50u) {
                    if (ina != 0u) {
                        h_win_at(first + ms, 20);
                    }
                    if (ms % 100u == 0u) {
                        h_pd_at(first + ms);
                    }
                    h_step_at(first + ms);
                }
                CHECK(servo_test_running(&h.t));
                CHECK_EQ(h.t.cc_readings, 10u);
                CHECK_EQ(h.t.cc_longest_ms, 900u);
                if (ina != 0u) {
                    h_win_at(first + 950u, 20);
                }
                const uint32_t at = first + k_last[k];
                h_pd_at(at);
                h.now = at;
                if (k_last[k] < SERVO_TEST_CC_ABORT_MS) {
                    CHECK(servo_test_running(&h.t));
                    CHECK_EQ(h.t.cc_longest_ms, 999u);
                    continue;
                }
                CHECK_EQ(h.t.end_ms, at);
                CHECK(!h.t.stalled);
                h_check_ended(SERVO_TEST_AB_CC);
                CHECK(strstr(h.out.report,
                             "Result:         ABORTED - constant current "
                             "for 1s\n") != NULL);
                CHECK(strstr(h.out.report, "Const. current: 11 supply "
                                           "readings, longest stretch 1000"
                                           "ms\n") != NULL);
            }
        }
    }
}

/* A spell of constant current shorter than a second -- an inrush, as the
 * PD mini reports for 0.43s on a healthy move -- ends nothing and fails
 * nothing: the report counts its readings and gives the longest spell. */
TEST_CASE(a_spell_of_constant_current_is_counted_and_fails_nothing)
{
    for (unsigned ina = 0u; ina < 2u; ++ina) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, ina != 0u);
        /* The PD mini's own current shows its moves on the PD mini's run. */
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_MOVE));
        h.cc = true;
        h_run_ms(430u);
        h.cc = false;
        h_run_ms(300u);
        h.cc = true;
        h_run_ms(120u);
        h.cc = false;
        h_out();
        CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
        CHECK(servo_test_verdict(&h.t) != SERVO_TEST_FAIL);
        CHECK(!h.t.stalled);
        CHECK(h.t.cc_readings >= 5u && h.t.cc_readings <= 7u);
        CHECK(h.t.cc_longest_ms >= 312u && h.t.cc_longest_ms <= 416u);
        char want[96];
        snprintf(want, sizeof(want), "Const. current: %lu supply readings, "
                 "longest stretch %lums\n", (unsigned long)h.t.cc_readings,
                 (unsigned long)h.t.cc_longest_ms);
        CHECK(strstr(h.out.report, want) != NULL);
        if (ina != 0u) {
            /* The rows of a run on the INA3221 carry the supply's mode. */
            CHECK(rows_with(h.out.csv, COL_MODE, "CC") >= 6u);
        } else {
            CHECK_EQ(rows_with(h.out.csv, COL_MODE, "CC"), h.t.cc_readings);
        }
    }
}

/* STALL AT against the current limit and the meter's range, in whole mA:
 * at or above the limit no reading passes it, because the supply holds the
 * current; at or above the range none does, because the part reads no
 * more.  The run starts all the same, and its report says so. */
TEST_CASE(a_stall_at_no_reading_can_pass_is_said_and_not_refused)
{
    static const struct {
        float stall, limit;
        bool  ina;
        bool  by_limit, by_range;
    } k_case[] = {
        { 1.99f, 2.00f, false, false, false },
        { 2.00f, 2.00f, false, true,  false },
        { 2.01f, 2.00f, false, true,  false },
        { 3.00f, 2.00f, false, true,  false },
        { 1.63f, 3.00f, true,  false, false },
        { 1.64f, 3.00f, true,  false, true  },
        { 2.00f, 3.00f, true,  false, true  },
        { 2.00f, 2.00f, true,  true,  true  },
        { 1.00f, 2.00f, true,  false, false },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, k_case[k].ina);
        c.stall_a = k_case[k].stall;
        c.i_limit = k_case[k].limit;
        bool by_limit = !k_case[k].by_limit, by_range = !k_case[k].by_range;
        CHECK_EQ(servo_test_stall_unreachable(&c, &by_limit, &by_range),
                 k_case[k].by_limit || k_case[k].by_range);
        CHECK_EQ(by_limit, k_case[k].by_limit);
        CHECK_EQ(by_range, k_case[k].by_range);
        h_start(&c);
        h_out();
        CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
        char want[96];
        snprintf(want, sizeof(want), "STALL AT %.2fA cannot be reached: "
                 "current limit %.2fA\n", (double)k_case[k].stall,
                 (double)k_case[k].limit);
        CHECK_EQ(strstr(h.out.report, want) != NULL, k_case[k].by_limit);
        snprintf(want, sizeof(want), "STALL AT %.2fA cannot be reached: "
                 "INA3221 range 1.638A\n", (double)k_case[k].stall);
        CHECK_EQ(strstr(h.out.report, want) != NULL, k_case[k].by_range);
        CHECK_EQ(strstr(h.out.report, "cannot be reached") != NULL,
                 k_case[k].by_limit || k_case[k].by_range);
    }
    CHECK(!servo_test_stall_unreachable(NULL, NULL, NULL));

    /* The line the SERVO screen shows under START TEST: the limit where
     * that is a reason, else the range, and nothing while STALL AT can be
     * reached. */
    servo_test_cfg_t c;
    char note[96] = "x";
    h_cfg(&c, true);
    CHECK(!servo_test_stall_note(&c, note, sizeof(note)));
    CHECK_STR_EQ(note, "");
    c.stall_a = 1.65f;
    CHECK(servo_test_stall_note(&c, note, sizeof(note)));
    CHECK_STR_EQ(note, "STALL AT 1.65A cannot be reached: INA3221 range "
                       "1.638A");
    c.stall_a = 2.0f;
    CHECK(servo_test_stall_note(&c, note, sizeof(note)));
    CHECK_STR_EQ(note, "STALL AT 2.00A cannot be reached: current limit "
                       "2.00A");
    CHECK(!servo_test_stall_note(&c, NULL, 0u));
    CHECK(!servo_test_stall_note(&c, note, 0u));
}

/* A servo on a stop against a supply that holds its limit, on the
 * INA3221: the windows read the limit, 2.000A on a 0.05Ohm shunt.  One
 * step under the limit STALL AT ends the run; at the limit and above it
 * the constant-current rule does, a second after the supply reports it. */
TEST_CASE(a_servo_held_at_the_limit_ends_the_run_on_either_rule)
{
    static const struct {
        float              stall;
        servo_test_abort_t why;
    } k_case[] = {
        { 1.99f, SERVO_TEST_AB_STALL },
        { 2.00f, SERVO_TEST_AB_CC },
        { 2.01f, SERVO_TEST_AB_CC },
        { 3.00f, SERVO_TEST_AB_CC },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, true);
        servo_test_meter_ina3221(&c.meter, 500u);
        c.stall_a = k_case[k].stall;
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_HOLD));
        const uint32_t on_stop = h.now;
        h.force_ma = 2000;          /* the limit, and not 1mA over it */
        h.cc       = true;
        h.pd_a     = 2.0f;
        for (uint32_t ms = 0u; ms < 1200u && servo_test_running(&h.t);
             ++ms) {
            h_ms();
        }
        /* 20 windows from the one that began at the stop; ten readings
         * of the supply, 104ms apart, after its first there. */
        CHECK(h.t.end_ms - on_stop >= 950u);
        CHECK(h.t.end_ms - on_stop <= 1150u);
        CHECK_EQ(h.t.stalled, k_case[k].why == SERVO_TEST_AB_STALL);
        h_check_ended(k_case[k].why);
    }
}

/* -------------------------------------------------------- every ending */

/* Every way a run on the INA3221 ends, each from a hold at an end with
 * the output on: the output asked off once, the servo let go once, the
 * report ABORTED with the reason, and nothing after it changing either. */
TEST_CASE(every_ending_of_a_run_on_the_ina3221_switches_off_and_is_marked)
{
    /* The caller's: STOP, the link, the screen left, the operator, the
     * settings changed, touch events lost. */
    static const servo_test_abort_t k_callers[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_LINK, SERVO_TEST_AB_LEFT,
        SERVO_TEST_AB_OPERATOR, SERVO_TEST_AB_SETTINGS, SERVO_TEST_AB_TOUCH,
    };
    servo_test_cfg_t c;
    h_cfg(&c, true);
    for (size_t k = 0u; k < sizeof(k_callers) / sizeof(k_callers[0]); ++k) {
        h_fresh(5000u);
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_HOLD));
        CHECK(h.output);
        servo_test_abort(&h.t, k_callers[k], h.now);
        h_check_ended(k_callers[k]);
    }

    /* A disarm. */
    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.armed = false;
    h_ms();
    h.armed = true;
    h_check_ended(SERVO_TEST_AB_DISARMED);

    /* The supply: not answering, tripped, its output off. */
    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.online = false;
    h_pd_at(h.now);
    h.online = true;
    h_check_ended(SERVO_TEST_AB_SUPPLY_LOST);

    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.output = false;
    h.trip   = 1u;
    h_pd_at(h.now);
    h.trip = 0u;
    h_check_ended(SERVO_TEST_AB_TRIPPED);

    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.output = false;
    h_pd_at(h.now);
    h_check_ended(SERVO_TEST_AB_SUPPLY_OFF);

    /* A set point not read back, and an output that does not come on: the
     * windows arrive throughout, and the 3000ms are the supply's. */
    h_fresh(5000u);
    h.take_set = false;
    h.set_v    = 9.0f;
    h_start(&c);
    h_run_ms(3000u);
    CHECK(servo_test_running(&h.t));
    h_ms();
    h_check_ended(SERVO_TEST_AB_SET_NOT_TAKEN);

    h_fresh(5000u);
    h.take_on = false;
    h_start(&c);
    h_run_ms(3001u);
    /* Nothing was switched on, and the off is asked all the same. */
    h_check_ended(SERVO_TEST_AB_NOT_ON);

    /* The cap comes down under the second step. */
    h_fresh(5000u);
    c.steps_v[1] = 6.0f;
    c.step_count = 2u;
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.v_max = 5.5f;
    for (uint32_t k = 0u; k < 20000u && servo_test_running(&h.t); ++k) {
        h_ms();
    }
    CHECK(h.set_v_max <= 4.8f + 0.001f);
    h_check_ended(SERVO_TEST_AB_CAP);
    c.step_count = 1u;

    /* The meter, as servo_test_meter_now() is told: each reason, a change
     * whose reason is not known, and a change count that moved under an
     * INA3221 that is the meter again. */
    static const struct {
        servo_test_meter_now_t now;
        servo_test_abort_t     why;
    } k_meter[] = {
        { { false, 3u, SERVO_TEST_AB_INA_RESET, 3u },
          SERVO_TEST_AB_INA_RESET },
        { { false, 3u, SERVO_TEST_AB_INA_SILENT, 3u },
          SERVO_TEST_AB_INA_SILENT },
        { { false, 3u, SERVO_TEST_AB_INA_NO_WINDOW, 3u },
          SERVO_TEST_AB_INA_NO_WINDOW },
        { { false, 3u, SERVO_TEST_AB_INA_SETUP, 3u },
          SERVO_TEST_AB_INA_SETUP },
        { { false, 3u, SERVO_TEST_AB_LINK, 3u }, SERVO_TEST_AB_LINK },
        /* No drop told, and one from before the run's start. */
        { { false, 3u, SERVO_TEST_AB_NONE, 0u }, SERVO_TEST_AB_INA_METER },
        { { false, 3u, SERVO_TEST_AB_INA_RESET, 1u },
          SERVO_TEST_AB_INA_METER },
        { { false, 3u, SERVO_TEST_AB_INA_RESET, 2u },
          SERVO_TEST_AB_INA_METER },
        /* Gone and back between two answers: the INA3221 again, two
         * changes on. */
        { { true, 4u, SERVO_TEST_AB_INA_SILENT, 3u },
          SERVO_TEST_AB_INA_SILENT },
    };
    for (size_t k = 0u; k < sizeof(k_meter) / sizeof(k_meter[0]); ++k) {
        h_fresh(5000u);
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_HOLD));
        /* The meter as it was at the start, with a drop from before it:
         * the run goes on. */
        const servo_test_meter_now_t same = {
            .ina3221 = true, .changes = 2u,
            .dropped = SERVO_TEST_AB_INA_RESET, .dropped_at = 1u,
        };
        servo_test_meter_now(&h.t, &same, h.now);
        CHECK(servo_test_running(&h.t));
        servo_test_meter_now(&h.t, &k_meter[k].now, h.now);
        CHECK_EQ(h.t.end_ms, h.now);
        h_check_ended(k_meter[k].why);
        CHECK_EQ(rows_with(h.out.csv, COL_METER, "PDMINI"), 0u);
    }
    /* The change count across 2^32. */
    h_fresh(5000u);
    c.meter_changes = 0xFFFFFFFFu;
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    const servo_test_meter_now_t wrapped = {
        .ina3221 = false, .changes = 0u,
        .dropped = SERVO_TEST_AB_INA_RESET, .dropped_at = 0u,
    };
    servo_test_meter_now(&h.t, &wrapped, h.now);
    h_check_ended(SERVO_TEST_AB_INA_RESET);
}

/* Facts under which the INA3221 is the meter, read at @p now
 * (servo_source.h). */
static sense_link_meter_t facts_good(uint32_t now)
{
    const sense_link_meter_t m = {
        .page = true, .wanted = true, .held = true, .setups = 3u,
        .status = true, .status_ms = now,
        .flags = (uint16_t)(LINK_SN_I3221_ONLINE | LINK_SN_I3221_ID_OK
                            | LINK_SN_BUS_OPEN),
        .resets_read = true, .resets = 0u,
        .win = true, .win_valid = true, .win_ms = now,
    };
    return m;
}

/* Each condition of servo_source.h failing under a run on the INA3221, by
 * the meter the real servo_source names from facts written by hand: the
 * run ends in the poll that drops the meter, with that condition's
 * reason.  A reset shows as a condition for one poll only: told to the run
 * three polls later, when the meter is the PD mini for another reason, it
 * is still the reset that ended the run. */
TEST_CASE(each_condition_that_drops_the_meter_ends_the_run)
{
    static const struct {
        servo_source_why_t why;
        servo_test_abort_t ab;
        const char        *text;
    } k_case[] = {
        { SERVO_SOURCE_WHY_OFF, SERVO_TEST_AB_INA_SETUP,
          "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_OLD, SERVO_TEST_AB_INA_SETUP,
          "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_NOT_HELD, SERVO_TEST_AB_INA_SETUP,
          "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_SILENT, SERVO_TEST_AB_INA_SILENT,
          "INA3221 not answering" },
        { SERVO_SOURCE_WHY_NO_WINDOW, SERVO_TEST_AB_INA_NO_WINDOW,
          "INA3221 window without current" },
        { SERVO_SOURCE_WHY_RESET, SERVO_TEST_AB_INA_RESET,
          "INA3221 reset itself" },
        { SERVO_SOURCE_WHY_NO_LINK, SERVO_TEST_AB_LINK, "link lost" },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        for (unsigned late = 0u; late < 2u; ++late) {
            servo_source_t src;
            servo_source_init(&src);
            /* 1050ms of good polls: the INA3221 is the meter. */
            uint32_t at = 3900u;
            for (; at <= 4950u; at += 50u) {
                const sense_link_meter_t m = facts_good(at);
                (void)servo_source_step(&src, at, true, 11u, &m);
            }
            CHECK_EQ(servo_source_id(&src), SERVO_SOURCE_INA3221);
            h_fresh(5000u);
            servo_test_cfg_t c;
            h_cfg(&c, true);
            c.meter_changes = servo_source_changes(&src);
            h_start(&c);
            CHECK(h_until(SERVO_TEST_PH_HOLD));
            /* Good polls under the run change nothing. */
            sense_link_meter_t m = facts_good(h.now);
            (void)servo_source_step(&src, h.now, true, 11u, &m);
            servo_test_meter_now_t now = meter_of(&src);
            servo_test_meter_now(&h.t, &now, h.now);
            CHECK(servo_test_running(&h.t));

            /* The poll in which the condition fails. */
            h_run_ms(50u);
            m = facts_good(h.now);
            uint16_t minor = 11u;
            bool up = true;
            switch (k_case[k].why) {
            case SERVO_SOURCE_WHY_OFF:       m.wanted = false; break;
            case SERVO_SOURCE_WHY_OLD:       minor = 10u; break;
            case SERVO_SOURCE_WHY_NOT_HELD:  m.held = false; break;
            case SERVO_SOURCE_WHY_SILENT:    m.flags = LINK_SN_BUS_OPEN;
                                             break;
            case SERVO_SOURCE_WHY_NO_WINDOW: m.win_valid = false; break;
            case SERVO_SOURCE_WHY_RESET:     m.resets = 1u; break;
            default:                         up = false; break;
            }
            CHECK_EQ(servo_source_step(&src, h.now, up, minor, &m),
                     up ? SERVO_SOURCE_PDMINI : SERVO_SOURCE_MODEL);
            CHECK_EQ(servo_source_why(&src), k_case[k].why);
            uint32_t drop_at = 0u;
            CHECK_EQ(servo_source_dropped(&src, &drop_at), k_case[k].why);
            CHECK_EQ(drop_at, servo_source_changes(&src));
            if (late != 0u) {
                /* Three more polls, every condition holding again, before
                 * the run hears of it: the meter is the PD mini while the
                 * INA3221 settles. */
                for (unsigned n = 0u; n < 3u; ++n) {
                    h_run_ms(50u);
                    m = facts_good(h.now);
                    m.resets = (k_case[k].why == SERVO_SOURCE_WHY_RESET)
                                   ? 1u : 0u;
                    (void)servo_source_step(&src, h.now, true, 11u, &m);
                }
                CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SETTLING);
                CHECK_EQ(servo_source_dropped(&src, NULL), k_case[k].why);
                CHECK(servo_test_running(&h.t));
            }
            now = meter_of(&src);
            servo_test_meter_now(&h.t, &now, h.now);
            h_check_ended(k_case[k].ab);
            char want[96];
            snprintf(want, sizeof(want), "Result:         ABORTED - %s\n",
                     k_case[k].text);
            CHECK(strstr(h.out.report, want) != NULL);
        }
    }
    /* A reset found with the part already taken offline, in one read: the
     * first condition that fails is that the part is not online, and the
     * reason kept for the drop is the reset. */
    servo_source_t src;
    servo_source_init(&src);
    for (uint32_t t = 3900u; t <= 4950u; t += 50u) {
        const sense_link_meter_t m = facts_good(t);
        (void)servo_source_step(&src, t, true, 11u, &m);
    }
    sense_link_meter_t off = facts_good(5000u);
    off.flags  = LINK_SN_BUS_OPEN;
    off.resets = 1u;
    CHECK_EQ(servo_source_step(&src, 5000u, true, 11u, &off),
             SERVO_SOURCE_PDMINI);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SILENT);
    CHECK_EQ(servo_source_dropped(&src, NULL), SERVO_SOURCE_WHY_RESET);
    /* The count moving later, under the PD mini, is no drop. */
    off.resets = 2u;
    (void)servo_source_step(&src, 5050u, true, 11u, &off);
    uint32_t at = 0u;
    CHECK_EQ(servo_source_dropped(&src, &at), SERVO_SOURCE_WHY_RESET);
    CHECK_EQ(at, 3u);
    CHECK_EQ(servo_source_changes(&src), 3u);

    servo_source_init(&src);
    at = 7u;
    CHECK_EQ(servo_source_dropped(&src, &at), SERVO_SOURCE_WHY_NONE);
    CHECK_EQ(at, 0u);
    CHECK_EQ(servo_source_dropped(NULL, &at), SERVO_SOURCE_WHY_NONE);
}

/* --------------------------------------------------------- the file */

/* The run's CSV read by the log viewer's parser: 19 columns on either
 * meter and 21 with the encoder's, none ragged, the time column found and
 * rising.  Files of the format before the meter's columns, 13 wide, read
 * as they did. */
TEST_CASE(the_csv_reads_back_in_the_viewer_old_and_new)
{
    for (unsigned variant = 0u; variant < 3u; ++variant) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, variant != 1u);
        c.enc_on = variant == 2u;
        h_start(&c);
        h_out();
        const char *head = (variant == 2u) ? servo_test_csv_header_enc()
                                           : servo_test_csv_header();
        CHECK(strlen(head) < SERVO_TEST_LINE_MAX);
        CHECK(strncmp(h.out.csv, head, strlen(head)) == 0);
        CHECK_EQ(h.out.csv[strlen(head)], '\n');
        log_source_t src;
        log_mem_ctx_t ctx;
        log_source_memory(&src, &ctx, h.out.csv, h.out.csv_len);
        log_csv_opts_t opts;
        log_csv_opts_default(&opts);
        log_analysis_t an;
        CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
        const int enc = (variant == 2u) ? 2 : 0;
        CHECK_EQ(an.n_columns, (int)COLS + enc);
        CHECK_EQ(an.delimiter, ';');
        CHECK_EQ(an.ragged_rows, 0);
        CHECK_EQ(an.time_index, 0);
        CHECK_EQ(an.row_count, (int)h.out.rows - 1);
        CHECK(an.columns[COL_TIME].monotonic);
        CHECK(an.columns[COL_CURRENT].numeric);
        CHECK(an.columns[COL_VOLTAGE].numeric);
        CHECK_STR_EQ(an.columns[COL_METER + enc].name, "meter");
        CHECK_STR_EQ(an.columns[COL_V_MIN + enc].name, "voltage min");
        CHECK_STR_EQ(an.columns[COL_V_MIN + enc].unit, "V");
        if (variant != 1u) {
            CHECK(an.columns[COL_WINDOW + enc].numeric);
            CHECK(an.columns[COL_I_MAX + enc].numeric);
            CHECK(an.columns[COL_I_MIN + enc].numeric);
            CHECK(an.columns[COL_V_MIN + enc].numeric);
            CHECK(an.columns[COL_CLIPPED + enc].numeric);
        }
    }
    /* The 13 columns come first, in the order they had. */
    CHECK_STR_EQ(servo_test_csv_header(),
                 "time (s);test;step;phase;command (us);position (us);"
                 "set (V);voltage (V);limit (A);current (A);power (W);"
                 "mode;travel (ms);meter;window;current max (A);"
                 "current min (A);voltage min (V);clipped");
    CHECK_EQ(strlen(servo_test_csv_header_enc()), 219u);
    CHECK(strstr(servo_test_csv_header_enc(),
                 ";travel (ms);angle (deg);travel angle (ms);meter;window;")
          != NULL);

    static const struct {
        const char *name;
        int         rows;
    } k_old[] = {
        { "servo-mg90s.csv", 870 },
        { "servo-1102hb.csv", 869 },
        { "servo-ms24-pdmini.csv", 1734 },
    };
    for (size_t k = 0u; k < sizeof(k_old) / sizeof(k_old[0]); ++k) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, k_old[k].name);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL);
        if (f == NULL) {
            continue;
        }
        static char text[200000];
        const size_t n = fread(text, 1u, sizeof(text) - 1u, f);
        fclose(f);
        text[n] = '\0';
        log_source_t src;
        log_mem_ctx_t ctx;
        log_source_memory(&src, &ctx, text, n);
        log_csv_opts_t opts;
        log_csv_opts_default(&opts);
        log_analysis_t an;
        CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
        CHECK_EQ(an.n_columns, 13);
        CHECK_EQ(an.ragged_rows, 0);
        CHECK_EQ(an.time_index, 0);
        CHECK_EQ(an.row_count, k_old[k].rows);
        CHECK(an.columns[COL_CURRENT].numeric);
    }
}

/* ---------------------------------------------- a recorded bench log */

/*
 * A bench log recorded on 0.14.0 beside a servo test of an MS24 digital
 * servo (fixtures/servo-ms24-windows.csv): 1310 rows for the 3817 windows
 * the INA3221 closed, since that firmware wrote a row for 35% of them.
 * Its windows, each at 50ms a number from the first, are fed to a run on
 * the INA3221 with a supply that answers throughout.
 *
 * The run reads every window the file holds up to its first gap longer
 * than 200ms, five numbers or more, counts the numbers missing between
 * them as skipped, and ends 200ms after the last window before that gap:
 * the 50th window, number 19804, followed by 20409.  A file with its
 * windows missing cannot feed a run.  The 0.14.0 log has no lowest
 * sample, no lowest voltage and no clip count: the mean stands in for the
 * first two.
 */
TEST_CASE(a_recorded_log_with_windows_missing_ends_the_run_at_its_first_gap)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/servo-ms24-windows.csv", FIXTURE_DIR);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    static struct {
        unsigned number;
        int      mean_ma, max_ma, mv;
    } rec[1400];
    unsigned n = 0u;
    char line[512];
    const bool has_header = fgets(line, sizeof(line), f) != NULL;
    CHECK(has_header);                                       /* the header */
    if (!has_header) {
        (void)fclose(f);
        return;
    }
    CHECK(strstr(line, ";window;ch1 current (A);ch1 max (A);ch1 voltage "
                       "(V);") != NULL);
    while (fgets(line, sizeof(line), f) != NULL && n < 1400u) {
        char cell[4][32];
        bool ok = true;
        for (unsigned k = 0u; k < 4u; ++k) {
            ok = ok && cell_of(line, 12u + k, cell[k], sizeof(cell[k]));
        }
        if (!ok || cell[0][0] == '\0' || cell[1][0] == '\0') {
            continue;                   /* a row without a window */
        }
        rec[n].number  = (unsigned)strtoul(cell[0], NULL, 10);
        rec[n].mean_ma = (int)lround(strtod(cell[1], NULL) * 1000.0);
        rec[n].max_ma  = (int)lround(strtod(cell[2], NULL) * 1000.0);
        rec[n].mv      = (int)lround(strtod(cell[3], NULL) * 1000.0);
        ++n;
    }
    fclose(f);
    CHECK_EQ(n, 1309u);
    CHECK_EQ(rec[0].number, 19750u);
    CHECK_EQ(rec[n - 1u].number, 23566u);

    /* The first gap longer than SERVO_TEST_WIN_STALE_MS, and what is
     * missing before it: a window that arrives at the 200th ms is read
     * before the run's step at that ms. */
    unsigned upto = 0u, missing = 0u;
    for (unsigned k = 1u; k < n; ++k) {
        const unsigned step = rec[k].number - rec[k - 1u].number;
        if (step * SERVO_TEST_WIN_MS > SERVO_TEST_WIN_STALE_MS) {
            upto = k;
            break;
        }
        missing += step - 1u;
    }
    CHECK_EQ(upto, 50u);
    CHECK_EQ(rec[upto - 1u].number, 19804u);
    CHECK_EQ(missing, 5u);

    h_fresh(5000u);
    h.win_on = false;
    servo_test_cfg_t c;
    h_cfg(&c, true);
    c.stall_a  = 3.0f;              /* as the recorded run's LIMITS page */
    c.by_moves = false;             /* and its 60s a step */
    c.time_s   = 60u;
    h_start(&c);
    const uint32_t t0 = h.now;
    unsigned next = 0u;
    while (servo_test_running(&h.t) && (uint32_t)(h.now - t0) < 60000u) {
        ++h.now;
        while (next < n
               && (rec[next].number - rec[0].number + 1u) * SERVO_TEST_WIN_MS
                      <= (uint32_t)(h.now - t0)) {
            servo_test_win_t w;
            memset(&w, 0, sizeof(w));
            w.number   = (uint16_t)rec[next].number;
            w.current  = true;
            w.voltage  = true;
            w.mean_ma  = (int16_t)rec[next].mean_ma;
            w.max_ma   = (int16_t)rec[next].max_ma;
            w.min_ma   = (int16_t)rec[next].mean_ma;
            w.mean_mv  = (uint16_t)rec[next].mv;
            w.min_mv   = (uint16_t)rec[next].mv;
            w.taken_ms = h.now;
            servo_test_window(&h.t, &w, 0u);
            ++next;
        }
        if ((int32_t)(h.now - h.pd_next) >= 0) {
            h.pd_next += 104u;
            h_pd_at(h.now);
        }
        h_step_at(h.now);
    }
    const uint32_t last = t0 + (rec[upto - 1u].number - rec[0].number + 1u)
                                   * SERVO_TEST_WIN_MS;
    CHECK_EQ(h.t.end_ms, last + SERVO_TEST_WIN_STALE_MS);
    CHECK_EQ(h.t.readings, upto);
    CHECK_EQ(h.t.skipped, missing);
    CHECK_EQ(h.t.samples, (uint16_t)rec[upto - 1u].number);
    CHECK_EQ(next, upto);
    h.win_on = true;
    h.win_next = h.now;
    h.win_number = (uint16_t)rec[n - 1u].number;
    h_check_ended(SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(h.out.rows - 1u, upto);
    CHECK_EQ(rows_with(h.out.csv, COL_METER, "INA3221"), upto);
    /* The first row is the file's first window. */
    char want[96];
    snprintf(want, sizeof(want), ";INA3221;19750;%.3f;%.3f;%.3f;0\n",
             (double)rec[0].max_ma / 1000.0, (double)rec[0].mean_ma / 1000.0,
             (double)rec[0].mv / 1000.0);
    CHECK(strstr(h.out.csv, want) != NULL);
    snprintf(want, sizeof(want), "Skipped:        %u windows the INA3221 "
             "closed never reached the test\n", missing);
    CHECK(strstr(h.out.report, want) != NULL);
}

int main(void)
{
    RUN(no_window_for_200_ms_ends_the_run);
    RUN(windows_without_both_quantities_do_not_hold_off_the_200_ms_end);
    RUN(no_supply_reading_for_1500_ms_ends_a_run_on_the_ina3221);
    RUN(an_idle_without_a_reading_ends_a_run_on_the_pd_mini);
    RUN(stall_at_is_judged_on_the_windows_mean);
    RUN(a_second_over_stall_at_ends_the_run_on_the_windows);
    RUN(a_second_over_stall_at_ends_the_run_on_the_pd_mini);
    RUN(constant_current_for_a_second_ends_the_run);
    RUN(a_spell_of_constant_current_is_counted_and_fails_nothing);
    RUN(a_stall_at_no_reading_can_pass_is_said_and_not_refused);
    RUN(a_servo_held_at_the_limit_ends_the_run_on_either_rule);
    RUN(every_ending_of_a_run_on_the_ina3221_switches_off_and_is_marked);
    RUN(each_condition_that_drops_the_meter_ends_the_run);
    RUN(the_csv_reads_back_in_the_viewer_old_and_new);
    RUN(a_recorded_log_with_windows_missing_ends_the_run_at_its_first_gap);
    out_reset(&h.out);
    return test_summary("servo_test_win");
}
