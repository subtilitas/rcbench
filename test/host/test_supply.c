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
    supply_reset_peaks(NULL);
    supply_track_peaks(NULL);
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

int main(void)
{
    RUN(a_set_point_is_clamped_and_stepped);
    RUN(the_model_holds_its_voltage_until_the_load_reaches_the_limit);
    RUN(set_points_follow_the_caps);
    RUN(a_run_counts_only_while_the_output_is_on);
    RUN(the_extremes_take_only_what_arrived);
    RUN(a_supply_row_writes_its_own_header_and_columns);
    RUN(a_supply_row_leaves_what_did_not_arrive_empty);
    return test_summary("supply");
}
