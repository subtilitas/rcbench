/*
 * Host unit tests for the supply model and a supply run's CSV rows.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "greatest.h"

#include "bench_state.h"
#include "log_writer.h"
#include "supply.h"

/* ---------------------------------------------------------------- snap */

TEST_CASE(a_set_point_is_clamped_and_stepped)
{
    CHECK_NEAR(supply_snap(2.0f, 3.3f, 21.0f, 0.02f), 3.3f, 1e-4f);
    CHECK_NEAR(supply_snap(30.0f, 3.3f, 21.0f, 0.02f), 21.0f, 1e-4f);
    CHECK_NEAR(supply_snap(5.011f, 3.3f, 21.0f, 0.02f), 5.02f, 1e-4f);
    CHECK_NEAR(supply_snap(5.009f, 3.3f, 21.0f, 0.02f), 5.0f, 1e-4f);
    CHECK_NEAR(supply_snap(1.23f, 0.5f, 5.0f, 0.0f), 1.23f, 1e-6f);
    const float nan = 0.0f / 0.0f;
    CHECK_NEAR(supply_snap(nan, 0.5f, 5.0f, 0.05f), 0.5f, 1e-6f);
}

/* ---------------------------------------------------------------- model */

TEST_CASE(the_model_holds_its_voltage_until_the_load_reaches_the_limit)
{
    supply_sim_t m;
    supply_sim_init(&m);
    supply_state_t st;
    memset(&st, 0, sizeof(st));

    supply_sim_step(&m, 0.05f, &st);          /* off */
    CHECK_EQ(st.mode, SUPPLY_MODE_OFF);
    CHECK_EQ(st.v, 0.0f);
    CHECK(st.online);

    supply_sim_set(&m, 6.0f, 2.0f);
    supply_sim_output(&m, true);
    /* Inside the burst: 1 A of resistance and 1.4 A of burst is over the
     * 2 A limit, so the current holds and the voltage falls. */
    supply_sim_step(&m, 0.1f, &st);
    CHECK_EQ(st.mode, SUPPLY_MODE_CC);
    CHECK_NEAR(st.i, 2.0f, 1e-4f);
    CHECK(st.v < 6.0f && st.v > 2.0f);

    /* Between bursts: the set voltage less the source drop, about 1 A. */
    supply_sim_step(&m, 1.0f, &st);
    CHECK_EQ(st.mode, SUPPLY_MODE_CV);
    CHECK_NEAR(st.v, 6.0f, 0.1f);
    CHECK_NEAR(st.i, 1.0f, 0.05f);
    CHECK_NEAR(st.p, st.v * st.i, 1e-4f);

    /* With the limit raised over the burst, the burst is carried in CV. */
    supply_sim_set(&m, 6.0f, 5.0f);
    supply_sim_output(&m, false);
    supply_sim_output(&m, true);
    supply_sim_step(&m, 0.1f, &st);
    CHECK_EQ(st.mode, SUPPLY_MODE_CV);
    CHECK_NEAR(st.i, 2.4f, 0.05f);
}

TEST_CASE(set_points_follow_the_caps)
{
    supply_sim_t m;
    supply_sim_init(&m);
    supply_sim_set(&m, 99.0f, 0.01f);
    CHECK_NEAR(m.set_v, m.caps.v_max, 1e-4f);
    CHECK_NEAR(m.set_i, m.caps.i_min, 1e-4f);
    supply_sim_init(NULL);
    supply_sim_set(NULL, 1.0f, 1.0f);
    supply_sim_output(NULL, true);
    supply_sim_step(NULL, 1.0f, NULL);
}

/* -------------------------------------------------- peaks and totals */

TEST_CASE(a_run_counts_only_while_the_output_is_on)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = 6.0f; st.i = 1.8f; st.p = st.v * st.i;

    supply_count_totals(&st, 1.0f);           /* output off */
    CHECK_EQ(st.charge_mah, 0.0f);
    CHECK_EQ(st.counted, 0u);

    st.output = true;
    for (int i = 0; i < 100; ++i) {
        supply_count_totals(&st, 1.0f);
    }
    CHECK_NEAR(st.charge_mah, 50.0f, 0.1f);   /* 1.8 A for 100 s */
    CHECK_NEAR(st.energy_wh, 0.3f, 0.001f);
    CHECK_EQ(st.counted, BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);

    supply_count_totals(&st, 60.0f);           /* a stall counts 1 s */
    CHECK_NEAR(st.charge_mah, 50.5f, 0.1f);

    supply_reset_totals(&st);
    CHECK_EQ(st.charge_mah, 0.0f);
    CHECK_EQ(st.counted, 0u);
    supply_count_totals(NULL, 1.0f);
    supply_reset_totals(NULL);
}

TEST_CASE(the_extremes_take_only_what_arrived)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    supply_reset_peaks(&st);                   /* nothing arrived yet */
    CHECK(!st.sag_seeded);

    /* An output switched off reads 0 V, which is no floor. */
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    supply_reset_peaks(&st);
    CHECK(!st.sag_seeded);
    st.output = true;
    supply_reset_peaks(&st);
    CHECK(st.sag_seeded);
    CHECK_EQ(st.v_min, 0.0f);

    st.v = 6.0f; st.i = 1.0f; st.p = 6.0f;
    supply_reset_peaks(&st);
    supply_track_peaks(&st);
    st.v = 3.6f; st.i = 2.0f; st.p = 7.2f;
    supply_track_peaks(&st);
    st.v = 5.9f; st.i = 1.1f; st.p = 6.5f;
    supply_track_peaks(&st);
    CHECK_NEAR(st.v_min, 3.6f, 1e-4f);
    CHECK_NEAR(st.i_max, 2.0f, 1e-4f);
    CHECK_NEAR(st.p_max, 7.2f, 1e-4f);

    /* A reading that did not arrive moves nothing. */
    st.ok = 0u; st.v = 0.0f; st.i = 9.0f;
    supply_track_peaks(&st);
    CHECK_NEAR(st.v_min, 3.6f, 1e-4f);
    CHECK_NEAR(st.i_max, 2.0f, 1e-4f);

    /* Nor does it seed a reset: a stale 9 A would hold the maximum above
     * every current the next run delivers. */
    st.i = 9.0f; st.p = 54.0f;
    st.ok = SUPPLY_OK_VOLTAGE;
    supply_reset_peaks(&st);
    CHECK_EQ(st.i_max, 0.0f);
    CHECK_EQ(st.p_max, 0.0f);
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.i = 1.5f; st.p = 9.0f;
    supply_track_peaks(&st);
    CHECK_NEAR(st.i_max, 1.5f, 1e-4f);
    CHECK_NEAR(st.p_max, 9.0f, 1e-4f);
    supply_reset_peaks(NULL);
    supply_track_peaks(NULL);
}

/* ------------------------------------------------------------- limits */

TEST_CASE(the_limits_narrow_the_caps_and_never_below_the_floor)
{
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    supply_limits_t lim = { 8.4f, 3.0f, 0.0f, 0.0f, 0.1f };
    supply_caps_t e = supply_caps_limited(&caps, &lim);
    CHECK_NEAR(e.v_max, 8.4f, 1e-4f);
    CHECK_NEAR(e.i_max, 3.0f, 1e-4f);
    CHECK_NEAR(e.v_min, caps.v_min, 1e-4f);
    CHECK_NEAR(e.v_step, caps.v_step, 1e-6f);

    /* A limit above the supply's own changes nothing. */
    lim.v_max = 30.0f;
    lim.i_max = 9.0f;
    e = supply_caps_limited(&caps, &lim);
    CHECK_NEAR(e.v_max, caps.v_max, 1e-4f);
    CHECK_NEAR(e.i_max, caps.i_max, 1e-4f);

    /* One below the supply's minimum leaves a single value, not an empty
     * range. */
    lim.v_max = 1.0f;
    lim.i_max = 0.1f;
    e = supply_caps_limited(&caps, &lim);
    CHECK_NEAR(e.v_max, caps.v_min, 1e-4f);
    CHECK_NEAR(e.i_max, caps.i_min, 1e-4f);

    e = supply_caps_limited(NULL, NULL);
    CHECK_NEAR(e.v_max, caps.v_max, 1e-4f);
}

TEST_CASE(a_trip_fires_once_a_reading_has_been_over_for_its_time)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.output = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = 6.0f;
    st.i = 2.6f;
    /* 100 ms over 2.5 A, at 20 Hz: the first reading over starts the count
     * and the one 100 ms after it fires, two intervals later. */
    const supply_limits_t lim = { 21.0f, 5.0f, 2.5f, 0.0f, 0.1f };
    supply_trip_t t;
    supply_trip_reset(&t);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_CURRENT);
    /* Fired, the count starts again. */
    CHECK_EQ(t.over_i_s, 0.0f);
    CHECK(!t.i_over);

    /* A reading back under starts the count again. */
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    st.i = 2.4f;
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    st.i = 2.6f;
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    /* One that did not arrive leaves it where it is: neither adds nor
     * starts again. */
    st.ok = SUPPLY_OK_VOLTAGE;
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.5f), SUPPLY_TRIP_NONE);
    CHECK_NEAR(t.over_i_s, 0.05f, 1e-5f);
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_CURRENT);

    /* A trip turned off forgets what it had counted: on again, the count
     * starts from the next reading over. */
    supply_trip_reset(&t);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    const supply_limits_t paused = { 21.0f, 5.0f, 0.0f, 0.0f, 0.1f };
    CHECK_EQ(supply_trip_step(&t, &paused, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK(!t.i_over);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_CURRENT);

    /* A threshold changed while counting starts the count again: time over
     * 2.5 A is not time over 2.55 A. */
    supply_trip_reset(&t);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 0.05f), SUPPLY_TRIP_NONE);
    const supply_limits_t raised = { 21.0f, 5.0f, 2.55f, 0.0f, 0.1f };
    CHECK_EQ(supply_trip_step(&t, &raised, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &raised, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &raised, &st, 0.05f), SUPPLY_TRIP_CURRENT);

    /* The voltage trip, at a trip time of 0: the first reading over. */
    const supply_limits_t vlim = { 21.0f, 5.0f, 0.0f, 6.5f, 0.0f };
    st.v = 6.6f;
    CHECK_EQ(supply_trip_step(&t, &vlim, &st, 0.05f), SUPPLY_TRIP_VOLTAGE);

    /* Nothing counts while the output is off, and a long stall counts 1 s. */
    st.output = false;
    CHECK_EQ(supply_trip_step(&t, &lim, &st, 5.0f), SUPPLY_TRIP_NONE);
    CHECK_EQ(t.over_i_s, 0.0f);
    st.output = true;
    const supply_limits_t slow = { 21.0f, 5.0f, 2.5f, 0.0f, 1.5f };
    CHECK_EQ(supply_trip_step(&t, &slow, &st, 0.05f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(&t, &slow, &st, 60.0f), SUPPLY_TRIP_NONE);
    CHECK_NEAR(t.over_i_s, 1.0f, 1e-4f);
    CHECK_EQ(supply_trip_step(&t, &slow, &st, -1.0f), SUPPLY_TRIP_NONE);

    /* Off at 0, and nothing without its arguments. */
    const supply_limits_t off = { 21.0f, 5.0f, 0.0f, 0.0f, 0.0f };
    st.i = 9.0f;
    st.v = 30.0f;
    CHECK_EQ(supply_trip_step(&t, &off, &st, 1.0f), SUPPLY_TRIP_NONE);
    CHECK_EQ(supply_trip_step(NULL, &off, &st, 1.0f), SUPPLY_TRIP_NONE);
    supply_trip_reset(NULL);
}

/* ------------------------------------------------------------ the CSV */

static char g_buf[2048];
static size_t g_len;

static int mem_write(void *ctx, const void *data, size_t len)
{
    (void)ctx;
    if (g_len + len >= sizeof(g_buf)) {
        return -1;
    }
    memcpy(g_buf + g_len, data, len);
    g_len += len;
    g_buf[g_len] = '\0';
    return (int)len;
}

static log_writer_t fresh_writer(void)
{
    g_len = 0;
    g_buf[0] = '\0';
    const log_sink_t sink = { .write = mem_write, .flush = NULL, .ctx = NULL };
    log_writer_t w;
    log_writer_init(&w, &sink);
    return w;
}

static const char *second_line(void)
{
    const char *p = strchr(g_buf, '\n');
    return (p != NULL) ? p + 1 : "";
}

TEST_CASE(a_supply_row_writes_its_own_header_and_columns)
{
    log_writer_t w = fresh_writer();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.set_v = 6.0f; st.set_i = 2.0f;
    st.v = 5.95f; st.i = 1.234f; st.p = 7.34f;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.mode = SUPPLY_MODE_CV; st.output = true; st.online = true;
    st.charge_mah = 12.0f; st.energy_wh = 0.07f;
    st.counted = BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY;
    CHECK(log_writer_supply_row(&w, 0.05f, &st));

    CHECK(strncmp(g_buf, "time (s);set (V);voltage (V);limit (A);current (A);"
                         "power (W);mode;charge (mAh);energy (Wh)\n",
                  strlen("time (s);set (V);voltage (V);limit (A);current (A);"
                         "power (W);mode;charge (mAh);energy (Wh)\n")) == 0);
    CHECK_STR_EQ(second_line(), "0.050;6.00;5.95;2.00;1.234;7.34;CV;12;0.07\n");
}

TEST_CASE(a_supply_row_leaves_what_did_not_arrive_empty)
{
    log_writer_t w = fresh_writer();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.set_v = 6.0f; st.set_i = 2.0f;
    st.ok = SUPPLY_OK_VOLTAGE;       /* no current */
    st.v = 6.0f; st.mode = SUPPLY_MODE_CC; st.online = true;
    CHECK(log_writer_supply_row(&w, 1.0f, &st));
    CHECK_STR_EQ(second_line(), "1.000;6.00;6.00;2.00;;;CC;;\n");

    /* And a supply that does not answer has no mode either. */
    log_writer_t w2 = fresh_writer();
    st.online = false; st.ok = 0u;
    CHECK(log_writer_supply_row(&w2, 2.0f, &st));
    CHECK_STR_EQ(second_line(), "2.000;6.00;;2.00;;;;;\n");
    CHECK(!log_writer_supply_row(NULL, 1.0f, &st));
    CHECK(!log_writer_supply_row(&w2, 1.0f, NULL));
}

TEST_CASE(a_row_that_does_not_fit_fails_the_log_and_writes_nothing_past_it)
{
    /* Values no supply gives -- a driver gone wrong -- format to dozens of
     * digits each.  The row is refused, the writer latches, and nothing is
     * written past the line (the sanitizer build holds the stack to it). */
    log_writer_t w = fresh_writer();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.set_v = 3e38f; st.set_i = 3e38f;
    st.v = 3e38f; st.i = 3e38f; st.p = 3e38f;
    st.charge_mah = 3e38f; st.energy_wh = 3e38f;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.counted = BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY;
    st.online = true;
    st.mode = SUPPLY_MODE_CV;
    CHECK(!log_writer_supply_row(&w, 1.0f, &st));
    CHECK(log_writer_failed(&w));
    CHECK(strchr(second_line(), '\n') == NULL);   /* no row went out */
}

int main(void)
{
    RUN(a_set_point_is_clamped_and_stepped);
    RUN(the_model_holds_its_voltage_until_the_load_reaches_the_limit);
    RUN(set_points_follow_the_caps);
    RUN(a_run_counts_only_while_the_output_is_on);
    RUN(the_extremes_take_only_what_arrived);
    RUN(the_limits_narrow_the_caps_and_never_below_the_floor);
    RUN(a_trip_fires_once_a_reading_has_been_over_for_its_time);
    RUN(a_supply_row_writes_its_own_header_and_columns);
    RUN(a_supply_row_leaves_what_did_not_arrive_empty);
    RUN(a_row_that_does_not_fit_fails_the_log_and_writes_nothing_past_it);
    return test_summary("supply");
}
