/*
 * The OpenYGE session's writes: the read before, one write per changed
 * index in rising order, the read after, and each way it ends: OK,
 * CHANGED, NOT_TAKEN, PARTIAL, GUARD, a refusal before any frame, write
 * replies at 399ms and 401ms, and an abort in every state.  On the
 * simulated ESC (electronic speed controller) of oy_sim.h, which records
 * every frame, so a write the guard should have stopped shows.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "oy_sim.h"

static oy_session_t g_s;
static uint16_t g_d[OPENYGE_MAX_PARAMS];

/* Connect, read, and start the draft from the read copy. */
static void ready(void)
{
    sim_reset(0);
    if (sim_connect_read(&g_s) != OY_OK) {
        T_FAIL("no read: %u", (unsigned)g_s.result);
    }
    memcpy(g_d, g_s.orig, sizeof(g_d));
}

/* Write frames recorded from @p from on. */
static unsigned writes_from(unsigned from)
{
    return sim_count(OPENYGE_FT_WRITE_PARAM_REQ, from);
}

/* Index of the @p k-th write frame recorded. */
static unsigned write_at(unsigned k)
{
    for (unsigned i = 0; i < sim.n_sent; ++i) {
        if (sim.sent[i].b[2] == OPENYGE_FT_WRITE_PARAM_REQ && k-- == 0u) {
            return i;
        }
    }
    return sim.n_sent;
}

TEST_CASE(a_write_reads_writes_each_change_rising_and_reads_again)
{
    ready();
    const uint16_t no = g_s.guard.read_no;
    g_d[26] = 0x0001u;      /* direction reversed */
    g_d[21] = 13;
    g_d[17] = 1200;
    const unsigned from = sim.n_sent;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK_EQ(g_s.state, OY_ST_RECHECK);
    CHECK_EQ(oy_session_writing(&g_s), false);
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_OK);
    CHECK_EQ(writes_from(from), 3u);
    static const uint16_t IDX[] = { 17, 21, 26 };
    static const uint16_t VAL[] = { 1200, 13, 1 };
    for (unsigned k = 0; k < 3u; ++k) {
        const unsigned i = write_at(k);
        CHECK_EQ(sim_index(i), IDX[k]);
        CHECK_EQ(sim_value(i), VAL[k]);
        /* Each write OY_REQ_PERIOD_MS after the reply before it. */
        CHECK_EQ(sim.sent[i].t - sim.sent[i - 1u].t,
                 SIM_DELAY_US + OY_REQ_PERIOD_MS * MS);
    }
    /* A whole table read before the first write: 32 requests at least. */
    CHECK(write_at(0) - from >= 32u);
    CHECK_EQ(sim.table[17], 1200u);
    CHECK_EQ(sim.table[21], 13u);
    CHECK_EQ(sim.table[26], 1u);
    CHECK_EQ(memcmp(g_s.orig, g_d, 32u * sizeof(uint16_t)), 0);
    CHECK_EQ(g_s.guard.read_no, (uint16_t)(no + 1u));
    CHECK_EQ(g_s.state, OY_ST_READY);
    /* The draft of the read before is not one of this read. */
    g_d[21] = 14;
    CHECK_EQ(oy_session_write(&g_s, g_d, 32, no, g_s.guard.read_crc),
             OY_GUARD);
    CHECK_EQ(g_s.refuse, OY_REF_OTHER_READ);
    /* The write ended: a lost ESC afterwards is silent, not a write cut. */
    sim.silent = true;
    CHECK_EQ(sim_run_ms(&g_s, SIM_DELAY_US / MS + OY_REQ_PERIOD_MS
                                  + OY_TRIES * OY_READ_TIMEOUT_MS),
             OY_SILENT);
}

TEST_CASE(a_draft_equal_to_the_read_sends_nothing)
{
    ready();
    const unsigned from = sim.n_sent;
    CHECK_EQ(sim_write(&g_s, g_d), OY_OK);
    CHECK_EQ(g_s.state, OY_ST_READY);
    CHECK_EQ(sim_run_ms(&g_s, 500u), OY_OK);
    CHECK_EQ(writes_from(from), 0u);
}

TEST_CASE(a_table_changed_since_the_read_ends_changed_with_nothing_written)
{
    ready();
    sim.table[9] = 4;       /* changed by a stick setup since the read */
    g_d[21] = 13;
    const unsigned from = sim.n_sent;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, OY_TABLE_CAP_MS), OY_CHANGED);
    CHECK_EQ(writes_from(from), 0u);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
    const unsigned n = sim.n_sent;
    CHECK_EQ(sim_run_ms(&g_s, 1000u), OY_CHANGED);
    CHECK_EQ(sim.n_sent, n);
}

TEST_CASE(writes_the_esc_does_not_store_end_not_taken)
{
    ready();
    sim.ignore_writes = true;
    g_d[21] = 13;
    g_d[22] = 130;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_NOT_TAKEN);
    CHECK_EQ(writes_from(0), 2u);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
    CHECK_EQ(g_s.orig[21], 12u);
}

TEST_CASE(a_write_stored_in_part_ends_partial)
{
    ready();
    sim.store_max = 1;
    g_d[21] = 13;
    g_d[22] = 130;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_PARTIAL);
    CHECK_EQ(writes_from(0), 2u);
    CHECK_EQ(g_s.orig[21], 13u);
    CHECK_EQ(g_s.orig[22], 120u);
}

/* One write with its reply @p late_ms late; write frames sent. */
static unsigned late_write(uint32_t late_ms, oy_res_t *r)
{
    ready();
    sim.wlate_n = 1;
    sim.wlate_us = late_ms * MS;
    g_d[21] = 13;
    if (sim_write(&g_s, g_d) != OY_BUSY) {
        T_FAIL("write refused");
    }
    *r = sim_until(&g_s, 3u * OY_TABLE_CAP_MS);
    return writes_from(0);
}

TEST_CASE(a_write_reply_at_399ms_counts_and_one_at_401ms_is_dropped)
{
    oy_res_t r;
    CHECK_EQ(late_write(OY_WRITE_TIMEOUT_MS - 1u, &r), 1u);
    CHECK_EQ(r, OY_OK);
    CHECK_EQ(g_s.misses, 0u);

    CHECK_EQ(late_write(OY_WRITE_TIMEOUT_MS + 1u, &r), 2u);
    CHECK_EQ(r, OY_OK);
    CHECK_EQ(g_s.misses, 1u);
    const unsigned a = write_at(0), b = write_at(1);
    CHECK_EQ(sim.sent[b].t - sim.sent[a].t, OY_WRITE_TIMEOUT_MS * MS);
    CHECK_EQ(sim.sent[b].b[4], (uint8_t)(sim.sent[a].b[4] + 1u));
    CHECK_EQ(sim_index(b), 21u);
    CHECK(g_s.dropped >= 1u);
}

TEST_CASE(write_replies_with_crc_errors_are_tried_three_times)
{
    ready();
    g_d[21] = 13;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK(sim_until_state(&g_s, OY_ST_WRITE, OY_TABLE_CAP_MS));
    sim.crc_n = OY_TRIES - 1u;
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_OK);
    CHECK_EQ(writes_from(0), OY_TRIES);

    ready();
    g_d[21] = 13;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK(sim_until_state(&g_s, OY_ST_WRITE, OY_TABLE_CAP_MS));
    sim.seq_n = OY_TRIES - 1u;
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_OK);
    CHECK_EQ(writes_from(0), OY_TRIES);
}

TEST_CASE(three_write_misses_stop_the_writes_and_the_read_after_decides)
{
    /* Not heard: nothing stored, the second index is never sent. */
    ready();
    g_d[21] = 13;
    g_d[22] = 130;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK(sim_until_state(&g_s, OY_ST_WRITE, OY_TABLE_CAP_MS));
    sim.drop_n = OY_TRIES;
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_NOT_TAKEN);
    CHECK_EQ(writes_from(0), OY_TRIES);
    for (unsigned k = 0; k < OY_TRIES; ++k) {
        CHECK_EQ(sim_index(write_at(k)), 21u);
    }

    /* Stored and not answered: the read after finds the value. */
    ready();
    g_d[21] = 13;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK(sim_until_state(&g_s, OY_ST_WRITE, OY_TABLE_CAP_MS));
    sim.wmute_n = OY_TRIES;
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_OK);
    CHECK_EQ(writes_from(0), OY_TRIES);
}

TEST_CASE(an_esc_lost_after_a_write_ends_partial)
{
    ready();
    g_d[21] = 13;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK(sim_until_state(&g_s, OY_ST_VERIFY, 2u * OY_TABLE_CAP_MS));
    CHECK(oy_session_writing(&g_s));
    sim.silent = true;
    CHECK_EQ(sim_until(&g_s, OY_TABLE_CAP_MS), OY_PARTIAL);
    CHECK_EQ(g_s.state, OY_ST_IDLE);

    /* The read after never completes: PARTIAL at the cap. */
    ready();
    g_d[21] = 13;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK(sim_until_state(&g_s, OY_ST_VERIFY, 2u * OY_TABLE_CAP_MS));
    sim.skip = 30;
    CHECK_EQ(sim_run_ms(&g_s, OY_TABLE_CAP_MS - 1u), OY_BUSY);
    CHECK_EQ(sim_run_ms(&g_s, 1u), OY_PARTIAL);

    /* Silent in the read before: nothing was written, so SILENT. */
    ready();
    g_d[21] = 13;
    sim.silent = true;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, OY_TABLE_CAP_MS), OY_SILENT);
    CHECK_EQ(writes_from(0), 0u);
}

TEST_CASE(a_refused_write_sends_no_frame_and_keeps_the_session)
{
    ready();
    g_d[25] = 1;
    CHECK_EQ(sim_write(&g_s, g_d), OY_GUARD);
    CHECK_EQ(g_s.refuse, OY_REF_TELEMETRY);
    CHECK_EQ(g_s.bad_index, 25u);
    g_d[25] = g_s.orig[25];
    g_d[21] = 0;
    CHECK_EQ(sim_write(&g_s, g_d), OY_GUARD);
    CHECK_EQ(g_s.refuse, OY_REF_VALUE);
    CHECK_EQ(g_s.bad_index, 21u);
    g_d[21] = 13;
    CHECK_EQ(oy_session_write(&g_s, g_d, 32, (uint16_t)(g_s.guard.read_no + 1u),
                              g_s.guard.read_crc), OY_GUARD);
    CHECK_EQ(g_s.refuse, OY_REF_OTHER_READ);
    CHECK_EQ(oy_session_write(&g_s, g_d, 31, g_s.guard.read_no,
                              g_s.guard.read_crc), OY_GUARD);
    CHECK_EQ(g_s.state, OY_ST_READY);
    CHECK_EQ(sim_run_ms(&g_s, 500u), OY_OK);
    CHECK_EQ(writes_from(0), 0u);

    /* A table of 31 is read-only. */
    sim_reset(0);
    sim.table[0] = 31;
    sim.cycle = 31;
    CHECK_EQ(sim_connect_read(&g_s), OY_OK);
    memcpy(g_d, g_s.orig, sizeof(g_d));
    g_d[21] = 13;
    CHECK_EQ(sim_write(&g_s, g_d), OY_NOT_KNOWN);
    CHECK_EQ(g_s.refuse, OY_REF_RO_COUNT);
    CHECK_EQ(sim_run_ms(&g_s, 500u), OY_OK);
    CHECK_EQ(writes_from(0), 0u);

    /* A write with no read this session. */
    sim_reset(0);
    CHECK_EQ(sim_connect(&g_s), OY_OK);
    CHECK_EQ(sim_write(&g_s, g_d), OY_NO_TABLE);
    CHECK_EQ(oy_session_write(&g_s, g_d, 32, 0, 0), OY_NO_TABLE);
    CHECK_EQ(writes_from(0), 0u);
}

TEST_CASE(the_guard_stops_a_target_changed_after_approval)
{
    ready();
    g_d[21] = 13;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    g_s.target[21] = 14;    /* not the value the guard approved */
    CHECK_EQ(sim_until(&g_s, 2u * OY_TABLE_CAP_MS), OY_GUARD);
    CHECK_EQ(g_s.refuse, OY_REF_NOT_APPROVED);
    CHECK_EQ(writes_from(0), 0u);
    CHECK_EQ(g_s.state, OY_ST_IDLE);
}

TEST_CASE(bec_to_120_goes_with_flag_bit_3_set)
{
    sim_reset(0);
    sim.table[26] = OY_FLAG_BEC12V;
    CHECK_EQ(sim_connect_read(&g_s), OY_OK);
    memcpy(g_d, g_s.orig, sizeof(g_d));
    g_d[2] = 120;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    CHECK_EQ(sim_until(&g_s, 3u * OY_TABLE_CAP_MS), OY_OK);
    CHECK_EQ(sim.table[2], 120u);
}

/* Bring a session to state @p st; for WRITE and VERIFY a write runs. */
static bool reach(uint8_t st)
{
    sim_reset(0);
    CHECK_EQ(oy_session_init(&g_s, &SIM_DRIVER), OY_OK);
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN), OY_BUSY);
    if (st == OY_ST_WAIT) {
        return sim_run_ms(&g_s, 10u) == OY_BUSY;
    }
    if (st == OY_ST_LISTEN || st == OY_ST_TAKE) {
        return sim_until_state(&g_s, st, 2000u);
    }
    if (sim_until(&g_s, 2000u) != OY_OK) {
        return false;
    }
    if (st == OY_ST_READY) {
        return true;
    }
    CHECK_EQ(oy_session_read(&g_s), OY_BUSY);
    if (st == OY_ST_READ) {
        return sim_run_ms(&g_s, 100u) == OY_BUSY;
    }
    if (sim_until(&g_s, OY_TABLE_CAP_MS) != OY_OK) {
        return false;
    }
    memcpy(g_d, g_s.orig, sizeof(g_d));
    g_d[21] = 13;
    g_d[22] = 130;
    CHECK_EQ(sim_write(&g_s, g_d), OY_BUSY);
    if (st == OY_ST_RECHECK) {
        return sim_run_ms(&g_s, 100u) == OY_BUSY;
    }
    /* WRITE: the first write frame is out. */
    if (st == OY_ST_WRITE) {
        for (unsigned i = 0; i < OY_TABLE_CAP_MS && writes_from(0) == 0u; ++i) {
            (void)sim_run_ms(&g_s, 1u);
        }
        return g_s.state == OY_ST_WRITE && writes_from(0) == 1u;
    }
    return sim_until_state(&g_s, st, 2u * OY_TABLE_CAP_MS);
}

TEST_CASE(an_abort_in_every_state_stops_every_frame)
{
    static const uint8_t STATES[] = {
        OY_ST_WAIT, OY_ST_LISTEN, OY_ST_TAKE, OY_ST_READY, OY_ST_READ,
        OY_ST_RECHECK, OY_ST_WRITE, OY_ST_VERIFY,
    };
    for (size_t k = 0; k < sizeof(STATES); ++k) {
        const uint8_t st = STATES[k];
        if (!reach(st)) {
            T_FAIL("state %u not reached", (unsigned)st);
            continue;
        }
        CHECK_EQ(g_s.state, st);
        const bool writing = st == OY_ST_WRITE || st == OY_ST_VERIFY;
        CHECK_EQ(oy_session_writing(&g_s), writing);
        const unsigned n = sim.n_sent;
        oy_session_abort(&g_s);
        CHECK_EQ(g_s.state, OY_ST_IDLE);
        CHECK_EQ(oy_session_step(&g_s), OY_ABORTED);
        CHECK_EQ(oy_session_cut_write(&g_s), writing);
        CHECK_EQ(oy_session_writing(&g_s), false);
        CHECK_EQ(sim_run_ms(&g_s, 2000u), OY_ABORTED);
        if (sim.n_sent != n) {
            T_FAIL("state %u: %u frames after the abort", (unsigned)st,
                   sim.n_sent - n);
        }
        /* A second abort and one in IDLE change nothing. */
        oy_session_abort(&g_s);
        CHECK_EQ(oy_session_step(&g_s), OY_ABORTED);
        CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN), OY_BUSY);
    }
}

TEST_CASE(stop_in_a_write_cuts_the_next_write_and_link_loss_in_the_read_after)
{
    /* STOP with the first of two writes out: the second never goes. */
    CHECK(reach(OY_ST_WRITE));
    CHECK(oy_session_writing(&g_s));
    oy_session_abort(&g_s);
    CHECK_EQ(sim_run_ms(&g_s, 2000u), OY_ABORTED);
    CHECK_EQ(writes_from(0), 1u);
    CHECK_EQ(sim_index(write_at(0)), 21u);
    CHECK(oy_session_cut_write(&g_s));

    /* Link loss in the read after: the table is not known. */
    CHECK(reach(OY_ST_VERIFY));
    oy_session_abort(&g_s);
    CHECK(oy_session_cut_write(&g_s));
    /* A new entry clears the mark. */
    CHECK_EQ(oy_session_enter(&g_s, OY_BOOT_MS_MIN), OY_BUSY);
    CHECK_EQ(oy_session_cut_write(&g_s), false);

    /* An abort before the first write frame leaves the table as read. */
    CHECK(reach(OY_ST_RECHECK));
    oy_session_abort(&g_s);
    CHECK_EQ(oy_session_cut_write(&g_s), false);
    CHECK_EQ(writes_from(0), 0u);
}

int main(void)
{
    RUN(a_write_reads_writes_each_change_rising_and_reads_again);
    RUN(a_draft_equal_to_the_read_sends_nothing);
    RUN(a_table_changed_since_the_read_ends_changed_with_nothing_written);
    RUN(writes_the_esc_does_not_store_end_not_taken);
    RUN(a_write_stored_in_part_ends_partial);
    RUN(a_write_reply_at_399ms_counts_and_one_at_401ms_is_dropped);
    RUN(write_replies_with_crc_errors_are_tried_three_times);
    RUN(three_write_misses_stop_the_writes_and_the_read_after_decides);
    RUN(an_esc_lost_after_a_write_ends_partial);
    RUN(a_refused_write_sends_no_frame_and_keeps_the_session);
    RUN(the_guard_stops_a_target_changed_after_approval);
    RUN(bec_to_120_goes_with_flag_bit_3_set);
    RUN(an_abort_in_every_state_stops_every_frame);
    RUN(stop_in_a_write_cuts_the_next_write_and_link_loss_in_the_read_after);
    return test_summary("openyge_session_write");
}
