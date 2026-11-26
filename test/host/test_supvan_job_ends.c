/**
 * @file test_supvan_job_ends.c
 * @brief protocols/supvan's job against the simulated printer (sv_sim.h)
 *        where it ends early: each of the 7 error flags before START and
 *        during step 11; no answer at each step; a write refused and a
 *        write lost; an abort at each of steps 4 to 11 sends STOP_PRINT
 *        once, and none before START or in step 12; the link down at each
 *        step, with and without its return inside 5s, and both counter
 *        verdicts after it; STOP_PRINT refused, lost and cut by the link.
 *
 * test_supvan_job.c holds the whole sequence and the counter's verdicts.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "sv_sim.h"

/** @brief The rig: job, printer and clock, off the stack. */
static sv_rig_t s_r;

/** @brief One status flag: its byte (14 to 17), its bit and its error. */
typedef struct {
    uint8_t at;   /**< status byte, 14 to 17 */
    uint8_t bit;  /**< the bit's mask */
    uint8_t err;  /**< the sv_err_t it ends a job with */
} flag_t;

/** @brief The 7 error flags of the report and the error of each. */
static const flag_t k_flags[7] = {
    { 16u, 0x08u, SV_ERR_COVER },    /* cover_open */
    { 17u, 0x01u, SV_ERR_NO_LABEL }, /* label_not_installed */
    { 14u, 0x04u, SV_ERR_NO_LABEL }, /* label_end */
    { 14u, 0x02u, SV_ERR_LABEL },    /* label_rw_error */
    { 14u, 0x08u, SV_ERR_LABEL },    /* label_mode_error */
    { 14u, 0x40u, SV_ERR_BATTERY },  /* low_battery */
    { 15u, 0x08u, SV_ERR_HOT },      /* head_temp_high */
};

/** @brief Steps 4 to 11, where an end sends STOP_PRINT. */
static const sv_step_t k_after_start[8] = {
    SV_STEP_START, SV_STEP_PRINTING, SV_STEP_BUFFER, SV_STEP_BULK,
    SV_STEP_DATA,  SV_STEP_PAUSE,    SV_STEP_FULL,   SV_STEP_DONE,
};

/**
 * @brief Run a PRINT to @p step; in step 4 on until START is out, so the
 *        printer has it.
 * @param r    the rig
 * @param step the step
 * @return true when the job is there
 */
static bool run_to(sv_rig_t *r, sv_step_t step)
{
    if (!rig_run_to(r, step, 20000u)) {
        return false;
    }
    while (step == SV_STEP_START && !r->job.started
           && r->job.step == SV_STEP_START) {
        rig_step(r);
    }
    return r->job.step == step;
}

/**
 * @brief The log's index of the last entry of @p cmd.
 * @param s   the simulator
 * @param cmd the command
 * @return the index, or SIM_LOG_MAX when none
 */
static unsigned last_of(const sv_sim_t *s, uint8_t cmd)
{
    unsigned at = SIM_LOG_MAX;
    for (unsigned i = 0; i < s->nlog; ++i) {
        if (s->log[i] == cmd) {
            at = i;
        }
    }
    return at;
}

TEST_CASE(each_error_flag_before_start_ends_the_job_without_stop)
{
    sv_rig_t *r = &s_r;
    for (unsigned i = 0; i < 7u; ++i) {
        for (int kind = SV_JOB_QUERY; kind <= SV_JOB_PRINT; ++kind) {
            CHECK(rig_start(r, (sv_job_kind_t)kind, 0u));
            r->sim.flags[k_flags[i].at - 14u] = k_flags[i].bit;
            CHECK(rig_run(r, 10000u));
            CHECK_EQ(r->job.err, k_flags[i].err);
            CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
            CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
            CHECK_EQ(sim_count(&r->sim, SV_C_INQUIRY), 1u);
        }
    }
}

TEST_CASE(each_error_flag_during_step_11_sends_stop_once)
{
    sv_rig_t *r = &s_r;
    for (unsigned i = 0; i < 7u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        CHECK(run_to(r, SV_STEP_DONE));
        r->sim.flags_at = r->now;
        r->sim.flags[k_flags[i].at - 14u] = k_flags[i].bit;
        CHECK(rig_run(r, 20000u));
        CHECK_EQ(r->job.err, k_flags[i].err);
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
        CHECK_EQ(r->sim.log[r->sim.nlog - 1u], SV_C_STOP);
        /* no counter read after an error: steps 2 and 3 read it */
        CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL), 2u);
    }
}

TEST_CASE(cover_wins_over_the_other_flags)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_QUERY, 0u));
    r->sim.flags[0] = 0x4Eu; /* label_rw, label_end, label_mode, battery */
    r->sim.flags[1] = 0x08u;
    r->sim.flags[2] = 0x08u;
    r->sim.flags[3] = 0x01u;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_ERR_COVER);
}

TEST_CASE(check_and_material_are_sent_twice_then_silent)
{
    sv_rig_t *r = &s_r;
    const uint8_t cmds[2] = { SV_C_CHECK, SV_C_MATERIAL };
    for (unsigned i = 0; i < 2u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        r->sim.silent_cmd = cmds[i];
        CHECK(rig_run(r, 10000u));
        CHECK_EQ(r->job.err, SV_ERR_SILENT);
        CHECK_EQ(sim_count(&r->sim, cmds[i]), 2u);
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
        /* 1s for each answer */
        const unsigned last = last_of(&r->sim, cmds[i]);
        CHECK_EQ(r->sim.log_at[last] - r->sim.log_at[last - 1u], 1000u);
        CHECK_EQ(r->now - 1u - r->sim.log_at[last], 1000u);
    }
    /* one answer missed, the retry answered: the job goes on */
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.silent_cmd = SV_C_CHECK;
    CHECK(!rig_run(r, 500u));
    r->sim.silent_cmd = 0u;
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK_EQ(sim_count(&r->sim, SV_C_CHECK), 2u);
}

TEST_CASE(no_answer_at_each_later_step_is_silent)
{
    sv_rig_t *r = &s_r;
    /* the command left unanswered, after how many answers of it, whether
     * STOP_PRINT follows */
    const struct {
        uint8_t cmd;
        uint8_t after;
        bool stop;
        uint8_t err;
    } k[] = {
        { SV_C_INQUIRY, 0u, false, SV_ERR_SILENT },  /* 3 READY */
        { SV_C_START, 0u, true, SV_ERR_SILENT },     /* 4 START */
        { SV_C_INQUIRY, 1u, true, SV_ERR_SILENT },   /* 5 PRINTING */
        { SV_C_INQUIRY, 2u, true, SV_ERR_SILENT },   /* 6 BUFFER */
        { SV_C_BULK, 0u, true, SV_ERR_SILENT },      /* 7 BULK */
        { SV_C_BUF_FULL, 0u, true, SV_ERR_SILENT },  /* 10 FULL */
        { SV_C_INQUIRY, 3u, true, SV_ERR_SILENT },   /* 11 DONE */
        { SV_C_MATERIAL, 1u, false, SV_ERR_SILENT }, /* 3 c0 once idle */
        { SV_C_MATERIAL, 2u, false, SV_ERR_SILENT }, /* 12 COUNTER */
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        r->sim.silent_cmd = k[i].cmd;
        r->sim.silent_after = k[i].after;
        CHECK(rig_run(r, 30000u));
        CHECK_EQ(r->job.err, k[i].err);
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), (k[i].stop ? 1u : 0u));
        CHECK_EQ(sim_count(&r->sim, k[i].cmd), (unsigned)k[i].after + 1u);
    }
}

TEST_CASE(printing_or_buf_full_that_never_comes_is_silent_after_3s)
{
    sv_rig_t *r = &s_r;
    for (unsigned i = 0; i < 2u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        if (i == 0u) {
            r->sim.printing_polls = 255u; /* step 5: printing never set */
        } else {
            r->sim.buf_full_polls = 255u; /* step 6: buf_full stays */
        }
        CHECK(rig_run(r, 20000u));
        CHECK_EQ(r->job.err, SV_ERR_SILENT);
        CHECK_EQ(sim_count(&r->sim, SV_C_BULK), 0u);
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
        /* the step is entered on an answer 5ms or more after START and
         * gives up 3s on, not at a poll's answer: 3,005 to 3,020ms */
        const uint32_t from = r->sim.log_at[last_of(&r->sim, SV_C_START)];
        const uint32_t to = r->sim.log_at[last_of(&r->sim, SV_C_STOP)];
        CHECK(to - from >= 5u + SV_PRINTING_MS);
        CHECK(to - from <= 20u + SV_PRINTING_MS);
    }
}

TEST_CASE(a_data_write_lost_is_silent_after_1s_and_stops)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.write_frag = 6u;
    r->sim.write_do = SIM_W_LOST;
    CHECK(run_to(r, SV_STEP_DATA));
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_ERR_SILENT);
    CHECK_EQ(r->sim.nfrag, 6u);
    CHECK_EQ(r->job.frame, 1);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
    CHECK_EQ(r->sim.log_at[last_of(&r->sim, SV_C_STOP)]
                 - r->sim.frag_at[4],
             11u + 1000u);
    CHECK_EQ(sim_count(&r->sim, SV_C_BUF_FULL), 0u);
}

TEST_CASE(a_write_refused_ends_the_job_with_stop_after_start_only)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.write_cmd = SV_C_CHECK;
    r->sim.write_do = SIM_W_REFUSE;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_ERR_WRITE);
    CHECK_EQ(r->sim.nlog, 0u);
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.write_cmd = SV_C_INQUIRY;
    r->sim.write_do = SIM_W_REFUSE;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_ERR_WRITE);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.write_frag = 1u;
    r->sim.write_do = SIM_W_REFUSE;
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_ERR_WRITE);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
    CHECK_EQ(r->sim.nfrag, 1u);
}

TEST_CASE(an_abort_at_each_of_steps_4_to_11_sends_stop_once)
{
    sv_rig_t *r = &s_r;
    for (size_t i = 0; i < 8u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        CHECK(run_to(r, k_after_start[i]));
        sv_job_abort(&r->job, r->now);
        sv_job_abort(&r->job, r->now); /* a second tap changes nothing */
        CHECK(rig_run(r, 5000u));
        CHECK_EQ(r->job.err, SV_ERR_ABORTED);
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
        CHECK_EQ(r->sim.log[r->sim.nlog - 1u], SV_C_STOP);
        CHECK_EQ(sim_count(&r->sim, SV_C_BUF_FULL),
                 ((k_after_start[i] >= SV_STEP_FULL) ? 1u : 0u));
        /* nothing is written once the job has ended */
        const uint8_t *d = NULL;
        uint16_t n = 0u;
        CHECK(!sv_job_tx(&r->job, r->now + 10000u, &d, &n));
    }
}

TEST_CASE(an_abort_before_start_or_in_step_12_sends_no_stop)
{
    sv_rig_t *r = &s_r;
    const sv_step_t steps[4] = { SV_STEP_CHECK, SV_STEP_MATERIAL,
                                 SV_STEP_READY, SV_STEP_COUNTER };
    for (size_t i = 0; i < 4u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        CHECK(run_to(r, steps[i]));
        sv_job_abort(&r->job, r->now);
        CHECK(sv_job_ended(&r->job));
        CHECK_EQ(r->job.err, SV_ERR_ABORTED);
        CHECK(rig_run(r, 2000u));
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    }
    /* step 4 entered, START not yet written: nothing to stop */
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    CHECK(rig_run_to(r, SV_STEP_START, 5000u));
    CHECK(!r->job.started);
    sv_job_abort(&r->job, r->now);
    CHECK(rig_run(r, 2000u));
    CHECK_EQ(r->job.err, SV_ERR_ABORTED);
    CHECK_EQ(sim_count(&r->sim, SV_C_START), 0u);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
}

TEST_CASE(stop_refused_lost_or_cut_still_ends_the_job)
{
    sv_rig_t *r = &s_r;
    const sim_write_t how[2] = { SIM_W_REFUSE, SIM_W_LOST };
    for (size_t i = 0; i < 2u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        r->sim.write_cmd = SV_C_STOP;
        r->sim.write_do = how[i];
        CHECK(run_to(r, SV_STEP_BULK));
        sv_job_abort(&r->job, r->now);
        CHECK(rig_run(r, 5000u));
        CHECK_EQ(r->job.err, SV_ERR_ABORTED);
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    }
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.write_cmd = SV_C_STOP;
    r->sim.write_do = SIM_W_LOST;
    CHECK(run_to(r, SV_STEP_PRINTING));
    sv_job_abort(&r->job, r->now);
    CHECK(rig_run_to(r, SV_STEP_STOP, 100u));
    rig_step(r);
    rig_link(r, false);
    CHECK(sv_job_ended(&r->job));
    CHECK_EQ(r->job.err, SV_ERR_ABORTED);
}

TEST_CASE(the_link_down_before_start_ends_the_job_at_once)
{
    sv_rig_t *r = &s_r;
    const sv_step_t steps[3] = { SV_STEP_CHECK, SV_STEP_MATERIAL,
                                 SV_STEP_READY };
    for (size_t i = 0; i < 3u; ++i) {
        for (int kind = SV_JOB_QUERY; kind <= SV_JOB_PRINT; ++kind) {
            CHECK(rig_start(r, (sv_job_kind_t)kind, 0u));
            CHECK(run_to(r, steps[i]));
            rig_link(r, false);
            CHECK(sv_job_ended(&r->job));
            CHECK_EQ(r->job.err, SV_ERR_LINK);
            CHECK(!r->job.relinked);
            rig_link(r, true);
            CHECK(rig_run(r, 1000u));
            CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
        }
    }
}

TEST_CASE(the_link_down_after_start_and_never_back_is_link_after_5s)
{
    sv_rig_t *r = &s_r;
    for (size_t i = 0; i < 8u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        CHECK(run_to(r, k_after_start[i]));
        rig_link(r, false);
        CHECK_EQ(r->job.step, SV_STEP_RELINK);
        const uint32_t t0 = r->now;
        CHECK(rig_run(r, 10000u));
        CHECK_EQ(r->job.err, SV_ERR_LINK);
        CHECK(!r->job.relinked);
        CHECK(!r->job.counted);
        CHECK_EQ(r->now - 1u - t0, SV_RELINK_MS);
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    }
}

TEST_CASE(the_link_back_inside_5s_reads_the_counter_for_the_verdict)
{
    sv_rig_t *r = &s_r;
    for (size_t i = 0; i < 8u; ++i) {
        CHECK(rig_start(r, SV_JOB_PRINT, 0u));
        CHECK(run_to(r, k_after_start[i]));
        const unsigned before = sim_count(&r->sim, SV_C_MATERIAL);
        rig_link(r, false);
        for (unsigned ms = 0; ms < 4999u; ++ms) {
            rig_step(r);
        }
        CHECK(!sv_job_ended(&r->job));
        rig_link(r, true);
        CHECK(rig_run(r, 10000u));
        CHECK(r->job.relinked);
        /* the printer prints a whole stream (BUF_FULL is written on
         * entry to step 10) and only that: the counter decides, and the
         * job is never sent again */
        const bool full = k_after_start[i] >= SV_STEP_FULL;
        CHECK_EQ(r->job.err, (full ? SV_OK : SV_ERR_LINK));
        /* the counter read 3 times and unchanged: a verdict */
        CHECK_EQ(r->job.counted, !full);
        CHECK_EQ(sim_count(&r->sim, SV_C_START), 1u);
        CHECK_EQ(sim_count(&r->sim, SV_C_BULK),
                 ((k_after_start[i] >= SV_STEP_DATA) ? 1u : 0u));
        CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL) - before,
                 (full ? 1u : SV_COUNTER_READS));
        CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
    }
}

TEST_CASE(the_link_down_in_step_12_relinks_once)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    r->sim.counter_lag = 1u;
    CHECK(run_to(r, SV_STEP_COUNTER));
    rig_link(r, false);
    CHECK_EQ(r->job.step, SV_STEP_RELINK);
    rig_link(r, true);
    /* BUF_FULL was written: step 11 checks the printer is idle first */
    CHECK_EQ(r->job.step, SV_STEP_DONE);
    CHECK(rig_run_to(r, SV_STEP_COUNTER, 1000u));
    /* a second drop after the relink ends the job, with no verdict */
    rig_link(r, false);
    CHECK(sv_job_ended(&r->job));
    CHECK_EQ(r->job.err, SV_ERR_LINK);
    CHECK(!r->job.counted);
    /* the link back with the printed label: PRINTED */
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    CHECK(run_to(r, SV_STEP_COUNTER));
    rig_link(r, false);
    rig_link(r, true);
    CHECK(rig_run(r, 5000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK(r->job.relinked);
}

TEST_CASE(a_link_drop_while_stop_is_due_ends_with_the_first_error)
{
    sv_rig_t *r = &s_r;
    CHECK(rig_start(r, SV_JOB_PRINT, 0u));
    CHECK(run_to(r, SV_STEP_DONE));
    r->sim.flags_at = r->now;
    r->sim.flags[2] = 0x08u;
    CHECK(rig_run_to(r, SV_STEP_STOP, 1000u));
    rig_link(r, false);
    CHECK(sv_job_ended(&r->job));
    CHECK_EQ(r->job.err, SV_ERR_COVER);
}

/**
 * @brief Run a PRINT to step 11 with @p print_ms of printing, drop the
 *        link 200ms after BUF_FULL and restore it.
 * @param r        the rig
 * @param print_ms how long the printer prints after BUF_FULL
 * @param prints   whether the label is used
 * @return true when the job was in step 11 at the drop
 */
static bool relink_while_printing(sv_rig_t *r, uint32_t print_ms, bool prints)
{
    if (!rig_start(r, SV_JOB_PRINT, 0u)) {
        return false;
    }
    r->sim.print_ms = print_ms;
    r->sim.prints = prints;
    if (!run_to(r, SV_STEP_DONE)) {
        return false;
    }
    while ((uint32_t)(r->now - r->sim.full_at) < 200u) {
        rig_step(r);
    }
    rig_link(r, false);
    rig_step(r);
    rig_link(r, true);
    return true;
}

TEST_CASE(a_reconnect_while_the_label_prints_waits_for_it)
{
    sv_rig_t *r = &s_r;
    /* 1.7s of printing, the link back 200ms in: 1.5s still to go */
    CHECK(relink_while_printing(r, 1700u, true));
    CHECK_EQ(r->job.step, SV_STEP_DONE);
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_OK);
    CHECK(r->job.relinked);
    CHECK_EQ(r->job.last.left, 294u);
    /* the counter is read only once the printer is idle */
    const unsigned c = last_of(&r->sim, SV_C_MATERIAL);
    CHECK(c < SIM_LOG_MAX && r->sim.log_at[c] - r->sim.full_at >= 1700u);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 0u);
}

TEST_CASE(a_reconnect_while_a_label_that_fails_prints_is_link_lost)
{
    sv_rig_t *r = &s_r;
    CHECK(relink_while_printing(r, 1700u, false));
    CHECK(rig_run(r, 20000u));
    CHECK_EQ(r->job.err, SV_ERR_LINK);
    CHECK(r->job.relinked);
    /* 3 counter reads after printing, not during it */
    unsigned first = SIM_LOG_MAX;
    for (unsigned i = r->sim.nlog; i-- > 0u;) {
        if (r->sim.log[i] == SV_C_MATERIAL
            && r->sim.log_at[i] > r->sim.full_at) {
            first = i;
        }
    }
    CHECK(first < SIM_LOG_MAX
          && r->sim.log_at[first] - r->sim.full_at >= 1700u);
    CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL), 2u + SV_COUNTER_READS);
}

TEST_CASE(printing_stuck_after_a_reconnect_is_silent_after_15s)
{
    sv_rig_t *r = &s_r;
    CHECK(relink_while_printing(r, 60000u, true));
    const uint32_t t0 = r->now;
    CHECK(rig_run(r, 30000u));
    CHECK_EQ(r->job.err, SV_ERR_SILENT);
    CHECK(r->job.relinked);
    CHECK(r->now - t0 >= SV_DONE_MS);
    CHECK(r->now - t0 < SV_DONE_MS + 200u);
    CHECK_EQ(sim_count(&r->sim, SV_C_STOP), 1u);
    /* no counter read: no verdict from a label still under the head */
    CHECK_EQ(sim_count(&r->sim, SV_C_MATERIAL), 2u);
}

int main(void)
{
    RUN(each_error_flag_before_start_ends_the_job_without_stop);
    RUN(each_error_flag_during_step_11_sends_stop_once);
    RUN(cover_wins_over_the_other_flags);
    RUN(check_and_material_are_sent_twice_then_silent);
    RUN(no_answer_at_each_later_step_is_silent);
    RUN(printing_or_buf_full_that_never_comes_is_silent_after_3s);
    RUN(a_data_write_lost_is_silent_after_1s_and_stops);
    RUN(a_write_refused_ends_the_job_with_stop_after_start_only);
    RUN(an_abort_at_each_of_steps_4_to_11_sends_stop_once);
    RUN(an_abort_before_start_or_in_step_12_sends_no_stop);
    RUN(stop_refused_lost_or_cut_still_ends_the_job);
    RUN(the_link_down_before_start_ends_the_job_at_once);
    RUN(the_link_down_after_start_and_never_back_is_link_after_5s);
    RUN(the_link_back_inside_5s_reads_the_counter_for_the_verdict);
    RUN(the_link_down_in_step_12_relinks_once);
    RUN(a_link_drop_while_stop_is_due_ends_with_the_first_error);
    RUN(a_reconnect_while_the_label_prints_waits_for_it);
    RUN(a_reconnect_while_a_label_that_fails_prints_is_link_lost);
    RUN(printing_stuck_after_a_reconnect_is_silent_after_15s);
    return test_summary("supvan_job_ends");
}
