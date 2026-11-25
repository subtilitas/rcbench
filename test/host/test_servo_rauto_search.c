/*
 * AUTO end points (servo_rudder_auto.h): the limit search that opens every
 * run (servo_rudder_auto_search.c).
 *
 * Under test: the search runs after the read session and before any other
 * pulse, out to the margin's range on both sides, scaled from the read's
 * NEUTRAL; its 1000us and 2000us
 * angles are the run's first measures; an end inside the end pulse is held
 * for its angle: short of TARGET less TOLERANCE it fails STOP AT <n>us
 * BEFORE TARGET with no write of a value, the other side not searched
 * where it is the first, and every output off; at or past it, the
 * boundary included, the side is measured at the end, written shorter and
 * verified there, nothing driven past it; an end that does not settle
 * takes the last free step's angle, scaled from the pulse it was taken at,
 * for the STOP test too; a stall stretch from before the hold ends with
 * it; the command range is checked for every NEUTRAL;
 * an end pulse off the 10us grid is held only once it, or a step past it,
 * is judged free; every first measure, at the end pulse and at a found
 * end, is reached from PULSE CENTER where the servo stands past it; an end at 995us lies outside the end pulse; the found ends cap every later pulse; a STOP RUN, a current above the ceiling and a reading that
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
    /* 1190us binds, 1200us is free: the end 1225us, where the surface
     * stands at 16.5°, short of 27.5° less 0.5°. */
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
    CHECK(g.run.ends[0].deg_taken && !g.run.ends[0].at_end);
    CHECK_NEAR(g.run.ends[0].deg, -16.5f, 0.01f);
    CHECK_EQ(g.run.ends[0].deg_us, 1225u);
    CHECK_EQ(g.run.step_n, 0u);
    /* The pulse write of the read session only; no value written. */
    CHECK_EQ(g.write_ops, g.pulse_ops);
    CHECK(g.cmd_lo >= 1190u);
    CHECK(g.cmd_hi <= RCENTER);
    CHECK(ended_safe());
    CHECK(in_report("Result:         FAIL - STOP AT 1225us BEFORE TARGET"));
    CHECK(in_report("Low end:        bound at 1190us, endpoint 1225us; free 0.150A, bound 1.500A"));
    CHECK(in_report("High end:       not searched"));
    CHECK(in_report("                -16.50° at the end 1225us; short of the target 27.5° by more than the tolerance"));
    CHECK(in_report("1190us  1.500A  "));
}

TEST_CASE(a_wide_throw_bound_past_the_target_is_written_shorter_at_its_end)
{
    /* TARGET 20.0°: 1040us binds, 1050us is free, the end 1075us, where
     * the surface stands at 25.5°, past the target.  The side is measured
     * at 1075us, 25.5° standing for 30.0° at 1000us (x 500 / 425); the
     * write takes 150 counts to 100, and the verify pass at 1075us reads
     * 17.0°, 20.0° at 1000us. */
    rig_init(0u);
    g.cfg.target_deg[0] = 20.0f;
    g.cfg.target_deg[1] = 20.0f;
    g.stop_us[0] = 1050u;
    CHECK(start());
    to_searched();
    CHECK(servo_rauto_running(&g.run));
    CHECK(g.run.ends[0].at_end);
    CHECK_EQ(g.run.ends[0].end_us, 1075u);
    CHECK_EQ(servo_rauto_side_us(&g.run, 0u), 1075u);
    CHECK_EQ(servo_rauto_side_us(&g.run, 1u), 2000u);
    CHECK_NEAR(g.run.s_deg[0], -25.5f, 0.01f);
    CHECK_NEAR(g.run.steps[0].measured_deg, 30.0f, 0.01f);
    /* Nothing driven past 1075us after the search. */
    g.cmd_lo = 0xFFFFu;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.cmd_lo, 1075u);
    CHECK_EQ(g.v[0], 100u);
    CHECK_EQ(g.run.vn, SERVO_RAUTO_VERIFY_N);
    CHECK(g.run.vwithin[0]);
    CHECK_NEAR(g.run.vdeg[0], 20.0f, 0.01f);
    CHECK(in_report("                -25.50° at the end 1075us; the side measured there, its angles scaled to 1000us by the linear map (x 1.176)"));
    CHECK(in_report("1075us  measured 20.00°  goal 20.00°  within"));
    CHECK(in_report("1075us  write 0  value -  measured 30.00°  goal 20.00°  outside"));
    CHECK(strstr(g.csv, "\n1;1075;0;;30.00;20.00;;;0;STEP;1075;;\n") != NULL);
    CHECK(ended_safe());
}

TEST_CASE(the_end_at_target_less_tolerance_passes_and_below_it_fails)
{
    /* The end 1075us at 25.5°: TARGET 26.0° less TOLERANCE 0.5° is 25.5°,
     * at the boundary: the run goes on.  TARGET 26.1°: 0.1° short of it,
     * STOP AT 1075us BEFORE TARGET. */
    rig_init(0u);
    g.cfg.target_deg[0] = 26.0f;
    g.stop_us[0] = 1050u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(g.run.ends[0].at_end);

    rig_init(0u);
    g.cfg.target_deg[0] = 26.1f;
    g.stop_us[0] = 1050u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STOP);
    CHECK_EQ(g.run.stop_us, 1075u);
    CHECK(!g.run.ends[0].at_end);
    CHECK_EQ(g.write_ops, g.pulse_ops);
    CHECK(ended_safe());
}

TEST_CASE(an_end_5us_inside_goes_on_there_and_995us_is_outside)
{
    /* At 10%: 970us binds, 980us free, the end 1005us, 5us inside 1000us
     * at 29.7°: measured and verified at 1005us. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.stop_us[0] = 980u;
    CHECK(start());
    to_searched();
    CHECK(g.run.ends[0].at_end);
    g.cmd_lo = 0xFFFFu;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.cmd_lo, 1005u);

    /* 960us binds, 970us free: the end 995us lies outside 1000us. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.stop_us[0] = 970u;
    CHECK(start());
    to_searched();
    CHECK(servo_rauto_running(&g.run));
    CHECK(g.run.ends[0].bound);
    CHECK(!g.run.ends[0].at_end && !g.run.ends[0].deg_taken);
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

TEST_CASE(an_end_inside_2000us_is_measured_there)
{
    /* 2000us itself binds: 1990us free, the end 1965us at 27.9°. */
    rig_init(0u);
    g.stop_us[1] = 1995u;
    CHECK(start());
    to_searched();
    CHECK(g.run.ends[0].done && !g.run.ends[0].bound);
    CHECK_EQ(g.run.ends[1].bind_us, 2000u);
    CHECK(g.run.ends[1].at_end);
    CHECK_EQ(servo_rauto_side_us(&g.run, 1u), 1965u);
    CHECK_NEAR(g.run.s_deg[1], 27.9f, 0.01f);
    g.cmd_hi = 0u;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.cmd_hi, 1965u);
    CHECK(ended_safe());
}

TEST_CASE(an_end_that_does_not_settle_takes_the_last_free_step)
{
    /* The reading jitters at the end 1075us: by the measure's wait the
     * angle is the last free step's, 1050us at 27.0°, and the report says
     * so; 27.0° lies past TARGET 20.0°. */
    rig_init(0u);
    g.cfg.target_deg[0] = 20.0f;
    g.stop_us[0] = 1050u;
    CHECK(start());
    while (servo_rauto_running(&g.run)
           && (!g.run.s_endhold || g.run.s_via)) {
        tick();
    }
    CHECK_EQ(g.cmd, 1075u);
    g.jitter = true;
    while (servo_rauto_running(&g.run) && g.run.s_endhold) {
        tick();
    }
    g.jitter = false;
    CHECK(servo_rauto_running(&g.run));
    CHECK(g.run.ends[0].fallback && g.run.ends[0].at_end);
    CHECK_EQ(g.run.ends[0].deg_us, 1050u);
    CHECK_NEAR(g.run.ends[0].deg, -27.0f, 0.01f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(in_report("                -27.00° at the last free step 1050us, the end not settled; the side measured there"));
}

TEST_CASE(a_fallback_angle_is_scaled_from_the_pulse_it_was_taken_at)
{
    /* PULSE CENTER 1500us, a stop past 1450us: 1440us binds, the end
     * 1475us.  TARGET 1.5°, TOLERANCE 0.1°, the register down to 1.  The
     * end does not settle: the last free step's 3.0° at 1450us, 50us from
     * the center, stands for 30.0° at 1000us (x 500 / 50), not 60.0°
     * (x 500 / 25, the end's).  150 counts x 1.5 / 30.0 is 7.5: the write
     * takes 8, as a settled end gives, and the run passes. */
    rig_init(0u);
    g.cfg.target_deg[0] = 1.5f;
    g.cfg.target_deg[1] = 1.5f;
    g.cfg.tol_ddeg = 1u;
    g.lo = 1u;
    g.stop_us[0] = 1450u;
    CHECK(start());
    while (servo_rauto_running(&g.run)
           && (!g.run.s_endhold || g.run.s_via)) {
        tick();
    }
    CHECK_EQ(g.cmd, 1475u);
    g.jitter = true;
    while (servo_rauto_running(&g.run) && g.run.s_endhold) {
        tick();
    }
    g.jitter = false;
    CHECK(g.run.ends[0].fallback && g.run.ends[0].at_end);
    CHECK_EQ(g.run.ends[0].deg_us, 1450u);
    CHECK_NEAR(g.run.ends[0].deg, -3.0f, 0.01f);
    /* As the end reads it by the linear map: 1.5°. */
    CHECK_NEAR(g.run.s_deg[0], -1.5f, 0.01f);
    to_searched();
    CHECK_NEAR(g.run.steps[0].measured_deg, 30.0f, 0.01f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(g.run.why != SERVO_RAUTO_WHY_CAP);
    CHECK_EQ(g.v[0], 8u);

    /* The same with the end settled: 1.5° at 1475us, 8 counts too. */
    rig_init(0u);
    g.cfg.target_deg[0] = 1.5f;
    g.cfg.target_deg[1] = 1.5f;
    g.cfg.tol_ddeg = 1u;
    g.lo = 1u;
    g.stop_us[0] = 1450u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(!g.run.ends[0].fallback);
    CHECK_EQ(g.v[0], 8u);
}

TEST_CASE(a_fallback_angle_is_judged_against_the_target_as_the_end_reads_it)
{
    /* PULSE CENTER 1500us, a stop past 1450us, the end 1475us, TARGET
     * 2.0°, TOLERANCE 0.1°, the register down to 1.  Settled, the end
     * reads 1.5°, short of 1.9°: STOP.  Not settled, the last free step's
     * 3.0° at 1450us reads 1.5° at the end too: STOP, no value written. */
    for (int jitter = 0; jitter < 2; ++jitter) {
        rig_init(0u);
        g.cfg.target_deg[0] = 2.0f;
        g.cfg.target_deg[1] = 2.0f;
        g.cfg.tol_ddeg = 1u;
        g.lo = 1u;
        g.stop_us[0] = 1450u;
        CHECK(start());
        while (servo_rauto_running(&g.run)
               && (!g.run.s_endhold || g.run.s_via)) {
            tick();
        }
        g.jitter = jitter != 0;
        while (servo_rauto_running(&g.run) && g.run.s_endhold) {
            tick();
        }
        g.jitter = false;
        run_for(RAUTO_MS);
        CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STOP);
        CHECK_EQ(g.run.stop_us, 1475u);
        CHECK_EQ(g.run.ends[0].fallback, jitter != 0);
        CHECK_EQ(g.run.ends[0].deg_us, jitter != 0 ? 1450u : 1475u);
        CHECK_EQ(g.write_ops, g.pulse_ops);
        CHECK(ended_safe());
    }
}

/* The rig to 1000us's judging window at 1150mA, held there at 650mA:
 * @p rise_ms before the hold ends 1150mA again, for @p ms in all.
 * @return when the hold ended, ms after the rig's start. */
static uint32_t stall_rig(uint32_t hold_end, uint32_t rise_ms, uint32_t ms)
{
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.extra_ma = 500;
    CHECK(start());
    while (servo_rauto_running(&g.run)
           && !(g.run.lf.cmd_us == 1000u && g.run.lf.measuring)) {
        tick();
    }
    g.extra_ma = 1000;
    while (servo_rauto_running(&g.run) && !g.run.s_hold) {
        tick();
    }
    CHECK(g.run.lf.elevated);
    g.extra_ma = 500;
    while (servo_rauto_running(&g.run) && g.run.s_hold) {
        if (hold_end != 0u && g.now - g.t0 + rise_ms == hold_end) {
            g.extra_ma = 1000;
        }
        tick();
    }
    const uint32_t end = g.now - g.t0;
    if (hold_end != 0u) {
        idle_for(ms - rise_ms);
        g.extra_ma = 500;
    }
    return end;
}

TEST_CASE(a_stall_stretch_before_the_hold_does_not_count_after_it)
{
    /* MARGIN 10%, 650mA free.  The window that judges 1000us reads
     * 1150mA, under 1.8 x 650mA: free, the stall guard's stretch begun.
     * 650mA through the hold, then 1150mA for 200ms from 60ms before the
     * search goes on toward 950us: no stretch above 1.0A lasts 400ms, so
     * no STALL. */
    const uint32_t end = stall_rig(0u, 0u, 0u);
    CHECK(servo_rauto_running(&g.run));
    CHECK_EQ(stall_rig(end, 60u, 200u), end);
    CHECK(servo_rauto_running(&g.run));
    run_for(RAUTO_MS);
    CHECK(g.run.why != SERVO_RAUTO_WHY_STALL);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
}

TEST_CASE(the_command_range_is_checked_for_every_neutral)
{
    /* PULSE MIN to MAX 1000-2000us, CENTER 1750us: 1000-2500us at margin
     * 0, a range a command names; at 10% the margin's widest, 936us for
     * NEUTRAL 1640us, makes it 936-2564us, past 2500us. */
    uint16_t us[2];
    CHECK(servo_rauto_cmd_range(0u, 1750u, 1000u, 2000u, us));
    CHECK_EQ(us[0], 1000u);
    CHECK_EQ(us[1], 2500u);
    CHECK(!servo_rauto_cmd_range(10u, 1750u, 1000u, 2000u, us));
    CHECK_EQ(us[0], 936u);
    CHECK_EQ(us[1], 2564u);
    /* CENTER 1500us at 10%: 936-2064us. */
    CHECK(servo_rauto_cmd_range(10u, 1500u, 1000u, 2000u, us));
    CHECK_EQ(us[0], 936u);
    CHECK_EQ(us[1], 2064u);
    /* CENTER 1250us at 50%: up to 2320us for NEUTRAL 1360us, 180us below,
     * under the 400us floor. */
    CHECK(!servo_rauto_cmd_range(50u, 1250u, 1000u, 2000u, us));
    CHECK_EQ(us[0], 180u);
    CHECK_EQ(us[1], 2320u);
    CHECK(!servo_rauto_cmd_range(10u, 1500u, 1000u, 2000u, NULL));
}

TEST_CASE(an_end_pulse_off_the_grid_is_held_only_once_judged_free)
{
    /* PULSE CENTER 1505us at 10%, a stop past 1002us: the steps land on
     * 1005us, free, and 995us, bound.  1000us lies past the stop and is
     * never held: the end is 1030us, 25us back from 1005us, measured
     * there; no stall.  With PULSE CENTER 1500us the step 1000us binds and
     * the end is 1035us. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.cfg.center_us = 1505u;
    g.cfg.target_deg[0] = 20.0f;
    g.cfg.target_deg[1] = 20.0f;
    g.stop_us[0] = 1002u;
    CHECK(start());
    bool held_1000 = false;
    while (servo_rauto_running(&g.run) && !g.run.searched) {
        tick();
        held_1000 = held_1000 || (g.run.s_hold && g.cmd == 1000u);
    }
    CHECK(!held_1000);
    CHECK(g.run.ends[0].bound);
    CHECK_EQ(g.run.ends[0].bind_us, 995u);
    CHECK_EQ(g.run.ends[0].end_us, 1030u);
    CHECK(g.run.ends[0].at_end);
    run_for(RAUTO_MS);
    CHECK(g.run.why != SERVO_RAUTO_WHY_STALL);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);

    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.cfg.target_deg[0] = 20.0f;
    g.cfg.target_deg[1] = 20.0f;
    g.stop_us[0] = 1002u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.ends[0].end_us, 1035u);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
}

/*
 * A pulse reached on the way in reads 0.33° less than on the way out.
 * PULSE CENTER 1505us at 10%, the servo holding the margin's pulses
 * 950.4us and 2049.6us, TARGET 29.7°, TOLERANCE 0.1°: the steps land on
 * 995us and 2005us, and 1000us and 2000us are reached from PULSE CENTER,
 * as every later measure: 30.0°, outside, so the run writes.  Reached from
 * 995us and 2005us they read 29.67°, within, and the run passed with no
 * write.  A found end after a bind is reached so too: 1075us at 25.5°.
 */
TEST_CASE(every_first_measure_comes_from_the_center_side)
{
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.cfg.center_us = 1505u;
    g.zero_us = 1505u;
    g.cfg.target_deg[0] = 29.7f;
    g.cfg.target_deg[1] = 29.7f;
    g.cfg.tol_ddeg = 1u;
    g.pulses[1] = 9504u;
    g.pulses[2] = 20496u;
    g.back_deg = 0.33f;
    CHECK(start());
    uint16_t from[2] = { 0u, 0u }, last = g.cmd;
    while (servo_rauto_running(&g.run) && !g.run.searched) {
        tick();
        if (g.cmd != last && (g.cmd == 1000u || g.cmd == 2000u)) {
            from[g.cmd == 2000u ? 1 : 0] = last;
        }
        last = g.cmd;
    }
    CHECK_EQ(from[0], 1505u);
    CHECK_EQ(from[1], 1505u);
    CHECK_NEAR(g.run.s_deg[0], -30.0f, 0.01f);
    CHECK_NEAR(g.run.s_deg[1], 30.0f, 0.01f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK(g.write_ops > 0u);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(g.run.vn > 0u);

    rig_init(0u);
    g.cfg.target_deg[0] = 20.0f;
    g.stop_us[0] = 1050u;
    g.back_deg = 0.33f;
    CHECK(start());
    last = g.cmd;
    uint16_t before_end = 0u;
    while (servo_rauto_running(&g.run) && !g.run.ends[0].deg_taken) {
        tick();
        if (g.cmd != last && g.cmd == 1075u) {
            before_end = last;
        }
        last = g.cmd;
    }
    CHECK_EQ(before_end, RCENTER);
    CHECK_NEAR(g.run.ends[0].deg, -25.5f, 0.01f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);

    /* Not still at PULSE CENTER on the way: NOT STILL, as an approach. */
    rig_init(0u);
    g.cfg.target_deg[0] = 20.0f;
    g.stop_us[0] = 1050u;
    CHECK(start());
    while (servo_rauto_running(&g.run) && !g.run.s_via) {
        tick();
    }
    CHECK_EQ(g.cmd, RCENTER);
    g.jitter = true;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NOT_STILL);
    CHECK(!g.run.ends[0].deg_taken);
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
    /* STOP RUN 2s into the search: ABORTED, every output off.  The run's
     * own command range applies from the search's start to its end. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    uint16_t us[2] = { 0u, 0u };
    CHECK(!servo_rauto_drive_range(&g.run, us));
    CHECK(start());
    CHECK(!servo_rauto_drive_range(&g.run, us));
    CHECK(run_to(SERVO_RAUTO_PH_SEARCH, RAUTO_MS));
    CHECK(servo_rauto_drive_range(&g.run, us));
    CHECK_EQ(us[0], 950u);
    CHECK_EQ(us[1], 2050u);
    CHECK(!servo_rauto_drive_range(NULL, us));
    CHECK(!servo_rauto_drive_range(&g.run, NULL));
    idle_for(2000u);
    CHECK_EQ(g.run.phase, SERVO_RAUTO_PH_SEARCH);
    servo_rauto_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    us[0] = us[1] = 0u;
    CHECK(!servo_rauto_drive_range(&g.run, us));
    CHECK_EQ(us[0], 0u);
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
    CHECK(strstr(g.csv, ";2250;S;150;30.00;;;;;SEARCH;2250;0.150;\n") != NULL);
}

TEST_CASE(the_search_reaches_the_range_scaled_from_the_neutral)
{
    /* NEUTRAL 1640.0us at 10%: PULSE LOWER 1000us - 64us, PULSE UPPER
     * 2000us + 36us; the search reaches 936us and 2036us, both free. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.pulses[0] = 16400u;
    CHECK(start());
    to_searched();
    CHECK(servo_rauto_running(&g.run));
    CHECK(!g.run.ends[0].bound && !g.run.ends[1].bound);
    CHECK_EQ(g.run.ends[0].end_us, 936u);
    CHECK_EQ(g.run.ends[1].end_us, 2036u);
    CHECK_EQ(g.cmd_lo, 936u);
    CHECK_EQ(g.cmd_hi, 2036u);
    CHECK_EQ(g.run.s_steps, g.run.s_plan);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.cmd_lo, 936u);
    CHECK_EQ(g.cmd_hi, 2036u);
    CHECK(in_report("Low end:        free to 936us"));
    CHECK(in_report("High end:       free to 2036us"));
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
    RUN(a_wide_throw_bound_past_the_target_is_written_shorter_at_its_end);
    RUN(the_end_at_target_less_tolerance_passes_and_below_it_fails);
    RUN(an_end_5us_inside_goes_on_there_and_995us_is_outside);
    RUN(an_end_inside_2000us_is_measured_there);
    RUN(an_end_that_does_not_settle_takes_the_last_free_step);
    RUN(a_fallback_angle_is_scaled_from_the_pulse_it_was_taken_at);
    RUN(a_fallback_angle_is_judged_against_the_target_as_the_end_reads_it);
    RUN(a_stall_stretch_before_the_hold_does_not_count_after_it);
    RUN(the_command_range_is_checked_for_every_neutral);
    RUN(an_end_pulse_off_the_grid_is_held_only_once_judged_free);
    RUN(every_first_measure_comes_from_the_center_side);
    RUN(a_pulse_past_a_found_end_ends_the_run);
    RUN(stop_run_and_the_current_ceiling_end_the_search);
    RUN(the_rows_fit_at_a_margin_of_50_percent);
    RUN(the_search_reaches_the_range_scaled_from_the_neutral);
    RUN(a_center_off_the_10us_grid_holds_1000us_and_2000us_too);
    return test_summary("servo_rauto_search");
}
