/*
 * AUTO end points (servo_rudder_auto.h): the verify pass after
 * programming.
 *
 * Under test: a programmed run measures 1000us, 2000us and PULSE CENTER
 * once more and passes with all three within; an end that moves after the
 * last write fails VERIFY on that end with all three measured and RESTORE
 * offered; a center off the zero fails VERIFY on the center; a reading
 * that never settles in the verify pass ends NOT STILL at the measure's
 * wait; STOP in the verify pass ends the run ABORTED with every output
 * off; a run that writes nothing does not verify; every end is approached
 * from PULSE CENTER and the verify pass's center from 2000us.  The CSV
 * carries the three as "V" rows.
 *
 * The rig is servo_rauto_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_rig.h"

/* The run stepped on to its verify pass's measure @p step. */
static bool on_to_verify(uint8_t step)
{
    for (uint32_t k = 0; k < RAUTO_MS; ++k) {
        if (!servo_rauto_running(&g.run)) {
            return false;
        }
        if (g.run.phase == SERVO_RAUTO_PH_VERIFY && g.run.vstep == step) {
            return true;
        }
        tick();
    }
    return false;
}

/* The run started and stepped to its verify pass's measure @p step. */
static bool to_verify(uint8_t step)
{
    return start() && on_to_verify(step);
}

TEST_CASE(a_programmed_run_verifies_both_ends_and_the_center_and_passes)
{
    rig_init(0u);
    CHECK(to_verify(0u));
    /* The last measure of the main pass is in; the verify pass starts at
     * 1000us. */
    CHECK_EQ(g.run.vn, 0u);
    CHECK_EQ(g.run.cmd_us, 1000u);
    const uint8_t steps = g.run.step_n;
    CHECK(on_to_verify(2u));
    CHECK_EQ(g.run.vn, 2u);
    CHECK_EQ(g.run.cmd_us, RCENTER);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.vn, SERVO_RAUTO_VERIFY_N);
    CHECK_EQ(g.run.step_n, steps);   /* the verify pass is no step */
    for (int k = 0; k < 2; ++k) {
        CHECK(g.run.vwithin[k]);
        CHECK(fabsf(g.run.vdeg[k] - g.run.goal_deg[k]) <= 0.5f);
    }
    CHECK(g.run.vwithin[2]);
    CHECK(fabsf(g.run.vdeg[2]) <= 0.5f);
    /* It ends at PULSE CENTER, every output off. */
    CHECK_EQ(g.cmd, RCENTER);
    CHECK(ended_safe());
    CHECK(!servo_rauto_restore(&g.run));
    CHECK(in_report("2000us  measured 27.60°  goal 27.50°  within"));
    CHECK(strstr(g.csv, ";2000;V;;27.60;27.50;;;1;VERIFY;2000;;\n") != NULL);
}

TEST_CASE(an_end_that_moves_after_the_last_write_fails_verify_on_that_end)
{
    rig_init(0u);
    CHECK(to_verify(0u));
    /* The 1000us side 2° further than the last measure took it. */
    g.v[0] = (uint16_t)(g.v[0] + 10u);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_VERIFY);
    CHECK_EQ(g.run.vbad, 0u);
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_VERIFY_LO);
    /* All three measured, the other two within. */
    CHECK_EQ(g.run.vn, SERVO_RAUTO_VERIFY_N);
    CHECK(!g.run.vwithin[0]);
    CHECK(g.run.vwithin[1]);
    CHECK(g.run.vwithin[2]);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
    CHECK(in_report("1000us outside the tolerance at verify"));
    CHECK(in_report("1000us  measured 29.60°  goal 27.50°  outside"));
}

TEST_CASE(a_center_off_the_zero_fails_verify_on_the_center)
{
    /* PULSE CENTER at 1510us on a rig whose zero is at 1500us: 0.6°. */
    rig_init(0u);
    g.cfg.center_us = 1510u;
    CHECK(to_verify(2u));
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_VERIFY);
    CHECK_EQ(g.run.vbad, 2u);
    CHECK(g.run.vwithin[0]);
    CHECK(g.run.vwithin[1]);
    CHECK(!g.run.vwithin[2]);
    CHECK_NEAR(g.run.vdeg[2], 0.552f, 0.01f);
    CHECK(ended_safe());
    CHECK(in_report("center outside the tolerance at verify"));
    CHECK(in_report("1510us  measured 0.55°  goal 0.00°  outside"));
}

TEST_CASE(a_reading_that_never_settles_in_verify_ends_not_still)
{
    rig_init(0u);
    CHECK(to_verify(1u));
    g.jitter = true;
    const uint32_t at = g.run.move_ms;
    while (servo_rauto_running(&g.run) && g.now - at < RAUTO_MS) {
        tick();
    }
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NOT_STILL);
    CHECK_EQ(g.run.end_ms - at, servo_rauto_wait_ms(&g.run));
    CHECK_EQ(g.run.vn, 1u);
    run_for(10u);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
}

TEST_CASE(stop_in_verify_ends_the_run_aborted_with_every_output_off)
{
    for (uint8_t step = 0; step < SERVO_RAUTO_VERIFY_N; ++step) {
        rig_init(0u);
        CHECK(to_verify(step));
        idle_for(300u);             /* before its angle is taken */
        CHECK(servo_rauto_running(&g.run));
        const unsigned arms = g.arms;
        servo_rauto_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
        CHECK(!servo_rauto_running(&g.run));
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
        CHECK_EQ(g.run.vn, step);
        servo_rauto_do_t d;
        servo_rauto_step(&g.run, g.now, NULL, &d);
        CHECK(!d.command);
        CHECK(d.off && d.release && d.disarm);
        take(&d);
        run_for(50u);
        CHECK(ended_safe());
        CHECK_EQ(g.arms, arms);     /* no ARM after the STOP */
    }
}

TEST_CASE(a_run_that_writes_nothing_does_not_verify)
{
    /* 25° x 1.1 = 27.5° at the start: both sides within, no write. */
    rig_init(0u);
    g.v[0] = g.v[1] = 138u;
    g.pulses[1] = 10000u;
    g.pulses[2] = 20000u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.write_ops, 0u);
    CHECK_EQ(g.run.vn, 0u);
    CHECK(in_report("No write: the measurements above are the last."));
    CHECK(strstr(g.csv, ";V;") == NULL);
}

/*
 * The approach: every end the run measures after the search, in the main
 * pass and in the verify pass, is commanded from PULSE CENTER after the
 * servo held still there SERVO_RAUTO_STILL_MS; the verify pass's center is
 * commanded from 2000us, where its measure before took its angle, with no
 * approach.  The search reaches each end from its 10us step before it.
 */
TEST_CASE(every_end_comes_from_the_center_and_the_center_from_2000us)
{
    rig_init(0u);
    CHECK(start());
    uint16_t last = g.cmd;
    uint32_t since = g.now;
    unsigned ends = 0u, approaches = 0u;
    bool center_seen = false;
    servo_rauto_phase_t was = g.run.phase;
    while (servo_rauto_running(&g.run)) {
        tick();
        if (g.run.phase == SERVO_RAUTO_PH_APPROACH
            && was != SERVO_RAUTO_PH_APPROACH) {
            ++approaches;
            /* An approach for an end goes to PULSE CENTER. */
            CHECK_EQ(g.run.cmd_us, RCENTER);
        }
        was = g.run.phase;
        if (g.cmd == last) {
            continue;
        }
        if (g.run.phase == SERVO_RAUTO_PH_SEARCH || !g.run.searched) {
            last = g.cmd;
            since = g.now;
            continue;
        }
        if (g.cmd == 1000u || g.cmd == 2000u) {
            ++ends;
            CHECK_EQ(last, RCENTER);
            /* Held at the center its still time and more. */
            CHECK(g.now - since > SERVO_RAUTO_STILL_MS);
        } else if (g.run.phase == SERVO_RAUTO_PH_VERIFY
                   && g.run.vstep == 2u) {
            center_seen = true;
            CHECK_EQ(last, 2000u);
        }
        last = g.cmd;
        since = g.now;
    }
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(center_seen);
    /* The main pass's 4 measures after the search's 2 and the verify
     * pass's 2 ends. */
    CHECK_EQ(ends, 4u + 2u);
    CHECK_EQ(approaches, ends);
    CHECK_EQ(g.run.step_n, 6u);
}

int main(void)
{
    RUN(a_programmed_run_verifies_both_ends_and_the_center_and_passes);
    RUN(an_end_that_moves_after_the_last_write_fails_verify_on_that_end);
    RUN(a_center_off_the_zero_fails_verify_on_the_center);
    RUN(a_reading_that_never_settles_in_verify_ends_not_still);
    RUN(stop_in_verify_ends_the_run_aborted_with_every_output_off);
    RUN(a_run_that_writes_nothing_does_not_verify);
    RUN(every_end_comes_from_the_center_and_the_center_from_2000us);
    return test_summary("servo_rauto_verify");
}
