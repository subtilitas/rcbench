/*
 * The automatic servo test on the INA3221's CH1 windows: what a window
 * reads, by hand.
 *
 * Windows and supply readings are given at the millisecond a case names
 * (servo_test_win_rig.h).
 *
 * Under test: a run reading the windows and none of the supply's readings,
 * the figures of the one meter never in the other's file; a run on the PD
 * mini reading no window; the INA3221's range following its shunt; a
 * window without both quantities taken as no reading; a negative current
 * at rest reported and judged by its magnitude; and clipped windows
 * counted and used as the values they are.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_test_win_rig.h"

/* ------------------------------------------------- the meter of a run */

/* A run on the INA3221 by hand: every row a window, the figures the
 * windows', the supply's 0.005A in none of them, and the report naming
 * the meter, the shunt and what the travel times are worth. */
TEST_CASE(a_run_reads_the_windows_and_none_of_the_supplys_readings)
{
    h_fresh(5000u);
    servo_test_cfg_t c;
    h_cfg(&c, true);
    c.travel_max_ms = 100u;         /* under every travel time: not checked */
    h_start(&c);
    h_out();
    CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_PASS);
    CHECK_EQ(h.offs, 1u);
    CHECK_EQ(h.releases, 1u);
    const servo_test_step_t *s = &h.t.steps[0];
    CHECK(s->done);
    CHECK_EQ(s->moves, 2u);
    CHECK_EQ(s->travels, 2u);
    /* IDLE's 1000ms hold 19 to 20 windows: one that began before it is
     * not in it. */
    CHECK(s->idle.n >= 19u && s->idle.n <= 20u);
    CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.020f, 0.0005f);
    /* Two visits to each end, a window every 50ms from the arrival. */
    CHECK(s->hold[0].n >= 22u && s->hold[0].n <= 24u);
    CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.020f, 0.0005f);
    /* The voltage is CH1's bus voltage, and its lowest the lowest sample. */
    CHECK_NEAR(s->v.sum / (float)s->v.n, 4.792f, 0.0005f);
    CHECK_NEAR(s->v_min, 4.592f, 0.0005f);
    /* The peak is a 1ms sample, 50mA above the highest mean. */
    CHECK_NEAR(s->move_peak_a, 0.450f, 0.0005f);
    CHECK_NEAR(s->move.sum / (float)s->move.n, 0.400f, 0.0005f);
    /* 200ms of movement: seen in the window after the command and gone
     * in the one after it ends, at most one window and a pass late. */
    CHECK(s->travel_max_ms >= 200u);
    CHECK(s->travel_max_ms <= 200u + 2u * SERVO_TEST_WIN_MS);
    CHECK_EQ(servo_test_travel_window_ms(&h.t), 3050u);

    /* Every row is a window. */
    CHECK_EQ(h.out.rows - 1u, h.t.readings);
    CHECK_EQ(h.t.skipped, 0u);
    CHECK(strncmp(h.out.csv, servo_test_csv_header(),
                  strlen(servo_test_csv_header())) == 0);
    CHECK_EQ(rows_with(h.out.csv, COL_METER, "INA3221"), h.out.rows - 1u);
    CHECK_EQ(rows_with(h.out.csv, COL_METER, "PDMINI"), 0u);
    CHECK_EQ(rows_with(h.out.csv, COL_CURRENT, "0.005"), 0u);
    CHECK(rows_with(h.out.csv, COL_CURRENT, "0.020") > 30u);
    CHECK(rows_with(h.out.csv, COL_I_MAX, "0.450") >= 8u);
    CHECK(rows_with(h.out.csv, COL_V_MIN, "4.592") > 30u);
    /* The supply's mode, as its last reading had it: none before the
     * first, OFF until the output is on. */
    CHECK_EQ(rows_with(h.out.csv, COL_MODE, "CV")
                 + rows_with(h.out.csv, COL_MODE, "OFF")
                 + rows_with(h.out.csv, COL_MODE, ""), h.out.rows - 1u);
    CHECK_EQ(rows_with(h.out.csv, COL_MODE, ""), 1u);
    CHECK(rows_with(h.out.csv, COL_MODE, "OFF") <= 3u);

    const char *r = h.out.report;
    CHECK(strstr(r, "Log:            the .CSV with this file's number, one "
                    "row per INA3221 window\n") != NULL);
    CHECK(strstr(r, "Supply:         PD mini\n") != NULL);
    CHECK(strstr(r, "Current:        INA3221, shunt 100.0mOhm, range "
                    "1.638A\n") != NULL);
    CHECK(strstr(r, "Voltage:        INA3221, load side of the shunt\n")
          != NULL);
    CHECK(strstr(r, "Shunt:          up to 0.164V lost across it at the "
                    "range") != NULL);
    CHECK(strstr(r, "Readings:       20.0 /s windows closed by the INA3221, "
                    "20.0 /s reached the test\n") != NULL);
    CHECK(strstr(r, "Skipped:        0 windows the INA3221 closed never "
                    "reached the test\n") != NULL);
    CHECK(strstr(r, "Resolution:     one window every 50ms: a travel "
                    "time is late by up to two windows and the poll that "
                    "reads them\n") != NULL);
    CHECK(strstr(r, "Lag:            about 50ms") != NULL);
    CHECK(strstr(r, "Repeats:") == NULL);
    CHECK(strstr(r, "Travel times:   an upper bound, not checked against "
                    "the limit\n") != NULL);
    CHECK(strstr(r, "limit 100ms: upper bound, not checked against the "
                    "limit\n") != NULL);
    CHECK(strstr(r, "Clipped:") == NULL);
    CHECK(strstr(r, "Const. current: 0 supply readings, longest stretch 0"
                    "ms\n") != NULL);
    CHECK(strstr(r, " 4.80    4.79  4.59   0.020  0.020  0.400  0.450  ")
          != NULL);
    CHECK(strstr(r, "Position: nothing measures the horn; every result is "
                    "the current of the INA3221 channel named above.\n")
          != NULL);
    CHECK(strstr(r, "every result is the supply's current") == NULL);
    CHECK(strstr(r, "INA3221:        not used") == NULL);
}

/* The same bench with the run on the PD mini: every row a reading of the
 * supply, no window in the file or in a figure, and the report saying why
 * the INA3221 that SETUP has on was not read. */
TEST_CASE(a_run_on_the_pd_mini_reads_no_window)
{
    static const struct {
        servo_test_ina_t why;
        const char      *text;
    } k_why[] = {
        { SERVO_TEST_INA_OLD, "the IOmcu is older than link protocol "
                              "4.11" },
        { SERVO_TEST_INA_NOT_HELD, "the IOmcu does not hold its "
                                   "set-up" },
        { SERVO_TEST_INA_SILENT, "it does not answer" },
        { SERVO_TEST_INA_NO_WINDOW, "no window with current in the last "
                                    "200ms" },
        { SERVO_TEST_INA_RESET, "it reset itself" },
        { SERVO_TEST_INA_SETTLING, "it has worked for less than 1s" },
        { SERVO_TEST_INA_MODEL, "the supply is the panel's model" },
    };
    for (size_t k = 0u; k < sizeof(k_why) / sizeof(k_why[0]); ++k) {
        h_fresh(5000u);
        h.rest_ma = 700;            /* the INA3221 reads another current */
        servo_test_cfg_t c;
        h_cfg(&c, false);
        c.ina_why = (uint8_t)k_why[k].why;
        h_start(&c);
        /* The INA3221 becomes the meter under the run, and stops being it:
         * nothing to a run that reads the PD mini. */
        h_until(SERVO_TEST_PH_IDLE);
        const servo_test_meter_now_t came = { .ina3221 = true,
                                              .changes = 3u };
        servo_test_meter_now(&h.t, &came, h.now);
        h_run_ms(300u);
        const servo_test_meter_now_t went = {
            .ina3221 = false, .changes = 4u,
            .dropped = SERVO_TEST_AB_INA_SILENT, .dropped_at = 4u,
        };
        servo_test_meter_now(&h.t, &went, h.now);
        CHECK(servo_test_running(&h.t));
        h_out();
        CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
        /* 0.005A at rest and moving alike: no move is seen. */
        CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_NOT_MEASURABLE);
        CHECK_EQ(h.out.rows - 1u, h.t.readings);
        CHECK_EQ(h.t.readings, h.pd_samples);
        CHECK_EQ(rows_with(h.out.csv, COL_METER, "PDMINI"), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_WINDOW, ""), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_I_MAX, ""), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_CLIPPED, ""), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_CURRENT, "0.700"), 0u);
        const servo_test_step_t *s = &h.t.steps[0];
        CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.005f, 0.0001f);
        CHECK_NEAR(s->v_min, 4.8f, 0.0001f);
        CHECK_EQ(h.t.clipped, 0u);
        char want[160];
        snprintf(want, sizeof(want), "INA3221:        not used: %s\n",
                 k_why[k].text);
        CHECK(strstr(h.out.report, want) != NULL);
        CHECK(strstr(h.out.report, "Supply:         PD mini\n") != NULL);
        CHECK(strstr(h.out.report, "Current:        PD mini\n") != NULL);
        CHECK(strstr(h.out.report, "Voltage:        PD mini\n") != NULL);
        CHECK(strstr(h.out.report, "Shunt:") == NULL);
        CHECK(strstr(h.out.report, "one row per supply reading") != NULL);
    }
    /* With the INA3221 off in SETUP the line is not written. */
    h_fresh(5000u);
    servo_test_cfg_t c;
    h_cfg(&c, false);
    h_start(&c);
    h_out();
    CHECK(strstr(h.out.report, "INA3221:") == NULL);
    /* A window handed to a run that is not running, and a run without a
     * window, take nothing. */
    servo_test_window(NULL, NULL, 0u);
    servo_test_window(&h.t, NULL, 0u);
    servo_test_meter_now(NULL, NULL, 0u);
    servo_test_meter_now(&h.t, NULL, 0u);
}

/* The meter's description: the range from the shunt SETUP holds. */
TEST_CASE(the_ina3221s_range_follows_its_shunt)
{
    servo_test_meter_t m;
    servo_test_meter_ina3221(&m, 1000u);        /* 0.1Ohm */
    CHECK_EQ(m.kind, SERVO_TEST_METER_INA3221);
    CHECK_STR_EQ(m.name, "INA3221");     /* the caller names the channel */
    CHECK_EQ(m.span_ms, 50u);
    CHECK_EQ(m.lag_ms, 50u);
    CHECK(!m.repeats);
    CHECK(m.upper_bound);
    CHECK_EQ(m.range_ma, 1638u);
    servo_test_meter_ina3221(&m, 500u);         /* 0.05Ohm */
    CHECK_EQ(m.range_ma, 3276u);
    servo_test_meter_ina3221(&m, 50u);          /* 5mOhm, SETUP's least */
    CHECK_EQ(m.range_ma, 32760u);
    servo_test_meter_ina3221(&m, 10u);          /* held to the field */
    CHECK_EQ(m.range_ma, 65535u);
    servo_test_meter_ina3221(&m, 0u);
    CHECK_EQ(m.range_ma, 0u);
    servo_test_meter_ina3221(NULL, 1000u);
    servo_test_meter_model(&m);
    CHECK_EQ(m.kind, SERVO_TEST_METER_MODEL);
    CHECK_EQ(m.lag_ms, 0u);
    CHECK(!m.repeats);
    CHECK(m.upper_bound);
    servo_test_meter_model(NULL);
    servo_test_meter_pdmini(&m);
    CHECK_EQ(m.kind, SERVO_TEST_METER_PDMINI);
    CHECK_EQ(m.span_ms, 0u);
    CHECK_STR_EQ(servo_test_meter_word(SERVO_TEST_METER_INA3221), "INA3221");
    CHECK_STR_EQ(servo_test_meter_word(SERVO_TEST_METER_PDMINI), "PDMINI");
    CHECK_STR_EQ(servo_test_meter_word(SERVO_TEST_METER_MODEL), "MODEL");
    CHECK_STR_EQ(servo_test_meter_word(99u), "PDMINI");
    CHECK_EQ(SERVO_TEST_WIN_MS, SENSE_WINDOW_MS);
}

/* A window that repeats the last number, one without current samples and
 * one without voltage samples are no reading: no row, no figure, and none
 * of them a window that arrived. */
TEST_CASE(a_window_without_both_quantities_is_no_reading)
{
    servo_test_cfg_t c;
    h_cfg(&c, true);
    h_idle_by_hand(5000u, &c);
    const uint32_t readings = h.t.readings;
    const uint32_t last = h.t.last_ms;
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.number   = h.win_number;              /* the last one again */
    w.current  = true;
    w.voltage  = true;
    w.mean_ma  = 900;
    w.taken_ms = h.now + 10u;
    servo_test_window(&h.t, &w, 0u);
    w.number  = (uint16_t)(h.win_number + 1u);
    w.current = false;
    servo_test_window(&h.t, &w, 0u);
    w.current = true;
    w.voltage = false;
    servo_test_window(&h.t, &w, 0u);
    CHECK_EQ(h.t.readings, readings);
    CHECK_EQ(h.t.last_ms, last);
    /* With both it is one, and a number two ahead counts one skipped. */
    w.voltage = true;
    w.number  = (uint16_t)(h.win_number + 2u);
    servo_test_window(&h.t, &w, 0u);
    CHECK_EQ(h.t.readings, readings + 1u);
    CHECK_EQ(h.t.skipped, 1u);
    CHECK_EQ(h.t.last_ms, h.now + 10u);
}

/* ----------------------------------------------------- signed, clipped */

/* A shunt fitted the other way round: every current negative.  The CSV
 * keeps the sign, the limits and STALL AT compare the magnitude, and the
 * report says the direction. */
TEST_CASE(a_current_negative_at_rest_is_reported_and_judged_by_magnitude)
{
    h_fresh(5000u);
    h.rest_ma = -120;
    h.move_ma = -600;
    servo_test_cfg_t c;
    h_cfg(&c, true);
    c.idle_max_a = 0.10f;           /* under 0.120A: FAIL by magnitude */
    c.hold_max_a = 0.20f;           /* over it: PASS */
    h_start(&c);
    h_out();
    CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
    CHECK(servo_test_negative_at_rest(&h.t));
    CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_FAIL);
    const servo_test_step_t *s = &h.t.steps[0];
    CHECK_NEAR(s->idle.sum / (float)s->idle.n, -0.120f, 0.0005f);
    /* The sample furthest from zero: the window's lowest, 5mA under. */
    CHECK_NEAR(s->peak_a, -0.605f, 0.0005f);
    CHECK_NEAR(h.t.stall_peak_a, -0.600f, 0.0005f);
    CHECK(!h.t.stalled);
    float a = 0.0f;
    CHECK(servo_test_max_idle(&h.t, &a));
    CHECK_NEAR(a, -0.120f, 0.0005f);
    CHECK(servo_test_max_hold(&h.t, &a));
    CHECK_NEAR(a, -0.120f, 0.0005f);
    CHECK(rows_with(h.out.csv, COL_CURRENT, "-0.120") > 30u);
    CHECK(rows_with(h.out.csv, COL_I_MIN, "-0.605") >= 4u);
    const char *r = h.out.report;
    CHECK(strstr(r, "Current reads negative at rest: shunt direction\n")
          != NULL);
    CHECK(strstr(r, "Idle current     highest -0.120A, limit 0.10A: "
                    "FAIL\n") != NULL);
    CHECK(strstr(r, "Holding current  highest -0.120A, limit 0.20A: "
                    "PASS\n") != NULL);
    CHECK(strstr(r, "Stall threshold  highest -0.600A, STALL AT 1.00A: "
                    "PASS\n") != NULL);

    /* -0.020A at rest is not said, -0.021A is; and a stall the other
     * way round ends the run. */
    for (int ma = -21; ma <= -20; ++ma) {
        h_fresh(5000u);
        h.rest_ma = ma;
        h_cfg(&c, true);
        h_start(&c);
        h_out();
        CHECK_EQ(servo_test_negative_at_rest(&h.t), ma == -21);
        CHECK_EQ(strstr(h.out.report, "Current reads negative at rest")
                 != NULL, ma == -21);
    }
    CHECK(!servo_test_negative_at_rest(NULL));
    h_fresh(5000u);
    h_cfg(&c, true);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.force_ma = -1200;
    h_run_ms(1100u);
    h_check_ended(SERVO_TEST_AB_STALL);
}

/* A clipped window is a reading like any other: its mean, its highest
 * sample and its place in the verdict are the values the part gave.  The
 * file carries the count of clipped samples and the report the count of
 * such windows.  How many of a window's samples are clipped decides
 * nothing: 44, 45 and all 50 read alike. */
TEST_CASE(a_clipped_window_is_the_value_it_is_and_is_counted)
{
    static const unsigned k_clipped[] = { 1u, 44u, 45u, 50u };
    for (size_t k = 0u; k < sizeof(k_clipped) / sizeof(k_clipped[0]); ++k) {
        for (int over = 0; over <= 1; ++over) {
            h_fresh(5000u);
            servo_test_cfg_t c;
            h_cfg(&c, true);
            /* The windows' mean while clipped is 1.588A: over a STALL
             * AT of 1.58A and under one of 1.59A. */
            c.stall_a = over ? 1.58f : 1.59f;
            h_start(&c);
            CHECK(h_until(SERVO_TEST_PH_HOLD));
            h.clipped  = k_clipped[k];
            h.force_ma = 1588;      /* its highest sample 1.638A */
            h_run_ms(400u);
            const uint32_t clipped = h.t.clipped;
            CHECK(clipped >= 7u && clipped <= 9u);
            CHECK_EQ(h.t.stalled, over != 0);
            h.clipped  = 0u;
            h.force_ma = INT16_MIN;
            h_out();
            CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
            CHECK_EQ(h.t.stalled, over != 0);
            if (over != 0) {
                CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_FAIL);
            }
            CHECK_EQ(h.t.clipped, clipped);
            char cell[8], want[128];
            snprintf(cell, sizeof(cell), "%u", k_clipped[k]);
            CHECK_EQ(rows_with(h.out.csv, COL_CLIPPED, cell), clipped);
            CHECK_EQ(rows_with(h.out.csv, COL_I_MAX, "1.638"), clipped);
            CHECK_EQ(rows_with(h.out.csv, COL_CURRENT, "1.588"), clipped);
            snprintf(want, sizeof(want), "Clipped:        %lu windows hold "
                     "a sample at an end of the range, 1.638A",
                     (unsigned long)clipped);
            CHECK(strstr(h.out.report, want) != NULL);
            CHECK(strstr(h.out.report, "Stall threshold  highest 1.588A")
                  != NULL);
        }
    }
}

int main(void)
{
    RUN(a_run_reads_the_windows_and_none_of_the_supplys_readings);
    RUN(a_run_on_the_pd_mini_reads_no_window);
    RUN(the_ina3221s_range_follows_its_shunt);
    RUN(a_window_without_both_quantities_is_no_reading);
    RUN(a_current_negative_at_rest_is_reported_and_judged_by_magnitude);
    RUN(a_clipped_window_is_the_value_it_is_and_is_counted);
    out_reset(&h.out);
    return test_summary("servo_test_win_read");
}
