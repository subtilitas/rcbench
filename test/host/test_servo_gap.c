/*
 * A measurement sensor's dropouts during a run (servo_gap.h), and the
 * automatic test riding through the output encoder's.
 *
 * Under test: a dropout of 300ms rides and one of 301ms ends the run, both
 * when the sensor answers again and against the clock; a second dropout
 * 9999ms after the start of the one before ends the run and one 10000ms
 * after rides; the clock across its wrap; the automatic test with the
 * AS5600 in use riding through a gap of 300ms and ending as an error at
 * 301ms, at its start, before its end, during a move whose angle had not
 * settled, and on a second gap within 10s; the gap in the CSV's angle gap
 * column and in the report, in EN and DE; a part that answers without its
 * magnet ending a gap; a run without the encoder taking none of it; a run
 * with the IMU as the angle source naming the IMU in its two reasons.
 *
 * The rig is servo_test_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_test_rig.h"

#include "ui_text.h"

/* ---------------------------------------------------------- the rule */

TEST_CASE(a_gap_of_300ms_rides_and_301ms_ends)
{
    for (uint32_t len = 299u; len <= 302u; ++len) {
        servo_gap_t gp;
        servo_gap_init(&gp);
        CHECK_EQ(servo_gap_lost(&gp, 1000u), SERVO_GAP_RIDES);
        /* A second "lost" while open changes nothing. */
        CHECK_EQ(servo_gap_lost(&gp, 1100u), SERVO_GAP_RIDES);
        CHECK_EQ(gp.from_ms, 1000u);
        if (len >= SERVO_GAP_MAX_MS) {
            CHECK_EQ(servo_gap_check(&gp, 1000u + SERVO_GAP_MAX_MS),
                     SERVO_GAP_RIDES);
        }
        const servo_gap_verdict_t by_clock = servo_gap_check(&gp, 1000u + len);
        CHECK_EQ(by_clock, (len > SERVO_GAP_MAX_MS) ? SERVO_GAP_TOO_LONG
                                                    : SERVO_GAP_RIDES);
        CHECK_EQ(servo_gap_back(&gp, 1000u + len),
                 (len > SERVO_GAP_MAX_MS) ? SERVO_GAP_TOO_LONG
                                          : SERVO_GAP_RIDES);
        CHECK(!gp.open);
        CHECK_EQ(gp.count, 1u);
        CHECK_EQ(gp.longest_ms, len);
        /* Closed: nothing more to judge. */
        CHECK_EQ(servo_gap_back(&gp, 9000u), SERVO_GAP_RIDES);
        CHECK_EQ(servo_gap_check(&gp, 9000u), SERVO_GAP_RIDES);
    }
    CHECK_EQ(SERVO_GAP_MAX_MS, 300u);
    CHECK_EQ(SERVO_GAP_EVERY_MS, 10000u);
}

TEST_CASE(a_second_gap_9999ms_after_the_first_ends_and_10000ms_rides)
{
    for (uint32_t apart = 9998u; apart <= 10001u; ++apart) {
        servo_gap_t gp;
        servo_gap_init(&gp);
        CHECK_EQ(servo_gap_lost(&gp, 500u), SERVO_GAP_RIDES);
        CHECK_EQ(servo_gap_back(&gp, 600u), SERVO_GAP_RIDES);
        CHECK_EQ(servo_gap_lost(&gp, 500u + apart),
                 (apart < SERVO_GAP_EVERY_MS) ? SERVO_GAP_TOO_SOON
                                              : SERVO_GAP_RIDES);
        CHECK_EQ(gp.count, 2u);
    }
    /* From the start of the one before, not from its end: a gap of 300ms
     * and the next 9800ms after its end are 10100ms apart. */
    servo_gap_t gp;
    servo_gap_init(&gp);
    (void)servo_gap_lost(&gp, 0u);
    CHECK_EQ(servo_gap_back(&gp, 300u), SERVO_GAP_RIDES);
    CHECK_EQ(servo_gap_lost(&gp, 9999u), SERVO_GAP_TOO_SOON);
}

TEST_CASE(the_rule_holds_across_the_clocks_wrap)
{
    servo_gap_t gp;
    servo_gap_init(&gp);
    const uint32_t t = 0xFFFFFF00u;
    CHECK_EQ(servo_gap_lost(&gp, t), SERVO_GAP_RIDES);
    CHECK_EQ(servo_gap_check(&gp, t + 300u), SERVO_GAP_RIDES);
    CHECK_EQ(servo_gap_check(&gp, t + 301u), SERVO_GAP_TOO_LONG);
    CHECK_EQ(servo_gap_back(&gp, t + 200u), SERVO_GAP_RIDES);
    CHECK_EQ(servo_gap_lost(&gp, t + 9999u), SERVO_GAP_TOO_SOON);
    servo_gap_init(&gp);
    (void)servo_gap_lost(&gp, t);
    (void)servo_gap_back(&gp, t + 10u);
    CHECK_EQ(servo_gap_lost(&gp, t + 10000u), SERVO_GAP_RIDES);
    /* An answer stamped before the dropout is a dropout of 0ms. */
    servo_gap_init(&gp);
    (void)servo_gap_lost(&gp, 5000u);
    CHECK_EQ(servo_gap_back(&gp, 4990u), SERVO_GAP_RIDES);
    CHECK_EQ(gp.longest_ms, 0u);
    /* NULL is no dropout. */
    servo_gap_init(NULL);
    CHECK_EQ(servo_gap_lost(NULL, 0u), SERVO_GAP_RIDES);
    CHECK_EQ(servo_gap_back(NULL, 0u), SERVO_GAP_RIDES);
    CHECK_EQ(servo_gap_check(NULL, 0u), SERVO_GAP_RIDES);
}

/* ------------------------------------------- the automatic test's encoder */

/* A run with the encoder in use, its horn read every 40ms. */
static void gap_rig(servo_test_cfg_t *c, uint16_t moves)
{
    rig_fresh();
    cfg_defaults(c);
    c->step_count = 1u;
    c->moves = moves;
    c->enc_on = true;
    c->enc_centre = ENC_CENTRE_COUNT;
    c->enc_cmd_deg[0] = -400.0f * ENC_DEG_PER_US;
    c->enc_cmd_deg[1] =  400.0f * ENC_DEG_PER_US;
    g.enc_model_on = true;
    g.enc_valid = true;
    g.enc_every = 40u;
    g.enc_deg_per_us = ENC_DEG_PER_US;
}

/* The part stops answering at @p at: "none" once, then no reading. */
static void enc_lost(uint32_t at)
{
    g.enc_valid = false;
    const servo_test_enc_t none = { .valid = false, .taken_ms = at };
    servo_test_encoder(&g.t, &none);
}

/* The part answers again at @p at, and every 40ms from there. */
static void enc_back(uint32_t at)
{
    const servo_test_enc_t e = { .valid = true, .raw = ENC_CENTRE_COUNT,
                                 .taken_ms = at };
    servo_test_encoder(&g.t, &e);
    g.enc_valid = true;
    g.enc_next  = at + 40u;
}

/* Frames up to and including @p t. */
static void run_until(uint32_t t)
{
    while ((int32_t)(t - g.now) > 0 && servo_test_running(&g.t)) {
        frame();
    }
}

/* The CSV's angle gap cell of each row holding one, summed, and the
 * number of such rows. */
static unsigned gap_cells(unsigned long *sum)
{
    unsigned rows = 0u;
    *sum = 0u;
    const char *p = (g.csv_text != NULL) ? strchr(g.csv_text, '\n') : NULL;
    while (p != NULL && p[1] != '\0') {
        const char *cell = p + 1;
        for (int k = 0; k < 15 && cell != NULL; ++k) {
            cell = strchr(cell, ';');
            cell = (cell != NULL) ? cell + 1 : NULL;
        }
        if (cell != NULL && *cell != ';') {
            *sum += strtoul(cell, NULL, 10);
            ++rows;
        }
        p = strchr(p + 1, '\n');
    }
    return rows;
}

TEST_CASE(the_test_rides_through_an_encoder_gap_of_300ms)
{
    servo_test_cfg_t c;
    gap_rig(&c, 3u);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(3000u);
    const uint32_t t = g.now;
    enc_lost(t);
    run_until(t + 300u);
    CHECK(servo_test_running(&g.t));
    enc_back(t + 300u);
    CHECK(servo_test_running(&g.t));
    run_out(120000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK_EQ(g.t.enc_gap.count, 1u);
    CHECK_EQ(g.t.enc_gap.longest_ms, 300u);
    unsigned long sum = 0u;
    CHECK_EQ(gap_cells(&sum), 1u);
    CHECK_EQ(sum, 300u);
    CHECK(strstr(g.report, "AS5600 gaps: 1, the longest 300ms. A run goes on "
                           "through one of up to 300ms each 10s; no angle "
                           "is judged in it.") != NULL);
}

TEST_CASE(an_encoder_gap_of_301ms_ends_the_test_as_an_error)
{
    for (int by_clock = 0; by_clock < 2; ++by_clock) {
        servo_test_cfg_t c;
        gap_rig(&c, 3u);
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        run_ms(3000u);
        const uint32_t t = g.now;
        enc_lost(t);
        run_until(t + 300u);
        CHECK(servo_test_running(&g.t));
        if (by_clock) {
            /* Never answers: the next frame, 320ms in, ends it. */
            frame();
        } else {
            enc_back(t + 301u);
            frame();
        }
        CHECK(!servo_test_running(&g.t));
        CHECK_EQ(g.t.why, SERVO_TEST_AB_ENC_SILENT);
        CHECK(servo_test_abort_error(g.t.why));
        CHECK(g.off_asked);
        CHECK(g.released);
        CHECK_EQ(g.t.enc_gap.longest_ms, by_clock ? 320u : 301u);
        run_out(10000u);
        CHECK(strstr(g.report, "AS5600 not answering") != NULL);
        CHECK(strstr(g.report, by_clock ? "AS5600 gaps: 1, the longest 320ms."
                                        : "AS5600 gaps: 1, the longest 301ms.")
              != NULL);
    }
}

TEST_CASE(a_second_encoder_gap_within_10s_ends_the_test)
{
    for (uint32_t apart = 9999u; apart <= 10000u; ++apart) {
        servo_test_cfg_t c;
        gap_rig(&c, 40u);
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        run_ms(2000u);
        const uint32_t t = g.now;
        enc_lost(t);
        run_until(t + 100u);
        enc_back(t + 100u);
        run_until(t + apart - 5u);
        CHECK(servo_test_running(&g.t));
        enc_lost(t + apart);
        if (apart < SERVO_GAP_EVERY_MS) {
            CHECK(!servo_test_running(&g.t));
            CHECK_EQ(g.t.why, SERVO_TEST_AB_ENC_GAPS);
            CHECK(servo_test_abort_error(g.t.why));
            frame();
            CHECK(g.off_asked);
        } else {
            CHECK(servo_test_running(&g.t));
            run_until(t + apart + 50u);
            enc_back(t + apart + 50u);
            run_ms(1000u);
            CHECK(servo_test_running(&g.t));
            CHECK_EQ(g.t.enc_gap.count, 2u);
        }
    }
}

/* With the IMU as the angle source the same three ends name the IMU: a
 * gap of 301ms closed by a reading, one past 300ms found by the clock,
 * and a second gap 9999ms after the first; the AS5600's words are not in
 * the report. */
TEST_CASE(an_imu_run_ends_on_its_gaps_with_the_imus_reasons)
{
    for (int how = 0; how < 3; ++how) {
        servo_test_cfg_t c;
        gap_rig(&c, 40u);
        c.enc_imu = true;
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        run_ms(2000u);
        const uint32_t t = g.now;
        enc_lost(t);
        if (how == 2) {
            run_until(t + 100u);
            enc_back(t + 100u);
            run_until(t + 9994u);
            CHECK(servo_test_running(&g.t));
            enc_lost(t + 9999u);
        } else {
            run_until(t + 300u);
            CHECK(servo_test_running(&g.t));
            if (how == 1) {
                enc_back(t + 301u);
            }
            frame();
        }
        CHECK(!servo_test_running(&g.t));
        CHECK_EQ(g.t.why, how == 2 ? SERVO_TEST_AB_IMU_GAPS
                                   : SERVO_TEST_AB_IMU_SILENT);
        CHECK(servo_test_abort_error(g.t.why));
        run_out(10000u);
        CHECK(g.off_asked);
        CHECK(g.released);
        CHECK(strstr(g.report, how == 2 ? "IMU dropped out twice in 10s"
                                        : "IMU not answering") != NULL);
        CHECK(strstr(g.report, "AS5600 not answering") == NULL);
        CHECK(strstr(g.report, "AS5600 dropped out") == NULL);
    }
    CHECK_EQ(servo_test_angle_abort(false, false), SERVO_TEST_AB_ENC_SILENT);
    CHECK_EQ(servo_test_angle_abort(false, true), SERVO_TEST_AB_ENC_GAPS);
    CHECK_EQ(servo_test_angle_abort(true, false), SERVO_TEST_AB_IMU_SILENT);
    CHECK_EQ(servo_test_angle_abort(true, true), SERVO_TEST_AB_IMU_GAPS);
}

/* A run that starts while the encoder does not answer starts in a gap:
 * an angle within 300ms rides, none ends the run at 301ms or after. */
TEST_CASE(a_test_started_in_an_encoder_gap_rides_300ms_of_it)
{
    for (int answers = 0; answers < 2; ++answers) {
        servo_test_cfg_t c;
        gap_rig(&c, 3u);
        g.enc_valid = false;
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        const uint32_t t = g.now;
        enc_lost(t);                    /* the screen's, at the start */
        run_until(t + 300u);
        CHECK(servo_test_running(&g.t));
        if (answers) {
            enc_back(t + 300u);
            run_out(120000u);
            CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
        } else {
            frame();
            CHECK(!servo_test_running(&g.t));
            CHECK_EQ(g.t.why, SERVO_TEST_AB_ENC_SILENT);
        }
    }
}

/* A gap open across a counted move that had not settled: the move is left
 * out of the angle's counts, neither unmoved nor late, and the current's
 * figures stand. */
TEST_CASE(a_gap_during_a_move_leaves_it_out_and_judges_nothing)
{
    servo_test_cfg_t c;
    gap_rig(&c, 3u);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    bool found = false;
    for (uint32_t k = 0; k < 60000u && !found; k += FRAME_MS) {
        frame();
        found = g.t.enc_open && g.t.enc_counted && g.t.enc_ok
                && !g.t.enc_moved && g.t.phase == SERVO_TEST_PH_MOVE;
    }
    CHECK(found);
    const uint32_t t = g.now;
    enc_lost(t);
    run_until(t + 280u);
    /* Arrived by the current while the angle is not read. */
    enc_back(t + 280u);
    run_out(120000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->travels, 3u);
    CHECK_EQ(s->timeouts, 0u);
    CHECK_EQ(s->enc_moves, 2u);
    CHECK_EQ(s->enc_unmoved, 0u);
    CHECK_EQ(s->enc_late, 0u);
    CHECK_EQ(g.t.enc_gap.count, 1u);
}

/* A gap that opens just before the run's own end: the run ends as it
 * would, PASS, and the open gap is counted with what it reached. */
TEST_CASE(a_gap_at_the_tests_end_leaves_its_verdict)
{
    servo_test_cfg_t c;
    gap_rig(&c, 3u);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    /* The last 200ms of the last hold, the run's last. */
    for (uint32_t k = 0; k < 120000u && servo_test_running(&g.t); k += FRAME_MS) {
        if (g.t.phase == SERVO_TEST_PH_HOLD && g.t.moves_done == 3u
            && g.now - g.t.hold_from_ms >= SERVO_TEST_HOLD_MIN_MS - 200u) {
            break;
        }
        frame();
    }
    CHECK(servo_test_running(&g.t));
    enc_lost(g.now);
    run_out(120000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK_EQ(g.t.enc_gap.count, 1u);
    CHECK(g.t.enc_gap.longest_ms <= SERVO_GAP_MAX_MS);
    /* After the end nothing is judged: a late answer changes nothing. */
    enc_back(g.now + 5000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.report, "AS5600 gaps: 1,") != NULL);
}

/* A part that answers without its magnet has answered: the gap is over.
 * Readings without a magnet open none. */
TEST_CASE(a_reading_without_a_magnet_ends_a_gap_and_opens_none)
{
    servo_test_cfg_t c;
    gap_rig(&c, 3u);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(2000u);
    const uint32_t t = g.now;
    enc_lost(t);
    run_until(t + 200u);
    const servo_test_enc_t dark = { .valid = false, .no_magnet = true,
                                    .taken_ms = t + 200u };
    servo_test_encoder(&g.t, &dark);
    CHECK(!g.t.enc_gap.open);
    CHECK_EQ(g.t.enc_gap.longest_ms, 200u);
    run_ms(2000u);                      /* no magnet for 2s: no gap */
    CHECK(servo_test_running(&g.t));
    enc_back(g.now);
    run_out(120000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK_EQ(g.t.enc_gap.count, 1u);
}

/* Without the encoder in use nothing of it is judged: a "none" for 2s ends
 * no run, and its CSV and report carry no gap. */
TEST_CASE(a_test_without_the_encoder_has_no_gaps)
{
    servo_test_cfg_t c;
    gap_rig(&c, 3u);
    c.enc_on = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(2000u);
    enc_lost(g.now);
    run_ms(2000u);
    CHECK(servo_test_running(&g.t));
    run_out(120000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK_EQ(g.t.enc_gap.count, 0u);
    CHECK(strstr(g.report, "AS5600 gaps") == NULL);
    CHECK(strstr(g.csv_text, "angle gap") == NULL);
}

/* The report's line and the two reasons in German, and the reasons inside
 * the result box's 31 characters. */
TEST_CASE(the_gap_texts_read_in_german)
{
    servo_test_cfg_t c;
    gap_rig(&c, 3u);
    ui_text_set_language(UI_LANG_DE);
    c.text = ui_servo_table();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(2000u);
    enc_lost(g.now);
    run_ms(400u);
    run_out(10000u);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_ENC_SILENT);
    CHECK(strstr(g.report, "AS5600 antwortet nicht") != NULL);
    CHECK(strstr(g.report, "AS5600-Lücken: 1, die längste 320ms. Ein Lauf "
                           "geht durch eine bis 300ms alle 10s weiter; "
                           "darin wird kein Winkel bewertet.") != NULL);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        const char *const *tb = ui_servo_table();
        /* ASCII in both languages: a byte a cell. */
        CHECK(strlen(servo_str_in(tb, SERVO_STR_AB_ENC_SILENT)) <= 31u);
        CHECK(strlen(servo_str_in(tb, SERVO_STR_AB_ENC_GAPS)) <= 31u);
        CHECK(strlen(servo_str_in(tb, SERVO_STR_AB_IMU_SILENT)) <= 31u);
        CHECK(strlen(servo_str_in(tb, SERVO_STR_AB_IMU_GAPS)) <= 31u);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_ENC_GAPS),
                 "AS5600 dropped out twice in 10s");
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_IMU_SILENT),
                 "IMU not answering");
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_IMU_GAPS),
                 "IMU dropped out twice in 10s");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(servo_str_in(ui_servo_table(), SERVO_STR_AB_IMU_SILENT),
                 "IMU antwortet nicht");
    CHECK_STR_EQ(servo_str_in(ui_servo_table(), SERVO_STR_AB_IMU_GAPS),
                 "IMU 2x in 10s ausgefallen");
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_gap_of_300ms_rides_and_301ms_ends);
    RUN(a_second_gap_9999ms_after_the_first_ends_and_10000ms_rides);
    RUN(the_rule_holds_across_the_clocks_wrap);
    RUN(the_test_rides_through_an_encoder_gap_of_300ms);
    RUN(an_encoder_gap_of_301ms_ends_the_test_as_an_error);
    RUN(a_second_encoder_gap_within_10s_ends_the_test);
    RUN(an_imu_run_ends_on_its_gaps_with_the_imus_reasons);
    RUN(a_test_started_in_an_encoder_gap_rides_300ms_of_it);
    RUN(a_gap_during_a_move_leaves_it_out_and_judges_nothing);
    RUN(a_gap_at_the_tests_end_leaves_its_verdict);
    RUN(a_reading_without_a_magnet_ends_a_gap_and_opens_none);
    RUN(a_test_without_the_encoder_has_no_gaps);
    RUN(the_gap_texts_read_in_german);
    return test_summary("servo_gap");
}
