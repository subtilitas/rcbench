/*
 * AUTO end points (servo_rudder_auto.h): the limit search that opens every
 * run (servo_rudder_auto_search.c).
 *
 * Under test: the search runs after the read session and before any other
 * pulse, out to the margin's range on both sides; its 1000us and 2000us
 * angles are the run's first measures; an end inside the end pulse, by
 * 5us and at the end pulse itself, fails STOP AT n us BEFORE TARGET with
 * no write of a value, the other side not searched where it is the first,
 * and every output off; an end at 995us passes; the found ends cap every
 * later pulse; a STOP RUN, a current above the ceiling and a reading that
 * does not settle at the end pulse end the run in the search as in any
 * phase; the rows stay within SERVO_RAUTO_SEARCH_MAX at a margin of 50%;
 * the CSV and the report carry one row a position.
 *
 * The rig is servo_rauto_rig.h; a stop at a pulse (stop_us[]) holds the
 * surface and draws 1500mA past it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_rig.h"

/* The run to the end of its limit search. */
static void to_searched(void)
{
    while (servo_rauto_running(&g.run) && !g.run.searched) {
        tick();
    }
}

TEST_CASE(the_search_runs_first_and_takes_the_first_measures)
{
    rig_init(0u);
    CHECK(start());
    /* Nothing driven past the center before the search. */
    while (servo_rauto_running(&g.run)
           && g.run.phase != SERVO_RAUTO_PH_SEARCH) {
        CHECK(g.cmd_lo >= RCENTER || g.cmd_lo == 0xFFFFu);
        tick();
    }
    CHECK_EQ(g.run.side, 0u);
    CHECK_EQ(g.run.step_n, 0u);
    to_searched();
    CHECK(g.run.s_took[0] && g.run.s_took[1]);
    CHECK_NEAR(g.run.s_deg[0], -30.0f, 0.01f);
    CHECK_NEAR(g.run.s_deg[1], 30.0f, 0.01f);
    /* The first measures are the first two steps, write 0. */
    CHECK_EQ(g.run.step_n, 2u);
    CHECK_EQ(g.run.steps[0].side, 0u);
    CHECK_EQ(g.run.steps[1].side, 1u);
    CHECK_EQ(g.run.steps[0].n, 0u);
    CHECK_NEAR(g.run.steps[0].measured_deg, 30.0f, 0.01f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(!g.run.ends[0].bound && !g.run.ends[1].bound);
    CHECK_EQ(g.run.ends[0].end_us, 1000u);
    CHECK_EQ(g.run.ends[1].end_us, 2000u);
}

TEST_CASE(an_end_inside_1000us_fails_stop_and_leaves_2000us_unsearched)
{
    /* 1190us binds, 1200us is free: the end 1225us. */
    rig_init(0u);
    g.stop_us[0] = 1200u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STOP);
    CHECK_EQ(g.run.stop_us, 1225u);
    CHECK(servo_rauto_why_error(g.run.why, g.run.test_why));
    CHECK(g.run.ends[0].done && g.run.ends[0].bound);
    CHECK_EQ(g.run.ends[0].bind_us, 1190u);
    CHECK(!g.run.ends[1].done);
    CHECK(!g.run.s_took[0]);
    CHECK_EQ(g.run.step_n, 0u);
    /* The pulse write of the read session only; no value written. */
    CHECK_EQ(g.write_ops, g.pulse_ops);
    CHECK(g.cmd_lo >= 1190u);
    CHECK(g.cmd_hi <= RCENTER);
    CHECK(ended_safe());
    CHECK(in_report("Result:         FAIL - STOP AT 1225us BEFORE TARGET"));
    CHECK(in_report("Low end:        bound at 1190us, endpoint 1225us; free 0.150A, bound 1.500A"));
    CHECK(in_report("High end:       not searched"));
    CHECK(in_report("1190us  1.500A  "));
}

TEST_CASE(an_end_5us_inside_the_target_fails_and_995us_passes)
{
    /* At 10%: 970us binds, 980us free, the end 1005us, 5us inside. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.stop_us[0] = 980u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STOP);
    CHECK_EQ(g.run.stop_us, 1005u);
    /* 1000us passed free on the way: its angle taken, no write. */
    CHECK(g.run.s_took[0]);
    CHECK_EQ(g.write_ops, g.pulse_ops);
    CHECK(ended_safe());

    /* 960us binds, 970us free: the end 995us lies outside the target. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.stop_us[0] = 970u;
    CHECK(start());
    to_searched();
    CHECK(servo_rauto_running(&g.run));
    CHECK(g.run.ends[0].bound);
    CHECK_EQ(g.run.ends[0].end_us, 995u);
    CHECK_EQ(g.run.ends[1].end_us, 2050u);
    /* Every pulse after the search within the found ends. */
    g.cmd_lo = 0xFFFFu;
    g.cmd_hi = 0u;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(g.cmd_lo >= 995u);
    CHECK(g.cmd_hi <= 2050u);
    CHECK(in_report("Low end:        bound at 960us, endpoint 995us"));
    CHECK(in_report("High end:       free to 2050us"));
}

TEST_CASE(an_end_at_2000us_bound_fails_on_the_2000us_side)
{
    /* 2000us itself binds: 1990us free, the end 1965us. */
    rig_init(0u);
    g.stop_us[1] = 1995u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STOP);
    CHECK_EQ(g.run.stop_us, 1965u);
    CHECK(g.run.ends[0].done && !g.run.ends[0].bound);
    CHECK_EQ(g.run.ends[1].bind_us, 2000u);
    CHECK(g.run.s_took[0] && !g.run.s_took[1]);
    CHECK_EQ(g.write_ops, g.pulse_ops);
    CHECK(ended_safe());
}

TEST_CASE(a_pulse_past_a_found_end_ends_the_run)
{
    /* Ends that move inward after the search: the next 1000us command
     * stops the run before it goes out. */
    rig_init(0u);
    CHECK(start());
    to_searched();
    CHECK(servo_rauto_running(&g.run));
    g.run.ends[0].end_us = 1010u;
    g.cmd_lo = 0xFFFFu;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STOP);
    CHECK_EQ(g.run.stop_us, 1010u);
    CHECK(g.cmd_lo >= 1010u);
    CHECK(ended_safe());
}

TEST_CASE(stop_run_and_the_current_ceiling_end_the_search)
{
    /* STOP RUN 2s into the search: ABORTED, every output off. */
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_SEARCH, RAUTO_MS));
    idle_for(2000u);
    CHECK_EQ(g.run.phase, SERVO_RAUTO_PH_SEARCH);
    servo_rauto_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    run_for(100u);
    CHECK(ended_safe());
    CHECK(in_report("Low end:        not searched"));

    /* A window above 3.0A in the search: HARD. */
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_SEARCH, RAUTO_MS));
    idle_for(1000u);
    g.extra_ma = 3100;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_HARD);
    CHECK(ended_safe());
}

TEST_CASE(the_rows_fit_at_a_margin_of_50_percent)
{
    /* 750us to 2250us: 76 positions a side, the center on each. */
    rig_init(0u);
    g.cfg.margin_pct = 50u;
    CHECK(start());
    to_searched();
    CHECK_EQ(g.run.srow_n, 152u);
    CHECK(g.run.srow_n <= SERVO_RAUTO_SEARCH_MAX);
    CHECK_EQ(g.run.s_steps, 150u);
    CHECK_EQ(g.run.s_plan, 150u);
    CHECK_EQ(g.run.ends[0].end_us, 750u);
    CHECK_EQ(g.run.ends[1].end_us, 2250u);
    CHECK_EQ(g.cmd_lo, 750u);
    CHECK_EQ(g.cmd_hi, 2250u);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    /* The CSV's last row: the 2000us side's last position. */
    CHECK(strstr(g.csv, ";2250;S;150;30.00;;;;\n") != NULL);
}

TEST_CASE(a_center_off_the_10us_grid_holds_1000us_and_2000us_too)
{
    /* PULSE CENTER 1505us at 10%: the steps land on 1005us and 995us,
     * 1995us and 2005us; 1000us and 2000us are commanded between them for
     * the first measure.  At 0% the last, short step is the end pulse. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.cfg.center_us = 1505u;
    CHECK(start());
    bool held[2] = { false, false };
    while (servo_rauto_running(&g.run) && !g.run.searched) {
        tick();
        if (g.run.s_hold) {
            held[g.run.side] = held[g.run.side] || g.cmd == (g.run.side == 0u
                                                       ? 1000u : 2000u);
        }
    }
    CHECK(held[0] && held[1]);
    CHECK_EQ(g.run.s_steps, g.run.s_plan);
    /* 1505us to 950us: 56 steps and 1000us; to 2050us: 55 and 2000us. */
    CHECK_EQ(g.run.s_plan, 113u);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);

    rig_init(0u);
    g.cfg.center_us = 1505u;
    CHECK(start());
    to_searched();
    CHECK_EQ(g.run.s_steps, g.run.s_plan);
    CHECK_EQ(g.run.s_plan, 101u);
}

int main(void)
{
    RUN(the_search_runs_first_and_takes_the_first_measures);
    RUN(an_end_inside_1000us_fails_stop_and_leaves_2000us_unsearched);
    RUN(an_end_5us_inside_the_target_fails_and_995us_passes);
    RUN(an_end_at_2000us_bound_fails_on_the_2000us_side);
    RUN(a_pulse_past_a_found_end_ends_the_run);
    RUN(stop_run_and_the_current_ceiling_end_the_search);
    RUN(the_rows_fit_at_a_margin_of_50_percent);
    RUN(a_center_off_the_10us_grid_holds_1000us_and_2000us_too);
    return test_summary("servo_rauto_search");
}
