/**
 * @file test_servo_limrun_rows.c
 * @brief LIMITnnn.CSV, the limit run's judged positions: one row a position a
 *        search judged, written before the report under one number, read
 *        back with LOGS' reader.
 *
 * The failures under test: a judged position missing from the file or
 * written twice; a row whose pulse, current, voltage or angle is not the
 * one the search judged; a bound row whose endpoint is not the search's; a
 * search number or side that does not name the search; the row at the
 * 600-row cap lost, or the one past it written or not counted; a run that
 * ends in its first search losing the rows it judged; a run that judged no
 * position opening an empty CSV; and the report not naming the rows past
 * the cap.
 *
 * The rig is servo_limrun_rig.h's: servo_sim's servo, stops at 1150us and
 * 1880us, the INA3221 as 50ms windows.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun_rig.h"

#include "limrun_rows_check.h"

/** @brief The CSV's line @p k, 0 the header, without its line feed. */
static void csv_line_of(unsigned k, char *out, size_t cap)
{
    const char *p = g.csv;
    for (unsigned i = 0u; i < k && p != NULL; ++i) {
        p = strchr(p, '\n');
        p = p != NULL ? p + 1 : NULL;
    }
    out[0] = '\0';
    if (p == NULL) {
        return;
    }
    const char *e = strchr(p, '\n');
    const size_t n = e != NULL ? (size_t)(e - p) : strlen(p);
    snprintf(out, cap, "%.*s", (int)(n < cap - 1u ? n : cap - 1u), p);
}

/** @brief A current in A as the rows hold it: whole mA, rounded half up. */
static uint16_t ma_of(float a)
{
    return (uint16_t)((double)a * 1000.0 + 0.5);
}

/** @brief Tick the rig until the run holds @p n rows, at most @p ms. */
static bool run_to_rows(unsigned n, uint32_t ms)
{
    for (uint32_t k = 0; k < ms && servo_limrun_running(&g.run); ++k) {
        if (g.run.rows_n >= n) {
            return true;
        }
        tick();
    }
    return g.run.rows_n >= n;
}

/* Search only on the programming port, an angle source fitted: one row a
 * judged position, both sides' searches whole, read back by LOGS' reader,
 * the bound rows at the pulses and endpoints the report names. */
TEST_CASE(a_search_only_run_writes_a_row_a_judged_position_and_reads_back)
{
    rig_init(0u, false);
    g.cfg.program = false;
    g.enc = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.opens_txt, 0u);
    CHECK_EQ(g.ends, 1u);
    CHECK_EQ(g.run.rows_past, 0u);
    CHECK(g.run.rows_n > 40u);
    CHECK_EQ(g.csv_lines, 1u + g.run.rows_n);
    char line[SERVO_TEST_LINE_MAX];
    csv_line_of(0u, line, sizeof(line));
    CHECK_STR_EQ(line, "row;search;side;pulse (us);current (A);voltage (V);"
                       "angle (deg);state;free (A);threshold (A);"
                       "end point (us)");
    rows_read_back(&g.run, g.csv);
    CHECK_EQ(rows_shape(&g.run, 1500u), 2u);

    /* The first row: the center, the first search, the low side, BASE. */
    const servo_limrun_row_t *w = &g.run.rows[0];
    CHECK_EQ(w->pulse_us, 1500u);
    CHECK_EQ(w->mv, 6000u);
    CHECK(w->angle_ok);
    csv_line_of(1u, line, sizeof(line));
    CHECK(strncmp(line, "1;1;LOW;1500;", 13) == 0);
    CHECK(strstr(line, ";6.00;") != NULL);
    CHECK(strstr(line, ";BASE;") != NULL);

    /* Each side's search ends on its bound row, at the first bound pulse,
     * with the endpoint and currents the report gives. */
    for (int s = 0; s < 2; ++s) {
        const servo_limrun_side_t *f = &g.run.first[s];
        unsigned at = g.run.rows_n;
        for (unsigned k = 0u; k < g.run.rows_n; ++k) {
            if (g.run.rows[k].side == s
                && g.run.rows[k].state == SERVO_LIMRUN_ROW_BOUND) {
                at = k;
            }
        }
        CHECK(at < g.run.rows_n);
        w = &g.run.rows[at];
        CHECK_EQ(w->search, 1u);
        CHECK_EQ(w->pulse_us, f->bind_us);
        CHECK_EQ(w->end_us, f->limit_us);
        CHECK_EQ(g.run.rows[at - 1u].pulse_us, f->free_us);
        CHECK_EQ(w->ma, ma_of(f->knee_a));
        CHECK_EQ(w->free_ma, ma_of(f->baseline_a));
        /* The angle at the last free row is the report's. */
        CHECK(f->enc);
        CHECK_NEAR(g.run.rows[at - 1u].deg, f->enc_deg, 1e-6);
    }
    /* The low side first, then the high side. */
    CHECK_EQ(g.run.rows[g.run.rows_n - 1u].side, SERVO_LIMRUN_HI);
    csv_line_of(g.run.rows_n, line, sizeof(line));
    char want[32];
    snprintf(want, sizeof(want), ";BOUND;");
    CHECK(strstr(line, want) != NULL);
    snprintf(want, sizeof(want), ";%u", (unsigned)g.run.first[1].limit_us);
    CHECK(strstr(line, want) != NULL);
    snprintf(want, sizeof(want), "%u;1;HIGH;%u;", (unsigned)g.run.rows_n,
             (unsigned)g.run.first[1].bind_us);
    CHECK(strncmp(line, want, strlen(want)) == 0);
    char rows[64];
    snprintf(rows, sizeof(rows), "Positions:      judged %u, all in the CSV\n",
             (unsigned)g.run.rows_n);
    CHECK(strstr(g.report, rows) != NULL);

    /* Without an angle source the angle cells stay empty. */
    rig_init(0u, false);
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK(g.run.rows_n > 40u);
    rows_read_back(&g.run, g.csv);
    csv_line_of(1u, line, sizeof(line));
    CHECK(strstr(line, ";6.00;;BASE;") != NULL);
}

/* A programmed run: the first searches numbered 1, those after a write 2,
 * the last check 3, which ends free on both sides. */
TEST_CASE(a_programmed_run_numbers_its_searches_one_two_and_three)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        CHECK_EQ(g.opens, 1u);
        CHECK_EQ(g.run.rows_past, 0u);
        CHECK_EQ(g.csv_lines, 1u + g.run.rows_n);
        rows_read_back(&g.run, g.csv);
        unsigned per[4] = { 0u, 0u, 0u, 0u };
        unsigned bound3 = 0u;
        for (unsigned k = 0u; k < g.run.rows_n; ++k) {
            const servo_limrun_row_t *w = &g.run.rows[k];
            ++per[w->search <= 3u ? w->search : 0u];
            bound3 += w->search == 3u && w->state == SERVO_LIMRUN_ROW_BOUND;
        }
        CHECK_EQ(per[0], 0u);
        CHECK(per[1] > 0u && per[2] > 0u && per[3] > 0u);
        CHECK_EQ(bound3, 0u);
        /* Two first searches, a search after each write that searched,
         * two checks: free out to PULSE LOWER and PULSE UPPER. */
        const unsigned searches = rows_shape(&g.run, 1500u);
        CHECK(searches >= 6u);
        const servo_limrun_row_t *last = &g.run.rows[g.run.rows_n - 1u];
        CHECK_EQ(last->search, 3u);
        CHECK_EQ(last->side, SERVO_LIMRUN_HI);
        CHECK_EQ(last->pulse_us, 2100u);
        CHECK_EQ(last->state, SERVO_LIMRUN_ROW_FREE);
    }
}

/* The cap: the 600th row is written, the 601st and every one after it
 * counted, and the report names both figures. */
TEST_CASE(the_row_at_the_cap_is_written_and_the_one_past_it_is_counted)
{
    rig_init(0u, false);
    g.cfg.program = false;
    CHECK(start());
    CHECK(run_to_rows(5u, RUN_MS));
    CHECK_EQ(g.run.rows_n, 5u);
    /* As though 593 positions more were held: the next judged position is
     * row 599. */
    const uint16_t fifth = g.run.rows[4].pulse_us;
    g.run.rows_n = (uint16_t)(SERVO_LIMRUN_ROWS_MAX - 2u);
    const uint8_t judged = g.run.judged;
    CHECK(run_to_rows(SERVO_LIMRUN_ROWS_MAX - 1u, RUN_MS));
    CHECK_EQ(g.run.rows[SERVO_LIMRUN_ROWS_MAX - 2u].pulse_us,
             (uint16_t)(fifth - 10u));
    CHECK(run_to_rows(SERVO_LIMRUN_ROWS_MAX, RUN_MS));
    CHECK_EQ(g.run.rows_past, 0u);
    const servo_limrun_row_t at_cap = g.run.rows[SERVO_LIMRUN_ROWS_MAX - 1u];
    CHECK_EQ(at_cap.pulse_us, (uint16_t)(fifth - 20u));
    CHECK_EQ(g.run.judged, (uint8_t)(judged + 2u));
    /* The next position judged is counted, not written. */
    const uint8_t before = g.run.judged;
    while (servo_limrun_running(&g.run) && g.run.judged == before) {
        tick();
    }
    CHECK_EQ(g.run.rows_n, SERVO_LIMRUN_ROWS_MAX);
    CHECK_EQ(g.run.rows_past, 1u);
    CHECK_EQ(g.run.rows[SERVO_LIMRUN_ROWS_MAX - 1u].pulse_us,
             at_cap.pulse_us);
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK(g.run.rows_past > 1u);
    CHECK_EQ(g.run.rows_n, SERVO_LIMRUN_ROWS_MAX);
    CHECK_EQ(g.csv_lines, 1u + SERVO_LIMRUN_ROWS_MAX);

    char line[SERVO_TEST_LINE_MAX];
    CHECK(servo_limrun_csv_line(&g.run, SERVO_LIMRUN_ROWS_MAX, line,
                                sizeof(line)));
    char want[32];
    snprintf(want, sizeof(want), "600;1;LOW;%u;", (unsigned)at_cap.pulse_us);
    CHECK(strncmp(line, want, strlen(want)) == 0);
    CHECK(!servo_limrun_csv_line(&g.run, SERVO_LIMRUN_ROWS_MAX + 1u, line,
                                 sizeof(line)));
    char rows[96];
    snprintf(rows, sizeof(rows),
             "Positions:      judged %lu, in the CSV 600, past its cap %lu\n",
             (unsigned long)(SERVO_LIMRUN_ROWS_MAX + g.run.rows_past),
             (unsigned long)g.run.rows_past);
    CHECK(strstr(g.report, rows) != NULL);
    CHECK(strstr(g.report, "all in the CSV") == NULL);
}

/* STOP in the first search: the rows judged so far are written, none of
 * them bound, and no endpoint; STOP before the first position judged
 * leaves the report alone, with no CSV and no positions line. */
TEST_CASE(a_run_aborted_in_the_first_search_writes_the_rows_it_judged)
{
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    CHECK(run_to_rows(10u, RUN_MS));
    servo_limrun_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    const uint16_t n = g.run.rows_n;
    run_for(100u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.rows_n, n);
    CHECK_EQ(n, 10u);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.opens_txt, 0u);
    CHECK_EQ(g.csv_lines, 1u + n);
    rows_read_back(&g.run, g.csv);
    CHECK_EQ(rows_shape(&g.run, CENTER), 1u);
    for (unsigned k = 0u; k < n; ++k) {
        CHECK_EQ(g.run.rows[k].search, 1u);
        CHECK_EQ(g.run.rows[k].side, SERVO_LIMRUN_LO);
        CHECK(g.run.rows[k].state != SERVO_LIMRUN_ROW_BOUND);
        CHECK_EQ(g.run.rows[k].pulse_us, (uint16_t)(CENTER - 10u * k));
    }
    CHECK(strstr(g.report, "Positions:      judged 10, all in the CSV\n")
          != NULL);

    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    idle_for(500u);                 /* still at the center, nothing judged */
    servo_limrun_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    run_for(100u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.rows_n, 0u);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.opens_txt, 1u);
    CHECK_EQ(g.csv_lines, 0u);
    CHECK(g.lines > 0u);
    CHECK(strstr(g.report, "Positions:") == NULL);
    char line[SERVO_TEST_LINE_MAX];
    CHECK(servo_limrun_csv_line(&g.run, 0u, line, sizeof(line)));
    CHECK(!servo_limrun_csv_line(&g.run, 1u, line, sizeof(line)));
}

/* A run that ends before any search, on a read that is no servo of the
 * family: the report alone. */
TEST_CASE(a_run_ended_at_the_read_writes_the_report_alone)
{
    rig_init(0u, false);
    g.not_kst = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.rows_n, 0u);
    CHECK_EQ(g.opens_txt, 1u);
    CHECK_EQ(g.csv_lines, 0u);
    CHECK_EQ(g.ends, 1u);
}

/* The line call's guards. */
TEST_CASE(the_csv_line_needs_a_finished_run_and_room)
{
    char buf[SERVO_TEST_LINE_MAX];
    servo_limrun_t idle;
    servo_limrun_init(&idle);
    CHECK(!servo_limrun_csv_line(NULL, 0u, buf, sizeof(buf)));
    CHECK(!servo_limrun_csv_line(&idle, 0u, buf, sizeof(buf)));
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    CHECK(run_to_rows(1u, RUN_MS));
    CHECK(!servo_limrun_csv_line(&g.run, 0u, buf, sizeof(buf)));
    run_for(RUN_MS);
    CHECK(!servo_limrun_csv_line(&g.run, 0u, NULL, sizeof(buf)));
    CHECK(!servo_limrun_csv_line(&g.run, 0u, buf, 0u));
    CHECK(servo_limrun_csv_line(&g.run, 0u, buf, sizeof(buf)));
    CHECK_STR_EQ(buf, SERVO_LIMRUN_CSV_HEAD);
    /* A buffer too short cuts the line, terminated. */
    char small[8];
    CHECK(servo_limrun_csv_line(&g.run, 1u, small, sizeof(small)));
    CHECK_EQ(strlen(small), 7u);
}

int main(void)
{
    RUN(a_search_only_run_writes_a_row_a_judged_position_and_reads_back);
    RUN(a_programmed_run_numbers_its_searches_one_two_and_three);
    RUN(the_row_at_the_cap_is_written_and_the_one_past_it_is_counted);
    RUN(a_run_aborted_in_the_first_search_writes_the_rows_it_judged);
    RUN(a_run_ended_at_the_read_writes_the_report_alone);
    RUN(the_csv_line_needs_a_finished_run_and_room);
    return test_summary("servo_limrun_rows");
}
