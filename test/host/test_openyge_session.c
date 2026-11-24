/*
 * The OpenYGE session up to a read: the boot wait, listening, takeover,
 * table reads at the count limits, reply timeouts at 199ms and 201ms,
 * tries on CRC errors and sequence mismatches, the sequence number and the
 * clock across their wraps, and a legacy ESC (electronic speed controller)
 * read only.  On the simulated ESC of oy_sim.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "oy_sim.h"

static oy_session_t g_s;

/* The time of recorded frame @p i after power-on, ms. */
static uint32_t at_ms(unsigned i)
{
    return (sim.sent[i].t - sim.t_on) / MS;
}

TEST_CASE(nothing_is_sent_in_the_boot_wait_and_takeover_follows_an_auto_frame)
{
    sim_reset(0);
    CHECK_EQ(oy_session_init(&g_s, &SIM_DRIVER), OY_OK);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN), OY_BUSY);
    CHECK_EQ(sim_run_ms(&g_s, OY_BOOT_MS_MIN - 1u), OY_BUSY);
    CHECK_EQ(g_s.state, OY_ST_WAIT);
    CHECK_EQ(sim.n_sent, 0u);
    /* Auto frames come every 50ms from 300ms; the one at 1050ms is the
     * first heard, and the request follows OY_REQ_PERIOD_MS after it. */
    CHECK_EQ(sim_run_ms(&g_s, 1u), OY_BUSY);
    CHECK_EQ(g_s.state, OY_ST_LISTEN);
    CHECK_EQ(sim_until(&g_s, 1000u), OY_OK);
    CHECK_EQ(sim.n_sent, 1u);
    CHECK_EQ(at_ms(0), 1050u + OY_REQ_PERIOD_MS);
    CHECK_EQ(sim.sent[0].b[2], OPENYGE_FT_TELE_REQ);
    CHECK_EQ(sim.sent[0].b[5], 0x81u);
    CHECK_EQ(g_s.state, OY_ST_READY);
    CHECK_EQ(g_s.addr, 1u);
}

TEST_CASE(the_boot_wait_runs_from_1000ms_to_20000ms)
{
    sim_reset(0);
    CHECK_EQ(oy_session_init(&g_s, &SIM_DRIVER), OY_OK);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN - 1u), OY_ERR_ARG);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MAX + 1u), OY_ERR_ARG);
    CHECK_EQ(oy_session_enter(NULL, OY_BOOT_MS), OY_ERR_ARG);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MAX), OY_BUSY);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MAX), OY_ERR_STATE);
    CHECK_EQ(sim_run_ms(&g_s, OY_BOOT_MS_MAX - 1u), OY_BUSY);
    CHECK_EQ(g_s.state, OY_ST_WAIT);
    CHECK_EQ(sim_run_ms(&g_s, 1u), OY_BUSY);
    CHECK_EQ(g_s.state, OY_ST_LISTEN);
    CHECK_EQ(sim_until(&g_s, 1000u), OY_OK);
    CHECK_EQ(at_ms(0), OY_BOOT_MS_MAX + 50u + OY_REQ_PERIOD_MS);
}

TEST_CASE(auto_frames_before_takeover_give_the_address_and_are_dropped)
{
    sim_reset(0);
    sim.dev = 5;
    sim.auto_us = 20u * MS;
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK_EQ(g_s.addr, 5u);
    CHECK_EQ(sim.sent[0].b[5], 0x85u);
    CHECK_EQ(at_ms(0), 1020u + OY_REQ_PERIOD_MS);
    CHECK(g_s.dropped >= 1u);
    CHECK(sim.taken);

    /* Device 0 is the broadcast address: the session uses 1. */
    sim_reset(0);
    sim.dev = 0;
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK_EQ(g_s.addr, OY_ADDR_DEFAULT);
    CHECK_EQ(sim.sent[0].b[5], 0x81u);
}

TEST_CASE(with_no_auto_frame_the_listen_runs_out_and_requests_go_to_address_1)
{
    sim_reset(0);
    sim.auto_us = 0;
    sim.dev = 9;
    CHECK_EQ(oy_session_init(&g_s, &SIM_DRIVER), OY_OK);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN), OY_BUSY);
    CHECK_EQ(sim_run_ms(&g_s, OY_BOOT_MS_MIN + OY_LISTEN_MS - 1u), OY_BUSY);
    CHECK_EQ(sim.n_sent, 0u);
    CHECK_EQ(sim_until(&g_s, 1000u), OY_OK);
    CHECK_EQ(at_ms(0), OY_BOOT_MS_MIN + OY_LISTEN_MS);
    CHECK_EQ(sim.sent[0].b[5], 0x81u);
}

TEST_CASE(a_silent_esc_at_boot_ends_silent_after_three_tries)
{
    sim_reset(0);
    sim.auto_us = 0;
    sim.silent = true;
    CHECK_EQ(oy_session_init(&g_s, &SIM_DRIVER), OY_OK);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN), OY_BUSY);
    const uint32_t first = OY_BOOT_MS_MIN + OY_LISTEN_MS;
    const uint32_t end = first + OY_TRIES * OY_READ_TIMEOUT_MS;
    CHECK_EQ(sim_run_ms(&g_s, end - 1u), OY_BUSY);
    CHECK_EQ(sim_run_ms(&g_s, 1u), OY_SILENT);
    CHECK_EQ(sim.n_sent, OY_TRIES);
    for (unsigned i = 0; i < OY_TRIES; ++i) {
        CHECK_EQ(at_ms(i), first + i * OY_READ_TIMEOUT_MS);
    }
    CHECK_EQ(g_s.state, OY_ST_IDLE);
    CHECK_EQ(sim_run_ms(&g_s, 2000u), OY_SILENT);
    CHECK_EQ(sim.n_sent, OY_TRIES);
}

/* Read with the ESC reporting @p count; the read's result.  A count the
 * session cannot hold ends the read at the first index 0, within 2000ms. */
static oy_res_t read_count(uint16_t count)
{
    sim_reset(0);
    sim.table[0] = count;
    sim.cycle = count == 0u ? 32u
              : count > OPENYGE_MAX_PARAMS ? OPENYGE_MAX_PARAMS : count;
    if (sim_connect(&g_s) != OY_OK || oy_session_read(&g_s) != OY_BUSY) {
        return OY_ERR_STATE;
    }
    const oy_res_t r = sim_until(&g_s, 2000u + 2u * OY_TABLE_CAP_MS);
    if (r == OY_NO_TABLE && sim.us - sim.t_on > 4000u * MS) {
        T_FAIL("count %u: NO_TABLE only at the cap", (unsigned)count);
    }
    return r;
}

TEST_CASE(a_table_reads_at_every_count_limit)
{
    CHECK_EQ(read_count(0), OY_NO_TABLE);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
    CHECK_EQ(read_count(1), OY_OK);
    CHECK_EQ(g_s.count, 1u);
    CHECK_EQ(g_s.guard.ro, OY_REF_RO_COUNT);
    CHECK_EQ(read_count(31), OY_OK);
    CHECK_EQ(g_s.guard.ro, OY_REF_RO_COUNT);
    CHECK_EQ(read_count(32), OY_OK);
    CHECK_EQ(g_s.count, 32u);
    CHECK_EQ(g_s.guard.ro, OY_REF_NONE);
    CHECK_EQ(memcmp(g_s.orig, sim.table, 32u * sizeof(uint16_t)), 0);
    CHECK_EQ(read_count(33), OY_OK);
    CHECK_EQ(g_s.count, 33u);
    CHECK_EQ(g_s.guard.ro, OY_REF_RO_COUNT);
    CHECK_EQ(read_count(64), OY_OK);
    CHECK_EQ(g_s.count, 64u);
    CHECK_EQ(g_s.guard.ro, OY_REF_RO_COUNT);
    CHECK_EQ(read_count(65), OY_NO_TABLE);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
}

TEST_CASE(a_read_that_never_completes_ends_at_the_cap)
{
    sim_reset(0);
    sim.skip = 31;
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK_EQ(oy_session_read(&g_s), OY_BUSY);
    CHECK_EQ(oy_session_read(&g_s), OY_ERR_STATE);
    CHECK_EQ(sim_run_ms(&g_s, OY_TABLE_CAP_MS - 1u), OY_BUSY);
    CHECK_EQ(sim_run_ms(&g_s, 1u), OY_NO_TABLE);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
}

/* Takeover with the first reply @p late_ms late; frames sent. */
static unsigned late_take(uint32_t late_ms, oy_res_t *r)
{
    sim_reset(0);
    sim.auto_us = 0;
    sim.late_n = 1;
    sim.late_us = late_ms * MS;
    *r = sim_connect(&g_s);
    return sim.n_sent;
}

TEST_CASE(a_reply_at_199ms_counts_and_one_at_201ms_is_dropped)
{
    oy_res_t r;
    CHECK_EQ(late_take(OY_READ_TIMEOUT_MS - 1u, &r), 1u);
    CHECK_EQ(r, OY_OK);
    CHECK_EQ(g_s.misses, 0u);

    CHECK_EQ(late_take(OY_READ_TIMEOUT_MS + 1u, &r), 2u);
    CHECK_EQ(r, OY_OK);
    CHECK_EQ(g_s.misses, 1u);
    /* Sent again at the timeout with a new sequence number; the late
     * reply carries the old one and is dropped. */
    CHECK_EQ(at_ms(1) - at_ms(0), OY_READ_TIMEOUT_MS);
    CHECK_EQ(sim.sent[1].b[4], (uint8_t)(sim.sent[0].b[4] + 1u));
    CHECK(g_s.dropped >= 1u);
    CHECK_EQ(g_s.seq, sim.sent[1].b[4]);
}

TEST_CASE(two_misses_are_tried_again_and_a_third_is_silent)
{
    sim_reset(0);
    sim.auto_us = 0;
    sim.drop_n = OY_TRIES - 1u;
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK_EQ(sim.n_sent, OY_TRIES);

    sim_reset(0);
    sim.auto_us = 0;
    sim.drop_n = OY_TRIES;
    CHECK_EQ(sim_connect(&g_s), OY_SILENT);
    CHECK_EQ(sim.n_sent, OY_TRIES);
}

TEST_CASE(crc_errors_are_tried_three_times)
{
    sim_reset(0);
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    sim.crc_n = OY_TRIES - 1u;
    CHECK_EQ(oy_session_read(&g_s), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, OY_TABLE_CAP_MS), OY_OK);
    CHECK_EQ(g_s.dec.crc_errors, OY_TRIES - 1u);

    sim_reset(0);
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    sim.crc_n = OY_TRIES;
    CHECK_EQ(oy_session_read(&g_s), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, OY_TABLE_CAP_MS), OY_SILENT);
    CHECK_EQ(g_s.dec.crc_errors, OY_TRIES);
}

TEST_CASE(a_sequence_mismatch_is_tried_three_times)
{
    sim_reset(0);
    sim.auto_us = 0;
    sim.seq_n = OY_TRIES - 1u;
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK_EQ(sim.n_sent, OY_TRIES);
    CHECK_EQ(g_s.dropped, OY_TRIES - 1u);

    sim_reset(0);
    sim.auto_us = 0;
    sim.seq_n = OY_TRIES;
    CHECK_EQ(sim_connect(&g_s), OY_SILENT);

    /* A reply of the other type counts as no reply. */
    sim_reset(0);
    sim.auto_us = 0;
    sim.type_n = 1;
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK_EQ(sim.n_sent, 2u);
}

TEST_CASE(the_sequence_number_wraps_from_255_to_0)
{
    sim_reset(0);
    CHECK_EQ(oy_session_init(&g_s, &SIM_DRIVER), OY_OK);
    g_s.seq = 250;
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, 5000u), OY_OK);
    CHECK_EQ(oy_session_read(&g_s), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, OY_TABLE_CAP_MS), OY_OK);
    CHECK(sim.n_sent > 10u);
    CHECK_EQ(sim.sent[0].b[4], 251u);
    bool zero = false;
    for (unsigned i = 1; i < sim.n_sent; ++i) {
        CHECK_EQ(sim.sent[i].b[4], (uint8_t)(sim.sent[i - 1u].b[4] + 1u));
        zero = zero || (sim.sent[i].b[4] == 0u && sim.sent[i - 1u].b[4] == 255u);
    }
    CHECK(zero);
}

/* Connect and read from clock @p start; the result, frames and the time of
 * the last frame after power-on. */
static oy_res_t run_from(uint32_t start, bool silent, unsigned *n,
                         uint32_t *last_ms)
{
    sim_reset(start);
    sim.silent = silent;
    oy_res_t r = sim_connect(&g_s);
    if (r == OY_OK) {
        r = oy_session_read(&g_s) == OY_BUSY ? sim_until(&g_s, 20000u)
                                             : OY_ERR_STATE;
    }
    *n = sim.n_sent;
    *last_ms = sim.n_sent > 0u ? at_ms(sim.n_sent - 1u) : 0u;
    return r;
}

TEST_CASE(the_clock_may_wrap_in_any_wait)
{
    unsigned n0 = 0, n1 = 0;
    uint32_t t0 = 0, t1 = 0;
    const oy_res_t r0 = run_from(0, false, &n0, &t0);
    CHECK_EQ(r0, OY_OK);
    /* The wrap in the boot wait, in the takeover and in the read. */
    static const uint32_t WRAP_MS[] = { 500, 1070, 1500 };
    for (size_t k = 0; k < sizeof(WRAP_MS) / sizeof(WRAP_MS[0]); ++k) {
        CHECK_EQ(run_from(0u - WRAP_MS[k] * MS, false, &n1, &t1), r0);
        CHECK_EQ(n1, n0);
        CHECK_EQ(t1, t0);
    }
    /* The wrap inside the 200ms timeout of a silent ESC. */
    const oy_res_t s0 = run_from(0, true, &n0, &t0);
    CHECK_EQ(s0, OY_SILENT);
    CHECK_EQ(run_from(0u - 1100u * MS, true, &n1, &t1), s0);
    CHECK_EQ(n1, n0);
    CHECK_EQ(t1, t0);
}

TEST_CASE(a_legacy_header_is_listened_to_and_read_only)
{
    sim_reset(0);
    sim.legacy = true;
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK(g_s.legacy);
    CHECK_EQ(oy_session_read(&g_s), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, OY_TABLE_CAP_MS), OY_OK);
    CHECK_EQ(g_s.count, 32u);
    CHECK_EQ(g_s.guard.ro, OY_REF_RO_LEGACY);
    uint16_t d[OPENYGE_MAX_PARAMS];
    memcpy(d, g_s.orig, sizeof(d));
    d[21] = 13;
    CHECK_EQ(sim_write(&g_s, d), OY_NOT_KNOWN);
    CHECK_EQ(g_s.refuse, OY_REF_RO_LEGACY);
    CHECK_EQ(sim_run_ms(&g_s, 2000u), OY_OK);
    CHECK_EQ(sim.n_sent, 0u);
}

TEST_CASE(requests_go_on_between_operations_and_a_lost_esc_is_silent)
{
    sim_reset(0);
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    const unsigned n = sim.n_sent;
    CHECK_EQ(sim_run_ms(&g_s, 1000u), OY_OK);
    /* One request each reply plus OY_REQ_PERIOD_MS: about 23 a second. */
    CHECK(sim.n_sent - n >= 20u);
    CHECK(sim.n_sent - n <= 25u);
    sim.silent = true;
    /* A reply in flight, the period, then three timeouts. */
    CHECK_EQ(sim_run_ms(&g_s, SIM_DELAY_US / MS + OY_REQ_PERIOD_MS
                                  + OY_TRIES * OY_READ_TIMEOUT_MS),
             OY_SILENT);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
    const unsigned m = sim.n_sent;
    CHECK_EQ(sim_run_ms(&g_s, 1000u), OY_SILENT);
    CHECK_EQ(sim.n_sent, m);
}

TEST_CASE(operations_out_of_order_and_nulls_are_refused)
{
    sim_reset(0);
    oy_driver_t d = SIM_DRIVER;
    CHECK_EQ(oy_session_init(NULL, &d), OY_ERR_ARG);
    CHECK_EQ(oy_session_init(&g_s, NULL), OY_ERR_ARG);
    d.tx = NULL;
    CHECK_EQ(oy_session_init(&g_s, &d), OY_ERR_ARG);
    d = SIM_DRIVER;
    d.rx = NULL;
    CHECK_EQ(oy_session_init(&g_s, &d), OY_ERR_ARG);
    d = SIM_DRIVER;
    d.now_us = NULL;
    CHECK_EQ(oy_session_init(&g_s, &d), OY_ERR_ARG);
    CHECK_EQ(oy_session_init(&g_s, &SIM_DRIVER), OY_OK);
    uint16_t t[OPENYGE_MAX_PARAMS] = { 0 };
    CHECK_EQ(oy_session_read(&g_s), OY_ERR_STATE);
    CHECK_EQ(oy_session_write(&g_s, t, 32, 0, 0), OY_ERR_STATE);
    CHECK_EQ(oy_session_write(&g_s, NULL, 32, 0, 0), OY_ERR_ARG);
    CHECK_EQ(oy_session_read(NULL), OY_ERR_ARG);
    CHECK_EQ(oy_session_write(NULL, t, 32, 0, 0), OY_ERR_ARG);
    CHECK_EQ(oy_session_step(NULL), OY_ERR_ARG);
    CHECK_EQ(oy_session_step(&g_s), OY_OK);
    oy_session_abort(NULL);
    CHECK_EQ(oy_session_writing(NULL), false);
    CHECK_EQ(oy_session_cut_write(NULL), false);
    CHECK_EQ(sim.n_sent, 0u);
}

int main(void)
{
    RUN(nothing_is_sent_in_the_boot_wait_and_takeover_follows_an_auto_frame);
    RUN(the_boot_wait_runs_from_1000ms_to_20000ms);
    RUN(auto_frames_before_takeover_give_the_address_and_are_dropped);
    RUN(with_no_auto_frame_the_listen_runs_out_and_requests_go_to_address_1);
    RUN(a_silent_esc_at_boot_ends_silent_after_three_tries);
    RUN(a_table_reads_at_every_count_limit);
    RUN(a_read_that_never_completes_ends_at_the_cap);
    RUN(a_reply_at_199ms_counts_and_one_at_201ms_is_dropped);
    RUN(two_misses_are_tried_again_and_a_third_is_silent);
    RUN(crc_errors_are_tried_three_times);
    RUN(a_sequence_mismatch_is_tried_three_times);
    RUN(the_sequence_number_wraps_from_255_to_0);
    RUN(the_clock_may_wrap_in_any_wait);
    RUN(a_legacy_header_is_listened_to_and_read_only);
    RUN(requests_go_on_between_operations_and_a_lost_esc_is_silent);
    RUN(operations_out_of_order_and_nulls_are_refused);
    return test_summary("openyge_session");
}
