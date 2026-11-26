/**
 * @file test_prn_job_vs_sv.c
 * @brief Byte identity, layer 3: protocols/supvan's sv_job and the engine
 *        with protocols/printers/supvan-t50m-pro.prn, each against its own
 *        simulated printer (sv_sim.h), through every scenario of
 *        test_supvan_job.c and test_supvan_job_ends.c.  Each scenario runs
 *        twice, once per job; the run fails on the first write whose time,
 *        bytes, length or kind differ, and on any state the scenario looks
 *        at (the step, the end, relinked, counted) that differs.
 *
 * The scenarios are those suites' actions without their checks: the two
 * suites hold sv_job to the protocol, this one holds the engine to sv_job.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "prn_dual.h"
#include "prn_fixture.h"

/** @brief The T50M Pro file. */
static prn_spec_t s_spec;
/** @brief The two runs of a scenario. */
static dual_t s_a;
static dual_t s_b;

/** @brief A scenario: actions on the rig. */
typedef void (*scenario_t)(dual_t *d);

/**
 * @brief Run a scenario on both jobs and compare the logs.
 * @param sc   the scenario
 * @param name its name for the message
 * @return true when the logs are equal
 */
static bool both(scenario_t sc, const char *name)
{
    memset(&s_a, 0, sizeof s_a);
    memset(&s_b, 0, sizeof s_b);
    s_a.prn = false;
    s_b.prn = true;
    s_a.spec = &s_spec;
    s_b.spec = &s_spec;
    sc(&s_a);
    sc(&s_b);
    const unsigned n = (s_a.nlog < s_b.nlog) ? s_a.nlog : s_b.nlog;
    for (unsigned i = 0; i < n; ++i) {
        const dual_ev_t *a = &s_a.log[i];
        const dual_ev_t *b = &s_b.log[i];
        if (memcmp(a, b, sizeof *a) != 0) {
            printf("      %s: entry %u differs: sv_job t=%lu n=%u kind=%u "
                   "err=%u flags=%u, engine t=%lu n=%u kind=%u err=%u "
                   "flags=%u\n",
                   name, i, (unsigned long)a->t, a->n, a->kind, a->err,
                   a->flags, (unsigned long)b->t, b->n, b->kind, b->err,
                   b->flags);
            return false;
        }
    }
    if (s_a.nlog != s_b.nlog || s_a.nlog >= DUAL_LOG_MAX) {
        printf("      %s: %u entries from sv_job, %u from the engine\n", name,
               s_a.nlog, s_b.nlog);
        return false;
    }
    return true;
}

#define BOTH(sc) CHECK(both(sc, #sc))

/* ------------------------------------------------- test_supvan_job.c */

static void print_in_order(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 1000u);
    dual_run(d, 20000u);
}

static void print_at_0(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    dual_run(d, 20000u);
}

static void query_alone(dual_t *d)
{
    dual_start(d, SV_JOB_QUERY, 0u);
    dual_run(d, 5000u);
}

static void roll_out_of_range(dual_t *d)
{
    dual_start(d, SV_JOB_QUERY, 0u);
    d->sim.roll.w_mm = 14u;
    dual_run(d, 5000u);
    dual_start(d, SV_JOB_PRINT, 0u);
    d->z.w_mm = 0u;
    dual_restart(d, SV_JOB_PRINT, 0u);
    d->sim.roll.h_mm = 101u;
    dual_run(d, 5000u);
    /* the other limits, the layout for any roll */
    const uint8_t w[3] = { 49u, 40u, 14u };
    const uint8_t h[3] = { 30u, 19u, 30u };
    for (unsigned i = 0; i < 3u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        d->z.w_mm = 0u;
        d->z.h_mm = 0u;
        dual_restart(d, SV_JOB_PRINT, 0u);
        d->sim.roll.w_mm = w[i];
        d->sim.roll.h_mm = h[i];
        dual_run(d, 5000u);
    }
}

static void zeroed_record(dual_t *d)
{
    for (int kind = SV_JOB_QUERY; kind <= SV_JOB_PRINT; ++kind) {
        dual_start(d, (sv_job_kind_t)kind, 0u);
        memset(&d->sim.roll, 0, sizeof d->sim.roll);
        dual_run(d, 5000u);
    }
}

static void roll_other_than_layout(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.roll.w_mm = 30u;
    d->sim.roll.h_mm = 20u;
    dual_run(d, 5000u);
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.roll.h_mm = 31u;
    dual_run(d, 5000u);
}

static void counter_stays(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.prints = false;
    dual_run(d, 20000u);
}

static void counter_lags(dual_t *d)
{
    for (uint8_t lag = 1u; lag <= 3u; ++lag) {
        dual_start(d, SV_JOB_PRINT, 0u);
        d->sim.counter_lag = lag;
        dual_run(d, 20000u);
    }
}

static void changed_uid(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.change_uid = true;
    memcpy(d->sim.uid_after, d->sim.roll.uid, SV_UID_LEN);
    d->sim.uid_after[6] ^= 0x01u;
    dual_run(d, 20000u);
}

static void counter_jumps(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.roll.left = 0u;
    dual_run(d, 20000u);
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.roll.left = 1u;
    dual_run(d, 20000u);
}

static void busy_4900_and_5100(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 100u);
    d->sim.busy_until = 100u + 4900u;
    dual_run(d, 30000u);
    dual_start(d, SV_JOB_PRINT, 100u);
    d->sim.busy_until = 100u + 5100u;
    dual_run(d, 30000u);
}

static void printing_and_buf_full_waited(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.printing_polls = 5u;
    d->sim.buf_full_polls = 9u;
    dual_run(d, 30000u);
}

static void clock_wrap(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0xFFFFFFFFu - 600u);
    d->sim.busy_until = 0xFFFFFFFFu - 600u + 400u;
    dual_run(d, 30000u);
    dual_start(d, SV_JOB_PRINT, 0xFFFFFFFFu - 2000u);
    d->sim.busy_until = 0xFFFFFFFFu - 2000u + 5100u;
    dual_run(d, 30000u);
}

static void other_answers_and_short_records(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.short_mat = 1u;
    dual_run(d, 20000u);
    dual_start(d, SV_JOB_QUERY, 0u);
    dual_run_to(d, SV_STEP_MATERIAL, 1000u);
    uint8_t stray[SV_CMD_LEN];
    sv_cmd_build(stray, SV_C_START, 0u, 0u);
    dual_rx(d, stray, sizeof stray);
    dual_run(d, 5000u);
}

static void notification_before_write_answer(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.latency_ms = 0u;
    d->ack_ms = 3u;
    dual_run(d, 20000u);
}

static void write_answer_before_notification(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.latency_ms = 10u;
    d->ack_ms = 0u;
    dual_run(d, 20000u);
}

static void label_used_while_busy(dual_t *d)
{
    for (unsigned prints = 0; prints < 2u; ++prints) {
        dual_start(d, SV_JOB_PRINT, 0u);
        d->sim.busy_until = 400u;
        d->sim.busy_drop = 1u;
        d->sim.prints = prints != 0u;
        dual_run(d, 20000u);
    }
}

static void roll_dimensions_on_their_own(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->z.h_mm = 0u;
    dual_restart(d, SV_JOB_PRINT, 0u);
    dual_run(d, 20000u);
    dual_start(d, SV_JOB_PRINT, 0u);
    d->z.w_mm = 0u;
    dual_restart(d, SV_JOB_PRINT, 0u);
    d->sim.roll.h_mm = 20u;
    dual_run(d, 20000u);
    dual_start(d, SV_JOB_PRINT, 0u);
    d->z.w_mm = 0u;
    dual_restart(d, SV_JOB_PRINT, 0u);
    d->sim.roll.w_mm = 48u;
    dual_run(d, 20000u);
}

/* A roll changed while step 3 waits: the counter's baseline read once
 * the printer is idle sees another UID, width or height.  No scenario of
 * test_supvan_job.c reaches these checks of sv_job's on_rebase(). */
static void roll_changed_while_busy(dual_t *d)
{
    for (unsigned i = 0; i < 3u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        d->z.w_mm = 0u;
        d->z.h_mm = 0u;
        dual_restart(d, SV_JOB_PRINT, 0u);
        d->sim.busy_until = 300u;
        dual_run_to(d, SV_STEP_READY, 1000u);
        if (i == 0u) {
            d->sim.roll.uid[0] ^= 0x01u;
        } else if (i == 1u) {
            d->sim.roll.w_mm = 30u;
        } else {
            d->sim.roll.h_mm = 40u;
        }
        dual_run(d, 20000u);
    }
}

/* -------------------------------------------- test_supvan_job_ends.c */

/** @brief The 7 error flags: status byte and bit. */
static const uint8_t k_flag_at[7] = { 16u, 17u, 14u, 14u, 14u, 14u, 15u };
static const uint8_t k_flag_bit[7] = { 0x08u, 0x01u, 0x04u, 0x02u,
                                       0x08u, 0x40u, 0x08u };
/** @brief Steps 4 to 11. */
static const int k_after_start[8] = {
    SV_STEP_START, SV_STEP_PRINTING, SV_STEP_BUFFER, SV_STEP_BULK,
    SV_STEP_DATA,  SV_STEP_PAUSE,    SV_STEP_FULL,   SV_STEP_DONE,
};

static void flags_before_start(dual_t *d)
{
    for (unsigned i = 0; i < 7u; ++i) {
        for (int kind = SV_JOB_QUERY; kind <= SV_JOB_PRINT; ++kind) {
            dual_start(d, (sv_job_kind_t)kind, 0u);
            d->sim.flags[k_flag_at[i] - 14u] = k_flag_bit[i];
            dual_run(d, 10000u);
        }
    }
}

static void flags_in_step_11(dual_t *d)
{
    for (unsigned i = 0; i < 7u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        dual_to(d, SV_STEP_DONE);
        d->sim.flags_at = d->now;
        d->sim.flags[k_flag_at[i] - 14u] = k_flag_bit[i];
        dual_run(d, 20000u);
    }
}

static void cover_wins(dual_t *d)
{
    dual_start(d, SV_JOB_QUERY, 0u);
    d->sim.flags[0] = 0x4Eu;
    d->sim.flags[1] = 0x08u;
    d->sim.flags[2] = 0x08u;
    d->sim.flags[3] = 0x01u;
    dual_run(d, 5000u);
    /* each flag below the one above it */
    for (unsigned i = 1u; i < 7u; ++i) {
        dual_start(d, SV_JOB_QUERY, 0u);
        for (unsigned k = i; k < 7u; ++k) {
            d->sim.flags[k_flag_at[k] - 14u] |= k_flag_bit[k];
        }
        dual_run(d, 5000u);
    }
}

static void check_and_material_twice(dual_t *d)
{
    const uint8_t cmds[2] = { SV_C_CHECK, SV_C_MATERIAL };
    for (unsigned i = 0; i < 2u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        d->sim.silent_cmd = cmds[i];
        dual_run(d, 10000u);
    }
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.silent_cmd = SV_C_CHECK;
    dual_run(d, 500u);
    d->sim.silent_cmd = 0u;
    dual_run(d, 20000u);
}

static void no_answer_later(dual_t *d)
{
    const struct {
        uint8_t cmd;
        uint8_t after;
    } k[] = {
        { SV_C_INQUIRY, 0u }, { SV_C_START, 0u },    { SV_C_INQUIRY, 1u },
        { SV_C_INQUIRY, 2u }, { SV_C_BULK, 0u },     { SV_C_BUF_FULL, 0u },
        { SV_C_INQUIRY, 3u }, { SV_C_MATERIAL, 1u }, { SV_C_MATERIAL, 2u },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        d->sim.silent_cmd = k[i].cmd;
        d->sim.silent_after = k[i].after;
        dual_run(d, 30000u);
    }
}

static void printing_or_buf_full_never(dual_t *d)
{
    for (unsigned i = 0; i < 2u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        if (i == 0u) {
            d->sim.printing_polls = 255u;
        } else {
            d->sim.buf_full_polls = 255u;
        }
        dual_run(d, 20000u);
    }
}

static void data_write_lost(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.write_frag = 6u;
    d->sim.write_do = SIM_W_LOST;
    dual_to(d, SV_STEP_DATA);
    dual_run(d, 20000u);
}

static void write_refused(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.write_cmd = SV_C_CHECK;
    d->sim.write_do = SIM_W_REFUSE;
    dual_run(d, 5000u);
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.write_cmd = SV_C_INQUIRY;
    d->sim.write_do = SIM_W_REFUSE;
    dual_run(d, 5000u);
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.write_frag = 1u;
    d->sim.write_do = SIM_W_REFUSE;
    dual_run(d, 5000u);
}

static void abort_in_steps_4_to_11(dual_t *d)
{
    for (size_t i = 0; i < 8u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        dual_to(d, k_after_start[i]);
        dual_abort(d);
        dual_abort(d);
        dual_run(d, 5000u);
        const uint8_t *p = NULL;
        uint16_t n = 0u;
        (void)dual_tx(d, d->now + 10000u, &p, &n);
        dual_mark(d);
    }
}

static void abort_before_start_or_in_12(dual_t *d)
{
    const int steps[4] = { SV_STEP_CHECK, SV_STEP_MATERIAL, SV_STEP_READY,
                           SV_STEP_COUNTER };
    for (size_t i = 0; i < 4u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        dual_to(d, steps[i]);
        dual_abort(d);
        dual_run(d, 2000u);
    }
    dual_start(d, SV_JOB_PRINT, 0u);
    dual_run_to(d, SV_STEP_START, 5000u);
    dual_abort(d);
    dual_run(d, 2000u);
}

static void stop_refused_lost_or_cut(dual_t *d)
{
    const sim_write_t how[2] = { SIM_W_REFUSE, SIM_W_LOST };
    for (size_t i = 0; i < 2u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        d->sim.write_cmd = SV_C_STOP;
        d->sim.write_do = how[i];
        dual_to(d, SV_STEP_BULK);
        dual_abort(d);
        dual_run(d, 5000u);
    }
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.write_cmd = SV_C_STOP;
    d->sim.write_do = SIM_W_LOST;
    dual_to(d, SV_STEP_PRINTING);
    dual_abort(d);
    dual_run_to(d, SV_STEP_STOP, 100u);
    dual_step_ms(d);
    dual_link(d, false);
}

static void link_down_before_start(dual_t *d)
{
    const int steps[3] = { SV_STEP_CHECK, SV_STEP_MATERIAL, SV_STEP_READY };
    for (size_t i = 0; i < 3u; ++i) {
        for (int kind = SV_JOB_QUERY; kind <= SV_JOB_PRINT; ++kind) {
            dual_start(d, (sv_job_kind_t)kind, 0u);
            dual_to(d, steps[i]);
            dual_link(d, false);
            dual_link(d, true);
            dual_run(d, 1000u);
        }
    }
}

static void link_down_after_start_never_back(dual_t *d)
{
    for (size_t i = 0; i < 8u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        dual_to(d, k_after_start[i]);
        dual_link(d, false);
        dual_run(d, 10000u);
    }
}

static void link_back_inside_5s(dual_t *d)
{
    for (size_t i = 0; i < 8u; ++i) {
        dual_start(d, SV_JOB_PRINT, 0u);
        dual_to(d, k_after_start[i]);
        dual_link(d, false);
        for (unsigned ms = 0; ms < 4999u; ++ms) {
            dual_step_ms(d);
        }
        dual_mark(d);
        dual_link(d, true);
        dual_run(d, 10000u);
    }
}

static void link_down_in_step_12(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.counter_lag = 1u;
    dual_to(d, SV_STEP_COUNTER);
    dual_link(d, false);
    dual_link(d, true);
    dual_run_to(d, SV_STEP_COUNTER, 1000u);
    dual_link(d, false);
    dual_start(d, SV_JOB_PRINT, 0u);
    dual_to(d, SV_STEP_COUNTER);
    dual_link(d, false);
    dual_link(d, true);
    dual_run(d, 5000u);
}

static void link_drop_while_stop_due(dual_t *d)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    dual_to(d, SV_STEP_DONE);
    d->sim.flags_at = d->now;
    d->sim.flags[2] = 0x08u;
    dual_run_to(d, SV_STEP_STOP, 1000u);
    dual_link(d, false);
}

/**
 * @brief test_supvan_job_ends.c's relink_while_printing().
 * @param d        the rig
 * @param print_ms how long the printer prints after BUF_FULL
 * @param prints   whether the label is used
 */
static void relink_while_printing(dual_t *d, uint32_t print_ms, bool prints)
{
    dual_start(d, SV_JOB_PRINT, 0u);
    d->sim.print_ms = print_ms;
    d->sim.prints = prints;
    dual_to(d, SV_STEP_DONE);
    while ((uint32_t)(d->now - d->sim.full_at) < 200u) {
        dual_step_ms(d);
    }
    dual_link(d, false);
    dual_step_ms(d);
    dual_link(d, true);
}

static void reconnect_while_printing(dual_t *d)
{
    relink_while_printing(d, 1700u, true);
    dual_run(d, 20000u);
    relink_while_printing(d, 1700u, false);
    dual_run(d, 20000u);
}

static void printing_stuck_after_reconnect(dual_t *d)
{
    relink_while_printing(d, 60000u, true);
    dual_run(d, 30000u);
}

/* ------------------------------------------------------------- cases */

TEST_CASE(the_t50m_pro_file_parses)
{
    CHECK(prn_load_t50m(&s_spec, NULL, NULL));
    CHECK_EQ(prn_spec_check(&s_spec), 0);
}

TEST_CASE(the_whole_sequence_and_the_counters_verdicts_match_sv_job)
{
    BOTH(print_in_order);
    BOTH(print_at_0);
    BOTH(query_alone);
    BOTH(roll_out_of_range);
    BOTH(zeroed_record);
    BOTH(roll_other_than_layout);
    BOTH(counter_stays);
    BOTH(counter_lags);
    BOTH(changed_uid);
    BOTH(counter_jumps);
    BOTH(busy_4900_and_5100);
    BOTH(printing_and_buf_full_waited);
    BOTH(clock_wrap);
    BOTH(other_answers_and_short_records);
    BOTH(notification_before_write_answer);
    BOTH(write_answer_before_notification);
    BOTH(label_used_while_busy);
    BOTH(roll_dimensions_on_their_own);
    BOTH(roll_changed_while_busy);
}

TEST_CASE(errors_aborts_and_the_link_match_sv_job)
{
    BOTH(flags_before_start);
    BOTH(flags_in_step_11);
    BOTH(cover_wins);
    BOTH(check_and_material_twice);
    BOTH(no_answer_later);
    BOTH(printing_or_buf_full_never);
    BOTH(data_write_lost);
    BOTH(write_refused);
    BOTH(abort_in_steps_4_to_11);
    BOTH(abort_before_start_or_in_12);
    BOTH(stop_refused_lost_or_cut);
    BOTH(link_down_before_start);
    BOTH(link_down_after_start_never_back);
    BOTH(link_back_inside_5s);
    BOTH(link_down_in_step_12);
    BOTH(link_drop_while_stop_due);
    BOTH(reconnect_while_printing);
    BOTH(printing_stuck_after_reconnect);
}

TEST_CASE(a_print_writes_what_the_station_captured)
{
    /* the logs are not empty: a whole print is 15 commands, 12 fragments
     * and the polls of step 11 */
    CHECK(both(print_at_0, "print_at_0"));
    unsigned frags = 0u;
    unsigned cmds = 0u;
    for (unsigned i = 0; i < s_b.nlog; ++i) {
        frags += (s_b.log[i].n == SV_FRAGMENT) ? 1u : 0u;
        cmds += (s_b.log[i].n == SV_CMD_LEN) ? 1u : 0u;
    }
    CHECK_EQ(frags, 12u);
    CHECK(cmds >= 13u);
    CHECK_EQ(s_b.pj.err, PRN_OK);
}

int main(void)
{
    RUN(the_t50m_pro_file_parses);
    RUN(the_whole_sequence_and_the_counters_verdicts_match_sv_job);
    RUN(errors_aborts_and_the_link_match_sv_job);
    RUN(a_print_writes_what_the_station_captured);
    return test_summary("prn_job_vs_sv");
}
