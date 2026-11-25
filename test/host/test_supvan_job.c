/**
 * @file test_supvan_job.c
 * @brief protocols/supvan's job against the simulated printer (sv_sim.h):
 *        the whole PRINT sequence in order with its parameters, the
 *        fragments' spacing and the pause; the stream the printer joins;
 *        QUERY alone; the label counter's verdicts (295 to 294, 295 three
 *        times, 294 on the second read, a changed UID, a count that jumps);
 *        the roll's checks; busy for 4.9s and 5.1s; a start refused; the
 *        clock's wrap.
 *
 * test_supvan_job_ends.c holds the errors, the aborts and the link.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "sv_sim.h"

/** @brief The rig: job, printer and clock, off the stack. */
static sv_rig_t s_r;

TEST_CASE(a_print_runs_every_step_in_order)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 1000u));
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK(!r->job.relinked);
    CHECK_EQ(r->job.frames, 3);
    CHECK_EQ(r->job.frame, 3);
    CHECK_EQ(r->job.mat.left, 295u);
    CHECK_EQ(r->job.last.left, 294u);
    /* CHECK, MATERIAL, INQUIRY (ready), START, INQUIRY (printing),
     * INQUIRY (buffer), BULK, 3 frames, BUF_FULL, INQUIRY until idle,
     * MATERIAL (counter) */
    const uint8_t head[] = { SV_C_CHECK,   SV_C_MATERIAL, SV_C_INQUIRY,
                             SV_C_START,   SV_C_INQUIRY,  SV_C_INQUIRY,
                             SV_C_BULK,    SIM_DATA,      SIM_DATA,
                             SIM_DATA,     SV_C_BUF_FULL };
    CHECK(r->sim.nlog > sizeof head + 1u);
    CHECK(memcmp(r->sim.log, head, sizeof head) == 0);
    for (unsigned i = sizeof head; i + 1u < r->sim.nlog; ++i) {
        CHECK_EQ(r->sim.log[i], SV_C_INQUIRY);
    }
    CHECK_EQ(r->sim.log[r->sim.nlog - 1u], SV_C_MATERIAL);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    CHECK_EQ(r->sim.log_p0[6], 512);
    CHECK_EQ(r->sim.log_p1[6], 3);
    CHECK_EQ(r->sim.log_p0[10], RIG_STREAM);
    CHECK_EQ(r->sim.log_p1[10], 60); /* 1,234 / 3 = 411 a buffer */
    CHECK_EQ(r->sim.nfrag, 12u);
    CHECK_EQ(r->sim.bad_frames, 0u);
    CHECK(memcmp(r->sim.stream, r->stream, RIG_STREAM) == 0);
    for (unsigned i = RIG_STREAM; i < 3u * SV_CHUNK; ++i) {
        CHECK_EQ(r->sim.stream[i], 0);
    }
}

TEST_CASE(fragments_are_10ms_apart_and_buf_full_20ms_after_the_last)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    /* the write's answer comes 1ms after the write in the rig */
    for (unsigned i = 1u; i < 12u; ++i) {
        CHECK_EQ(r->sim.frag_at[i] - r->sim.frag_at[i - 1u], 11u);
    }
    CHECK_EQ(r->sim.log_at[10] - r->sim.frag_at[11], 21u);
    /* the polls in step 11 are 100ms after each answer (5ms latency) */
    CHECK_EQ(r->sim.log_at[12] - r->sim.log_at[11], 105u);
    CHECK_EQ(r->sim.log_at[13] - r->sim.log_at[12], 105u);
}

TEST_CASE(a_query_reads_the_device_the_roll_and_the_status_only)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_QUERY, 0u));
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK_EQ(r->sim.nlog, 3u);
    CHECK_EQ(r->sim.log[0], SV_C_CHECK);
    CHECK_EQ(r->sim.log[1], SV_C_MATERIAL);
    CHECK_EQ(r->sim.log[2], SV_C_INQUIRY);
    CHECK_EQ(r->job.mat.w_mm, 40);
    CHECK_EQ(r->job.mat.h_mm, 30);
    CHECK_EQ(r->job.mat.gap_mm, 3);
    CHECK_EQ(r->job.mat.left, 295u);
    CHECK_EQ(r->job.st.count, 42);
    CHECK(!r->job.started);
}

TEST_CASE(a_query_reports_a_roll_out_of_range_and_a_print_refuses_it)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_QUERY, 0u));
    r->sim.roll.w_mm = 14u;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK_EQ(sv_roll_check(&r->job.mat), SV_ERR_SMALL);
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->z.w_mm = 0u; /* laid out for any roll */
    CHECK(sv_job_start(&r->job, SV_JOB_PRINT, &r->z, 0u));
    r->sim.roll.h_mm = 101u;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_ERR_LARGE);
    CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
}

TEST_CASE(a_zeroed_record_is_no_label_before_start)
{
    sv_rig_t *r = &s_r;
    for (int kind = SV_JOB_QUERY; kind <= SV_JOB_PRINT; ++kind) {
        CHECK(rig_start(r, (sv_job_kind_t)kind, 0u));
        memset(&r->sim.roll, 0, sizeof r->sim.roll);
        CHECK(rig_run(r, 5000u));
        CHECK_EQ(r->job.err, SV_ERR_NO_LABEL);
        CHECK_EQ(r->sim.nlog, 2u);
    }
}

TEST_CASE(a_roll_other_than_the_layouts_is_roll_changed_before_start)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.roll.w_mm = 30u;
    r->sim.roll.h_mm = 20u;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_ERR_ROLL);
    CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.roll.h_mm = 31u;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_ERR_ROLL);
}

TEST_CASE(a_counter_that_stays_three_times_is_not_printed)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.prints = false;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_ERR_NOT_PRINTED);
    CHECK_EQ(r->job.last.left, 295u);
    CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL), 1u + 3u);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    const unsigned n = r->sim.nlog;
    CHECK_EQ(r->sim.log_at[n - 1u] - r->sim.log_at[n - 2u], 305u);
    CHECK_EQ(r->sim.log_at[n - 2u] - r->sim.log_at[n - 3u], 305u);
}

TEST_CASE(a_counter_that_moves_on_the_second_read_is_printed)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.counter_lag = 1u;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL), 1u + 2u);
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.counter_lag = 2u;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL), 1u + 3u);
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.counter_lag = 3u;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_ERR_NOT_PRINTED);
}

TEST_CASE(a_changed_uid_gives_no_verdict)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.change_uid = true;
    memcpy(r->sim.uid_after, r->sim.roll.uid, SV_UID_LEN);
    r->sim.uid_after[6] ^= 0x01u;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_ERR_ROLL);
    CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL), 2u);
}

TEST_CASE(a_counter_that_jumps_gives_no_verdict)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.roll.left = 0u; /* the print would read 0xFFFFFFFF */
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_ERR_ROLL);
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.roll.left = 1u;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK_EQ(r->job.last.left, 0u);
}

TEST_CASE(busy_for_4900ms_starts_and_for_5100ms_is_busy)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 100u));
    r->sim.busy_until = 100u + 4900u;
    CHECK(rig_run(r, 30000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK(rig_start(r, SV_JOB_PRINT, 100u));
    r->sim.busy_until = 100u + 5100u;
    CHECK(rig_run(r, 30000u));
    CHECK_EQ(r->job.err, SV_ERR_BUSY);
    CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    CHECK(r->now - 100u >= 5000u);
    CHECK(r->now - 100u < 5200u);
}

TEST_CASE(printing_and_buf_full_are_waited_for)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.printing_polls = 5u;
    r->sim.buf_full_polls = 9u;
    CHECK(rig_run(r, 30000u));
    CHECK_EQ(r->job.err, SV_OK);
    /* START, then 6 polls for printing, 4 more for buf_full, then BULK */
    CHECK_EQ(r->sim.log[3], SV_C_START);
    for (unsigned i = 4u; i < 14u; ++i) {
        CHECK_EQ(r->sim.log[i], SV_C_INQUIRY);
    }
    CHECK_EQ(r->sim.log[14], SV_C_BULK);
}

TEST_CASE(a_job_runs_across_the_clocks_wrap)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0xFFFFFFFFu - 600u));
    r->sim.busy_until = 0xFFFFFFFFu - 600u + 400u;
    CHECK(rig_run(r, 30000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK(rig_start(r, SV_JOB_PRINT, 0xFFFFFFFFu - 2000u));
    r->sim.busy_until = 0xFFFFFFFFu - 2000u + 5100u;
    CHECK(rig_run(r, 30000u));
    CHECK_EQ(r->job.err, SV_ERR_BUSY);
}

TEST_CASE(a_print_without_a_usable_stream_does_not_start)
{
    sv_job_t *j = &s_r.job;
    uint8_t z[4] = { 0 };
    sv_stream_t s = { z, 4u, 1u, 0u, 0u };
    memset(j, 0, sizeof(*j));
    CHECK(!sv_job_start(j, SV_JOB_PRINT, NULL, 0u));
    CHECK_EQ(j->step, SV_STEP_IDLE);
    s.len = 0u;
    CHECK(!sv_job_start(j, SV_JOB_PRINT, &s, 0u));
    s.len = SV_STREAM_MAX + 1u;
    CHECK(!sv_job_start(j, SV_JOB_PRINT, &s, 0u));
    s.len = 4u;
    s.buffers = 0u;
    CHECK(!sv_job_start(j, SV_JOB_PRINT, &s, 0u));
    s.buffers = 1u;
    s.data = NULL;
    CHECK(!sv_job_start(j, SV_JOB_PRINT, &s, 0u));
    CHECK_EQ(j->step, SV_STEP_IDLE);
    s.data = z;
    s.len = SV_STREAM_MAX;
    CHECK(sv_job_start(j, SV_JOB_PRINT, &s, 0u));
    CHECK_EQ(j->frames, 132);
    /* an idle job takes nothing */
    memset(j, 0, sizeof(*j));
    const uint8_t *d = NULL;
    uint16_t n = 0u;
    CHECK(!sv_job_tx(j, 0u, &d, &n));
    sv_job_tick(j, 5000u);
    sv_job_abort(j, 0u);
    sv_job_link(j, false, 0u);
    sv_job_written(j, true, 0u);
    sv_job_rx(j, z, sizeof z, 0u);
    CHECK_EQ(j->step, SV_STEP_IDLE);
    CHECK(!sv_job_ended(j));
}

TEST_CASE(answers_to_other_commands_and_short_records_are_skipped)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.short_mat = 1u;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK_EQ(r->sim.log[1], SV_C_MATERIAL);
    CHECK_EQ(r->sim.log[2], SV_C_MATERIAL);
    /* a stray answer in the middle of a step changes nothing */
    CHECK(rig_start(r, SV_JOB_QUERY, 0u));
    CHECK(rig_run_to(r, SV_STEP_MATERIAL, 1000u));
    uint8_t stray[SV_CMD_LEN];
    sv_cmd_build(stray, SV_C_START, 0u, 0u);
    sv_job_rx(&r->job, stray, sizeof stray, r->now);
    CHECK_EQ(r->job.step, SV_STEP_MATERIAL);
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_OK);
}

int main(void)
{
    RUN(a_print_runs_every_step_in_order);
    RUN(fragments_are_10ms_apart_and_buf_full_20ms_after_the_last);
    RUN(a_query_reads_the_device_the_roll_and_the_status_only);
    RUN(a_query_reports_a_roll_out_of_range_and_a_print_refuses_it);
    RUN(a_zeroed_record_is_no_label_before_start);
    RUN(a_roll_other_than_the_layouts_is_roll_changed_before_start);
    RUN(a_counter_that_stays_three_times_is_not_printed);
    RUN(a_counter_that_moves_on_the_second_read_is_printed);
    RUN(a_changed_uid_gives_no_verdict);
    RUN(a_counter_that_jumps_gives_no_verdict);
    RUN(busy_for_4900ms_starts_and_for_5100ms_is_busy);
    RUN(printing_and_buf_full_are_waited_for);
    RUN(a_job_runs_across_the_clocks_wrap);
    RUN(a_print_without_a_usable_stream_does_not_start);
    RUN(answers_to_other_commands_and_short_records_are_skipped);
    return test_summary("supvan_job");
}
