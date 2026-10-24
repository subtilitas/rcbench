/*
 * The automatic servo test against the servo model and the supply model:
 * the judging of a move and of a reading.
 *
 * Under test: the arrival of a move, taken at a level held harder than the
 * servo moves, past an acceleration spike and at a moving current equal to
 * the holding current; a meter's lag; a frozen current, readings skipped
 * and a count starting again; a step cut short; a start refused; an output
 * already on; a repeated sample; the caps it never exceeds; the outbox;
 * the CSV read back by the log viewer's parser; a reading at the move's
 * deadline; a servo held at the current limit; a reading equal to a limit
 * or to STALL AT; a count step of half the range; and a run ended before
 * its first step.
 *
 * The rig is servo_test_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "log_csv.h"
#include "servo_test_rig.h"

/*
 * A servo with its own current profile: after 40ms of latency it moves for
 * 1200ms at @p mv amperes, the first 100ms of the motion at @p spike when
 * that is above zero, and settles at the end's holding level -- @p lo at the
 * low end, 0.05A at the centre and the high end.  Readings every 100ms,
 * @p off ms after the run starts.  Six counted moves.
 */
typedef struct {
    float    lo, mv, spike;
    uint16_t cmd, prev;
    uint32_t cmd_at;
} profile_t;

static float profile_level(const profile_t *p, uint16_t us)
{
    return (us < 1300u) ? p->lo : 0.05f;
}

static float profile_current(const profile_t *p, uint32_t now)
{
    const uint32_t dt = now - p->cmd_at;
    if (p->cmd == p->prev || dt < 40u) {
        return profile_level(p, p->prev);
    }
    if (dt >= 40u + 1200u) {
        return profile_level(p, p->cmd);
    }
    if (p->spike > 0.0f && dt - 40u < 100u) {
        return p->spike;
    }
    return p->mv;
}

static void run_profile(servo_test_t *t, float lo, float mv, float spike,
                        uint32_t off)
{
    profile_t p = { lo, mv, spike, CENTRE, CENTRE, 0u };
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.settle_ms  = 0u;
    c.moves      = 6u;
    c.report     = false;
    servo_test_init(t);
    uint32_t now = 1000u, next = now + off;
    uint16_t samples = 1u;
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 4.8f;
    r.set_v = 4.8f;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.mode = 1u;
    r.taken_ms = now;
    (void)servo_test_start(t, &c, now, &r, 1.0f, 20.0f);
    for (int k = 0; k < 20000 && servo_test_running(t); ++k) {
        now += 10u;
        if (now >= next) {
            next += 100u;
            r.samples = ++samples;
            r.taken_ms = now;
            r.i = profile_current(&p, now);
            servo_test_reading(t, &r, 0u);
        }
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(t, now, &in, &d);
        if (d.command) {
            p.prev = p.cmd;
            p.cmd = d.cmd_us;
            p.cmd_at = now;
        }
        while (servo_test_peek(t, NULL) != SERVO_TEST_OUT_NONE) {
            servo_test_pop(t);
        }
    }
}

/* Every move timed at the servo's 1240ms, late by no more than a reading:
 * none early. */
static void check_on_time(const servo_test_t *t)
{
    const servo_test_step_t *s = &t->steps[0];
    CHECK_EQ(t->why, SERVO_TEST_AB_NONE);
    CHECK_EQ(s->travels, 6u);
    CHECK_EQ(s->timeouts, 0u);
    CHECK(s->travel_sum_ms / s->travels >= 1240u);
    CHECK(s->travel_max_ms <= 1240u + 100u);
    CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.40f, 0.001f);
    CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.05f, 0.001f);
}

/* An end held at 0.40A, harder than the 0.25A the servo moves at: never
 * passed on the way, so the settled readings there are the arrival. */
TEST_CASE(an_end_held_harder_than_the_servo_moves_is_reached_settled)
{
    servo_test_t t;
    run_profile(&t, 0.40f, 0.25f, 0.0f, 0u);
    check_on_time(&t);
}

/* The same end, with the first 100ms of every move at 0.9A: that reading
 * lies above the end's level, and the moving current after it below. A
 * reading below the level is not the arrival, whatever the phase of the
 * readings against the motion. */
TEST_CASE(an_acceleration_spike_above_the_level_is_not_the_arrival)
{
    for (uint32_t off = 0u; off < 100u; off += 25u) {
        servo_test_t t;
        run_profile(&t, 0.40f, 0.25f, 0.9f, off);
        check_on_time(&t);
        CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_PASS);
    }
}

/* The limit, as documented: a servo that moves at the very current it holds
 * the low end with cannot be told from one already there.  Moves to the high
 * end are timed; moves to the low end end at their first settled readings,
 * a reading or two after the command. */
TEST_CASE(a_moving_current_equal_to_the_holding_current_is_the_limit)
{
    servo_test_t t;
    run_profile(&t, 0.40f, 0.40f, 0.0f, 0u);
    const servo_test_step_t *s = &t.steps[0];
    CHECK_EQ(s->travels, 6u);
    CHECK(s->travel_max_ms >= 1240u);                 /* to the high end */
    CHECK(s->travel_sum_ms / s->travels < 1240u);     /* to the low end */
}

/*
 * A slow servo read through a lagging meter: it holds 0.05A, moves at
 * 0.30A for @p travel_ms after 40ms of latency, and the meter shows each
 * current @p lag_ms late, read every 100ms.  Four counted moves.  A
 * @p travel_ms of 0 is a servo that moves normally to place both ends, then
 * sticks at 0.30A from its first counted move on, the third.
 */
static void run_lagged(servo_test_t *t, uint32_t travel_ms, uint32_t lag_ms,
                       const servo_test_meter_t *meter)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.settle_ms  = 0u;
    c.moves      = 4u;
    c.report     = false;
    c.meter      = *meter;
    servo_test_init(t);
    uint32_t now = 1000u, next = now, cmd_at = 0u, first_at = 0u;
    uint16_t cmd = CENTRE, prev = CENTRE, samples = 1u;
    unsigned moves = 0u;
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 4.8f;
    r.set_v = 4.8f;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.mode = 1u;
    r.taken_ms = now;
    (void)servo_test_start(t, &c, now, &r, 1.0f, 20.0f);
    for (int k = 0; k < 20000 && servo_test_running(t); ++k) {
        now += 10u;
        if (now >= next) {
            next += 100u;
            /* What the servo drew lag_ms ago. */
            const uint32_t seen = now - lag_ms;
            const uint32_t dt = seen - cmd_at;
            float i = 0.05f;
            const uint32_t travel = (travel_ms == 0u) ? 1200u : travel_ms;
            if (first_at != 0u && (int32_t)(seen - first_at) >= 40) {
                i = 0.30f;                              /* stuck */
            } else if (cmd != prev && (int32_t)dt >= 40
                       && dt < 40u + travel) {
                i = 0.30f;
            }
            r.samples = ++samples;
            r.taken_ms = now;
            r.i = i;
            servo_test_reading(t, &r, 0u);
        }
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(t, now, &in, &d);
        if (d.command) {
            prev = cmd;
            cmd = d.cmd_us;
            cmd_at = now;
            if (cmd != CENTRE && ++moves == 3u && travel_ms == 0u) {
                first_at = now;
            }
        }
        while (servo_test_peek(t, NULL) != SERVO_TEST_OUT_NONE) {
            servo_test_pop(t);
        }
    }
}

/* A servo that arrives at 2840ms shows it on a meter 300ms behind at
 * about 3140ms, after the 3000ms window: the meter's lag widens the
 * window to 3300ms, so the move is timed and not late.  Without the lag
 * stated the same readings are late.  A servo that never arrives is late
 * on the lagging meter too, and fails the run. */
TEST_CASE(a_meters_lag_widens_the_window_and_a_stuck_move_is_late)
{
    static servo_test_t t;
    servo_test_meter_t pd;
    servo_test_meter_pdmini(&pd);
    servo_test_init(&t);
    CHECK_EQ(servo_test_travel_window_ms(&t), 3000u);
    CHECK_EQ(servo_test_travel_window_ms(NULL), 3000u);

    run_lagged(&t, 2800u, 300u, &pd);
    CHECK_EQ(servo_test_travel_window_ms(&t), 3300u);
    CHECK_EQ(t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(t.steps[0].timeouts, 0u);
    CHECK_EQ(t.steps[0].travels, 4u);
    CHECK(t.steps[0].travel_max_ms > SERVO_TEST_TRAVEL_TIMEOUT_MS);
    CHECK(t.steps[0].travel_max_ms <= 3300u);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_PASS);

    servo_test_meter_t none;
    memset(&none, 0, sizeof(none));
    run_lagged(&t, 2800u, 300u, &none);
    CHECK(t.steps[0].timeouts > 0u);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_FAIL);

    /* Stuck at 0.30A from its first counted move on: that move rises and
     * never comes back, late at 3300ms, and fails the run.  The end it
     * then holds measures 0.30A, so moves after it show no change. */
    run_lagged(&t, 0u, 300u, &pd);
    CHECK_EQ(t.why, SERVO_TEST_AB_NONE);
    CHECK(t.steps[0].timeouts > 0u);
    CHECK_EQ(t.steps[0].travels, 0u);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_FAIL);
}

/* A supply whose reading count stops while its page goes on answering --
 * display reads on the IOmcu that are slow or fail -- shows the same
 * current over and over.  None of it is a new reading: no row is logged,
 * nothing is measured, and 1.5s on the run ends as STALE. */
TEST_CASE(a_frozen_current_is_no_reading)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    CHECK(servo_test_running(&g.t));
    const uint32_t readings = g.t.readings;
    const unsigned rows = g.csv;
    g.frozen = true;
    run_ms(SERVO_TEST_STALE_MS - 100u);
    CHECK(servo_test_running(&g.t));
    CHECK_EQ(g.t.readings, readings);
    CHECK_EQ(g.csv, rows);
    run_ms(400u);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_STALE);
    CHECK(g.off_asked);
    run_out(10000u);
    CHECK(strstr(g.report, "ABORTED - no new supply reading") != NULL);
}

/* Readings the supply took between two that reached the test are counted
 * and named in the report; the rate taken by the supply counts them. */
TEST_CASE(skipped_readings_are_reported)
{
    rig_fresh();
    g.sample_step = 3u;                  /* two skipped between each */
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(g.t.skipped, 2u * (g.t.readings - 1u));
    float per_s = 0.0f, mod_s = 0.0f;
    CHECK(servo_test_rates(&g.t, &per_s, &mod_s, NULL));
    CHECK_NEAR(mod_s, 3.0f * per_s, 0.01f);
    char want[64];
    snprintf(want, sizeof(want), "Skipped:        %lu readings",
             (unsigned long)g.t.skipped);
    CHECK(strstr(g.report, want) != NULL);
}

/* A count that starts again, as after an IOmcu restart, is one
 * reading, not tens of thousands skipped. */
TEST_CASE(a_count_starting_again_is_not_readings_skipped)
{
    rig_fresh();
    g.samples = 5000u;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(3000u);
    const uint32_t before = g.t.module_samples;
    g.samples = 2u;
    run_out(60000u);
    CHECK_EQ(g.t.skipped, 0u);
    CHECK(g.t.module_samples - before < 1000u);
}

/* An abort mid-step marks that step cut short. */
TEST_CASE(a_step_cut_short_is_said_so)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(6000u);
    servo_test_abort(&g.t, SERVO_TEST_AB_STOP, g.now);
    run_out(10000u);
    CHECK(strstr(g.report, "(cut short)") != NULL);
    /* An abort after the end changes nothing. */
    servo_test_abort(&g.t, SERVO_TEST_AB_LINK, g.now);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_STOP);
}

/* A start is refused, and nothing changes, for no step, a step outside the
 * caps, a supply that does not answer, and ends that do not straddle the
 * centre. */
TEST_CASE(a_start_is_refused_for_what_cannot_run)
{
    servo_test_cfg_t c;
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 0u;
    CHECK_EQ(start(&c), SERVO_TEST_START_NO_STEPS);
    CHECK(!servo_test_running(&g.t));

    cfg_defaults(&c);
    c.steps_v[2] = 8.4f;
    c.step_count = 3u;
    g.v_max = 8.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_ABOVE_CAP);
    g.v_max = 21.0f;
    g.v_min = 5.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_ABOVE_CAP);
    g.v_min = 3.3f;

    g.online = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_NO_SUPPLY);
    g.online = true;

    cfg_defaults(&c);
    c.end_lo_us = CENTRE;
    CHECK_EQ(start(&c), SERVO_TEST_START_BAD_ENDS);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(servo_test_peek(&g.t, NULL), SERVO_TEST_OUT_NONE);
    CHECK(servo_test_drained(&g.t));

    CHECK_EQ(servo_test_start(NULL, &c, 0u, NULL, 0.0f, 0.0f),
             SERVO_TEST_START_NO_STEPS);
    CHECK_STR_EQ(servo_test_start_name(SERVO_TEST_START_ABOVE_CAP),
                 "A STEP IS OUTSIDE THE CAPS");
}

/* An output already on is not switched on again, and the run watches it
 * from the start. */
TEST_CASE(an_output_already_on_is_used_as_it_is)
{
    rig_fresh();
    supply_sim_output(&g.sup, true);
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    const servo_test_in_t in = { true, 21.0f };
    servo_test_do_t d;
    servo_test_step(&g.t, g.now, &in, &d);
    CHECK(d.set);
    CHECK(!d.on);
    CHECK(d.command);
    CHECK_EQ(d.cmd_us, CENTRE);
    supply_sim_set(&g.sup, d.set_v, d.set_i);   /* as frame() would */
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
}

/* A sample that repeats the last reading's count measures nothing and logs
 * no row; its state is still checked. */
TEST_CASE(a_repeated_sample_is_not_a_reading)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(2000u);
    const uint32_t readings = g.t.readings;
    servo_test_reading_t r = reading(0.12f);
    r.samples = g.t.samples;
    servo_test_reading(&g.t, &r, 0u);
    CHECK_EQ(g.t.readings, readings);
    r.online = false;
    servo_test_reading(&g.t, &r, 0u);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_SUPPLY_LOST);
}

/* An outbox nobody empties loses rows and counts them; the report says so,
 * and the run's files still end. */
TEST_CASE(a_full_outbox_counts_the_rows_it_loses)
{
    rig_fresh();
    g.drain = false;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(5000u);
    CHECK(g.t.rows_lost > 0u);
    CHECK(!servo_test_drained(&g.t));
    g.drain = true;
    run_out(60000u);
    CHECK(servo_test_drained(&g.t));
    CHECK_EQ(g.ends, 1u);
    char want[64];
    snprintf(want, sizeof(want), "lost to a full queue");
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, " 0 lost") == NULL);
}

/* The CSV opens in the log viewer: its parser finds the columns, the time
 * axis and the numbers. */
TEST_CASE(the_csv_reads_back_in_the_viewer)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(g.csv_text != NULL);
    CHECK(strncmp(g.csv_text, servo_test_csv_header(),
                  strlen(servo_test_csv_header())) == 0);
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g.csv_text, g.csv_len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.n_columns, 19);
    CHECK_EQ(an.delimiter, ';');
    CHECK_EQ(an.ragged_rows, 0);
    CHECK_EQ(an.time_index, 0);
    CHECK_EQ(an.row_count, (int)g.csv - 1);
    CHECK(an.columns[9].numeric);        /* current (A) */
    CHECK(an.columns[0].monotonic);
    /* An arrival carries its travel time, and every row its meter. */
    CHECK(strstr(g.csv_text, ";CV;700;PDMINI;;;;;\n") != NULL);
}

/* ---------------------------------------------------- the move's deadline */

static servo_test_t g_dl;
static uint16_t     g_dl_samples;

/* A reading of @p i amps at @p t, at a 5.00V set point, then a pass. */
static void dl_feed(uint32_t t, float i)
{
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 5.0f;
    r.i = i;
    r.set_v = 5.0f;
    r.set_i = 2.0f;
    r.mode = 1u;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.samples = ++g_dl_samples;
    r.taken_ms = t;
    servo_test_reading(&g_dl, &r, 0u);
    const servo_test_in_t in = { true, 20.0f };
    servo_test_do_t d;
    servo_test_step(&g_dl, t, &in, &d);
}

/* The brown-out's first move, centre to the high end, on the PD mini's
 * meter: 0.12A at rest, 0.9A moving every 100ms, and the reading back
 * at 0.12A @p back_ms after the command. */
static void dl_run(uint32_t back_ms)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    c.settle_ms = 100u;
    servo_test_meter_pdmini(&c.meter);
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = true;
    last.output = true;
    servo_test_init(&g_dl);
    g_dl_samples = 0u;
    CHECK_EQ(servo_test_start(&g_dl, &c, 1000u, &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
    uint32_t t = 1000u;
    while (g_dl.phase != SERVO_TEST_PH_MOVE && t < 10000u) {
        t += 100u;
        dl_feed(t, 0.12f);
    }
    CHECK_EQ(g_dl.phase, SERVO_TEST_PH_MOVE);
    const uint32_t cmd = g_dl.cmd_ms;
    for (t = cmd + 100u; t < cmd + back_ms; t += 100u) {
        dl_feed(t, 0.9f);
    }
    dl_feed(cmd + back_ms, 0.12f);
}

/* The window is 3000ms and the PD mini's 300ms lag: a reading back at
 * the level 1ms before 3300ms is the arrival, one at 3300ms is not --
 * the move is late. */
TEST_CASE(a_reading_at_the_deadline_is_late)
{
    CHECK_EQ(servo_test_travel_window_ms(&g_dl), 3000u);
    dl_run(3299u);
    CHECK_EQ(g_dl.steps[0].moves, 1u);
    CHECK_EQ(g_dl.steps[0].travels, 1u);
    CHECK_EQ(g_dl.steps[0].travel_max_ms, 3299u);
    CHECK_EQ(g_dl.steps[0].timeouts, 0u);
    CHECK_EQ(servo_test_travel_window_ms(&g_dl), 3300u);

    dl_run(3300u);
    CHECK_EQ(g_dl.steps[0].moves, 1u);
    CHECK_EQ(g_dl.steps[0].travels, 0u);
    CHECK_EQ(g_dl.steps[0].timeouts, 1u);
    CHECK_EQ(g_dl.phase, SERVO_TEST_PH_HOLD);
}

/* ------------------------------------------------- stall and the limits */

/* A LIMITS value as settings.c holds it: snapped to its 0.01A step in
 * float, which for 0.05A is 0.049999997. */
static float limit_held(unsigned ma)
{
    return 0.0f + roundf(((float)ma / 1000.0f - 0.0f) / 0.01f) * 0.01f;
}

/* A servo binding at its high end against a supply whose current limit is
 * STALL AT, both 2.00A as shipped: no reading is above STALL AT, the
 * supply reads constant current, and the run ends on that. */
TEST_CASE(a_servo_held_at_the_current_limit_ends_the_run)
{
    rig_fresh();
    g.cc_limits = true;
    g.servo.cfg.stop_hi_us = 1850u;     /* 50us short of the high end */
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.steps_v[0] = 6.0f;
    c.by_moves   = false;
    c.time_s     = 60u;
    c.i_limit    = 2.0f;
    c.stall_a    = 2.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK(g.cc_readings > 0u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_ABORTED);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_CC);
    CHECK(strstr(g.report, "Result:         ABORTED - constant current for "
                           "1 s") != NULL);
    CHECK(strstr(g.report, "Result:         PASS") == NULL);
    CHECK(g.off_asked);
    CHECK(g.released);
    CHECK(!g.sup.output);
    /* On for the set point, SETTLE, IDLE, the two moves to the stop and the
     * second of constant current, not for the step's 60s. */
    CHECK(g.on_ms < 8000u);
}

/* A reading of the same number of mA as a LIMITS value is not above it:
 * idle and holding currents of 0.050A against limits of 0.05A pass, and
 * 0.051A fails. */
TEST_CASE(a_reading_equal_to_a_limit_passes_it)
{
    for (unsigned ma = 50u; ma <= 51u; ++ma) {
        servo_test_t t;
        servo_test_cfg_t c;
        cfg_defaults(&c);
        c.step_count = 1u;
        c.settle_ms  = 0u;
        c.moves      = 2u;
        c.idle_max_a = limit_held(50u);
        c.hold_max_a = limit_held(50u);
        servo_test_init(&t);
        uint32_t now = 1000u, next = now;
        uint16_t samples = 1u;
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
        uint32_t cmd_at = now;
        bool moved = false;         /* a move was commanded */
        for (int k = 0; k < 20000 && servo_test_running(&t); ++k) {
            now += 10u;
            if (now >= next) {
                next += 100u;
                r.samples = ++samples;
                r.taken_ms = now;
                /* 0.3A for 300ms from 40ms after a command, the level
                 * under test otherwise. */
                const uint32_t dt = now - cmd_at;
                r.i = (moved && dt >= 40u && dt < 340u)
                          ? 0.3f : (float)ma / 1000.0f;
                servo_test_reading(&t, &r, 0u);
            }
            const servo_test_in_t in = { true, 20.0f };
            servo_test_do_t d;
            servo_test_step(&t, now, &in, &d);
            if (d.command && d.cmd_us != CENTRE) {
                cmd_at = now;
                moved  = true;
            }
            while (servo_test_peek(&t, NULL) != SERVO_TEST_OUT_NONE) {
                servo_test_pop(&t);
            }
        }
        CHECK_EQ(t.why, SERVO_TEST_AB_NONE);
        CHECK(t.steps[0].idle.n >= 9u);
        CHECK_EQ(servo_test_verdict(&t),
                 (ma == 50u) ? SERVO_TEST_PASS : SERVO_TEST_FAIL);
        char line[SERVO_TEST_LINE_MAX];
        bool idle_seen = false, hold_seen = false;
        for (unsigned k = 0; servo_report_line(&t, k, line, sizeof(line));
             ++k) {
            if (strncmp(line, "Idle current ", 13) == 0) {
                idle_seen = true;
                CHECK(strstr(line, (ma == 50u) ? ": PASS" : ": FAIL") != NULL);
            }
            if (strncmp(line, "Holding current ", 16) == 0) {
                hold_seen = true;
                CHECK(strstr(line, (ma == 50u) ? ": PASS" : ": FAIL") != NULL);
            }
        }
        CHECK(idle_seen && hold_seen);
    }
}

/* One reading of the same number of mA as STALL AT is not a stall, for
 * each of the 99 values STALL AT takes, 0.10 to 5.00A in steps of 0.05A,
 * as settings.c holds them; 1mA more is. */
TEST_CASE(a_reading_equal_to_stall_at_is_not_a_stall)
{
    unsigned at_equal = 0u, above = 0u;
    for (unsigned ma = 100u; ma <= 5000u; ma += 50u) {
        for (unsigned over = 0u; over <= 1u; ++over) {
            servo_test_t t;
            servo_test_cfg_t c;
            cfg_defaults(&c);
            c.step_count = 1u;
            c.settle_ms  = 0u;
            c.report     = false;
            c.stall_a = 0.1f + roundf(((float)ma / 1000.0f - 0.1f) / 0.05f)
                                   * 0.05f;
            servo_test_init(&t);
            servo_test_reading_t r;
            memset(&r, 0, sizeof(r));
            r.v = 4.8f;
            r.set_v = 4.8f;
            r.output = true;
            r.online = true;
            r.ok = true;
            r.mode = 1u;
            r.taken_ms = 1000u;
            (void)servo_test_start(&t, &c, 1000u, &r, 1.0f, 20.0f);
            const servo_test_in_t in = { true, 20.0f };
            servo_test_do_t d;
            /* SET takes the first reading; SETTLE of 0ms ends at the next
             * pass; the reading after is IDLE's. */
            r.samples = 1u;
            r.taken_ms = 1010u;
            servo_test_reading(&t, &r, 0u);
            servo_test_step(&t, 1020u, &in, &d);
            CHECK_EQ(t.phase, SERVO_TEST_PH_IDLE);
            r.samples = 2u;
            r.taken_ms = 1030u;
            r.i = (float)(ma + over) / 1000.0f;
            servo_test_reading(&t, &r, 0u);
            if (t.stalled) {
                if (over != 0u) {
                    ++above;
                } else {
                    ++at_equal;
                }
            }
        }
    }
    CHECK_EQ(at_equal, 0u);
    CHECK_EQ(above, 99u);
}


/* A reading count that steps by half its range or more is the count
 * starting again, counted as one reading and nothing skipped; a step one
 * short of that is that many readings taken, all but one of them skipped. */
TEST_CASE(a_count_step_of_half_the_range_is_a_restart_and_one_less_is_not)
{
    static const uint16_t k_step[] = { 0x8000u, 0x7FFFu };
    for (unsigned k = 0; k < 2u; ++k) {
        rig_fresh();
        servo_test_cfg_t c;
        cfg_defaults(&c);
        c.step_count = 1u;
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        run_ms(3000u);
        const uint32_t taken   = g.t.module_samples;
        const uint32_t skipped = g.t.skipped;
        const uint32_t seen    = g.t.readings;
        /* The next reading's count is k_step[k] past the last one's. */
        g.samples = (uint16_t)(g.samples + k_step[k] - 1u);
        while (g.t.readings == seen && servo_test_running(&g.t)) {
            frame();
        }
        CHECK_EQ(g.t.readings, seen + 1u);
        const bool restart = k_step[k] >= 0x8000u;
        CHECK_EQ(g.t.module_samples - taken, restart ? 1u : k_step[k]);
        CHECK_EQ(g.t.skipped - skipped, restart ? 0u : k_step[k] - 1u);
    }
}

/*
 * A run asks for its first set point, the output on and the centre at its
 * start, and the first step hands that over.  A run that ends before that
 * step, or in it, hands over off and release and none of the three: the
 * supply is not switched on, set or the servo commanded by a run that is
 * over.
 */
TEST_CASE(a_run_ended_before_its_first_step_asks_nothing_on)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);
    servo_test_do_t d;

    /* What was pending, from a run left to take its first step. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    const servo_test_in_t armed = { true, g.v_max };
    servo_test_step(&g.t, g.now + FRAME_MS, &armed, &d);
    CHECK(servo_test_running(&g.t));
    CHECK(d.set && d.on && d.command);
    CHECK(!d.off && !d.release);

    /* Aborted between the start and the first step. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    servo_test_abort(&g.t, SERVO_TEST_AB_STOP, g.now);
    servo_test_step(&g.t, g.now + FRAME_MS, &armed, &d);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(g.t.why, SERVO_TEST_AB_STOP);
    CHECK(d.off && d.release);
    CHECK(!d.on);
    CHECK(!d.set);
    CHECK(!d.command);

    /* A bench that is disarmed when the first step comes. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    const servo_test_in_t disarmed = { false, g.v_max };
    servo_test_step(&g.t, g.now + FRAME_MS, &disarmed, &d);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(g.t.why, SERVO_TEST_AB_DISARMED);
    CHECK(d.off && d.release);
    CHECK(!d.on);
    CHECK(!d.set);
    CHECK(!d.command);
}

int main(void)
{
    RUN(an_end_held_harder_than_the_servo_moves_is_reached_settled);
    RUN(an_acceleration_spike_above_the_level_is_not_the_arrival);
    RUN(a_moving_current_equal_to_the_holding_current_is_the_limit);
    RUN(a_meters_lag_widens_the_window_and_a_stuck_move_is_late);
    RUN(a_frozen_current_is_no_reading);
    RUN(skipped_readings_are_reported);
    RUN(a_count_starting_again_is_not_readings_skipped);
    RUN(a_step_cut_short_is_said_so);
    RUN(a_start_is_refused_for_what_cannot_run);
    RUN(an_output_already_on_is_used_as_it_is);
    RUN(a_repeated_sample_is_not_a_reading);
    RUN(a_full_outbox_counts_the_rows_it_loses);
    RUN(the_csv_reads_back_in_the_viewer);
    RUN(a_reading_at_the_deadline_is_late);
    RUN(a_servo_held_at_the_current_limit_ends_the_run);
    RUN(a_reading_equal_to_a_limit_passes_it);
    RUN(a_reading_equal_to_stall_at_is_not_a_stall);
    RUN(a_count_step_of_half_the_range_is_a_restart_and_one_less_is_not);
    RUN(a_run_ended_before_its_first_step_asks_nothing_on);
    free(g.csv_text);
    return test_summary("servo_test_judge");
}
