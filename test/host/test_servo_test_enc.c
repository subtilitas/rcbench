/*
 * The automatic servo test with the output encoder.
 *
 * Under test: angles from the centre count across the wrap; a run with it
 * reporting each step's end angles, their error against the commanded
 * angle and a travel time from the start of the angle's stillness, beside
 * the current's; the same run without it unchanged, its CSV header and
 * report included; a servo that turns less than commanded showing the
 * error; one that never moves counted unmoved; an encoder that gives
 * nothing said so; a stillness that began before the command not taken
 * for its end; movement, end angles and their error taken on the circle,
 * with a horn at rest across the half turn from the centre, end angles
 * either side of it, and ends more than 180 degrees apart or from the
 * centre; a reading without a magnet counted and reported as the reason
 * for missing angles, and readings with a weak or strong field used and
 * counted.
 *
 * The rig is servo_test_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_test_rig.h"

/* ---------------------------------------------------- the output encoder */

/* A rig whose horn is read by an encoder turning @p gain degrees a us. */
static void enc_rig(float gain, servo_test_cfg_t *c)
{
    rig_fresh();
    cfg_defaults(c);
    c->step_count = 1u;
    c->moves = 3u;
    c->enc_on = true;
    c->enc_centre = ENC_CENTRE_COUNT;
    c->enc_cmd_deg[0] = -400.0f * ENC_DEG_PER_US;
    c->enc_cmd_deg[1] =  400.0f * ENC_DEG_PER_US;
    g.enc_model_on = true;
    g.enc_valid = true;
    g.enc_every = 40u;
    g.enc_deg_per_us = gain;
}

TEST_CASE(an_angle_is_counted_from_the_centre_round_the_circle)
{
    CHECK_NEAR(servo_test_enc_deg(3000u, 3000u), 0.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_deg(4096u - 1u, 0u), -0.0879f, 0.0001f);
    CHECK_NEAR(servo_test_enc_deg(10u, 4090u), 16.0f * 360.0f / 4096.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(4090u, 10u), -16.0f * 360.0f / 4096.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(1024u, 0u), 90.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(3072u, 0u), -90.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(2048u, 0u), -180.0f, 0.001f);
    /* The tolerance in degrees is 12 counts. */
    CHECK_NEAR(SERVO_TEST_ENC_TOL_DEG, 12.0f * 360.0f / 4096.0f, 0.001f);
    CHECK_EQ(SERVO_TEST_ENC_TOL_COUNTS, 12u);
}

TEST_CASE(a_reversed_profile_reads_the_encoder_in_the_commanded_direction)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    /* REVERSE on: the commanded angles are flipped, and the measured
     * angle follows, so a horn at the right end shows no error. */
    c.reverse = true;
    c.enc_cmd_deg[0] =  400.0f * ENC_DEG_PER_US;
    c.enc_cmd_deg[1] = -400.0f * ENC_DEG_PER_US;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_travels, 3u);
    CHECK(s->enc_end_off[0].n + s->enc_end_off[1].n == 3u);
    float end = 0.0f;
    if (servo_test_enc_end(&g.t, s, 0u, &end, NULL)) {
        CHECK_NEAR(end, 36.0f, 0.5f);
    }
    if (servo_test_enc_end(&g.t, s, 1u, &end, NULL)) {
        CHECK_NEAR(end, -36.0f, 0.5f);
    }
    float deg = 0.0f;
    CHECK(servo_test_enc_at(&g.t, g.now, &deg));
    CHECK(fabsf(deg) <= 36.5f);
    CHECK(strstr(g.report, "Commanded: +36.0deg at the low end, -36.0deg at the high end.") != NULL);
}

TEST_CASE(a_run_with_the_encoder_reports_the_angle_beside_the_current)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    const servo_test_step_t *s = &g.t.steps[0];
    /* The current's figures are as without the encoder. */
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->travels, 3u);
    CHECK(s->travel_max_ms >= TRAVEL_MS);
    /* And the angle's: each counted move seen, settled, none unmoved. */
    CHECK_EQ(s->enc_moves, 3u);
    CHECK_EQ(s->enc_travels, 3u);
    CHECK_EQ(s->enc_unmoved, 0u);
    CHECK_EQ(s->enc_late, 0u);
    CHECK(s->enc_end_off[0].n + s->enc_end_off[1].n == 3u);
    float lo = -36.0f, hi = 36.0f;
    (void)servo_test_enc_end(&g.t, s, 0u, &lo, NULL);
    (void)servo_test_enc_end(&g.t, s, 1u, &hi, NULL);
    CHECK_NEAR(lo, -36.0f, 0.5f);
    CHECK_NEAR(hi, 36.0f, 0.5f);
    /* The settle begins as the horn comes within the tolerance of its
     * end: up to the tolerance's worth of travel before the whole move,
     * within a frame and a reading of it. */
    const float tol_ms = SERVO_TEST_ENC_TOL_DEG / ENC_DEG_PER_US / 1.2f;
    CHECK((float)s->enc_travel_max_ms >= (float)TRAVEL_MS - tol_ms - 2.0f * FRAME_MS);
    CHECK(s->enc_travel_max_ms <= TRAVEL_MS + 100u);
    CHECK(s->enc_travel_sum_ms / s->enc_travels <= s->enc_travel_max_ms);
    CHECK(g.t.enc_reads > 100u);

    /* The report: the encoder's header line, its table and its notes. */
    CHECK(strstr(g.report, "Encoder:        AS5600 on the horn shaft, centre count 3000,") != NULL);
    CHECK(strstr(g.report, "ENCODER (angles in degrees from the centre count, times in ms)") != NULL);
    CHECK(strstr(g.report, "Set V  End lo   Err lo   End hi   Err hi   Travel Longest Moves Unmoved Late") != NULL);
    CHECK(strstr(g.report, "Commanded: -36.0deg at the low end, +36.0deg at the high end.") != NULL);
    CHECK(strstr(g.report, "Settled: the angle has stayed within 1.05deg for 100ms.") != NULL);
    CHECK(strstr(g.report, "Deadband: not measured; the moves go end to end.") != NULL);
    CHECK(strstr(g.report, "Position: the AS5600 measures the horn;") != NULL);
    CHECK(strstr(g.report, "Position: nothing measures the horn") == NULL);
    /* Side by side: the current's table is there too. */
    CHECK(strstr(g.report, "RESULTS PER STEP (currents in mA, times in ms)") != NULL);

    /* The CSV has two more columns before the meter's six, and the settle
     * on the row after it. */
    CHECK(strncmp(g.csv_text, servo_test_csv_header_enc(),
                  strlen(servo_test_csv_header_enc())) == 0);
    CHECK(strstr(servo_test_csv_header_enc(), ";travel (ms);angle (deg);travel angle (ms)") != NULL);
    unsigned rows = 0u, with_angle = 0u, with_settle = 0u;
    for (const char *p = strchr(g.csv_text, '\n'); p != NULL && *p != '\0';) {
        const char *e = strchr(p + 1, '\n');
        if (e == NULL) {
            break;
        }
        unsigned fields = 1u;
        for (const char *q = p + 1; q < e; ++q) {
            fields += (*q == ';') ? 1u : 0u;
        }
        CHECK_EQ(fields, 21u);
        ++rows;
        /* Cells 13 and 14: the angle and the settle. */
        const char *angle = csv_cell(p + 1, 13u);
        const char *settle = csv_cell(p + 1, 14u);
        CHECK(angle != NULL && settle != NULL);
        if (angle == NULL || settle == NULL) {
            break;
        }
        with_settle += (settle[1] != ';') ? 1u : 0u;
        with_angle += (angle[1] != ';') ? 1u : 0u;
        CHECK(strncmp(csv_cell(p + 1, 15u), ";PDMINI;;;;;\n", 13) == 0);
        p = e;
    }
    CHECK(rows > 40u);
    CHECK(with_angle > rows / 2u);
    /* One settle for each of the 3 counted moves and the 2 that place the
     * horn first. */
    CHECK_EQ(with_settle, 5u);
}

TEST_CASE(without_the_encoder_the_run_and_its_files_are_as_before)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    c.enc_on = false;               /* readings arrive, the run ignores them */
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK(g.enc_sent > 100u);
    CHECK_EQ(g.t.enc_reads, 0u);
    CHECK_EQ(g.t.steps[0].enc_moves, 0u);
    CHECK(strncmp(g.csv_text, servo_test_csv_header(),
                  strlen(servo_test_csv_header())) == 0);
    CHECK(strstr(g.csv_text, "angle") == NULL);
    CHECK(strstr(g.report, "ENCODER") == NULL);
    CHECK(strstr(g.report, "Encoder:") == NULL);
    CHECK(strstr(g.report, "Deadband") == NULL);
    CHECK(strstr(g.report, "Position: nothing measures the horn") != NULL);
    for (const char *p = strchr(g.csv_text, '\n'); p != NULL && *p != '\0';) {
        const char *e = strchr(p + 1, '\n');
        if (e == NULL) {
            break;
        }
        unsigned fields = 1u;
        for (const char *q = p + 1; q < e; ++q) {
            fields += (*q == ';') ? 1u : 0u;
        }
        CHECK_EQ(fields, 19u);
        p = e;
    }
}

/* A horn that turns 0.07 degrees a us where 0.09 is commanded: 28 degrees
 * at the ends, not 36. */
TEST_CASE(a_servo_that_turns_less_than_commanded_shows_the_error)
{
    servo_test_cfg_t c;
    enc_rig(0.07f, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_travels, 3u);
    CHECK(s->enc_end_off[0].n + s->enc_end_off[1].n == 3u);
    float end = 0.0f;
    if (servo_test_enc_end(&g.t, s, 0u, &end, NULL)) {
        CHECK_NEAR(end, -28.0f, 0.5f);
    }
    if (servo_test_enc_end(&g.t, s, 1u, &end, NULL)) {
        CHECK_NEAR(end, 28.0f, 0.5f);
    }
    /* Err is End less commanded: +8.00 at the low end, -8.00 at the high. */
    CHECK(strstr(g.report, "-28.0") != NULL);
    CHECK(strstr(g.report, "+28.0") != NULL);
    CHECK(strstr(g.report, "+7.9") != NULL);
    CHECK(strstr(g.report, "-7.9") != NULL);
}

TEST_CASE(a_servo_that_does_not_move_is_counted_unmoved_by_the_angle)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    g.brownout_v = 30.0f;           /* never enough voltage to move */
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->enc_moves, 3u);
    CHECK_EQ(s->enc_unmoved, 3u);
    CHECK_EQ(s->enc_travels, 0u);
    CHECK_EQ(s->enc_late, 0u);
    CHECK(strstr(g.report, "Unmoved: the angle did not leave 2.0deg of its start.") != NULL);
}

TEST_CASE(an_encoder_that_gives_nothing_is_said_so)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    g.enc_valid = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK_EQ(g.t.enc_reads, 0u);
    CHECK_EQ(g.t.steps[0].enc_moves, 0u);
    CHECK(strstr(g.report, "No angle reading reached the run.") != NULL);
    CHECK(strstr(g.report, "No magnet:") == NULL);  /* nobody said so */
    CHECK(strstr(g.report, "Field:") == NULL);
    /* The current's result stands. */
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.csv_text, ";CV;") != NULL);
}

TEST_CASE(an_invalid_reading_ends_the_angle_until_the_next)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(1000u);
    float deg = 0.0f;
    CHECK(servo_test_enc_at(&g.t, g.now, &deg));
    servo_test_enc_t none = { false, 0u, 0u, g.now, false, false, false, false };
    servo_test_encoder(&g.t, &none);
    CHECK(!servo_test_enc_at(&g.t, g.now, &deg));
    servo_test_encoder(&g.t, NULL);
    servo_test_encoder(NULL, &none);
    run_ms(100u);
    CHECK(servo_test_enc_at(&g.t, g.now, &deg));    /* the model's next reading */
}

/* Runs until a counted move has begun with a start angle and has not moved
 * yet; false when none does within 60s. */
static bool run_to_counted_move_start(void)
{
    for (uint32_t k = 0; k < 60000u; k += FRAME_MS) {
        frame();
        if (g.t.enc_open && g.t.enc_counted && g.t.enc_ok
            && !g.t.enc_moved && g.t.phase == SERVO_TEST_PH_MOVE) {
            return true;
        }
    }
    return false;
}

/* A gap in the angle after a move's valid start leaves the move unmeasured:
 * not moved, not late, not counted, however the angle goes on afterwards. */
TEST_CASE(a_gap_in_the_angle_leaves_the_open_move_unmeasured)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    const servo_test_enc_t none = { false, 0u, 0u, g.now, false, false, false, false };
    servo_test_encoder(&g.t, &none);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->enc_moves, 2u);
    CHECK_EQ(s->enc_travels, 2u);
    CHECK_EQ(s->enc_unmoved, 0u);
    CHECK_EQ(s->enc_late, 0u);
}

/* A reading that follows lost ones -- the queue to the screen overflowed
 * with the only "none" in it -- ends the validity as a "none" does: the open
 * move is unmeasured, and the reading itself is kept. */
TEST_CASE(a_reading_after_lost_ones_leaves_the_open_move_unmeasured)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    const unsigned held = g.t.enc_hist_n;
    CHECK(held > 0u);
    const servo_test_enc_t after = { true, 100u, 0u, g.now, true, false, false, false };
    servo_test_encoder(&g.t, &after);
    CHECK(!g.t.enc_ok);
    CHECK_EQ(g.t.enc_hist_n, 1u);               /* the earlier ones are gone */
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->enc_moves, 2u);
    CHECK_EQ(s->enc_unmoved, 0u);
    CHECK_EQ(s->enc_late, 0u);
}

/* A move that had settled before the gap keeps its result. */
TEST_CASE(a_gap_after_the_move_settled_keeps_its_result)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    for (uint32_t k = 0; k < 10000u && !g.t.enc_settled; k += FRAME_MS) {
        frame();
    }
    CHECK(g.t.enc_settled);
    const servo_test_enc_t none = { false, 0u, 0u, g.now, false, false, false, false };
    servo_test_encoder(&g.t, &none);
    run_out(120000u);
    CHECK_EQ(g.t.steps[0].enc_moves, 3u);
    CHECK_EQ(g.t.steps[0].enc_travels, 3u);
}

/* An abort cuts the open move's window short: it is not counted unmoved or
 * late, as the current-based results do not count it either. */
TEST_CASE(an_abort_does_not_count_the_move_it_cut_short)
{
    static const servo_test_abort_t why[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_DISARMED, SERVO_TEST_AB_LINK,
        SERVO_TEST_AB_SUPPLY_LOST,
    };
    for (unsigned k = 0; k < sizeof why / sizeof why[0]; ++k) {
        servo_test_cfg_t c;
        enc_rig(ENC_DEG_PER_US, &c);
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        CHECK(run_to_counted_move_start());
        const uint16_t before = g.t.steps[0].enc_moves;
        servo_test_abort(&g.t, why[k], g.now);
        CHECK(!servo_test_running(&g.t));
        CHECK_EQ(g.t.steps[0].enc_moves, before);
        CHECK_EQ(g.t.steps[0].enc_unmoved, 0u);
        CHECK_EQ(g.t.steps[0].enc_late, 0u);
    }
}

/* A move whose angle had settled is a finished measurement: an abort in its
 * hold still counts it. */
TEST_CASE(an_abort_after_the_angle_settled_counts_the_move)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    for (uint32_t k = 0; k < 10000u && !g.t.enc_settled; k += FRAME_MS) {
        frame();
    }
    CHECK(g.t.enc_settled);
    const uint16_t before = g.t.steps[0].enc_moves;
    servo_test_abort(&g.t, SERVO_TEST_AB_STOP, g.now);
    CHECK_EQ(g.t.steps[0].enc_moves, before + 1u);
    CHECK_EQ(g.t.steps[0].enc_travels, 1u);
}

/* The panel drains the encoder's readings before the supply's, so a row can
 * be older than the newest reading.  The row takes the newest reading not
 * later than itself. */
TEST_CASE(a_row_takes_the_newest_angle_not_later_than_itself)
{
    servo_test_t t;
    servo_test_init(&t);
    t.cfg.enc_centre = 0u;
    float deg = 0.0f;
    CHECK(!servo_test_enc_at(&t, 1000u, &deg));            /* none yet */
    for (unsigned k = 0; k < 5u; ++k) {                    /* 1000 ... 1160 */
        const servo_test_enc_t e = { true, (uint16_t)(100u * (k + 1u)), 0u,
                                     1000u + 40u * k, false, false, false, false };
        servo_test_encoder(&t, &e);
    }
    /* A row at 1100: the reading of 1080, not the newer ones. */
    CHECK(servo_test_enc_at(&t, 1100u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(300u, 0u), 0.0001f);
    /* At a reading's own time: that reading. */
    CHECK(servo_test_enc_at(&t, 1120u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(400u, 0u), 0.0001f);
    /* Before the oldest: none, and not the newest by an unsigned wrap. */
    CHECK(!servo_test_enc_at(&t, 999u, &deg));
    CHECK(!servo_test_enc_at(&t, 0u, &deg));
    /* Older than SERVO_TEST_ENC_STALE_MS to the newest not later: none. */
    CHECK(servo_test_enc_at(&t, 1160u + SERVO_TEST_ENC_STALE_MS, &deg));
    CHECK(!servo_test_enc_at(&t, 1160u + SERVO_TEST_ENC_STALE_MS + 1u, &deg));
    /* The centre is the one in force when the row is matched. */
    t.cfg.enc_centre = 500u;
    CHECK(servo_test_enc_at(&t, 1160u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(500u, 500u), 0.0001f);
    /* The ring keeps the last SERVO_TEST_ENC_HIST readings. */
    for (unsigned k = 0; k < 40u; ++k) {
        const servo_test_enc_t e = { true, (uint16_t)(2000u + k), 0u,
                                     2000u + 40u * k, false, false, false, false };
        servo_test_encoder(&t, &e);
    }
    CHECK_EQ(t.enc_hist_n, SERVO_TEST_ENC_HIST);
    CHECK(servo_test_enc_at(&t, 2000u + 40u * 39u + 10u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(2039u, 500u), 0.0001f);
    /* The oldest one kept is 15 readings back; one older is gone. */
    CHECK(servo_test_enc_at(&t, 2000u + 40u * 24u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(2024u, 500u), 0.0001f);
    CHECK(!servo_test_enc_at(&t, 2000u + 40u * 24u - 1u, &deg)
          || fabsf(deg - servo_test_enc_deg(2024u, 500u)) > 0.0001f);
    /* Across the 32-bit wrap of the clock. */
    servo_test_init(&t);
    const servo_test_enc_t w = { true, 700u, 0u, 0xFFFFFFF0u, false, false, false, false };
    servo_test_encoder(&t, &w);
    CHECK(servo_test_enc_at(&t, 20u, &deg));               /* 36ms later */
    CHECK_NEAR(deg, servo_test_enc_deg(700u, 0u), 0.0001f);
    CHECK(!servo_test_enc_at(&t, 0xFFFFFFE0u, &deg));      /* before it */
}

TEST_CASE(rows_older_than_the_encoders_readings_still_get_their_angle)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    c.moves = 1u;
    g.enc_every = 20u;
    g.row_lag_ms = 30u;             /* every row older than the reading before it */
    g.check_rows = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    unsigned rows = 0u, with_angle = 0u;
    for (const char *p = strchr(g.csv_text, '\n'); p != NULL && *p != '\0';) {
        const char *e = strchr(p + 1, '\n');
        if (e == NULL) {
            break;
        }
        const char *angle = csv_cell(p + 1, 13u);
        CHECK(angle != NULL);
        if (angle == NULL) {
            break;
        }
        ++rows;
        with_angle += (angle[1] != ';') ? 1u : 0u;
        p = e;
    }
    CHECK(rows > 40u);
    /* Rows have an angle from the first reading on; the few before it do
     * not. */
    CHECK(with_angle + 8u >= rows);
    /* The settles are logged, each once, none on a row older than the
     * reading that found it. */
    CHECK_EQ(g.settle_rows, 3u);
}

/* Stillness that began before the command is the servo at rest, not the
 * move's end: a horn that starts late shows no settle until it has moved
 * and stopped again. */
TEST_CASE(a_stillness_from_before_the_command_is_not_the_end_of_the_move)
{
    servo_test_cfg_t c;
    memset(&c, 0, sizeof(c));
    c.enc_on = true;
    c.enc_centre = 0u;
    servo_test_t t;
    servo_test_init(&t);
    t.cfg = c;
    t.state = SERVO_TEST_RUNNING;
    t.step_count = 1u;
    t.enc_open = true;
    t.enc_counted = true;
    t.enc_ok = true;
    t.enc_end = 1u;
    t.enc_cmd_ms = 10000u;
    t.enc_start_raw = 0u;
    /* Moved 5 degrees, still for 200ms, the stillness older than the
     * command: not settled. */
    servo_test_enc_t e = { true, (uint16_t)(5.0f * 4096.0f / 360.0f), 200u, 10100u, false, false, false, false };
    servo_test_encoder(&t, &e);
    CHECK(t.enc_moved);
    CHECK(!t.enc_settled);
    /* Still for 100ms from 10050: after the command, settled at 50ms. */
    e.taken_ms = 10150u;
    e.still_ms = 100u;
    servo_test_encoder(&t, &e);
    CHECK(t.enc_settled);
    CHECK_EQ(t.enc_travel_ms, 50u);
    CHECK_EQ(t.enc_travel_now_ms, 50u);
    /* Settled once: a later reading does not move it. */
    e.taken_ms = 10300u;
    e.still_ms = 250u;
    servo_test_encoder(&t, &e);
    CHECK_EQ(t.enc_travel_ms, 50u);
    /* Less than SERVO_TEST_ENC_HOLD_MS still is not settled. */
    servo_test_t u = t;
    u.enc_settled = false;
    e.taken_ms = 10400u;
    e.still_ms = SERVO_TEST_ENC_HOLD_MS - 1u;
    servo_test_encoder(&u, &e);
    CHECK(!u.enc_settled);
    /* A reading from before the command is not the move. */
    servo_test_t w = t;
    w.enc_settled = false;
    w.enc_moved = false;
    e.taken_ms = 9000u;
    e.still_ms = 500u;
    servo_test_encoder(&w, &e);
    CHECK(!w.enc_moved);
}

TEST_CASE(a_start_angle_older_than_half_a_second_leaves_the_move_unjudged)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    g.enc_every = 2000u;            /* a reading every 2s: too old to start from */
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK(s->enc_moves < 3u);
}

/* ------------------------------------------------ the angle on the circle */

/* The first step's row of the report's ENCODER table: the end angles and
 * their errors.  False when the row is missing or has a "--" in it. */
static bool enc_row(float *lo, float *elo, float *hi, float *ehi)
{
    const char *p = strstr(g.report, "Set V  End lo");
    if (p == NULL || (p = strchr(p, '\n')) == NULL) {
        return false;
    }
    float v = 0.0f;
    return sscanf(p + 1, "%f %f %f %f %f", &v, lo, elo, hi, ehi) == 5;
}

/* A horn that holds still with the centre a half turn away: its count
 * flutters by 2, 2047 and 2049 from the centre, +179.9 and -179.9 degrees.
 * That is 0.18 degrees of movement, not 359.8: every move is unmoved. */
TEST_CASE(two_counts_across_the_half_turn_are_not_a_move)
{
    servo_test_cfg_t c;
    enc_rig(0.0f, &c);
    c.enc_centre = (uint16_t)(ENC_CENTRE_COUNT - 2048u);
    g.enc_flutter = 1;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    /* The rig did read either side of the half turn. */
    CHECK_NEAR(servo_test_enc_deg(ENC_CENTRE_COUNT - 1u, c.enc_centre),
               179.91f, 0.01f);
    CHECK_NEAR(servo_test_enc_deg(ENC_CENTRE_COUNT + 1u, c.enc_centre),
               -179.91f, 0.01f);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_moves, 3u);
    CHECK_EQ(s->enc_unmoved, 3u);
    CHECK_EQ(s->enc_late, 0u);
    CHECK_EQ(s->enc_travels, 0u);
}

/* The high end lies a half turn from the centre and settles 2 counts short
 * of it after one move and 2 counts past it after the next: +179.8 and
 * -179.8 degrees.  Their mean is the half turn, not 0, and against a
 * commanded 180 degrees the error is about 0, not 180. */
TEST_CASE(end_angles_either_side_of_the_half_turn_average_on_the_circle)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    c.moves = 4u;
    const uint16_t hi_count = (uint16_t)(ENC_CENTRE_COUNT + 410u);  /* +36deg */
    c.enc_centre = (uint16_t)(hi_count - 2048u);
    c.enc_cmd_deg[0] = 108.0f;
    c.enc_cmd_deg[1] = 180.0f;
    g.enc_by_move = 2;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_travels, 4u);
    /* The rig did leave the high end on both sides of the half turn. */
    bool below = false, above = false;
    for (unsigned k = 0u; k < 16u && k < g.cmds; ++k) {
        const float d = servo_test_enc_deg(g.enc_at_cmd[k], c.enc_centre);
        below = below || (d > 179.0f);
        above = above || (d < -179.0f);
    }
    CHECK(below && above);
    float lo = 0.0f, elo = 0.0f, hi = 0.0f, ehi = 0.0f;
    CHECK(enc_row(&lo, &elo, &hi, &ehi));
    CHECK(fabsf(hi) > 179.5f);
    CHECK(fabsf(ehi) < 0.5f);
    CHECK_NEAR(lo, 108.0f, 0.5f);
    CHECK(fabsf(elo) < 0.5f);
}

/* A servo that turns 200 degrees between its ends.  With the centre in the
 * middle each end is within 180 degrees of it and reads as it is, -100 and
 * +100.  With the centre at the low end the high end is 200 degrees from
 * it and reads 360 less, -160; the error against a commanded 200 is still
 * about 0, taken on the circle. */
TEST_CASE(ends_more_than_180_degrees_apart_are_reported_within_one_turn)
{
    servo_test_cfg_t c;
    enc_rig(0.25f, &c);
    c.moves = 4u;
    c.enc_cmd_deg[0] = -100.0f;
    c.enc_cmd_deg[1] =  100.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_moves, 4u);
    CHECK_EQ(s->enc_travels, 4u);
    CHECK_EQ(s->enc_unmoved, 0u);
    float lo = 0.0f, elo = 0.0f, hi = 0.0f, ehi = 0.0f;
    CHECK(enc_row(&lo, &elo, &hi, &ehi));
    CHECK_NEAR(lo, -100.0f, 0.5f);
    CHECK_NEAR(hi, 100.0f, 0.5f);
    CHECK(fabsf(elo) < 0.5f && fabsf(ehi) < 0.5f);

    enc_rig(0.25f, &c);
    c.moves = 4u;
    /* 100 degrees are 1138 counts: the centre at the low end. */
    c.enc_centre = (uint16_t)(ENC_CENTRE_COUNT - 1138u);
    c.enc_cmd_deg[0] = 0.0f;
    c.enc_cmd_deg[1] = 200.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    s = &g.t.steps[0];
    CHECK_EQ(s->enc_travels, 4u);
    CHECK_EQ(s->enc_unmoved, 0u);
    CHECK(enc_row(&lo, &elo, &hi, &ehi));
    CHECK_NEAR(lo, 0.0f, 0.5f);
    CHECK_NEAR(hi, -160.0f, 0.5f);
    CHECK(fabsf(elo) < 0.5f && fabsf(ehi) < 0.5f);
    CHECK(strstr(g.report, "End: the settled angle, -180 to +180deg from the "
                           "centre count. Err: End minus commanded, the "
                           "shortest way round.") != NULL);
}

TEST_CASE(a_steps_end_angle_is_a_mean_taken_from_its_first)
{
    CHECK_NEAR(servo_test_enc_wrap(0.0f), 0.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_wrap(179.9f), 179.9f, 0.0001f);
    CHECK_NEAR(servo_test_enc_wrap(180.0f), -180.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_wrap(-180.0f), -180.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_wrap(190.0f), -170.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_wrap(-190.0f), 170.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_wrap(-360.0f), 0.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_wrap(540.0f), -180.0f, 0.0001f);

    servo_test_t t;
    servo_test_init(&t);
    t.cfg.enc_centre = 0u;
    t.cfg.enc_cmd_deg[0] = -170.0f;
    t.cfg.enc_cmd_deg[1] = 170.0f;
    servo_test_step_t s;
    memset(&s, 0, sizeof(s));
    float deg = 1.0f, err = 1.0f;
    CHECK(!servo_test_enc_end(&t, &s, 1u, &deg, &err));    /* none settled */
    CHECK_NEAR(deg, 1.0f, 0.0001f);
    /* The first at 2040 counts (+179.30 degrees), three offsets from it:
     * 0, +16 and +8 counts, the mean 8 counts on: 2048, the half turn. */
    s.enc_end_ref[1] = 2040u;
    s.enc_end_off[1].n = 3u;
    s.enc_end_off[1].sum = 24.0f * 360.0f / 4096.0f;
    CHECK(servo_test_enc_end(&t, &s, 1u, &deg, &err));
    CHECK_NEAR(deg, -180.0f, 0.001f);
    CHECK_NEAR(err, 10.0f, 0.001f);        /* 180 less 170, not -350 */
    CHECK(servo_test_enc_end(&t, &s, 1u, NULL, NULL));
    /* REVERSE on negates the angle; the half turn stays -180. */
    t.cfg.reverse = true;
    s.enc_end_off[1].sum = 0.0f;
    CHECK(servo_test_enc_end(&t, &s, 1u, &deg, &err));
    CHECK_NEAR(deg, -179.30f, 0.01f);
    CHECK_NEAR(err, 10.70f, 0.01f);        /* -179.30 less 170, wrapped */
    CHECK(!servo_test_enc_end(&t, &s, 0u, &deg, &err));
    CHECK(!servo_test_enc_end(&t, &s, 2u, &deg, &err));
    CHECK(!servo_test_enc_end(NULL, &s, 1u, &deg, &err));
    CHECK(!servo_test_enc_end(&t, NULL, 1u, &deg, &err));
}

/* ------------------------------------------------------------ the magnet */

/* The sensor answers and sees no magnet: the reading is not valid and says
 * why.  The angle ends as with any invalid reading -- the open move is not
 * counted -- and the report gives the reason. */
TEST_CASE(a_reading_without_a_magnet_is_no_angle_and_the_report_says_why)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    servo_test_enc_t none = { 0 };
    none.no_magnet = true;
    none.raw       = 1234u;             /* whatever the part read */
    none.taken_ms  = g.now;
    servo_test_encoder(&g.t, &none);
    float deg = 0.0f;
    CHECK(!servo_test_enc_at(&g.t, g.now, &deg));
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(g.t.enc_no_magnet, 1u);
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->enc_moves, 2u);
    CHECK_EQ(s->enc_travels, 2u);
    CHECK(strstr(g.report, "No magnet: the AS5600 reported none 1 time(s). "
                           "No angle is logged then, and an unsettled move "
                           "is not counted.") != NULL);
    CHECK(strstr(g.report, "No angle reading reached the run.") == NULL);
    CHECK(strstr(g.report, "Field:") == NULL);

    /* No magnet for the whole run: no reading reached it, and why. */
    enc_rig(ENC_DEG_PER_US, &c);
    g.enc_valid = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    none.taken_ms = g.now;
    servo_test_encoder(&g.t, &none);
    run_out(120000u);
    CHECK_EQ(g.t.enc_reads, 0u);
    CHECK_EQ(g.t.steps[0].enc_moves, 0u);
    CHECK(strstr(g.report, "No angle reading reached the run.") != NULL);
    CHECK(strstr(g.report, "No magnet: the AS5600 reported none 1 time(s).")
          != NULL);
    /* Outside a run nothing is counted. */
    servo_test_t idle;
    servo_test_init(&idle);
    servo_test_encoder(&idle, &none);
    CHECK_EQ(idle.enc_no_magnet, 0u);
}

/* The field too weak with the magnet detected: the angles are positions,
 * the run uses them as any other, and the report says how many there
 * were. */
TEST_CASE(readings_with_a_weak_field_are_used_and_counted)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    g.enc_weak = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_moves, 3u);
    CHECK_EQ(s->enc_travels, 3u);
    CHECK(g.t.enc_reads > 100u);
    CHECK_EQ(g.t.enc_weak, g.t.enc_reads);
    CHECK_EQ(g.t.enc_strong, 0u);
    char want[96];
    snprintf(want, sizeof(want), "Field: %lu reading(s) with the magnet too "
             "weak, 0 too strong. The angle is used;",
             (unsigned long)g.t.enc_reads);
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, "No magnet:") == NULL);
    /* Too strong is counted beside it. */
    servo_test_enc_t e = { 0 };
    e.valid = true;
    e.strong = true;
    g.t.state = SERVO_TEST_RUNNING;
    servo_test_encoder(&g.t, &e);
    CHECK_EQ(g.t.enc_strong, 1u);
}

int main(void)
{
    RUN(an_angle_is_counted_from_the_centre_round_the_circle);
    RUN(a_reversed_profile_reads_the_encoder_in_the_commanded_direction);
    RUN(a_run_with_the_encoder_reports_the_angle_beside_the_current);
    RUN(without_the_encoder_the_run_and_its_files_are_as_before);
    RUN(a_servo_that_turns_less_than_commanded_shows_the_error);
    RUN(a_servo_that_does_not_move_is_counted_unmoved_by_the_angle);
    RUN(an_encoder_that_gives_nothing_is_said_so);
    RUN(an_invalid_reading_ends_the_angle_until_the_next);
    RUN(a_gap_in_the_angle_leaves_the_open_move_unmeasured);
    RUN(a_gap_after_the_move_settled_keeps_its_result);
    RUN(an_abort_does_not_count_the_move_it_cut_short);
    RUN(an_abort_after_the_angle_settled_counts_the_move);
    RUN(a_row_takes_the_newest_angle_not_later_than_itself);
    RUN(rows_older_than_the_encoders_readings_still_get_their_angle);
    RUN(a_reading_after_lost_ones_leaves_the_open_move_unmeasured);
    RUN(a_stillness_from_before_the_command_is_not_the_end_of_the_move);
    RUN(two_counts_across_the_half_turn_are_not_a_move);
    RUN(end_angles_either_side_of_the_half_turn_average_on_the_circle);
    RUN(ends_more_than_180_degrees_apart_are_reported_within_one_turn);
    RUN(a_steps_end_angle_is_a_mean_taken_from_its_first);
    RUN(a_reading_without_a_magnet_is_no_angle_and_the_report_says_why);
    RUN(readings_with_a_weak_field_are_used_and_counted);
    RUN(a_start_angle_older_than_half_a_second_leaves_the_move_unjudged);
    free(g.csv_text);
    return test_summary("servo_test_enc");
}
