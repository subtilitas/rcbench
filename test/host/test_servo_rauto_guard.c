/*
 * AUTO end points (servo_rudder_auto.h): the guards and the error paths.
 *
 * Under test: a stall before the goal ends the run as SURFACE BINDS AT the
 * angle, one at the goal as a stall; a sample at 3000mA ends it and one
 * at 2999mA does not, a clipped one does; window means above 1000mA end it
 * at 400ms and not at 350ms; every one of those ends with the output off
 * and SAFE asked; not still within 3000ms ends NOT_STILL and still at
 * 3000ms is taken; the angle lost for 300ms is ridden through and 301ms
 * ends the run, as does a second gap within 10s; no window for 200ms; the
 * bench not switching within 3s; the programmer silent for 150s; a refused
 * CONNECT, write and LEAVE; a CONNECT that finds no servo, or another one;
 * a write that reads back other values; the operator's SAFE; RESTORE only
 * after a write; the words.
 *
 * The rig is servo_rauto_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_rig.h"

/* A run started and in its first measure of the 1000us side. */
static bool at_first_move(void)
{
    rig_init(0u);
    return start() && run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS);
}

TEST_CASE(a_stall_before_the_goal_ends_binds_with_the_angle)
{
    rig_init(0u);
    g.bind[1] = 20.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_BINDS);
    CHECK_NEAR(g.run.bind_deg, 20.0f, 0.01f);
    CHECK(ended_safe());
    CHECK(servo_rauto_why_error(g.run.why, g.run.test_why));
    CHECK(in_report("Result:         FAIL - surface binds at 20.0°"));
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_BINDS);

    /* Bound at the goal less the tolerance: the surface is not short. */
    rig_init(0u);
    g.bind[1] = 27.5f - 0.2f;
    g.cfg.tol_ddeg = 2u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STALL);
    CHECK(ended_safe());

    /* A stall while the 1000us side is checked, short of its goal. */
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_CHECK, RAUTO_MS));
    g.bind[0] = 10.0f;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_BINDS);
    CHECK_NEAR(g.run.bind_deg, -10.0f, 0.01f);

    /* A stall while the servo is held at the centre. */
    rig_init(0u);
    CHECK(start());
    g.extra_ma = 1100;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_STALL);
    CHECK_EQ(g.run.end_ms - g.run.start_ms, 400u);
}

TEST_CASE(a_hard_sample_ends_the_run_with_the_output_off_and_safe_asked)
{
    CHECK(at_first_move());
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.current = true;
    w.voltage = true;
    w.taken_ms = g.now;
    w.max_ma = 2999;
    servo_rauto_window(&g.run, &w);
    w.min_ma = -2999;
    servo_rauto_window(&g.run, &w);
    CHECK(servo_rauto_running(&g.run));
    w.max_ma = 3000;
    servo_rauto_window(&g.run, &w);
    CHECK(!servo_rauto_running(&g.run));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_HARD);
    run_for(10u);
    CHECK(ended_safe());

    CHECK(at_first_move());
    w.max_ma = 0;
    w.min_ma = -3000;
    servo_rauto_window(&g.run, &w);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_HARD);

    CHECK(at_first_move());
    w.min_ma = 0;
    w.mean_ma = 3000;
    servo_rauto_window(&g.run, &w);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_HARD);

    CHECK(at_first_move());
    w.mean_ma = 100;
    w.clipped = 1u;
    servo_rauto_window(&g.run, &w);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_HARD);

    /* A window without a current is no window. */
    CHECK(at_first_move());
    w.clipped = 0u;
    w.current = false;
    w.max_ma = 5000;
    servo_rauto_window(&g.run, &w);
    servo_rauto_window(&g.run, NULL);
    servo_rauto_window(NULL, &w);
    CHECK(servo_rauto_running(&g.run));
}

TEST_CASE(window_means_above_1000ma_end_the_run_at_400ms)
{
    CHECK(at_first_move());
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.current = true;
    w.mean_ma = 1001;
    const uint32_t t = g.now + 1000u;
    g.windows = false;
    for (uint32_t k = 0; k < 7u; ++k) {
        w.taken_ms = t + k * 50u;
        servo_rauto_window(&g.run, &w);
    }
    /* From t - 50 to t + 300: 350ms. */
    CHECK(servo_rauto_running(&g.run));
    w.mean_ma = -1001;
    w.taken_ms = t + 350u;
    servo_rauto_window(&g.run, &w);
    CHECK(!servo_rauto_running(&g.run));
    CHECK_EQ(g.run.end_ms, t + 350u);

    /* A window at 1000mA resets the time. */
    CHECK(at_first_move());
    g.windows = false;
    w.mean_ma = 1001;
    w.taken_ms = g.now + 50u;
    servo_rauto_window(&g.run, &w);
    w.mean_ma = 1000;
    w.taken_ms += 50u;
    servo_rauto_window(&g.run, &w);
    w.mean_ma = 1001;
    for (int k = 0; k < 7; ++k) {
        w.taken_ms += 50u;
        servo_rauto_window(&g.run, &w);
    }
    CHECK(servo_rauto_running(&g.run));
}

TEST_CASE(not_still_within_3s_ends_not_still)
{
    rig_init(0u);
    g.jitter = true;
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS));
    const uint32_t at = g.run.move_ms;
    CHECK(run_to(SERVO_RAUTO_PH_SETTLE, 1000u));
    CHECK_EQ(g.now - at, 500u);
    while (g.now - at < SERVO_RAUTO_MOVE_MS - 1u) {
        tick();
    }
    CHECK(servo_rauto_running(&g.run));
    tick();
    CHECK(!servo_rauto_running(&g.run));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NOT_STILL);
    CHECK_EQ(g.run.end_ms - at, SERVO_RAUTO_MOVE_MS);
    run_for(10u);
    CHECK(ended_safe());

    /* Still exactly at 3000ms: the angle is taken. */
    CHECK(at_first_move());
    servo_rauto_do_t d;
    servo_rauto_in_t in = rig_in();
    const uint32_t m = g.run.move_ms;
    in.still_ms = 499u;
    in.angle_deg = -30.0f;
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.current = true;
    for (uint32_t k = 1; k < SERVO_RAUTO_MOVE_MS; ++k) {
        if (k % 50u == 0u) {
            w.taken_ms = m + k;
            servo_rauto_window(&g.run, &w);
        }
        servo_rauto_step(&g.run, m + k, &in, &d);
    }
    CHECK(servo_rauto_running(&g.run));
    /* Still 499ms is not still. */
    CHECK_EQ(g.run.step_n, 0u);
    in.still_ms = 500u;
    servo_rauto_step(&g.run, m + 3000u, &in, &d);
    CHECK(servo_rauto_running(&g.run));
    CHECK_EQ(g.run.step_n, 1u);
    CHECK_NEAR(g.run.steps[0].measured_deg, 30.0f, 1e-4f);
}

/* The angle source stops answering at @p at. */
static void lost(uint32_t at)
{
    const servo_angle_in_t a = { .valid = false, .taken_ms = at };
    servo_rauto_angle(&g.run, &a);
}

/* It answers again at @p at. */
static void back(uint32_t at)
{
    const servo_angle_in_t a = { .valid = true, .deg = -30.0f,
                                 .taken_ms = at };
    servo_rauto_angle(&g.run, &a);
}

TEST_CASE(the_angle_lost_for_301ms_ends_the_run_and_300ms_is_ridden_through)
{
    CHECK(at_first_move());
    g.angle_on = false;
    const uint32_t t = g.now;
    lost(t);
    idle_for(300u);
    CHECK(servo_rauto_running(&g.run));
    back(t + 300u);
    CHECK(servo_rauto_running(&g.run));
    g.angle_on = true;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.gap.count, 1u);
    CHECK(in_report("Angle gaps:     1, the longest 300ms"));

    /* Against the clock: ends 1ms past 300ms. */
    CHECK(at_first_move());
    g.angle_on = false;
    lost(g.now);
    idle_for(300u);
    CHECK(servo_rauto_running(&g.run));
    tick();
    CHECK(!servo_rauto_running(&g.run));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_ANGLE);
    run_for(10u);
    CHECK(ended_safe());
    CHECK(servo_rauto_why_error(SERVO_RAUTO_WHY_ANGLE, SERVO_TEST_AB_NONE));

    /* Answering again after 301ms ends it as well. */
    CHECK(at_first_move());
    g.angle_on = false;
    lost(g.now);
    back(g.now + 301u);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_ANGLE);

    /* A second gap 9999ms after the start of the first. */
    CHECK(at_first_move());
    g.angle_on = false;
    const uint32_t u = g.now;
    lost(u);
    back(u + 10u);
    lost(u + 9999u);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_ANGLE);

    CHECK(at_first_move());
    g.angle_on = false;
    lost(g.now);
    back(g.now + 10u);
    lost(g.now + 10000u);
    CHECK(servo_rauto_running(&g.run));
    servo_rauto_angle(&g.run, NULL);
    servo_rauto_angle(NULL, NULL);

    /* No angle is taken inside a gap. */
    CHECK(at_first_move());
    servo_rauto_do_t d;
    servo_rauto_in_t in = rig_in();
    in.still_ms = 1000u;
    lost(g.run.move_ms + 400u);
    servo_rauto_step(&g.run, g.run.move_ms + 500u, &in, &d);
    CHECK_EQ(g.run.step_n, 0u);
}

TEST_CASE(the_meter_and_the_bench_silent_end_the_run)
{
    /* No window for 200ms. */
    CHECK(at_first_move());
    g.windows = false;
    const uint32_t last = g.run.last_win_ms;
    while (servo_rauto_running(&g.run)) {
        tick();
    }
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_TEST);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(g.run.end_ms - last, SERVO_LIMRUN_WIN_STALE_MS);
    CHECK(servo_rauto_why_error(g.run.why, g.run.test_why));
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_AB_WIN_STALE);

    /* SAFE asked and not taken within 3s. */
    rig_init(0u);
    g.bench_ms = 0xFFFFFFFFu;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_BENCH);
    CHECK_EQ(g.run.end_ms - g.run.start_ms,
             SERVO_LIMRUN_RETURN_MS + SERVO_LIMRUN_BENCH_MS);
    CHECK(!servo_rauto_why_error(g.run.why, g.run.test_why));

    /* ARMED asked and not taken within 3s. */
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_ARM, RAUTO_MS));
    g.bench_ms = 0xFFFFFFFFu;
    g.bench_to = -1;
    const uint32_t a = g.now;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_BENCH);
    CHECK_EQ(g.run.end_ms - a, SERVO_LIMRUN_BENCH_MS);

    /* The programmer silent for 150s. */
    rig_init(0u);
    g.prog_silent = true;
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_READ, RAUTO_MS));
    const uint32_t p = g.run.phase_ms;
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_PROG);
    CHECK_EQ(g.run.end_ms - p, SERVO_LIMRUN_PROG_MS);
    CHECK_EQ(g.offs, 1u);

    /* The operator's SAFE while the servo is held at the centre, and while
     * a measure drives it. */
    rig_init(0u);
    CHECK(start());
    idle_for(10u);
    g.live = false;
    run_for(10u);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_DISARMED);
    CHECK(at_first_move());
    g.live = false;
    run_for(10u);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_TEST);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_DISARMED);
    CHECK(ended_safe());
    /* The caller's STOP. */
    CHECK(at_first_move());
    servo_rauto_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    run_for(10u);
    char head[96];
    snprintf(head, sizeof(head), "Result:         ABORTED - %s\n",
             servo_str(SERVO_STR_AB_STOP));
    CHECK(in_report(head));
    servo_rauto_abort(&g.run, SERVO_TEST_AB_LINK, g.now);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_STOP);
}

TEST_CASE(a_refused_connect_ends_prog_and_restore_is_offered_after_a_write)
{
    /* The read session's CONNECT refused: nothing written. */
    rig_init(0u);
    g.refuse_connect = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_PROG);
    CHECK(!servo_rauto_restore(&g.run));
    CHECK(!servo_rauto_wrote(&g.run));
    CHECK(in_report("No servo read: nothing written."));
    CHECK(in_report("No measurements taken."));

    /* The second write's CONNECT refused: RESTORE. */
    rig_init(0u);
    g.refuse_connect = 3u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_PROG);
    CHECK(servo_rauto_restore(&g.run));
    CHECK(in_report("Restore:        RESTORE on the programming page"));
    CHECK_EQ(g.leave_asks, 2u);
    CHECK_EQ(g.offs, 1u);

    /* A later CONNECT that finds no servo, and one that reads another. */
    rig_init(0u);
    g.no_servo_at = 2u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_PROG);
    CHECK(g.run.end_ms - g.run.start_ms < 60000u);
    rig_init(0u);
    g.change_at = 2u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_PROG);
    CHECK(servo_rauto_restore(&g.run));

    /* A refused write, a lost one, a failed LEAVE. */
    rig_init(0u);
    g.refuse_write = 2u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE);
    CHECK(servo_rauto_restore(&g.run));
    rig_init(0u);
    g.lose_write = 2u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE);
    /* Read back as before, the pulses already 1000 to 2000us: nothing
     * changed, no RESTORE. */
    rig_init(0u);
    g.pulses[1] = 10000u;
    g.pulses[2] = 20000u;
    g.lose_write = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE);
    CHECK(!servo_rauto_restore(&g.run));
    CHECK(servo_rauto_wrote(&g.run));
    rig_init(0u);
    g.leave_fails = true;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_PROG);
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_L_WHY_PROG);

    /* The programmer refuses a write before it is sent, or LEAVE. */
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_WRITE, RAUTO_MS));
    servo_rauto_refused(&g.run, g.now);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE);
    CHECK(!servo_rauto_restore(&g.run));    /* nothing was sent */
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_LEAVE, RAUTO_MS));
    CHECK(servo_rauto_programming(&g.run));
    CHECK(servo_rauto_switching(&g.run));
    servo_rauto_refused(&g.run, g.now);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_PROG);
    /* The hold at PULSE CENTRE before SAFE is no programmer's step. */
    rig_init(0u);
    CHECK(start());
    CHECK_EQ(g.run.phase, SERVO_RAUTO_PH_SAFE);
    CHECK(!servo_rauto_programming(&g.run));
    CHECK(!servo_rauto_switching(&g.run));
    /* Outside the programmer's steps a refusal changes nothing. */
    rig_init(0u);
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_WRITE, RAUTO_MS));
    CHECK(servo_rauto_programming(&g.run));
    CHECK(at_first_move());
    CHECK_EQ(g.run.phase_ms, g.run.move_ms);
    CHECK(!servo_rauto_programming(&g.run));
    CHECK(!servo_rauto_switching(&g.run));
    servo_rauto_refused(&g.run, g.now);
    servo_rauto_no_servo(&g.run, g.now);
    servo_rauto_connected(&g.run, NULL, g.now);
    servo_rauto_written(&g.run, true, g.v, g.now);
    servo_rauto_pulsed(&g.run, true, g.pulses, g.now);
    servo_rauto_left(&g.run, true, g.now);
    CHECK(servo_rauto_running(&g.run));
    servo_rauto_refused(NULL, 0u);
    servo_rauto_no_servo(NULL, 0u);
    CHECK(!servo_rauto_wrote(NULL));
    CHECK(!servo_rauto_restore(NULL));
}

TEST_CASE(a_read_that_is_no_servo_fails_and_gives_the_port_back)
{
    rig_init(0u);
    g.no_servo_at = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_SERVO);
    /* LEAVE asked, no output off over the session. */
    CHECK_EQ(g.leave_asks, 1u);
    CHECK_EQ(g.offs, 0u);
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_NO_SERVO);

    rig_init(0u);
    g.not_servo = true;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_SERVO);

    /* Values the servo's own rules do not take. */
    static const uint16_t bad[][4] = {
        { 40u, 150u, 50u, 255u }, { 150u, 40u, 50u, 255u },
        { 256u, 150u, 50u, 255u }, { 150u, 256u, 50u, 255u },
        { 150u, 150u, 200u, 100u },
    };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k) {
        rig_init(0u);
        g.v[0] = bad[k][0];
        g.v[1] = bad[k][1];
        g.lo = bad[k][2];
        g.hi = bad[k][3];
        CHECK(start());
        run_for(RAUTO_MS);
        CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_SERVO);
    }
    /* lo and hi one value: read, and the goal out of its reach. */
    rig_init(0u);
    g.v[0] = g.v[1] = 150u;
    g.lo = g.hi = 150u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_LIMIT);
    rig_init(0u);
    g.v[0] = 50u;
    g.v[1] = 255u;
    g.cfg.target_deg[0] = 10.0f / 1.1f;
    g.cfg.target_deg[1] = 51.0f / 1.1f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
}

TEST_CASE(the_words_name_every_reason_and_phase)
{
    servo_rauto_t r;
    servo_rauto_init(&r);
    CHECK_EQ(servo_rauto_why_str(NULL), SERVO_STR_AB_NONE);
    static const servo_str_t k_why[SERVO_RAUTO_WHY_COUNT] = {
        SERVO_STR_AB_NONE, SERVO_STR_AB_STOP, SERVO_STR_L_WHY_HARD,
        SERVO_STR_L_WHY_STALL, SERVO_STR_RA_WHY_BINDS,
        SERVO_STR_RA_WHY_NOT_STILL, SERVO_STR_RA_WHY_ANGLE,
        SERVO_STR_RA_WHY_NO_SERVO, SERVO_STR_L_WHY_PROG,
        SERVO_STR_L_WHY_WRITE, SERVO_STR_RA_WHY_MAPPING,
        SERVO_STR_RA_WHY_LIMIT, SERVO_STR_RA_WHY_WRITE_CAP,
        SERVO_STR_L_WHY_BENCH, SERVO_STR_L_WHY_SUPPLY,
    };
    r.test_why = SERVO_TEST_AB_STOP;
    for (int k = 0; k < (int)SERVO_RAUTO_WHY_COUNT; ++k) {
        r.why = (servo_rauto_why_t)k;
        CHECK_EQ(servo_rauto_why_str(&r), k_why[k]);
        CHECK(k == 0 || servo_str(servo_rauto_why_str(&r))[0] != '\0');
    }
    r.why = SERVO_RAUTO_WHY_COUNT;
    CHECK_EQ(servo_rauto_why_str(&r), SERVO_STR_AB_NONE);
    static const servo_str_t k_ph[] = {
        SERVO_STR_PHASE_NONE, SERVO_STR_RA_PH_READ, SERVO_STR_PHASE_MOVE,
        SERVO_STR_PHASE_SETTLE, SERVO_STR_L_PH_SAFE, SERVO_STR_L_PH_CONNECT,
        SERVO_STR_L_PH_WRITE, SERVO_STR_L_PH_LEAVE, SERVO_STR_L_PH_ARM,
        SERVO_STR_L_PH_CHECK,
    };
    for (int k = 0; k < (int)(sizeof(k_ph) / sizeof(k_ph[0])); ++k) {
        CHECK_EQ(servo_rauto_phase_str((servo_rauto_phase_t)k), k_ph[k]);
    }
    CHECK_EQ(servo_rauto_phase_str((servo_rauto_phase_t)99), SERVO_STR_PHASE_NONE);
    CHECK(servo_rauto_why_error(SERVO_RAUTO_WHY_HARD, SERVO_TEST_AB_NONE));
    CHECK(servo_rauto_why_error(SERVO_RAUTO_WHY_STALL, SERVO_TEST_AB_NONE));
    CHECK(!servo_rauto_why_error(SERVO_RAUTO_WHY_TEST, SERVO_TEST_AB_STOP));
    CHECK(!servo_rauto_why_error(SERVO_RAUTO_WHY_LIMIT, SERVO_TEST_AB_NONE));
    CHECK(!servo_rauto_why_error(SERVO_RAUTO_WHY_NOT_STILL, SERVO_TEST_AB_NONE));
    CHECK_STR_EQ(servo_str(SERVO_STR_RA_BINDS_AT), "surface binds at %.1f°");

    /* A step with nothing to step. */
    servo_rauto_do_t d;
    memset(&d, 0xFF, sizeof(d));
    servo_rauto_step(NULL, 0u, NULL, &d);
    CHECK(!d.command && !d.off);
    servo_rauto_step(&r, 0u, NULL, NULL);
    rig_init(0u);
    CHECK(start());
    servo_rauto_step(&g.run, 1u, NULL, NULL);
    servo_rauto_step(&g.run, 2u, NULL, &d);
    CHECK(d.command);
    CHECK_EQ(d.cmd_us, RCENTRE);
    /* Handed out once. */
    servo_rauto_step(&g.run, 3u, NULL, &d);
    CHECK(!d.command);
}

/* A STOP while a write is on its way, the pulse write and then an end
 * point's: it may have landed, so RESTORE is offered and the report says
 * so; a STOP before any value changed offers none. */
TEST_CASE(a_stop_with_a_write_on_its_way_offers_restore)
{
    for (int which = 0; which < 2; ++which) {
        rig_init(0u);
        CHECK(start());
        const int op = which == 0 ? OP_PULSES : OP_WRITE;
        while (servo_rauto_running(&g.run) && g.op != op) {
            tick();
        }
        CHECK(servo_rauto_running(&g.run));
        CHECK(g.run.in_flight);
        servo_rauto_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
        CHECK(!g.run.in_flight);
        CHECK(servo_rauto_restore(&g.run));
        run_for(1000u);
        CHECK(ended_safe());
        CHECK(in_report("Restore:        RESTORE on the programming page"));
    }
    /* The pulses already 1000 to 2000us: no pulse write, and a STOP in
     * the first measure leaves nothing to restore. */
    rig_init(0u);
    g.pulses[1] = 10000u;
    g.pulses[2] = 20000u;
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS));
    CHECK(!g.run.in_flight);
    servo_rauto_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    CHECK(!servo_rauto_restore(&g.run));
}

int main(void)
{
    RUN(a_stop_with_a_write_on_its_way_offers_restore);
    RUN(a_stall_before_the_goal_ends_binds_with_the_angle);
    RUN(a_hard_sample_ends_the_run_with_the_output_off_and_safe_asked);
    RUN(window_means_above_1000ma_end_the_run_at_400ms);
    RUN(not_still_within_3s_ends_not_still);
    RUN(the_angle_lost_for_301ms_ends_the_run_and_300ms_is_ridden_through);
    RUN(the_meter_and_the_bench_silent_end_the_run);
    RUN(a_refused_connect_ends_prog_and_restore_is_offered_after_a_write);
    RUN(a_read_that_is_no_servo_fails_and_gives_the_port_back);
    RUN(the_words_name_every_reason_and_phase);
    return test_summary("servo_rauto_guard");
}
