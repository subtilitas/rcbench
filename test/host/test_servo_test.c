/*
 * The automatic servo test against the servo model and the supply model:
 * the run.
 *
 * Under test: what it measures at each step and how near the travel time
 * comes to the modelled servo's; length by time; the brown-out walk; PASS
 * and FAIL against the limits; every abort, each leaving the output asked
 * off, the servo let go and a report that says ABORTED and why; and the
 * words, which come from one table.
 *
 * The rig is servo_test_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_test_rig.h"

/* ------------------------------------------------------------------ tests */

/* Two steps run through: each measures idle, moving and holding current and
 * the travel time, the run passes, the output goes off and the servo is let
 * go, and the report and the CSV are handed over and ended. */
TEST_CASE(a_run_measures_each_step_and_passes)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.idle_max_a = 0.3f;
    c.hold_max_a = 0.3f;
    c.travel_max_ms = 1000u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(servo_test_running(&g.t));
    CHECK_EQ(servo_test_steps_planned(&g.t), 2u);
    run_out(120000u);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(g.off_asked);
    CHECK(g.released);
    CHECK(!g.sup.output);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.ends, 1u);
    CHECK(g.txt > 30u);
    CHECK(g.csv > 100u);
    CHECK_EQ(g.t.rows_lost, 0u);
    CHECK_EQ(g.t.step_count, 2u);
    for (unsigned k = 0; k < 2u; ++k) {
        const servo_test_step_t *s = &g.t.steps[k];
        CHECK(s->done);
        CHECK_EQ(s->moves, 4u);
        CHECK_EQ(s->travels, 4u);
        CHECK_EQ(s->timeouts, 0u);
        CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.12f, 0.03f);
        CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.12f, 0.03f);
        CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.12f, 0.03f);
        CHECK_NEAR(s->move.sum / (float)s->move.n, 0.95f, 0.03f);
        /* Late by up to one reading and one frame, never early. */
        CHECK(s->travel_max_ms >= TRAVEL_MS);
        CHECK(s->travel_max_ms <= TRAVEL_MS + 100u + FRAME_MS);
        CHECK_NEAR(s->v.sum / (float)s->v.n, s->set_v, 0.06f);
    }
    CHECK(strstr(g.report, "Result:         PASS") != NULL);
    CHECK(strstr(g.report, "Device:         DS3218") != NULL);
    CHECK(strstr(g.report, "STANDARD PWM") != NULL);
    CHECK(strstr(g.report, " 4.80V 6.00V") != NULL);
    CHECK(strstr(g.report, "NOT MEASURED") != NULL);
    float per_s = 0.0f, mod_s = 0.0f;
    uint32_t every = 0u;
    CHECK(servo_test_rates(&g.t, &per_s, &mod_s, &every));
    CHECK_NEAR(per_s, 10.0f, 0.2f);
    CHECK_EQ(every, 100u);       /* 99.8ms, rounded */
    CHECK(servo_test_drained(&g.t));
    CHECK(g.set_v_max <= 6.0f + 0.001f);
}

/* At 20 readings a second the travel time comes nearer: late by up to one
 * reading's 50ms and a frame. */
TEST_CASE(the_travel_time_is_late_by_at_most_a_reading)
{
    rig_fresh();
    g.read_every_ms = 50u;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->travels, 4u);
    CHECK(s->travel_max_ms >= TRAVEL_MS);
    CHECK(s->travel_max_ms <= TRAVEL_MS + 50u + FRAME_MS);
    CHECK(s->travel_sum_ms / s->travels >= TRAVEL_MS);
}

/* LENGTH BY TIME: counted moves until TEST TIME has passed. */
TEST_CASE(length_by_time_moves_for_the_test_time)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.by_moves = false;
    c.time_s = 6u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    /* A move and its hold take about 0.77 + 0.6s. */
    CHECK(g.t.steps[0].moves >= 4u);
    CHECK(g.t.steps[0].moves <= 6u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
}

/*
 * A servo whose ends hold at different currents -- 0.40A at the low end,
 * which is loaded, 0.05A at the high end and the centre -- and whose
 * current ramps up from where it was at 0.95A a second while it moves,
 * for 1200ms end to end.  Neither the first reading of a move, still at
 * the start end's level, nor the ramp passing the destination's level on
 * its way up is the arrival: every travel time is the 1200ms the servo
 * takes, late by no more than a reading.
 */
static float ramp_level(uint16_t us) { return (us < 1300u) ? 0.40f : 0.05f; }

TEST_CASE(unequal_end_levels_do_not_arrive_early)
{
    servo_test_t t;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.settle_ms  = 0u;
    c.report     = false;
    servo_test_init(&t);
    uint32_t now = 1000u, cmd_at = 0u, next = now;
    uint16_t cmd = CENTRE, from = CENTRE, samples = 1u;
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 4.8f;
    r.set_v = 4.8f;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.mode = 1u;
    r.taken_ms = now;
    CHECK_EQ(servo_test_start(&t, &c, now, &r, 1.0f, 20.0f),
             SERVO_TEST_START_OK);
    for (int k = 0; k < 6000 && servo_test_running(&t); ++k) {
        now += 10u;
        if (now >= next) {
            next += 100u;
            const uint32_t dt = now - cmd_at;
            float i = ramp_level(cmd);
            if (cmd != from && dt < 1200u) {
                i = ramp_level(from) + 0.00095f * (float)dt;
                if (i > 1.0f) {
                    i = 1.0f;
                }
            }
            r.samples = ++samples;
            r.taken_ms = now;
            r.i = i;
            servo_test_reading(&t, &r, 0u);
        }
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(&t, now, &in, &d);
        if (d.command) {
            from = cmd;
            cmd = d.cmd_us;
            cmd_at = now;
        }
        while (servo_test_peek(&t, NULL) != SERVO_TEST_OUT_NONE) {
            servo_test_pop(&t);
        }
    }
    const servo_test_step_t *s = &t.steps[0];
    CHECK_EQ(t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(s->moves, 4u);
    CHECK_EQ(s->travels, 4u);
    CHECK_EQ(s->timeouts, 0u);
    CHECK(s->travel_sum_ms / s->travels >= 1200u);   /* none early */
    CHECK(s->travel_max_ms <= 1200u + 100u + 10u);
    /* And each end's level is that end's, not a reading of the ramp. */
    CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.40f, 0.001f);
    CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.05f, 0.001f);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_PASS);
}

/* HV SERVO in the report says what ran: on with neither of its steps
 * chosen runs nothing above 6.0V. */
TEST_CASE(the_report_says_whether_a_step_above_6_v_ran)
{
    servo_test_cfg_t c;
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.hv = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(strstr(g.report, "HV servo:       ON, no step above 6.0V chosen")
          != NULL);

    rig_fresh();
    cfg_defaults(&c);
    c.steps_v[0] = 7.4f;
    c.step_count = 1u;
    c.hv = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(strstr(g.report, "HV servo:       ON, steps above 6.0V run")
          != NULL);
}

/* The brown-out walk stops at the first voltage that shows no movement. */
TEST_CASE(the_brownout_walk_stops_where_the_servo_stops)
{
    rig_fresh();
    g.brownout_v = 4.1f;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK_EQ(servo_test_steps_planned(&g.t), 1u);
    run_out(120000u);
    float moved = 0.0f;
    bool stopped = false;
    CHECK(servo_test_brownout(&g.t, &moved, &stopped));
    CHECK_NEAR(moved, 4.2f, 0.001f);
    CHECK(stopped);
    CHECK(strstr(g.report, "Movement seen at 4.20V; no movement seen at "
                           "4.00V.") != NULL);
    /* 5.0 down to 4.0 in 0.2V steps. */
    CHECK_EQ(g.t.step_count, 6u);
    CHECK(g.set_v_max <= 5.0f + 0.001f);
    CHECK(g.off_asked);
}

/* A servo that moves at every voltage: the walk ends at the floor, the
 * supply's lowest set point where that is above 3.0V. */
TEST_CASE(the_brownout_walk_ends_at_the_floor)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK_EQ(servo_test_steps_planned(&g.t), 2u);
    run_out(200000u);
    float moved = 0.0f;
    bool stopped = true;
    CHECK(servo_test_brownout(&g.t, &moved, &stopped));
    CHECK(!stopped);
    /* 5.0 down in 0.2V steps to 3.4, then the floor itself: 3.3V, the
     * model's lowest set point, as the report says. */
    CHECK_NEAR(moved, 3.3f, 0.001f);
    CHECK_NEAR(g.t.steps[g.t.step_count - 1u].set_v, 3.3f, 0.001f);
    CHECK(strstr(g.report, "in 0.20V steps to 3.30V") != NULL);
    CHECK(strstr(g.report, "down to 3.30V") != NULL);
    CHECK(strstr(g.report, "lower not tested") != NULL);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.report, "Brown-out start  movement seen at 5.00V: PASS\n")
          != NULL);

    /* Not moving at the first: said so. */
    rig_fresh();
    g.brownout_v = 9.0f;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(!servo_test_brownout(&g.t, &moved, &stopped));
    CHECK(stopped);
    CHECK(strstr(g.report, "No movement seen at 5.00V, the first step: "
                           "not measurable.") != NULL);
    /* The walk is the whole run, and it measured nothing: the result and
     * the limits say where, not "0 of 0 moves". */
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK(strstr(g.report, "Result:         NOT MEASURABLE - no movement "
                           "seen at 5.00V, the brown-out walk's first "
                           "voltage\n") != NULL);
    CHECK(strstr(g.report, "0 of 0") == NULL);
    CHECK(strstr(g.report, "Brown-out start  no movement seen at 5.00V: "
                           "NOT MEASURABLE\n") != NULL);

    /* A step that moves and passes, and a walk that sees nothing at its
     * first voltage: the walk measured nothing, so the run is not PASS. */
    rig_fresh();
    g.brownout_v = 9.0f;
    cfg_defaults(&c);
    c.steps_v[0] = 9.6f;
    c.step_count = 1u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK_EQ(g.t.steps[0].no_rise, 0u);
    CHECK_EQ(g.t.steps[0].timeouts, 0u);
    CHECK(!g.t.steps[1].moved);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK(strstr(g.report, "Result:         NOT MEASURABLE - no movement "
                           "seen at 5.00V, the brown-out walk's first "
                           "voltage\n") != NULL);
    CHECK(strstr(g.report, "Moves seen       0 unseen: PASS\n") != NULL);
    CHECK(strstr(g.report, "Brown-out start  no movement seen at 5.00V: "
                           "NOT MEASURABLE\n") != NULL);
}

/* A cap under 5.0V starts the walk at the cap. */
TEST_CASE(the_brownout_walk_starts_under_the_cap)
{
    rig_fresh();
    g.v_max = 4.5f;
    g.brownout_v = 4.2f;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(g.set_v_max <= 4.5f + 0.001f);
    CHECK_NEAR(g.t.steps[0].set_v, 4.5f, 0.001f);
}

/* Each limit on the LIMITS page fails the run when exceeded; 0 is not
 * checked. */
TEST_CASE(each_limit_fails_the_run)
{
    servo_test_cfg_t c;

    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.idle_max_a = 0.05f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    CHECK(strstr(g.report, "Result:         FAIL") != NULL);
    CHECK(strstr(g.report, "Idle current     highest") != NULL);

    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.travel_max_ms = 500u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    CHECK(strstr(g.report, "Travel time      longest 700ms, limit 500ms: "
                           "FAIL") != NULL);

    /* The same servo read by the PD mini: its readings lag and repeat, so
     * the travel time is an upper bound, reported and not checked. */
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.travel_max_ms = 500u;
    servo_test_meter_pdmini(&c.meter);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.report, "Supply:         PD mini\n") != NULL);
    CHECK(strstr(g.report, "Lag:            about 300ms from a change of "
                           "current to the reading that shows it\n") != NULL);
    CHECK(strstr(g.report, "Repeats:        a reading can repeat") != NULL);
    CHECK(strstr(g.report, "Travel times:   an upper bound, not checked "
                           "against the limit\n") != NULL);
    CHECK(strstr(g.report, "Travel time      longest 700ms, limit 500ms: "
                           "upper bound, not checked against the limit\n")
          != NULL);
    servo_test_meter_pdmini(NULL);          /* nothing to fill: no harm */

    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.hold_max_a = 0.05f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);

    /* STALL AT under the moving current: counted, though nothing lasts a
     * second above it. */
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.stall_a = 0.5f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    CHECK(g.t.stalled);
}

/* An end past the linkage's stop: the servo pushes there, its holding
 * current is high, and above STALL AT for a second the run ends. */
TEST_CASE(a_servo_pushing_on_a_stop_ends_the_run)
{
    rig_fresh();
    g.servo.cfg.stop_hi_us = 1880u;      /* 20us short of the high end */
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.stall_a = 1.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_ABORTED);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_STALL);
    CHECK(g.off_asked);
    CHECK(g.released);
    CHECK(strstr(g.report, "Result:         ABORTED - above STALL AT") != NULL);

    /* Under STALL AT the run goes on, and the high end's holding current
     * fails it. */
    rig_fresh();
    g.servo.cfg.stop_hi_us = 1880u;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.hold_max_a = 0.5f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK(s->hold[1].sum / (float)s->hold[1].n > 0.9f);
    CHECK_EQ(s->timeouts, 0u);      /* each end's level measured first */
}

/* A servo that does not move: no counted move shows movement, so none is
 * timed and none is late.  The current cannot tell it from a servo moving
 * under the threshold, so the step and the run read NOT MEASURABLE, not
 * FAIL, and the report says how many moves went unseen. */
TEST_CASE(a_servo_that_does_not_move_is_not_measurable)
{
    rig_fresh();
    g.brownout_v = 99.0f;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.moves = 2u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK_EQ(g.t.steps[0].travels, 0u);
    CHECK_EQ(g.t.steps[0].no_rise, 2u);
    CHECK_EQ(g.t.steps[0].timeouts, 0u);
    CHECK(strstr(g.report, "Result:         NOT MEASURABLE - 2 of 2 counted "
                           "moves showed no movement in the current") != NULL);
    /* The step's line, then the note under it. */
    CHECK(strstr(g.report, "    2    0      2\n 4.80  NOT MEASURABLE\n")
          != NULL);
    CHECK(strstr(g.report, "Moves arrived    0 late: PASS") != NULL);
    CHECK(strstr(g.report, "Moves seen       2 unseen: NOT MEASURABLE")
          != NULL);
    /* No move arrived: there is no longest travel time to state. */
    CHECK(strstr(g.report, "Travel time      longest --, limit OFF: not "
                           "measured, no move arrived\n") != NULL);
    CHECK(strstr(g.report, "longest 0ms") == NULL);

    /* Nor on the PD mini with a limit set: not an upper bound of 0ms. */
    rig_fresh();
    g.brownout_v = 99.0f;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.moves = 2u;
    c.travel_max_ms = 500u;
    servo_test_meter_pdmini(&c.meter);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK(strstr(g.report, "Travel time      longest --, limit 500ms: not "
                           "measured, no move arrived\n") != NULL);
    CHECK(strstr(g.report, "upper bound, not checked against the limit\n")
          != NULL);                         /* the header line, not this */
    CHECK(strstr(g.report, "longest 0ms") == NULL);

    /* A limit exceeded still fails it. */
    rig_fresh();
    g.brownout_v = 99.0f;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.moves = 2u;
    c.idle_max_a = 0.05f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
}

/* Every abort: the output asked off, the servo let go, ABORTED and the
 * reason in the report, and the files ended. */
static void check_aborted(servo_test_abort_t why)
{
    run_out(60000u);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(g.t.why, why);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_ABORTED);
    CHECK(g.off_asked);
    CHECK(g.released);
    CHECK_EQ(g.ends, 1u);
    char want[80];
    snprintf(want, sizeof(want), "Result:         ABORTED - %s",
             servo_test_abort_name(why));
    CHECK(strstr(g.report, want) != NULL);
}

TEST_CASE(every_abort_switches_off_lets_go_and_reports)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);

    /* The caller's: STOP, link, leaving, the operator. */
    const servo_test_abort_t callers[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_LINK, SERVO_TEST_AB_LEFT,
        SERVO_TEST_AB_OPERATOR, SERVO_TEST_AB_SETTINGS, SERVO_TEST_AB_TOUCH,
    };
    for (size_t k = 0; k < sizeof(callers) / sizeof(callers[0]); ++k) {
        rig_fresh();
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        run_ms(4000u);
        servo_test_abort(&g.t, callers[k], g.now);
        check_aborted(callers[k]);
    }

    /* A disarm. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.armed = false;
    check_aborted(SERVO_TEST_AB_DISARMED);

    /* The output switched off elsewhere, and a trip. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    supply_sim_output(&g.sup, false);
    check_aborted(SERVO_TEST_AB_SUPPLY_OFF);

    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    supply_sim_output(&g.sup, false);
    g.trip = 1u;
    check_aborted(SERVO_TEST_AB_TRIPPED);

    /* A supply that stops answering, and readings that stop. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.online = false;
    check_aborted(SERVO_TEST_AB_SUPPLY_LOST);

    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.readings = false;
    run_ms(SERVO_TEST_STALE_MS - 200u);
    CHECK(servo_test_running(&g.t));
    check_aborted(SERVO_TEST_AB_STALE);

    /* A set point the supply does not take, and an output that does not
     * come on. */
    rig_fresh();
    g.take_set = false;
    supply_sim_set(&g.sup, 9.0f, 2.0f);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    check_aborted(SERVO_TEST_AB_SET_NOT_TAKEN);

    rig_fresh();
    g.take_on = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    check_aborted(SERVO_TEST_AB_NOT_ON);

    /* The cap comes down under the next step while the run goes on. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.v_max = 5.5f;
    check_aborted(SERVO_TEST_AB_CAP);
    CHECK(g.set_v_max <= 4.8f + 0.001f);
    CHECK(strstr(g.report, "(cut short)") == NULL);   /* 4.8V ran out */
    CHECK(strstr(g.report, " 6.00  not run") != NULL);
}

/* The words come from one table. */
TEST_CASE(the_words_come_from_one_table)
{
    CHECK_STR_EQ(servo_test_phase_name(SERVO_TEST_PH_MOVE), "MOVE");
    CHECK_STR_EQ(servo_test_phase_name((servo_test_phase_t)99), "");
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_STOP), "STOP");
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_COUNT), "");
    CHECK_STR_EQ(servo_test_verdict_name(SERVO_TEST_FAIL), "FAIL");
    CHECK_STR_EQ(servo_test_verdict_name(SERVO_TEST_ABORTED), "ABORTED");
    CHECK_STR_EQ(servo_test_verdict_name(SERVO_TEST_NOT_MEASURABLE),
                 "NOT MEASURABLE");
    CHECK_STR_EQ(servo_test_start_name((servo_test_start_t)99), "");
    CHECK_STR_EQ(servo_str(SERVO_STR_COUNT), "");
    for (int k = 0; k < (int)SERVO_STR_COUNT; ++k) {
        CHECK(servo_str((servo_str_t)k) != NULL);
    }
    servo_test_t t;
    servo_test_init(&t);
    char b[8];
    CHECK(!servo_report_line(NULL, 0u, b, sizeof(b)));
    CHECK(servo_test_drained(&t));
    CHECK(!servo_test_rates(&t, NULL, NULL, NULL));
}

int main(void)
{
    RUN(a_run_measures_each_step_and_passes);
    RUN(the_travel_time_is_late_by_at_most_a_reading);
    RUN(length_by_time_moves_for_the_test_time);
    RUN(unequal_end_levels_do_not_arrive_early);
    RUN(the_report_says_whether_a_step_above_6_v_ran);
    RUN(the_brownout_walk_stops_where_the_servo_stops);
    RUN(the_brownout_walk_ends_at_the_floor);
    RUN(the_brownout_walk_starts_under_the_cap);
    RUN(each_limit_fails_the_run);
    RUN(a_servo_pushing_on_a_stop_ends_the_run);
    RUN(a_servo_that_does_not_move_is_not_measurable);
    RUN(every_abort_switches_off_lets_go_and_reports);
    RUN(the_words_come_from_one_table);
    free(g.csv_text);
    return test_summary("servo_test");
}
