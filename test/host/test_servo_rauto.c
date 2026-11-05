/*
 * AUTO end points (servo_rudder_auto.h): the run's flow.
 *
 * Under test: a linear servo lands within the tolerance in one write a
 * side; one whose writes move 95% of the linear change lands in two; the
 * goal is the target times 1 + margin; a value is clamped to lo and hi and
 * the run fails LIMIT when the goal needs more; eight writes a side end in
 * WRITE_CAP; the first write finds the register of each side and a wrong
 * guess is swapped, twice not found is MAPPING; both end pulses on one side
 * of the zero is MAPPING; the KST pulse write comes before any end pulse,
 * Hitec gets none; the report lists every measure and the CSV one row a
 * measure; start refuses what lies outside its ranges; the 32-bit ms wrap
 * inside a settle.
 *
 * The rig is servo_rauto_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_rig.h"

TEST_CASE(a_linear_servo_lands_within_tolerance_in_one_write_a_side)
{
    rig_init(1000u);
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NONE);
    CHECK_EQ(g.run.writes[0], 1u);
    CHECK_EQ(g.run.writes[1], 1u);
    /* 150 counts give 30°; the goal 27.5° takes 137.5, rounded 138. */
    CHECK_EQ(g.v[0], 138u);
    CHECK_EQ(g.v[1], 138u);
    CHECK_EQ(g.run.reg_side[0], 0);
    CHECK_EQ(g.run.reg_side[1], 1);
    /* The read session and one session a write. */
    CHECK_EQ(g.connects, 3u);
    CHECK_EQ(g.run.sessions, 3u);
    CHECK_EQ(g.pulse_ops, 1u);
    CHECK(g.pulses_before_drive);
    CHECK_EQ(g.pulses[1], 10000u);
    CHECK_EQ(g.pulses[2], 20000u);
    CHECK_EQ(g.cmd_lo, 1000u);
    CHECK_EQ(g.cmd_hi, 2000u);
    /* PASS too ends with the output off and SAFE. */
    CHECK(ended_safe());
    CHECK(!servo_rauto_restore(&g.run));
    CHECK(servo_rauto_wrote(&g.run));
    CHECK(servo_rauto_drained(&g.run));
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.ends, 1u);
}

TEST_CASE(a_servo_five_percent_off_lands_in_two_writes)
{
    rig_init(0u);
    g.resp = 0.95f;
    g.cfg.target_deg[0] = 20.0f;
    g.cfg.target_deg[1] = 20.0f;
    g.cfg.tol_ddeg = 2u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.writes[0], 2u);
    CHECK_EQ(g.run.writes[1], 2u);
    CHECK(fabsf(g.run.measured_deg[0] - 22.0f) <= 0.2f);
    CHECK(fabsf(g.run.measured_deg[1] - 22.0f) <= 0.2f);
}

TEST_CASE(the_goal_is_target_times_one_plus_margin)
{
    rig_init(0u);
    g.cfg.target_deg[0] = 20.0f;
    g.cfg.target_deg[1] = 24.0f;
    g.cfg.margin_pct = 50u;
    CHECK(start());
    CHECK_NEAR(g.run.goal_deg[0], 30.0f, 1e-4f);
    CHECK_NEAR(g.run.goal_deg[1], 36.0f, 1e-4f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    /* 30° is what 150 counts give: no write on the 1000us side. */
    CHECK_EQ(g.run.writes[0], 0u);
    CHECK_EQ(g.v[0], 150u);
    CHECK_EQ(g.v[1], 180u);

    rig_init(0u);
    g.cfg.margin_pct = 0u;
    g.cfg.target_deg[0] = 30.0f;
    g.cfg.target_deg[1] = 30.0f;
    CHECK(start());
    CHECK_NEAR(g.run.goal_deg[0], 30.0f, 1e-4f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.writes[0] + g.run.writes[1], 0u);
    /* No write: the read session only, and its pulse write. */
    CHECK_EQ(g.connects, 1u);
    CHECK(!servo_rauto_restore(&g.run));
}

TEST_CASE(a_value_is_clamped_to_lo_and_hi_and_the_run_fails_limit_when_the_goal_needs_more)
{
    CHECK_EQ(servo_rauto_scaled(150u, 40.0f, 30.0f, 50u, 255u), 200u);
    CHECK_EQ(servo_rauto_scaled(150u, 60.0f, 30.0f, 50u, 255u), 255u);
    CHECK_EQ(servo_rauto_scaled(150u, 5.0f, 30.0f, 50u, 255u), 50u);
    CHECK_EQ(servo_rauto_scaled(150u, 30.0f, 0.0f, 50u, 255u), 50u);
    CHECK_EQ(servo_rauto_scaled(150u, 30.0f, -2.0f, 50u, 255u), 50u);
    CHECK_EQ(servo_rauto_scaled(100u, 30.1f, 30.0f, 50u, 255u), 100u);
    CHECK_EQ(servo_rauto_scaled(100u, 30.2f, 30.0f, 50u, 255u), 101u);
    CHECK_EQ(servo_rauto_scaled(100u, 50.0f, 20.0f, 50u, 250u), 250u);
    CHECK_EQ(servo_rauto_scaled(100u, 50.0f, 20.0f, 50u, 249u), 249u);

    /* The 2000us side needs 175 counts and takes at most 160. */
    rig_init(0u);
    g.hi = 160u;
    g.cfg.target_deg[0] = 25.0f;
    g.cfg.target_deg[1] = 35.0f / 1.1f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_LIMIT);
    CHECK_EQ(g.v[1], 160u);
    CHECK_EQ(g.run.writes[1], 1u);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_LIMIT);

    /* Already at hi: no write, LIMIT at once. */
    rig_init(0u);
    g.hi = 150u;
    g.cfg.target_deg[1] = 35.0f / 1.1f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_LIMIT);
    CHECK_EQ(g.run.writes[1], 0u);

    /* 1° a count: a goal 0.4° off rounds to the value in force, and the
     * value moves one count toward it. */
    rig_init(0u);
    g.v[0] = g.v[1] = 30u;
    g.lo = 10u;
    g.hi = 60u;
    g.gain = 1.0f;
    g.cfg.target_deg[0] = 30.4f / 1.1f;
    g.cfg.target_deg[1] = 30.0f / 1.1f;
    g.cfg.tol_ddeg = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK(g.run.step_n >= 2u);
    CHECK_EQ(g.run.steps[1].value, 31u);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE_CAP);
}

TEST_CASE(eight_writes_a_side_end_in_write_cap)
{
    /* Each write moves the side 1.9 times the linear change: the error
     * swings round the goal and shrinks by a tenth a write. */
    rig_init(0u);
    g.resp = 1.9f;
    g.cfg.target_deg[0] = 20.0f / 1.1f;
    g.cfg.tol_ddeg = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE_CAP);
    CHECK_EQ(g.run.writes[0], SERVO_RAUTO_WRITES_MAX);
    CHECK_EQ(g.write_ops, 1u + SERVO_RAUTO_WRITES_MAX);
    /* The first measure and one after each write. */
    CHECK_EQ(g.run.step_n, 1u + SERVO_RAUTO_WRITES_MAX);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
}

TEST_CASE(the_first_write_finds_the_register_of_each_side_and_a_wrong_guess_is_swapped)
{
    /* The second register moves the 1000us side. */
    rig_init(0u);
    g.map[0] = 1;
    g.map[1] = 0;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.reg_side[0], 1);
    CHECK_EQ(g.run.reg_side[1], 0);
    CHECK_EQ(g.v[0], 138u);
    CHECK_EQ(g.v[1], 138u);
    /* The guess, the swap, the 2000us side's write. */
    CHECK_EQ(g.run.writes[0], 2u);
    CHECK_EQ(g.run.writes[1], 1u);
    CHECK(in_report("moves the 1000us side"));

    /* A servo whose registers move nothing it is measured on. */
    rig_init(0u);
    g.resp = 0.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_MAPPING);
    CHECK_EQ(g.run.writes[0], 2u);
    /* The first register back to its value. */
    CHECK_EQ(g.v[0], 150u);
    CHECK(ended_safe());

    /* A write that moves the side away from the goal. */
    rig_init(0u);
    g.resp = -1.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_MAPPING);
    CHECK_EQ(g.run.writes[0], 1u);
}

TEST_CASE(both_end_pulses_on_one_side_of_the_zero_end_mapping)
{
    rig_init(0u);
    g.dir[0] = 1.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_MAPPING);

    /* A side that does not move from the zero. */
    rig_init(0u);
    g.v[0] = 50u;
    g.gain = 0.002f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_MAPPING);
    CHECK_EQ(g.write_ops, 1u);
}

TEST_CASE(the_pulse_write_comes_first_on_kst_and_hitec_gets_none)
{
    rig_init(0u);
    g.pulses[1] = 10000u;
    g.pulses[2] = 20000u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK(!in_report("Pulse write"));

    rig_init(0u);
    g.cfg.family = SERVO_RAUTO_FAM_HITEC_D;
    memset(g.pulses, 0, sizeof(g.pulses));
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK(in_report("Servo:          DIGITAL, Hitec D"));
    CHECK(in_report("LEFT END (0xB2): 150 before, 138 after"));

    rig_init(0u);
    g.cfg.family = SERVO_RAUTO_FAM_HITEC_C;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK(in_report("Servo:          DIGITAL, Hitec\n"));

    /* Pulses that do not land end the run as a write. */
    rig_init(0u);
    g.pulses_lost = true;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE);
    CHECK(!g.driven);
}

TEST_CASE(the_report_lists_every_step_and_the_csv_one_row_a_step)
{
    rig_init(0u);
    g.cfg.len_mm = 100.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    /* 1000us first, its write, 2000us, its write, the 1000us side's
     * check after the second register's write. */
    CHECK_EQ(g.run.step_n, 5u);
    CHECK(in_report("RCBENCH RUDDER AUTO REPORT"));
    CHECK(in_report("Result:         PASS"));
    CHECK(in_report("Device:         AILERON L"));
    CHECK(in_report("Output:         PROG PORT 1 GPIO 13"));
    CHECK(in_report("Angle:          IMU X+, deflection from zero"));
    CHECK(in_report("Current:        INA3221 0x41 CH1, 50ms windows"));
    CHECK(in_report("a sample at 3.0A, above 1.0A for 400ms"));
    CHECK(in_report("Targets:        1000us 25.0°, 2000us 25.0°, margin 10%"));
    CHECK(in_report("Goals:          1000us 27.50°, 2000us 27.50°, tolerance 0.5°"));
    CHECK(in_report("Length:         100mm; goals 1000us 46.2mm"));
    CHECK(in_report("Pulse write:    PULSE LOWER 900.0 -> 1000.0us"));
    CHECK(in_report("LEFT ANGLE:     150 before, 138 after; moves the 1000us side"));
    CHECK(in_report("RIGHT ANGLE:    150 before, 138 after; moves the 2000us side"));
    CHECK(in_report("Writes:         1000us side 1, 2000us side 1, at most 8 writes a side; sessions 3"));
    CHECK(in_report("Backup:         the panel's memory only"));
    CHECK(in_report("1000us  write 0  value -  measured 30.00°  goal 27.50°  outside"));
    CHECK(in_report("1000us  write 1  value 138  measured 27.60°  goal 27.50°  within"));
    CHECK(in_report("2000us  write 0  value 150  measured 30.00°"));
    CHECK(in_report("Model: deflection"));
    CHECK(!in_report("Restore:"));
    CHECK(!in_report("Angle gaps"));
    CHECK_EQ(g.rows, 1u + 5u);
    static const char head[] = "step;side (us);write;value;measured (deg);"
                               "goal (deg);measured (mm);goal (mm);within\n";
    CHECK(strncmp(g.csv, head, sizeof(head) - 1u) == 0);
    CHECK(strstr(g.csv, "\n1;1000;0;;30.00;27.50;50.00;46.17;0\n") != NULL);
    CHECK(strstr(g.csv, "\n2;1000;1;138;27.60;27.50;46.33;46.17;1\n") != NULL);
    CHECK(strstr(g.csv, "\n5;1000;1;138;27.60;27.50;") != NULL);

    /* German, a backup named, no length: the mm columns empty. */
    static const char *de[SERVO_STR_COUNT];
    de[SERVO_STR_RA_R_TITLE] = "RCBENCH RUDER-AUTO-BERICHT";
    rig_init(0u);
    g.cfg.text = de;
    CHECK(start());
    servo_rauto_backup(&g.run, "BEFORE.KST");
    CHECK(run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS));
    servo_rauto_backup(&g.run, "KST00007.KST");
    servo_rauto_backup(&g.run, "");
    servo_rauto_backup(&g.run, NULL);
    run_for(RAUTO_MS);
    CHECK(in_report("RCBENCH RUDER-AUTO-BERICHT"));
    CHECK(in_report("Backup:         KST00007.KST"));
    CHECK(strstr(g.csv, "\n1;1000;0;;30.00;27.50;;;0\n") != NULL);

    char buf[64];
    CHECK(!servo_rauto_report_line(NULL, 0u, buf, sizeof(buf)));
    CHECK(!servo_rauto_report_line(&g.run, 0u, NULL, sizeof(buf)));
    CHECK(!servo_rauto_report_line(&g.run, 0u, buf, 0u));
    CHECK(!servo_rauto_report_line(&g.run, 999u, buf, sizeof(buf)));
    CHECK(servo_rauto_report_line(&g.run, 0u, buf, sizeof(buf)));
    servo_rauto_t idle;
    servo_rauto_init(&idle);
    CHECK(!servo_rauto_report_line(&idle, 0u, buf, sizeof(buf)));
    const char *t = "x";
    CHECK_EQ(servo_rauto_peek(&idle, &t), SERVO_TEST_OUT_NONE);
    CHECK_STR_EQ(t, "");
    CHECK_EQ(servo_rauto_peek(NULL, NULL), SERVO_TEST_OUT_NONE);
    servo_rauto_pop(NULL);
    servo_rauto_pop(&idle);
    CHECK(servo_rauto_drained(NULL));
    CHECK(servo_rauto_drained(&idle));
    CHECK_EQ(servo_rauto_peek(&g.run, NULL), SERVO_TEST_OUT_NONE);
    servo_rauto_pop(&g.run);
}

TEST_CASE(start_refuses_a_target_out_of_range_a_margin_above_50_and_a_tolerance_outside)
{
    servo_rauto_t r;
    rig_init(0u);
    servo_rauto_cfg_t c = g.cfg;
    CHECK(!servo_rauto_start(NULL, &c, 0u));
    CHECK(!servo_rauto_start(&r, NULL, 0u));
    servo_rauto_init(NULL);

    c.target_deg[0] = 0.1f;
    c.target_deg[1] = 90.0f;
    CHECK(servo_rauto_start(&r, &c, 0u));
    /* RUNNING: a second start is refused. */
    CHECK(!servo_rauto_start(&r, &c, 0u));

    static const float bad[] = { 0.09f, 90.01f, -1.0f, 0.0f, NAN };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k) {
        for (int s = 0; s < 2; ++s) {
            c = g.cfg;
            c.target_deg[s] = bad[k];
            CHECK(!servo_rauto_start(&r, &c, 0u) || servo_rauto_running(&r));
            servo_rauto_init(&r);
            CHECK(!servo_rauto_start(&r, &c, 0u));
        }
    }
    c = g.cfg;
    c.margin_pct = SERVO_RAUTO_MARGIN_MAX;
    CHECK(servo_rauto_start(&r, &c, 0u));
    servo_rauto_init(&r);
    c.margin_pct = SERVO_RAUTO_MARGIN_MAX + 1u;
    CHECK(!servo_rauto_start(&r, &c, 0u));

    c = g.cfg;
    c.tol_ddeg = SERVO_RAUTO_TOL_MIN_DDEG;
    CHECK(servo_rauto_start(&r, &c, 0u));
    servo_rauto_init(&r);
    c.tol_ddeg = 50u;                   /* 5.0° */
    CHECK(servo_rauto_start(&r, &c, 0u));
    servo_rauto_init(&r);
    c.tol_ddeg = SERVO_RAUTO_TOL_MIN_DDEG - 1u;
    CHECK(!servo_rauto_start(&r, &c, 0u));
    c.tol_ddeg = 51u;
    CHECK(!servo_rauto_start(&r, &c, 0u));

    c = g.cfg;
    c.family = (uint8_t)SERVO_RAUTO_FAM_HITEC_C + 1u;
    CHECK(!servo_rauto_start(&r, &c, 0u));
    c = g.cfg;
    c.centre_us = 1000u;
    CHECK(!servo_rauto_start(&r, &c, 0u));
    c.centre_us = 2000u;
    CHECK(!servo_rauto_start(&r, &c, 0u));
    c.centre_us = 1001u;
    CHECK(servo_rauto_start(&r, &c, 0u));
    servo_rauto_init(&r);
    c.centre_us = 1999u;
    CHECK(servo_rauto_start(&r, &c, 0u));
    CHECK(!servo_rauto_running(NULL));
}

TEST_CASE(the_clock_wrap_inside_a_settle_does_not_end_it)
{
    /* The first move's command lands about 1.4s after the start: the
     * clock wraps 1.6s in, inside that settle. */
    rig_init(0xFFFFFFFFu - 1900u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS));
    const uint32_t at = g.run.move_ms;
    CHECK(at > 0xFFFFF000u);
    CHECK(run_to(SERVO_RAUTO_PH_SAFE, 5000u));
    CHECK(g.now < 0x1000u);
    CHECK_EQ(g.run.step_n, 1u);
    CHECK(g.run.steps[0].measured_deg > 29.9f);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
}

TEST_CASE(a_first_measure_at_the_tolerance_gives_the_side_its_direction)
{
    /* 2 counts at 0.25° a count: 0.5°, the tolerance itself. */
    rig_init(0u);
    g.gain = 0.25f;
    g.v[0] = 2u;
    g.lo = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.sign[0], -1);
    CHECK_EQ(g.v[0], 110u);
}

TEST_CASE(a_found_register_is_not_tried_again_on_a_later_write)
{
    /* The first write lands 95% of the way; then the servo moves only 30%
     * of what a write asks: the register stays the one found. */
    rig_init(0u);
    g.resp = 0.95f;
    g.cfg.target_deg[0] = 20.0f;
    g.cfg.tol_ddeg = 2u;
    CHECK(start());
    while (servo_rauto_running(&g.run) && g.run.step_n < 2u) {
        tick();
    }
    CHECK_EQ(g.run.reg_side[0], 0);
    CHECK(!g.run.map_open);
    g.v0[0] = g.v[0];
    g.resp = 0.3f;
    run_for(RAUTO_MS);
    CHECK(g.run.why != SERVO_RAUTO_WHY_MAPPING);
    CHECK_EQ(g.run.reg_side[0], 0);
    CHECK(g.run.writes[0] >= 2u);
}

TEST_CASE(the_step_list_keeps_its_34_entries_and_drops_later_ones)
{
    CHECK_EQ(sizeof(g.run.steps) / sizeof(g.run.steps[0]), 34u);
    CHECK_EQ(SERVO_RAUTO_STEPS_MAX, 34u);
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS));
    g.run.step_n = SERVO_RAUTO_STEPS_MAX;
    CHECK(run_to(SERVO_RAUTO_PH_SAFE, RAUTO_MS));
    CHECK_EQ(g.run.step_n, SERVO_RAUTO_STEPS_MAX);
    CHECK(servo_rauto_running(&g.run));
}

/* Each write moves a side 1.35 times the linear change, 17° goals, 0.1°
 * tolerance: both sides take all 8 writes, and the 1000us side's CHECK
 * after the 2000us side's writes is recorded as the last measure. */
TEST_CASE(eight_writes_a_side_keep_every_measure_and_the_last_check)
{
    rig_init(0u);
    g.resp = 1.35f;
    g.cfg.target_deg[0] = 17.0f / 1.1f;
    g.cfg.target_deg[1] = 17.0f / 1.1f;
    g.cfg.tol_ddeg = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.writes[0], SERVO_RAUTO_WRITES_MAX);
    CHECK_EQ(g.run.writes[1], SERVO_RAUTO_WRITES_MAX);
    CHECK(g.run.step_n <= SERVO_RAUTO_STEPS_MAX);
    const servo_rauto_step_t *last = &g.run.steps[g.run.step_n - 1u];
    CHECK_EQ(last->side, 0u);
    CHECK(last->within);
    CHECK_EQ(last->n, SERVO_RAUTO_WRITES_MAX);
    /* The 2000us side's last measure, within, just before it. */
    CHECK_EQ(g.run.steps[g.run.step_n - 2u].side, 1u);
    CHECK(g.run.steps[g.run.step_n - 2u].within);
}

/* The 1000us side is the second register's, which holds 150 counts (30°);
 * the first register sits at lo, 50, and cannot lower the side to its 22°
 * goal.  The run writes the second register instead and finds the mapping
 * the other way round; LIMIT only where neither register can move. */
TEST_CASE(an_unmapped_side_tries_the_second_register_before_limit)
{
    rig_init(0u);
    g.map[0] = 1;
    g.map[1] = 0;
    g.v[0] = 50u;
    g.v[1] = 150u;
    g.cfg.target_deg[0] = 20.0f;
    g.cfg.target_deg[1] = 20.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.reg_side[0], 1);
    CHECK_EQ(g.run.reg_side[1], 0);
    CHECK_EQ(g.v[0], 110u);
    CHECK_EQ(g.v[1], 110u);
    CHECK_EQ(g.run.writes[0], 1u);
    CHECK_EQ(g.run.writes[1], 1u);

    /* Both registers at lo, the side above its goal: LIMIT, no write. */
    rig_init(0u);
    g.v[0] = g.v[1] = 50u;
    g.cfg.target_deg[0] = 5.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_LIMIT);
    CHECK_EQ(g.run.writes[0], 0u);
    CHECK_EQ(g.write_ops, 1u);          /* the pulse write alone */
}

TEST_CASE(a_check_outside_the_tolerance_writes_that_side_again)
{
    rig_init(0u);
    CHECK(start());
    /* The 1000us side within, then 10% more travel on it. */
    while (servo_rauto_running(&g.run) && !g.run.verified[0]) {
        tick();
    }
    g.dir[0] = -1.1f;
    while (servo_rauto_running(&g.run) && g.run.phase != SERVO_RAUTO_PH_CHECK) {
        tick();
    }
    CHECK(g.run.verified[0]);
    while (servo_rauto_running(&g.run) && g.run.phase == SERVO_RAUTO_PH_CHECK) {
        tick();
    }
    CHECK(!g.run.verified[0]);
    CHECK_EQ(g.run.phase, SERVO_RAUTO_PH_SAFE);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.writes[0], 2u);
}

int main(void)
{
    RUN(a_check_outside_the_tolerance_writes_that_side_again);
    RUN(a_first_measure_at_the_tolerance_gives_the_side_its_direction);
    RUN(a_found_register_is_not_tried_again_on_a_later_write);
    RUN(the_step_list_keeps_its_34_entries_and_drops_later_ones);
    RUN(eight_writes_a_side_keep_every_measure_and_the_last_check);
    RUN(an_unmapped_side_tries_the_second_register_before_limit);
    RUN(a_linear_servo_lands_within_tolerance_in_one_write_a_side);
    RUN(a_servo_five_percent_off_lands_in_two_writes);
    RUN(the_goal_is_target_times_one_plus_margin);
    RUN(a_value_is_clamped_to_lo_and_hi_and_the_run_fails_limit_when_the_goal_needs_more);
    RUN(eight_writes_a_side_end_in_write_cap);
    RUN(the_first_write_finds_the_register_of_each_side_and_a_wrong_guess_is_swapped);
    RUN(both_end_pulses_on_one_side_of_the_zero_end_mapping);
    RUN(the_pulse_write_comes_first_on_kst_and_hitec_gets_none);
    RUN(the_report_lists_every_step_and_the_csv_one_row_a_step);
    RUN(start_refuses_a_target_out_of_range_a_margin_above_50_and_a_tolerance_outside);
    RUN(the_clock_wrap_inside_a_settle_does_not_end_it);
    return test_summary("servo_rauto");
}
