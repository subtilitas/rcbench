/*
 * KST session against a simulated servo: entry, the read passes, a plan's
 * writes with read-back, retry and roll-back, the full verify, and what the
 * master does on the wire while it runs them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_session.h"
#include "kst_sim.h"

static sim_t g_sim;
static sim_t g_sim_b;
static kst_session_t g_ses;
static kst_session_t g_ses_b;

#define BIT(reg) ((uint32_t)1 << (reg))

/* A servo and a session that has not entered yet. */
static void fresh(sim_t *sim, kst_session_t *s)
{
    kst_driver_t d;

    sim_init(sim);
    d = sim_driver(sim);
    CHECK(kst_session_init(s, &d));
}

static void enter(sim_t *sim, kst_session_t *s)
{
    CHECK_EQ(kst_session_enter(s), KST_SES_BUSY);
    CHECK_EQ(sim_run(sim, s), KST_SES_OK);
    /* The gaps of the entry sequence are its own; the cases measure what
     * follows. */
    sim->min_gap_us = 0xFFFFFFFFu;
    sim->min_gap_after_silence_us = 0xFFFFFFFFu;
}

static void read_all(sim_t *sim, kst_session_t *s)
{
    CHECK_EQ(kst_session_read_all(s), KST_SES_BUSY);
    CHECK_EQ(sim_run(sim, s), KST_SES_OK);
}

/* In programming mode with the image read. */
static void up(sim_t *sim, kst_session_t *s)
{
    fresh(sim, s);
    enter(sim, s);
    read_all(sim, s);
}

static kst_ses_t run_plan(sim_t *sim, kst_session_t *s, const kst_plan_t *plan)
{
    const kst_ses_t r = kst_session_write(s, plan, false);

    return r == KST_SES_BUSY ? sim_run(sim, s) : r;
}

/* A plan that sets one field of the session's image. */
static kst_plan_t edit_plan(const kst_session_t *s, kst_field_id_t id,
                            unsigned raw)
{
    kst_image_t target = s->image;
    kst_plan_t plan;

    CHECK(kst_field_edit(&target, id, (uint16_t)raw));
    CHECK_EQ(kst_plan_edit(&s->image, &target, 0, &plan, NULL), KST_PLAN_OK);
    return plan;
}

/* What no session may do, whatever the servo does. */
static void conduct(const sim_t *sim)
{
    CHECK_EQ(sim->n_bad_frames, 0);
    CHECK_EQ(sim->n_bad_writes, 0);
    CHECK_EQ(sim->n_overlap, 0);
    CHECK(sim->min_gap_us >= KST_GAP_US);
    CHECK(sim->min_gap_after_silence_us >= KST_GAP_AFTER_FAULT_US);
}

static bool servo_holds(const sim_t *sim, const kst_image_t *img)
{
    return memcmp(sim->reg, img->r, KST_REG_COUNT) == 0;
}

/* The next 3 reads of @p reg after @p skip answer 3 different wrong
 * values: with 2 true reads left in 5, no 3 agree. */
static void spoil_reads(sim_t *sim, int reg, unsigned skip)
{
    sim_add_fault(sim, SIM_READ, reg, skip, 1, SIM_F_READ_VALUE, 0xE1);
    sim_add_fault(sim, SIM_READ, reg, skip, 1, SIM_F_READ_VALUE, 0xE2);
    sim_add_fault(sim, SIM_READ, reg, skip, 1, SIM_F_READ_VALUE, 0xE3);
}

/* --- init and arguments --------------------------------------------------------- */

TEST_CASE(a_driver_without_all_3_functions_is_refused)
{
    sim_init(&g_sim);
    {
        kst_driver_t d = sim_driver(&g_sim);

        CHECK(!kst_session_init(NULL, &d));
        CHECK(!kst_session_init(&g_ses, NULL));
        d.start = NULL;
        CHECK(!kst_session_init(&g_ses, &d));
        d = sim_driver(&g_sim);
        d.poll = NULL;
        CHECK(!kst_session_init(&g_ses, &d));
        d = sim_driver(&g_sim);
        d.now_us = NULL;
        CHECK(!kst_session_init(&g_ses, &d));
        /* Such a session starts nothing. */
        CHECK_EQ(kst_session_enter(&g_ses), KST_SES_ERR_ARG);
        CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_ERR_ARG);
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_OK);
        d = sim_driver(&g_sim);
        CHECK(kst_session_init(&g_ses, &d));
    }
    CHECK_EQ(g_ses.in_mode, 0);
    CHECK_EQ(g_ses.has_image, 0);
    CHECK_EQ(g_ses.has_backup, 0);
    CHECK_EQ(g_ses.locked, 0);
    CHECK_EQ(g_sim.n_frames, 0);
}

TEST_CASE(missing_arguments_are_refused)
{
    const kst_image_t img = sim_bench_image();
    kst_plan_t plan;

    CHECK_EQ(kst_session_enter(NULL), KST_SES_ERR_ARG);
    CHECK_EQ(kst_session_read_all(NULL), KST_SES_ERR_ARG);
    CHECK_EQ(kst_session_verify(NULL, &img), KST_SES_ERR_ARG);
    CHECK_EQ(kst_plan_release_pairing(&img, &plan), KST_PLAN_OK);
    CHECK_EQ(kst_session_write(NULL, &plan, false), KST_SES_ERR_ARG);
    CHECK_EQ(kst_session_step(NULL), KST_SES_ERR_ARG);
    kst_session_abort(NULL);
    kst_session_power_cycled(NULL);

    up(&g_sim, &g_ses);
    CHECK_EQ(kst_session_verify(&g_ses, NULL), KST_SES_ERR_ARG);
    CHECK_EQ(g_ses.result, KST_SES_ERR_ARG);
    CHECK_EQ(kst_session_write(&g_ses, NULL, false), KST_SES_ERR_ARG);
    CHECK_EQ(kst_session_step(&g_ses), KST_SES_ERR_ARG);
}

TEST_CASE(nothing_but_enter_runs_outside_programming_mode)
{
    const kst_image_t img = sim_bench_image();
    kst_plan_t plan;

    fresh(&g_sim, &g_ses);
    CHECK_EQ(kst_plan_release_pairing(&img, &plan), KST_PLAN_OK);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(g_ses.result, KST_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(kst_session_verify(&g_ses, &img), KST_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(kst_session_step(&g_ses), KST_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(g_sim.n_frames, 0);
}

TEST_CASE(one_operation_runs_at_a_time)
{
    const kst_image_t img = sim_bench_image();
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    CHECK_EQ(kst_plan_release_pairing(&img, &plan), KST_PLAN_OK);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_ERR_BUSY);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_ERR_BUSY);
    CHECK_EQ(kst_session_verify(&g_ses, &img), KST_SES_ERR_BUSY);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BUSY);
    /* The running operation is not disturbed. */
    CHECK_EQ(g_ses.result, KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_sim.n_reads, 2u * 96u);
    conduct(&g_sim);
}

/* --- entry --------------------------------------------------------------------- */

TEST_CASE(entry_holds_the_line_low_100_ms_then_reads_and_syncs)
{
    fresh(&g_sim, &g_ses);
    g_sim.now_us = 777;
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(g_ses.result, KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_ses.result, KST_SES_OK);
    CHECK_EQ(g_ses.in_mode, 1);
    CHECK(g_sim.in_mode);
    CHECK_EQ(g_sim.n_frames, 2);
    CHECK_EQ(g_sim.log[0].kind, SIM_READ);
    CHECK_EQ(g_sim.log[0].reg, 0x01);
    CHECK(g_sim.log[0].t_us - 777u >= KST_ENTRY_LOW_US);
    CHECK(g_sim.log[0].t_us - 777u < KST_ENTRY_LOW_US + 200u);
    CHECK_EQ(g_sim.log[1].kind, SIM_SYNC);
    /* The acknowledge: flags 10, value 00. */
    CHECK_EQ(g_ses.last_reply.result, KST_RX_OK);
    CHECK_EQ(g_ses.last_reply.flags, 2);
    CHECK_EQ(g_ses.last_reply.value, 0);
    /* 3 ms after the read that nothing answered, not 20 ms. */
    CHECK(g_sim.min_gap_after_silence_us >= KST_GAP_US);
    CHECK(g_sim.min_gap_after_silence_us < KST_GAP_US + 200u);
    CHECK_EQ(g_ses.stats.replies, 1);
    CHECK_EQ(g_ses.stats.faults, 1);
    CHECK_EQ(g_sim.n_bad_frames, 0);
    CHECK_EQ(g_sim.n_overlap, 0);
    /* The result again on every later call. */
    CHECK_EQ(kst_session_step(&g_ses), KST_SES_OK);
    CHECK_EQ(g_sim.n_frames, 2);
}

TEST_CASE(a_servo_already_in_the_mode_answers_the_first_read)
{
    fresh(&g_sim, &g_ses);
    g_sim.in_mode = true;
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_ses.in_mode, 1);
    CHECK_EQ(g_sim.n_frames, 1);
    CHECK_EQ(g_sim.n_syncs, 0);
    CHECK_EQ(g_ses.last_reply.flags, 3);
    CHECK_EQ(g_ses.last_reply.value, 0xF5);
}

TEST_CASE(a_servo_in_the_mode_does_not_acknowledge_a_sync)
{
    /* The first read is lost; the sync is not answered; the last read is. */
    fresh(&g_sim, &g_ses);
    g_sim.in_mode = true;
    sim_add_fault(&g_sim, SIM_READ, -1, 0, 1, SIM_F_NO_REPLY, 0);
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_ses.in_mode, 1);
    CHECK_EQ(g_sim.n_frames, 3);
    CHECK_EQ(g_sim.log[0].kind, SIM_READ);
    CHECK_EQ(g_sim.log[1].kind, SIM_SYNC);
    CHECK_EQ(g_sim.log[2].kind, SIM_READ);
    CHECK_EQ(g_sim.log[2].reg, 0x01);
    CHECK(g_sim.min_gap_us >= KST_GAP_US);
    CHECK(g_sim.min_gap_after_silence_us < KST_GAP_US + 200u);
}

TEST_CASE(a_lost_acknowledge_is_found_by_the_last_read)
{
    fresh(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_SYNC, -1, 0, 1, SIM_F_NO_REPLY, 0);
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_ses.in_mode, 1);
    CHECK_EQ(g_sim.n_frames, 3);
}

TEST_CASE(no_servo_ends_the_entry_after_read_sync_read)
{
    fresh(&g_sim, &g_ses);
    g_sim.powered = false;
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_NO_SERVO);
    CHECK_EQ(g_ses.result, KST_SES_ERR_NO_SERVO);
    CHECK_EQ(g_ses.in_mode, 0);
    CHECK_EQ(g_sim.n_frames, 3);
    CHECK_EQ(g_ses.last_reply.result, KST_RX_NO_REPLY);
    CHECK_EQ(g_ses.stats.faults, 3);
    CHECK_EQ(g_ses.stats.replies, 0);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_ERR_NOT_IN_MODE);
    /* A second try is a whole sequence again. */
    g_sim.powered = true;
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_sim.n_frames, 5);
    CHECK(g_sim.log[3].t_us - g_sim.log[2].t_us >= KST_ENTRY_LOW_US);
    CHECK_EQ(g_sim.n_overlap, 0);
}

TEST_CASE(an_acknowledge_at_another_bit_rate_is_no_entry)
{
    /* Half-cell 51.15 us: half the bit rate.  No other rate is tried. */
    fresh(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_SYNC, -1, 0, 1, SIM_F_SHAPE_HALF, 51150);
    sim_add_fault(&g_sim, SIM_READ, -1, 0, 2, SIM_F_NO_REPLY, 0);
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_NO_SERVO);
    CHECK_EQ(g_ses.in_mode, 0);
    CHECK_EQ(g_sim.n_frames, 3);
}

/* --- read all ------------------------------------------------------------------ */

TEST_CASE(read_all_is_3_passes_over_32_registers)
{
    const kst_image_t bench = sim_bench_image();

    up(&g_sim, &g_ses);
    CHECK_EQ(g_sim.n_reads, 96);
    CHECK_EQ(g_sim.n_frames, 2 + 96);
    for (unsigned i = 0; i < 96u; ++i) {
        CHECK_EQ(g_sim.log[2u + i].kind, SIM_READ);
        CHECK_EQ(g_sim.log[2u + i].reg, i % 32u);
    }
    CHECK_EQ(g_ses.has_image, 1);
    CHECK_EQ(g_ses.has_backup, 1);
    CHECK_EQ(g_ses.fingerprint_ok, 1);
    CHECK_EQ(g_ses.fingerprint.rules, 0);
    CHECK_EQ(g_ses.bad_regs, 0);
    CHECK_EQ(memcmp(&g_ses.image, &bench, sizeof(bench)), 0);
    CHECK_EQ(memcmp(&g_ses.backup, &bench, sizeof(bench)), 0);
    CHECK_EQ(g_ses.locked, 0);
    /* Timing of the reads, as the servo sends them. */
    CHECK_EQ(g_ses.stats.replies, 97);
    CHECK_EQ(g_ses.stats.faults, 1);
    CHECK(g_ses.stats.delay_min_ns >= 437000u);
    CHECK(g_ses.stats.delay_max_ns <= 439000u);
    CHECK(g_ses.stats.delay_min_ns <= g_ses.stats.delay_max_ns);
    CHECK(g_ses.stats.half_min_ns >= 25500u);
    CHECK(g_ses.stats.half_max_ns <= 25650u);
    CHECK(g_ses.stats.half_min_ns <= g_ses.stats.half_max_ns);
    /* 3 ms between a reply window and the next frame, and no more than a
     * step of the caller on top. */
    conduct(&g_sim);
    CHECK(g_sim.min_gap_us < KST_GAP_US + 200u);
}

TEST_CASE(the_first_image_read_stays_the_backup)
{
    const kst_image_t bench = sim_bench_image();

    up(&g_sim, &g_ses);
    g_sim.reg[0x03] = 0x21;
    read_all(&g_sim, &g_ses);
    CHECK_EQ(g_ses.image.r[0x03], 0x21);
    CHECK_EQ(memcmp(&g_ses.backup, &bench, sizeof(bench)), 0);
}

TEST_CASE(one_deviating_read_costs_2_more_passes)
{
    up(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, 0x05, 0, 1, SIM_F_READ_VALUE, 0x00);
    read_all(&g_sim, &g_ses);
    CHECK_EQ(g_sim.n_reads, 96 + 160);
    CHECK_EQ(g_ses.image.r[0x05], 0x77);
    CHECK_EQ(g_ses.bad_regs, 0);
    conduct(&g_sim);
}

TEST_CASE(three_equal_reads_in_5_are_enough_and_2_are_not)
{
    /* 2 wrong reads, 3 true ones. */
    up(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, 0x05, 1, 2, SIM_F_READ_VALUE, 0x00);
    read_all(&g_sim, &g_ses);
    CHECK_EQ(g_sim.n_reads, 96 + 160);
    CHECK_EQ(g_ses.image.r[0x05], 0x77);

    /* 3 different wrong reads, 2 true ones. */
    up(&g_sim, &g_ses);
    spoil_reads(&g_sim, 0x05, 0);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_READ);
    CHECK_EQ(g_sim.n_reads, 96 + 160);
    CHECK(g_ses.bad_regs == BIT(0x05));
    /* The image is the last good one. */
    CHECK_EQ(g_ses.image.r[0x05], 0x77);
    CHECK_EQ(g_ses.has_image, 1);

    /* 2 lost reads and 1 wrong one: 2 true ones are left. */
    up(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, 0x1F, 0, 2, SIM_F_NO_REPLY, 0);
    sim_add_fault(&g_sim, SIM_READ, 0x1F, 0, 1, SIM_F_READ_VALUE, 0x01);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_READ);
    CHECK(g_ses.bad_regs == BIT(0x1F));
    conduct(&g_sim);
}

TEST_CASE(a_first_read_that_fails_leaves_no_backup)
{
    kst_plan_t plan;

    fresh(&g_sim, &g_ses);
    enter(&g_sim, &g_ses);
    spoil_reads(&g_sim, 0x00, 0);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_READ);
    CHECK(g_ses.bad_regs == BIT(0x00));
    CHECK_EQ(g_ses.has_image, 0);
    CHECK_EQ(g_ses.has_backup, 0);
    {
        const kst_image_t img = sim_bench_image();

        CHECK_EQ(kst_plan_release_pairing(&img, &plan), KST_PLAN_OK);
    }
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_NO_BACKUP);
    CHECK_EQ(g_ses.result, KST_SES_ERR_NO_BACKUP);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_pass_without_a_single_reply_is_no_servo)
{
    up(&g_sim, &g_ses);
    g_sim.powered = false;
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_NO_SERVO);
    CHECK_EQ(g_sim.n_frames, 2 + 96 + 32);
    CHECK(g_ses.bad_regs == 0xFFFFFFFFu);
    /* 20 ms after every window that held nothing. */
    conduct(&g_sim);
    CHECK(g_sim.min_gap_after_silence_us < KST_GAP_AFTER_FAULT_US + 200u);
    /* The image read before is kept. */
    CHECK_EQ(g_ses.has_image, 1);
}

TEST_CASE(a_lost_reply_is_followed_by_20_ms_of_silence)
{
    up(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, 0x07, 0, 1, SIM_F_NO_REPLY, 0);
    read_all(&g_sim, &g_ses);
    CHECK_EQ(g_sim.n_reads, 96 + 160);
    CHECK_EQ(g_ses.stats.faults, 2);
    conduct(&g_sim);
    CHECK(g_sim.min_gap_after_silence_us < KST_GAP_AFTER_FAULT_US + 200u);
    CHECK(g_sim.min_gap_us < KST_GAP_US + 200u);
}

/* A read-all with one reply bent; returns the number of reads it took. */
static unsigned reads_with(sim_action_t action, uint32_t arg,
                           kst_rx_t *result)
{
    up(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, 0x1F, 0, 1, action, arg);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    /* Pass 0, register 0x1F: 32 reads in. */
    while (g_sim.n_reads < 96u + 32u || g_sim.busy) {
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
        g_sim.now_us += 100u;
    }
    CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
    *result = g_ses.last_reply.result;
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_ses.image.r[0x1F], 0x00);
    conduct(&g_sim);
    return g_sim.n_reads - 96u;
}

TEST_CASE(a_reply_outside_its_time_or_rate_is_not_taken)
{
    kst_rx_t rx = KST_RX_OK;

    /* The accept window of a read reply is 350 us to 600 us. */
    CHECK_EQ(reads_with(SIM_F_SHAPE_DELAY, 350000, &rx), 96);
    CHECK_EQ(rx, KST_RX_OK);
    CHECK_EQ(g_ses.stats.delay_min_ns, 350000);
    CHECK_EQ(reads_with(SIM_F_SHAPE_DELAY, 349000, &rx), 160);
    CHECK(rx != KST_RX_OK);
    CHECK_EQ(reads_with(SIM_F_SHAPE_DELAY, 600000, &rx), 96);
    CHECK_EQ(rx, KST_RX_OK);
    CHECK_EQ(g_ses.stats.delay_max_ns, 600000);
    CHECK_EQ(reads_with(SIM_F_SHAPE_DELAY, 601000, &rx), 160);
    CHECK_EQ(rx, KST_RX_LATE);

    /* Half-cell 25.575 us +/- 5 %. */
    CHECK_EQ(reads_with(SIM_F_SHAPE_HALF, KST_RX_HALF_CELL_MIN_NS, &rx), 96);
    CHECK_EQ(rx, KST_RX_OK);
    CHECK_EQ(g_ses.stats.half_min_ns, KST_RX_HALF_CELL_MIN_NS);
    CHECK_EQ(reads_with(SIM_F_SHAPE_HALF, KST_RX_HALF_CELL_MIN_NS - 100u, &rx),
             160);
    CHECK(rx != KST_RX_OK);
    CHECK_EQ(reads_with(SIM_F_SHAPE_HALF, KST_RX_HALF_CELL_MAX_NS, &rx), 96);
    CHECK_EQ(rx, KST_RX_OK);
    CHECK_EQ(g_ses.stats.half_max_ns, KST_RX_HALF_CELL_MAX_NS);
    CHECK_EQ(reads_with(SIM_F_SHAPE_HALF, KST_RX_HALF_CELL_MAX_NS + 100u, &rx),
             160);
    CHECK(rx != KST_RX_OK);

    /* A 1 us pulse inside a cell is dropped and counted. */
    CHECK_EQ(reads_with(SIM_F_SHAPE_GLITCH, 30000, &rx), 96);
    CHECK_EQ(rx, KST_RX_OK);
}

/* --- writes -------------------------------------------------------------------- */

TEST_CASE(a_write_is_read_before_read_back_twice_and_verified_in_full)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_BUSY);
    /* The plan is copied. */
    memset(&plan, 0xFF, sizeof(plan));
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_sim.n_reads, 96 + 96 + 2 + 96);
    CHECK_EQ(g_sim.log[2 + 96 + 96].kind, SIM_WRITE);
    CHECK_EQ(g_sim.log[2 + 96 + 96].reg, 0x03);
    CHECK_EQ(g_sim.log[2 + 96 + 96].value, 19);
    CHECK_EQ(g_sim.log[2 + 96 + 97].kind, SIM_READ);
    CHECK_EQ(g_sim.log[2 + 96 + 97].reg, 0x03);
    CHECK_EQ(g_sim.log[2 + 96 + 98].kind, SIM_READ);
    CHECK_EQ(g_sim.log[2 + 96 + 98].reg, 0x03);
    CHECK_EQ(g_sim.log[2 + 96 + 99].reg, 0x00);
    CHECK_EQ(g_sim.reg[0x03], 19);
    CHECK_EQ(g_ses.image.r[0x03], 19);
    CHECK_EQ(g_ses.backup.r[0x03], 20);
    CHECK_EQ(g_ses.step_index, 1);
    CHECK_EQ(g_ses.diff_regs, 0);
    CHECK_EQ(g_ses.locked, 0);
    conduct(&g_sim);
}

TEST_CASE(a_plan_of_no_writes_reads_and_ends)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    CHECK_EQ(kst_plan_edit(&g_ses.image, &g_ses.image, 0, &plan, NULL),
             KST_PLAN_OK);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_sim.n_reads, 96 + 96);
}

TEST_CASE(max_duty_goes_to_02_then_to_01)
{
    kst_plan_t plan;
    unsigned at = 2 + 96 + 96;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_DUTY, 200);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 2);
    CHECK_EQ(g_sim.log[at].kind, SIM_WRITE);
    CHECK_EQ(g_sim.log[at].reg, 0x02);
    CHECK_EQ(g_sim.log[at + 3].kind, SIM_WRITE);
    CHECK_EQ(g_sim.log[at + 3].reg, 0x01);
    CHECK_EQ(g_sim.reg[0x01], 200);
    CHECK_EQ(g_sim.reg[0x02], 200);
    CHECK_EQ(g_ses.step_index, 2);
    conduct(&g_sim);
}

TEST_CASE(the_reply_to_a_write_is_not_what_the_write_is_judged_by)
{
    kst_plan_t plan;

    /* No reply to the write at all. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, -1, 0, 1, SIM_F_NO_REPLY, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_ses.stats.faults, 2);
    conduct(&g_sim);
    CHECK(g_sim.min_gap_after_silence_us < KST_GAP_AFTER_FAULT_US + 200u);

    /* A reply before 3.3 ms and one after 4.2 ms. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, -1, 0, 1, SIM_F_SHAPE_DELAY, 3299000);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_ses.stats.faults, 2);
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, -1, 0, 1, SIM_F_SHAPE_DELAY, 4201000);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_ses.stats.faults, 2);
    /* At the bounds the reply counts. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, -1, 0, 1, SIM_F_SHAPE_DELAY, 3300000);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_ses.stats.faults, 1);
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, -1, 0, 1, SIM_F_SHAPE_DELAY, 4200000);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_ses.stats.faults, 1);
    /* The write timing is not in the read statistics. */
    CHECK(g_ses.stats.delay_max_ns <= 439000u);
}

TEST_CASE(a_lost_read_back_is_settled_by_a_full_read)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    /* 3 reads of 0x03 before the write, then the first read-back. */
    sim_add_fault(&g_sim, SIM_READ, 0x03, 3, 1, SIM_F_NO_REPLY, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_sim.n_reads, 96 + 96 + 2 + 96 + 96);

    /* The second read-back wrong. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_READ, 0x03, 4, 1, SIM_F_READ_VALUE, 20);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_sim.n_reads, 96 + 96 + 2 + 96 + 96);
    conduct(&g_sim);
}

TEST_CASE(a_write_that_does_not_take_is_tried_3_times)
{
    kst_plan_t plan;

    /* 2 writes ignored: the third takes. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 2, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 3);
    CHECK_EQ(g_sim.reg[0x03], 19);
    CHECK_EQ(g_ses.locked, 0);
    conduct(&g_sim);

    /* 3 writes ignored: given up, nothing to undo. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 3, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_WRITE_FAILED);
    CHECK_EQ(g_sim.n_writes, 3);
    CHECK_EQ(g_sim.reg[0x03], 20);
    CHECK_EQ(g_ses.image.r[0x03], 20);
    CHECK_EQ(g_ses.step_index, 0);
    CHECK_EQ(g_ses.locked, 0);
    /* Every write is followed by reads, never by another write. */
    for (unsigned i = 0; i + 1u < g_sim.n_frames; ++i) {
        if (g_sim.log[i].kind == SIM_WRITE) {
            CHECK_EQ(g_sim.log[i + 1u].kind, SIM_READ);
        }
    }
    conduct(&g_sim);
    /* The session takes the next plan. */
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.reg[0x03], 19);
}

TEST_CASE(a_failed_write_keeps_the_steps_that_were_settled_before_it)
{
    kst_image_t target;
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    target = g_ses.image;
    CHECK(kst_field_edit(&target, KST_F_BOOST, 19));
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
    CHECK_EQ(kst_plan_edit(&g_ses.image, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    sim_add_fault(&g_sim, SIM_WRITE, 0x05, 0, 3, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_WRITE_FAILED);
    CHECK_EQ(g_ses.step_index, 1);
    CHECK_EQ(g_sim.reg[0x03], 19);
    CHECK_EQ(g_sim.reg[0x05], 0x77);
    CHECK(servo_holds(&g_sim, &g_ses.image));
    CHECK_EQ(g_ses.locked, 0);
    CHECK_EQ(g_sim.n_writes, 4);
    conduct(&g_sim);
}

TEST_CASE(a_register_left_at_a_third_value_is_written_back)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 3, SIM_F_WRITE_VALUE, 0x55);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_ROLLED_BACK);
    CHECK_EQ(g_sim.n_writes, 4);
    CHECK_EQ(g_sim.log[g_sim.n_frames - 96u - 3u].kind, SIM_WRITE);
    CHECK_EQ(g_sim.log[g_sim.n_frames - 96u - 3u].value, 20);
    CHECK_EQ(g_sim.reg[0x03], 20);
    CHECK(servo_holds(&g_sim, &g_ses.backup));
    CHECK_EQ(g_ses.step_index, 0);
    CHECK_EQ(g_ses.locked, 0);
    conduct(&g_sim);
}

TEST_CASE(a_torn_duty_pair_is_rolled_back)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_DUTY, 200);
    /* 02 takes, 01 does not. */
    sim_add_fault(&g_sim, SIM_WRITE, 0x01, 0, 3, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_ROLLED_BACK);
    CHECK_EQ(g_sim.n_writes, 1 + 3 + 1);
    CHECK_EQ(g_sim.reg[0x01], 0xF5);
    CHECK_EQ(g_sim.reg[0x02], 0xF5);
    CHECK(servo_holds(&g_sim, &g_ses.image));
    CHECK(servo_holds(&g_sim, &g_ses.backup));
    CHECK_EQ(g_ses.step_index, 0);
    CHECK_EQ(g_ses.diff_regs, 0);
    CHECK_EQ(g_ses.locked, 0);
    /* The undo of 02 is read back twice and the whole image after it. */
    {
        const unsigned undo = g_sim.n_frames - 96u - 3u;

        CHECK_EQ(g_sim.log[undo].kind, SIM_WRITE);
        CHECK_EQ(g_sim.log[undo].reg, 0x02);
        CHECK_EQ(g_sim.log[undo].value, 0xF5);
        CHECK_EQ(g_sim.log[undo + 1u].reg, 0x02);
        CHECK_EQ(g_sim.log[undo + 2u].reg, 0x02);
    }
    conduct(&g_sim);
}

TEST_CASE(a_roll_back_that_fails_3_times_leaves_the_session_locked)
{
    kst_plan_t plan;
    kst_plan_t restore;
    kst_plan_t boost;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_DUTY, 200);
    boost = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x01, 0, 3, SIM_F_WRITE_IGNORE, 0);
    /* The first write of 02 takes; the 3 that undo it do not. */
    sim_add_fault(&g_sim, SIM_WRITE, 0x02, 1, 3, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_TORN);
    CHECK_EQ(g_sim.n_writes, 1 + 3 + 3);
    CHECK_EQ(g_sim.reg[0x01], 0xF5);
    CHECK_EQ(g_sim.reg[0x02], 200);
    CHECK_EQ(g_ses.locked, 1);
    /* The image is the last one read: the torn pair. */
    CHECK_EQ(g_ses.image.r[0x01], 0xF5);
    CHECK_EQ(g_ses.image.r[0x02], 200);
    conduct(&g_sim);

    /* Only a restore is taken now. */
    read_all(&g_sim, &g_ses);
    CHECK_EQ(g_ses.fingerprint_ok, 0);
    CHECK(g_ses.fingerprint.rules == ((uint16_t)1 << KST_FP_DUTY_COPY));
    CHECK_EQ(kst_session_write(&g_ses, &boost, false), KST_SES_ERR_LOCKED);
    CHECK_EQ(g_ses.result, KST_SES_ERR_LOCKED);
    CHECK_EQ(kst_plan_release_pairing(&g_ses.image, &plan), KST_PLAN_OK);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_LOCKED);
    CHECK_EQ(kst_plan_restore(&g_ses.image, &g_ses.backup, &restore),
             KST_PLAN_OK);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &restore), KST_SES_OK);
    CHECK(servo_holds(&g_sim, &g_ses.backup));
    CHECK_EQ(g_ses.locked, 0);
    CHECK_EQ(g_ses.fingerprint_ok, 1);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    conduct(&g_sim);
}

TEST_CASE(a_torn_position_pair_is_rolled_back)
{
    kst_plan_t plan;

    /* Lower 0x465 to 0x500: 09 then 07. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_PULSE_LOWER, 0x500);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[1].reg, 0x07);
    sim_add_fault(&g_sim, SIM_WRITE, 0x07, 0, 3, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_ROLLED_BACK);
    CHECK(servo_holds(&g_sim, &g_ses.backup));
    CHECK_EQ(g_ses.step_index, 0);
    CHECK_EQ(g_ses.locked, 0);
    conduct(&g_sim);
}

TEST_CASE(a_plan_with_an_extra_write_rolls_back_through_it)
{
    kst_image_t start = sim_bench_image();
    kst_plan_t plan;

    CHECK(kst_field_edit(&start, KST_F_PULSE_UPPER, 0xBFF));
    CHECK(kst_field_edit(&start, KST_F_UNCONT_POS, 0xB20));
    fresh(&g_sim, &g_ses);
    memcpy(g_sim.reg, start.r, sizeof(g_sim.reg));
    enter(&g_sim, &g_ses);
    read_all(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_PULSE_UPPER, 0xC00);
    CHECK_EQ(plan.n, 3);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.n_writes, 3);
    CHECK_EQ(g_sim.reg[0x08], 0x00);
    CHECK_EQ(g_sim.reg[0x09] & 0xF0, 0xC0);

    /* The last of the 3 writes fails: 09 and the first write go back. */
    fresh(&g_sim, &g_ses);
    memcpy(g_sim.reg, start.r, sizeof(g_sim.reg));
    enter(&g_sim, &g_ses);
    read_all(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_PULSE_UPPER, 0xC00);
    sim_add_fault(&g_sim, SIM_WRITE, 0x08, 1, 3, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_ROLLED_BACK);
    CHECK_EQ(g_sim.n_writes, 2 + 3 + 2);
    CHECK(servo_holds(&g_sim, &start));
    CHECK_EQ(g_ses.step_index, 0);
    CHECK_EQ(g_ses.locked, 0);
    conduct(&g_sim);
}

/* --- address errors -------------------------------------------------------------- */

TEST_CASE(a_write_that_lands_in_another_register_ends_the_plan)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 1, SIM_F_WRITE_TO, 0x13);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_UNINTENDED);
    CHECK(g_ses.diff_regs == BIT(0x13));
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_ses.image.r[0x13], 19);
    CHECK_EQ(g_ses.locked, 1);
    conduct(&g_sim);
}

TEST_CASE(a_write_that_lands_in_a_second_register_is_caught_by_the_verify)
{
    kst_plan_t plan;
    kst_plan_t restore;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    /* The read-back of 0x03 is right both times. */
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 1, SIM_F_WRITE_ALSO, 0x13);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_VERIFY);
    CHECK(g_ses.diff_regs == BIT(0x13));
    CHECK_EQ(g_sim.n_reads, 96 + 96 + 2 + 96);
    CHECK_EQ(g_ses.image.r[0x03], 19);
    CHECK_EQ(g_ses.image.r[0x13], 19);
    CHECK_EQ(g_ses.step_index, 1);
    CHECK_EQ(g_ses.locked, 1);

    /* A verify that passes against another image does not unlock. */
    {
        const kst_image_t seen = g_ses.image;

        CHECK_EQ(kst_session_verify(&g_ses, &seen), KST_SES_BUSY);
        CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
        CHECK_EQ(g_ses.locked, 1);
    }
    /* The restore reaches 0x13, a register no edit writes. */
    CHECK_EQ(kst_plan_restore(&g_ses.image, &g_ses.backup, &restore),
             KST_PLAN_OK);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &restore), KST_SES_OK);
    CHECK(servo_holds(&g_sim, &g_ses.backup));
    CHECK_EQ(g_ses.locked, 0);
    conduct(&g_sim);
}

TEST_CASE(an_undo_that_lands_in_a_second_register_is_caught)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 3, SIM_F_WRITE_VALUE, 0x55);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 1, SIM_F_WRITE_ALSO, 0x13);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_VERIFY);
    CHECK(g_ses.diff_regs == BIT(0x13));
    CHECK_EQ(g_ses.locked, 1);
}

TEST_CASE(a_servo_that_differs_from_the_plans_start_is_not_written)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    g_sim.reg[0x10] = 0x95;
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_CHANGED);
    CHECK(g_ses.diff_regs == BIT(0x10));
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_ses.locked, 0);
    CHECK_EQ(g_ses.image.r[0x10], 0x95);
}

/* --- refusals before any frame --------------------------------------------------- */

TEST_CASE(a_plan_that_no_planner_builds_is_refused)
{
    kst_plan_t good;
    kst_plan_t plan;
    unsigned frames;

    up(&g_sim, &g_ses);
    frames = g_sim.n_frames;
    good = edit_plan(&g_ses, KST_F_BOOST, 19);

    plan = good;
    plan.n = KST_PLAN_MAX_STEPS + 1u;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    CHECK_EQ(g_ses.result, KST_SES_ERR_BAD_PLAN);
    plan = good;
    plan.kind = 3;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    /* A step that does not start from what the step before left. */
    plan = good;
    plan.step[0].prev = 21;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    /* Register 0x00 and an address above 0x1F. */
    plan = good;
    plan.step[0].reg = 0x00;
    plan.step[0].prev = 0x00;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    plan = good;
    plan.step[0].reg = 0x20;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    /* Steps that do not end on the target. */
    plan = good;
    plan.target.r[0x05] = 0x78;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    /* An edit that writes 0x1D. */
    plan = good;
    plan.step[0].reg = 0x1D;
    plan.step[0].prev = 0x10;
    plan.step[0].value = 0x00;
    plan.target = plan.start;
    plan.target.r[0x1D] = 0x00;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    /* A release that writes something else. */
    plan = good;
    plan.kind = KST_PLAN_RELEASE_PAIRING;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    /* An edit whose target breaks a hard rule: Max Duty raw 251. */
    plan = good;
    plan.n = 2;
    plan.step[0].reg = 0x02;
    plan.step[0].prev = 0xF5;
    plan.step[0].value = 251;
    plan.step[1].reg = 0x01;
    plan.step[1].prev = 0xF5;
    plan.step[1].value = 251;
    plan.target = plan.start;
    plan.target.r[0x01] = 251;
    plan.target.r[0x02] = 251;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    plan.step[0].value = 250;
    plan.step[1].value = 250;
    plan.target.r[0x01] = 250;
    plan.target.r[0x02] = 250;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_BUSY);
    kst_session_abort(&g_ses);
    CHECK_EQ(kst_session_step(&g_ses), KST_SES_ERR_ABORTED);
    /* A locked field without its unlock. */
    plan = good;
    plan.step[0].reg = 0x04;
    plan.step[0].prev = 0x48;
    plan.step[0].value = 0x08;
    plan.target = plan.start;
    plan.target.r[0x04] = 0x08;
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_BAD_PLAN);
    /* An edit marked unchecked. */
    plan = good;
    plan.unchecked = 1;
    CHECK_EQ(kst_session_write(&g_ses, &plan, true), KST_SES_ERR_BAD_PLAN);
    /* A restore to something that is not the backup. */
    plan = good;
    plan.kind = KST_PLAN_RESTORE;
    CHECK_EQ(kst_session_write(&g_ses, &plan, true), KST_SES_ERR_BAD_PLAN);

    CHECK_EQ(g_sim.n_frames, frames);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_ses.locked, 0);
}

TEST_CASE(a_servo_that_fails_the_fingerprint_is_read_only)
{
    kst_plan_t plan;

    fresh(&g_sim, &g_ses);
    g_sim.reg[0x06] = 0x05;
    enter(&g_sim, &g_ses);
    read_all(&g_sim, &g_ses);
    CHECK_EQ(g_ses.fingerprint_ok, 0);
    CHECK(g_ses.fingerprint.rules == ((uint16_t)1 << KST_FP_R06));
    CHECK(g_ses.fingerprint.regs == BIT(0x06));
    CHECK_EQ(g_ses.has_backup, 1);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_FINGERPRINT);
    CHECK_EQ(kst_plan_release_pairing(&g_ses.image, &plan), KST_PLAN_OK);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_FINGERPRINT);
    CHECK_EQ(g_sim.n_writes, 0);
    /* Reading and verifying stay open. */
    CHECK_EQ(kst_session_verify(&g_ses, &g_ses.backup), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
}

/* --- restore, pairing, verify ---------------------------------------------------- */

TEST_CASE(releasing_the_pairing_and_restoring_it)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    CHECK_EQ(kst_plan_release_pairing(&g_ses.image, &plan), KST_PLAN_OK);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.reg[0x1D], 0x00);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_ses.locked, 0);
    CHECK_EQ(kst_plan_restore(&g_ses.image, &g_ses.backup, &plan), KST_PLAN_OK);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK_EQ(g_sim.reg[0x1D], 0x10);
    CHECK(servo_holds(&g_sim, &g_ses.backup));
    conduct(&g_sim);
}

TEST_CASE(an_unchecked_restore_needs_its_confirmation)
{
    kst_image_t first = sim_bench_image();
    kst_plan_t plan;

    CHECK(kst_field_edit(&first, KST_F_PULSE_UPPER, 0xC00));
    CHECK(kst_field_edit(&first, KST_F_UNCONT_POS, 0xBFF));
    fresh(&g_sim, &g_ses);
    memcpy(g_sim.reg, first.r, sizeof(g_sim.reg));
    enter(&g_sim, &g_ses);
    read_all(&g_sim, &g_ses);
    /* The servo as another tool left it. */
    g_sim.reg[0x08] = 0xFF;
    g_sim.reg[0x09] = (uint8_t)((g_sim.reg[0x09] & 0x0Fu) | 0xB0u);
    read_all(&g_sim, &g_ses);
    CHECK_EQ(kst_plan_restore(&g_ses.image, &g_ses.backup, &plan), KST_PLAN_OK);
    CHECK_EQ(plan.unchecked, 1);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_ERR_UNCONFIRMED);
    CHECK_EQ(g_ses.result, KST_SES_ERR_UNCONFIRMED);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(kst_session_write(&g_ses, &plan, true), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK(servo_holds(&g_sim, &first));
    CHECK_EQ(g_sim.n_writes, 2);
    conduct(&g_sim);
}

TEST_CASE(verify_names_the_registers_that_differ)
{
    kst_image_t want;

    up(&g_sim, &g_ses);
    want = g_ses.image;
    CHECK_EQ(kst_session_verify(&g_ses, &want), KST_SES_BUSY);
    /* The expected image is copied. */
    memset(&want, 0xEE, sizeof(want));
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_ses.diff_regs, 0);
    CHECK_EQ(g_sim.n_reads, 96 + 96);

    want = g_ses.image;
    g_sim.reg[0x00] = 0x01;
    g_sim.reg[0x1F] = 0x80;
    CHECK_EQ(kst_session_verify(&g_ses, &want), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_VERIFY);
    CHECK(g_ses.diff_regs == (BIT(0x00) | BIT(0x1F)));
    CHECK_EQ(g_ses.image.r[0x1F], 0x80);
    CHECK_EQ(g_ses.fingerprint_ok, 0);
    /* A verify alone locks nothing. */
    CHECK_EQ(g_ses.locked, 0);

    /* A read that fails is not a verify result. */
    spoil_reads(&g_sim, 0x0A, 0);
    CHECK_EQ(kst_session_verify(&g_ses, &want), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_READ);
    CHECK(g_ses.bad_regs == BIT(0x0A));
    conduct(&g_sim);
}

TEST_CASE(a_verify_against_the_backup_unlocks)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 1, SIM_F_WRITE_TO, 0x13);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_UNINTENDED);
    CHECK_EQ(g_ses.locked, 1);
    /* Still different: stays locked. */
    CHECK_EQ(kst_session_verify(&g_ses, &g_ses.backup), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_VERIFY);
    CHECK_EQ(g_ses.locked, 1);
    /* Put right by other means. */
    g_sim.reg[0x13] = 0x01;
    CHECK_EQ(kst_session_verify(&g_ses, &g_ses.backup), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_ses.locked, 0);
}

/* --- reads that fail inside a plan ------------------------------------------------ */

TEST_CASE(a_failed_read_before_the_first_write_locks_nothing)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    spoil_reads(&g_sim, 0x09, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_READ);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_ses.locked, 0);

    g_sim.powered = false;
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_NO_SERVO);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_ses.locked, 0);
}

TEST_CASE(a_failed_read_after_a_write_locks_the_session)
{
    kst_plan_t plan;

    /* In the verify after the last write: 3 reads of 0x09 before it. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    spoil_reads(&g_sim, 0x09, 3);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_READ);
    CHECK(g_ses.bad_regs == BIT(0x09));
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_ses.locked, 1);

    /* In the full read after a read-back that did not match. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 1, SIM_F_WRITE_IGNORE, 0);
    spoil_reads(&g_sim, 0x09, 3);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_READ);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_ses.locked, 1);

    /* In the full read after the roll-back: 3 + 3 * 3 reads before it. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 3, SIM_F_WRITE_IGNORE, 0);
    spoil_reads(&g_sim, 0x09, 12);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_READ);
    CHECK_EQ(g_sim.n_writes, 3);
    CHECK_EQ(g_ses.locked, 1);
    conduct(&g_sim);
}

/* --- abort, power, driver --------------------------------------------------------- */

TEST_CASE(an_abort_in_the_entry_wait_sends_nothing)
{
    fresh(&g_sim, &g_ses);
    kst_session_abort(&g_ses);   /* nothing runs: no effect */
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    for (unsigned i = 0; i < 500u; ++i) {
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
        g_sim.now_us += 100u;
    }
    kst_session_abort(&g_ses);
    CHECK_EQ(kst_session_step(&g_ses), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_ses.result, KST_SES_ERR_ABORTED);
    CHECK_EQ(g_sim.n_frames, 0);
    CHECK_EQ(g_ses.in_mode, 0);
    /* The session starts again. */
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
}

TEST_CASE(an_abort_never_cuts_a_frame)
{
    unsigned frames;

    up(&g_sim, &g_ses);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    while (!g_sim.busy || g_sim.n_reads < 96u + 10u) {
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
        g_sim.now_us += 100u;
    }
    frames = g_sim.n_frames;
    kst_session_abort(&g_ses);
    /* The transaction on the wire runs to its end first. */
    CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_ABORTED);
    CHECK(!g_sim.busy);
    CHECK_EQ(g_sim.n_frames, frames);
    /* The next operation keeps the gap. */
    read_all(&g_sim, &g_ses);
    conduct(&g_sim);
}

TEST_CASE(an_abort_after_the_first_write_locks_and_before_it_does_not)
{
    kst_plan_t plan;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_DUTY, 200);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_BUSY);
    while (g_sim.n_reads < 96u + 40u) {
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
        g_sim.now_us += 100u;
    }
    kst_session_abort(&g_ses);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_ses.locked, 0);

    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_BUSY);
    while (g_sim.n_writes < 1u) {
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
        g_sim.now_us += 100u;
    }
    kst_session_abort(&g_ses);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_sim.reg[0x02], 200);
    CHECK_EQ(g_sim.reg[0x01], 0xF5);
    CHECK_EQ(g_ses.locked, 1);
    CHECK_EQ(g_ses.step_index, 0);
    conduct(&g_sim);
}

TEST_CASE(a_power_cycle_leaves_the_mode_and_ends_what_runs)
{
    up(&g_sim, &g_ses);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    for (unsigned i = 0; i < 300u; ++i) {
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
        g_sim.now_us += 100u;
    }
    g_sim.in_mode = false;
    kst_session_power_cycled(&g_ses);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_ses.in_mode, 0);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_ERR_NOT_IN_MODE);
    /* The backup of the session is kept. */
    CHECK_EQ(g_ses.has_backup, 1);
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_sim.n_syncs, 2);
    CHECK_EQ(g_ses.last_reply.flags, 2);
    CHECK_EQ(kst_session_verify(&g_ses, &g_ses.backup), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK_EQ(g_sim.n_overlap, 0);
}

TEST_CASE(a_driver_that_refuses_a_frame_ends_the_operation)
{
    kst_plan_t plan;

    fresh(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_ANY, -1, 0, 1, SIM_F_START_FAIL, 0);
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_DRIVER);
    CHECK_EQ(g_ses.in_mode, 0);
    CHECK_EQ(g_sim.n_frames, 0);

    up(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, 0x04, 0, 1, SIM_F_START_FAIL, 0);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_DRIVER);
    CHECK_EQ(g_sim.n_reads, 96 + 4);
    read_all(&g_sim, &g_ses);

    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, -1, 0, 1, SIM_F_START_FAIL, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_DRIVER);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_sim.reg[0x03], 20);
    CHECK_EQ(g_sim.n_overlap, 0);
}

TEST_CASE(a_driver_that_never_finishes_ends_the_operation)
{
    kst_plan_t plan;
    uint32_t t0;

    up(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, 0x04, 0, 1, SIM_F_STALL, 0);
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    while (!g_sim.stalled) {
        CHECK_EQ(kst_session_step(&g_ses), KST_SES_BUSY);
        g_sim.now_us += 100u;
    }
    t0 = g_sim.now_us;
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_ERR_DRIVER);
    /* Frame 1.3 ms, window 1.5 ms, slack 5 ms. */
    CHECK(g_sim.now_us - t0 > 7700u);
    CHECK(g_sim.now_us - t0 < 8200u);
    CHECK_EQ(g_ses.locked, 0);

    /* After a write frame the servo is in doubt. */
    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_BOOST, 19);
    sim_add_fault(&g_sim, SIM_WRITE, -1, 0, 1, SIM_F_STALL, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_ERR_DRIVER);
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK_EQ(g_ses.locked, 1);
}

/* The entry with its first capture handed over @p late_us after the window;
 * the clock moves 1 us per step. */
static kst_ses_t enter_with_late_poll(uint32_t late_us)
{
    fresh(&g_sim, &g_ses);
    sim_add_fault(&g_sim, SIM_READ, -1, 0, 1, SIM_F_POLL_LATE, late_us);
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    for (unsigned i = 0; i < 400000u; ++i) {
        const kst_ses_t r = kst_session_step(&g_ses);

        if (r != KST_SES_BUSY) {
            return r;
        }
        g_sim.now_us += 1u;
    }
    return KST_SES_BUSY;
}

TEST_CASE(the_driver_has_5_ms_after_the_window)
{
    /* A read of 0x01 is 52 half-cells, 1320.8 us; the window 1500 us. */
    CHECK_EQ(enter_with_late_poll(KST_DRIVER_SLACK_US), KST_SES_OK);
    CHECK_EQ(g_sim.n_frames, 2);
    CHECK_EQ(enter_with_late_poll(KST_DRIVER_SLACK_US + 1u),
             KST_SES_ERR_DRIVER);
    CHECK_EQ(g_sim.n_frames, 1);
    CHECK_EQ(g_ses.in_mode, 0);
}

/* --- clock and instances ---------------------------------------------------------- */

TEST_CASE(the_clock_may_wrap_in_any_wait)
{
    kst_plan_t plan;

    /* The wrap falls into the 100 ms of the entry. */
    fresh(&g_sim, &g_ses);
    g_sim.now_us = 0xFFFFFFFFu - 50000u;
    {
        const kst_driver_t d = sim_driver(&g_sim);

        CHECK(kst_session_init(&g_ses, &d));
    }
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(sim_run(&g_sim, &g_ses), KST_SES_OK);
    CHECK(g_sim.log[0].t_us < 0x80000000u);
    CHECK(g_sim.log[0].t_us - (0xFFFFFFFFu - 50000u) >= KST_ENTRY_LOW_US);
    CHECK(g_sim.log[0].t_us - (0xFFFFFFFFu - 50000u) < KST_ENTRY_LOW_US + 200u);

    /* The wrap falls into the reads and writes of a plan. */
    fresh(&g_sim, &g_ses);
    g_sim.now_us = 0xFFFFFFFFu - 900000u;
    {
        const kst_driver_t d = sim_driver(&g_sim);

        CHECK(kst_session_init(&g_ses, &d));
    }
    enter(&g_sim, &g_ses);
    read_all(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_DUTY, 200);
    sim_add_fault(&g_sim, SIM_READ, 0x11, 0, 1, SIM_F_NO_REPLY, 0);
    CHECK_EQ(run_plan(&g_sim, &g_ses, &plan), KST_SES_OK);
    CHECK(g_sim.now_us < 0x80000000u);
    conduct(&g_sim);
    CHECK(g_sim.min_gap_us < KST_GAP_US + 200u);
    CHECK(g_sim.min_gap_after_silence_us < KST_GAP_AFTER_FAULT_US + 200u);
}

TEST_CASE(a_slow_caller_makes_the_gaps_longer_and_nothing_else)
{
    kst_plan_t plan;
    kst_ses_t r = KST_SES_BUSY;

    up(&g_sim, &g_ses);
    plan = edit_plan(&g_ses, KST_F_DUTY, 200);
    CHECK_EQ(kst_session_write(&g_ses, &plan, false), KST_SES_BUSY);
    /* One call every 4.7 ms. */
    for (unsigned i = 0; i < 20000u && r == KST_SES_BUSY; ++i) {
        r = kst_session_step(&g_ses);
        g_sim.now_us += 4700u;
    }
    CHECK_EQ(r, KST_SES_OK);
    CHECK_EQ(g_sim.reg[0x01], 200);
    conduct(&g_sim);
}

TEST_CASE(two_sessions_run_side_by_side)
{
    kst_plan_t plan_a;
    kst_plan_t plan_b;
    kst_ses_t a = KST_SES_BUSY;
    kst_ses_t b = KST_SES_BUSY;

    fresh(&g_sim, &g_ses);
    fresh(&g_sim_b, &g_ses_b);
    g_sim_b.reg[0x03] = 0x30;
    g_sim_b.now_us = 123456;
    CHECK_EQ(kst_session_enter(&g_ses), KST_SES_BUSY);
    CHECK_EQ(kst_session_enter(&g_ses_b), KST_SES_BUSY);
    for (unsigned i = 0; i < 100000u && (a == KST_SES_BUSY || b == KST_SES_BUSY);
         ++i) {
        a = kst_session_step(&g_ses);
        b = kst_session_step(&g_ses_b);
        g_sim.now_us += 100u;
        g_sim_b.now_us += 130u;
    }
    CHECK_EQ(a, KST_SES_OK);
    CHECK_EQ(b, KST_SES_OK);
    g_sim.min_gap_after_silence_us = 0xFFFFFFFFu;
    g_sim_b.min_gap_after_silence_us = 0xFFFFFFFFu;
    CHECK_EQ(kst_session_read_all(&g_ses), KST_SES_BUSY);
    CHECK_EQ(kst_session_read_all(&g_ses_b), KST_SES_BUSY);
    a = KST_SES_BUSY;
    b = KST_SES_BUSY;
    for (unsigned i = 0; i < 100000u && (a == KST_SES_BUSY || b == KST_SES_BUSY);
         ++i) {
        a = kst_session_step(&g_ses);
        b = kst_session_step(&g_ses_b);
        g_sim.now_us += 100u;
        g_sim_b.now_us += 130u;
    }
    CHECK_EQ(a, KST_SES_OK);
    CHECK_EQ(b, KST_SES_OK);
    CHECK_EQ(g_ses.image.r[0x03], 0x14);
    CHECK_EQ(g_ses_b.image.r[0x03], 0x30);

    plan_a = edit_plan(&g_ses, KST_F_BOOST, 19);
    plan_b = edit_plan(&g_ses_b, KST_F_DEAD_BAND, 50);
    CHECK_EQ(kst_session_write(&g_ses, &plan_a, false), KST_SES_BUSY);
    CHECK_EQ(kst_session_write(&g_ses_b, &plan_b, false), KST_SES_BUSY);
    a = KST_SES_BUSY;
    b = KST_SES_BUSY;
    for (unsigned i = 0; i < 100000u && (a == KST_SES_BUSY || b == KST_SES_BUSY);
         ++i) {
        a = kst_session_step(&g_ses);
        b = kst_session_step(&g_ses_b);
        g_sim.now_us += 100u;
        g_sim_b.now_us += 130u;
    }
    CHECK_EQ(a, KST_SES_OK);
    CHECK_EQ(b, KST_SES_OK);
    CHECK_EQ(g_sim.reg[0x03], 19);
    CHECK_EQ(g_sim.reg[0x05], 0x77);
    CHECK_EQ(g_sim_b.reg[0x03], 0x30);
    CHECK_EQ(g_sim_b.reg[0x05], 0x72);
    conduct(&g_sim);
    conduct(&g_sim_b);
}

int main(void)
{
    RUN(a_driver_without_all_3_functions_is_refused);
    RUN(missing_arguments_are_refused);
    RUN(nothing_but_enter_runs_outside_programming_mode);
    RUN(one_operation_runs_at_a_time);
    RUN(entry_holds_the_line_low_100_ms_then_reads_and_syncs);
    RUN(a_servo_already_in_the_mode_answers_the_first_read);
    RUN(a_servo_in_the_mode_does_not_acknowledge_a_sync);
    RUN(a_lost_acknowledge_is_found_by_the_last_read);
    RUN(no_servo_ends_the_entry_after_read_sync_read);
    RUN(an_acknowledge_at_another_bit_rate_is_no_entry);
    RUN(read_all_is_3_passes_over_32_registers);
    RUN(the_first_image_read_stays_the_backup);
    RUN(one_deviating_read_costs_2_more_passes);
    RUN(three_equal_reads_in_5_are_enough_and_2_are_not);
    RUN(a_first_read_that_fails_leaves_no_backup);
    RUN(a_pass_without_a_single_reply_is_no_servo);
    RUN(a_lost_reply_is_followed_by_20_ms_of_silence);
    RUN(a_reply_outside_its_time_or_rate_is_not_taken);
    RUN(a_write_is_read_before_read_back_twice_and_verified_in_full);
    RUN(a_plan_of_no_writes_reads_and_ends);
    RUN(max_duty_goes_to_02_then_to_01);
    RUN(the_reply_to_a_write_is_not_what_the_write_is_judged_by);
    RUN(a_lost_read_back_is_settled_by_a_full_read);
    RUN(a_write_that_does_not_take_is_tried_3_times);
    RUN(a_failed_write_keeps_the_steps_that_were_settled_before_it);
    RUN(a_register_left_at_a_third_value_is_written_back);
    RUN(a_torn_duty_pair_is_rolled_back);
    RUN(a_roll_back_that_fails_3_times_leaves_the_session_locked);
    RUN(a_torn_position_pair_is_rolled_back);
    RUN(a_plan_with_an_extra_write_rolls_back_through_it);
    RUN(a_write_that_lands_in_another_register_ends_the_plan);
    RUN(a_write_that_lands_in_a_second_register_is_caught_by_the_verify);
    RUN(an_undo_that_lands_in_a_second_register_is_caught);
    RUN(a_servo_that_differs_from_the_plans_start_is_not_written);
    RUN(a_plan_that_no_planner_builds_is_refused);
    RUN(a_servo_that_fails_the_fingerprint_is_read_only);
    RUN(releasing_the_pairing_and_restoring_it);
    RUN(an_unchecked_restore_needs_its_confirmation);
    RUN(verify_names_the_registers_that_differ);
    RUN(a_verify_against_the_backup_unlocks);
    RUN(a_failed_read_before_the_first_write_locks_nothing);
    RUN(a_failed_read_after_a_write_locks_the_session);
    RUN(an_abort_in_the_entry_wait_sends_nothing);
    RUN(an_abort_never_cuts_a_frame);
    RUN(an_abort_after_the_first_write_locks_and_before_it_does_not);
    RUN(a_power_cycle_leaves_the_mode_and_ends_what_runs);
    RUN(a_driver_that_refuses_a_frame_ends_the_operation);
    RUN(a_driver_that_never_finishes_ends_the_operation);
    RUN(the_driver_has_5_ms_after_the_window);
    RUN(the_clock_may_wrap_in_any_wait);
    RUN(a_slow_caller_makes_the_gaps_longer_and_nothing_else);
    RUN(two_sessions_run_side_by_side);
    return test_summary("kst_session");
}
