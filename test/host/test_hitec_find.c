/*
 * FIND STOP on the Hitec programmer's model (shared/bench/hitec_move.c):
 * the search on each side to a block, with the end point 75 counts inside
 * the stop taken into the draft and nothing written; the reach's edge with
 * no block; the 40s cap, the way to the neutral included; a stop too close
 * to the neutral; the operator's end, before and during the search; the
 * refusals; the positions taking no edit while it runs; a bind on the way
 * to the neutral; MOVE OFF, ABORT and STOP during the search.  The rig is
 * hitec_prog_rig.h, its servo follows the target (hitec_sim.h, d_follow)
 * and stops at d_block_lo and d_block_hi.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "hitec_move.h"

/* The block of station step S-L6: about -45deg, position 4840. */
#define BLOCK_L 4840u
/* Its mirror on the right. */
#define BLOCK_R 11540u

static uint32_t faults(void)
{
    return hitec_prog_gate_faults(&g_gate);
}

static hitec_move_phase_t phase(void)
{
    return hitec_prog_move_phase(model());
}

/* Run until the loop is in @p ph; 60s is the bound. */
static void until_move(hitec_move_phase_t ph)
{
    for (unsigned i = 0; i < 60000u && phase() != ph; ++i) {
        pms();
    }
    CHECK_EQ(phase(), ph);
}

/* Run until message @p id is said; @p ms is the bound. */
static void until_msg(unsigned id, unsigned ms)
{
    for (unsigned i = 0; i < ms && msg() != id; ++i) {
        pms();
    }
    CHECK_EQ(msg(), id);
}

/* A D485HW whose horn follows its target, at the neutral, held, and MOVE
 * SERVO on with its start's read done. */
static void moving(void)
{
    rig_from(0u, SUPPLY_ON, false);
    servo_d485();
    g_drv.d_follow = true;
    g_drv.dreg[HITEC_D_REG_TARGET] = HITEC_LIVE_D_T_MID;
    sim_d_follow();
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_NONE);
    until_move(HITEC_MOVE_RUN);
}

/* The draft's value of a D position. */
static int32_t draft(uint8_t id)
{
    hitec_value_t v;

    CHECK_EQ(hitec_param_get(hitec_prog_draft(model()), HITEC_LAYOUT_D, id,
                             &v), HITEC_GET_OK);
    return v.value;
}

TEST_CASE(find_stop_left_sets_left_end_75_counts_inside_the_block_and_writes_nothing)
{
    const unsigned saves0 = g_drv.d_saves;
    const hitec_prog_msg_t *m;
    unsigned end;
    uint32_t f0;

    moving();
    f0 = faults();
    g_drv.d_block_lo = BLOCK_L;
    CHECK_EQ(hitec_prog_find_start(model(), HITEC_FIND_LEFT),
             HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
    until_move(HITEC_MOVE_FIND);
    until_msg(HITEC_PROG_MSG_FIND_FOUND_L, 60000u);
    m = hitec_prog_message(model());
    end = m->reg;
    /* S-L6: the stop within 0.5deg (37 counts) of the block's position. */
    CHECK(m->arg >= BLOCK_L);
    CHECK(m->arg <= BLOCK_L + HITEC_LIVE_FIND_STEP);
    CHECK_EQ(m->reg, m->arg + HITEC_LIVE_BACK);
    CHECK_EQ(hitec_prog_level(msg()), HITEC_PROG_INFO);
    /* The draft takes it and the horn follows it; nothing is written. */
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), (int32_t)m->reg);
    CHECK_EQ(model()->mv.follow, HITEC_P_D_LEFT_END);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
    CHECK_EQ(hitec_prog_find_side(model()), -1);
    CHECK_EQ(g_drv.dreg[0xB2], 3351u);
    CHECK_EQ(g_drv.d_saves, saves0);
    CHECK_EQ(faults(), f0);
    for (unsigned i = 0; i < 5000u
         && g_drv.dreg[HITEC_D_REG_POSITION] != m->reg; ++i) {
        pms();
    }
    CHECK(g_drv.dreg[HITEC_D_REG_POSITION] + 13u >= m->reg);
    CHECK(g_drv.dreg[HITEC_D_REG_POSITION] <= m->reg + 13u);
    /* The positions take edits again. */
    CHECK(hitec_prog_move_live(model(), HITEC_P_D_LEFT_END));
    /* MOVE OFF: the end, the draft keeps the end point, nothing saved. */
    hitec_prog_move_stop(model());
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_DONE);
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), (int32_t)end);
    CHECK_EQ(g_drv.dreg[0xB2], 3351u);
    CHECK_EQ(g_drv.d_saves, saves0);
    CHECK_EQ(faults(), f0);
}

TEST_CASE(find_stop_right_sets_right_end_75_counts_inside_the_block)
{
    const hitec_prog_msg_t *m;

    moving();
    g_drv.d_block_hi = BLOCK_R;
    CHECK_EQ(hitec_prog_find_start(model(), HITEC_FIND_RIGHT),
             HITEC_PROG_MSG_NONE);
    until_msg(HITEC_PROG_MSG_FIND_FOUND_R, 60000u);
    m = hitec_prog_message(model());
    CHECK(m->arg <= BLOCK_R);
    CHECK(m->arg + HITEC_LIVE_FIND_STEP >= BLOCK_R);
    CHECK_EQ(m->reg, m->arg - HITEC_LIVE_BACK);
    CHECK_EQ(draft(HITEC_P_D_RIGHT_END), (int32_t)m->reg);
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), 3351);
    CHECK_EQ(model()->mv.follow, HITEC_P_D_RIGHT_END);
    CHECK_EQ(g_drv.dreg[0xB0], 13003u);
}

TEST_CASE(the_search_moves_37_counts_every_2_passes_from_the_neutral)
{
    unsigned moves;
    uint16_t last;

    moving();
    g_drv.d_block_lo = BLOCK_L;
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    for (unsigned i = 0; i < 10000u && !model()->mv.find_go; ++i) {
        pms();
    }
    CHECK(model()->mv.find_go);
    CHECK_EQ(model()->mv.fs.p, 8192u);
    /* Each change of the search's command is one step outward, after two
     * passes at a position. */
    last = model()->mv.fs.p;
    moves = g_drv.d_moves;
    for (unsigned k = 0; k < 5u; ++k) {
        for (unsigned i = 0; i < 5000u && model()->mv.fs.p == last; ++i) {
            pms();
        }
        CHECK_EQ(model()->mv.fs.p, last - HITEC_LIVE_FIND_STEP);
        CHECK(g_drv.d_moves - moves >= HITEC_LIVE_FIND_PASSES);
        last = model()->mv.fs.p;
        moves = g_drv.d_moves;
    }
}

TEST_CASE(no_block_up_to_the_reach_edge_says_so_and_leaves_the_draft)
{
    moving();
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    until_msg(HITEC_PROG_MSG_FIND_FREE, 60000u);
    CHECK_EQ(hitec_prog_message(model())->arg, 2606u);
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), 3351);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
    /* The horn goes back to the followed NEUTRAL. */
    for (unsigned i = 0; i < 20000u
         && g_drv.dreg[HITEC_D_REG_POSITION] != 8192u; ++i) {
        pms();
    }
    CHECK_EQ(g_drv.dreg[HITEC_D_REG_POSITION], 8192u);
}

TEST_CASE(the_search_ends_at_40s_with_the_position_said)
{
    uint32_t t0;

    moving();
    /* Slow frames: the edge is not reached in 40s. */
    g_frame_ms = 60u;
    t0 = g_ms;
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    until_msg(HITEC_PROG_MSG_FIND_TIME, 60000u);
    CHECK(g_ms - t0 >= HITEC_LIVE_FIND_MS);
    CHECK(g_ms - t0 < HITEC_LIVE_FIND_MS + 1000u);
    CHECK(hitec_prog_message(model())->arg > 2606u);
    CHECK(hitec_prog_message(model())->arg < 8192u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
}

TEST_CASE(the_cap_counts_the_way_to_the_neutral_too)
{
    uint32_t t0;
    uint32_t t_go = 0u;

    moving();
    /* The horn follows LEFT END to the reach edge and stands there. */
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 2000),
             HITEC_PROG_EDIT_OK);
    for (unsigned i = 0; i < 20000u
         && g_drv.dreg[HITEC_D_REG_POSITION] != 2606u; ++i) {
        pms();
    }
    CHECK_EQ(g_drv.dreg[HITEC_D_REG_POSITION], 2606u);
    /* Slow frames: the way to the neutral takes seconds. */
    g_frame_ms = 60u;
    t0 = g_ms;
    (void)hitec_prog_find_start(model(), HITEC_FIND_RIGHT);
    for (unsigned i = 0; i < 60000u && msg() != HITEC_PROG_MSG_FIND_TIME;
         ++i) {
        if (t_go == 0u && model()->mv.find_go) {
            t_go = g_ms;
        }
        pms();
    }
    CHECK_EQ(msg(), HITEC_PROG_MSG_FIND_TIME);
    CHECK(t_go != 0u);
    CHECK(t_go - t0 > 1000u);
    /* 40s from the request, not from the search's start at the neutral. */
    CHECK(g_ms - t0 >= HITEC_LIVE_FIND_MS);
    CHECK(g_ms - t0 < HITEC_LIVE_FIND_MS + 1000u);
    CHECK_EQ(draft(HITEC_P_D_RIGHT_END), 13003);
}

TEST_CASE(a_stop_too_close_to_the_neutral_leaves_the_draft)
{
    moving();
    g_drv.d_block_lo = 8152u;
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    until_msg(HITEC_PROG_MSG_FIND_NEAR, 60000u);
    CHECK_EQ(hitec_prog_message(model())->arg, 8152u);
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), 3351);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
}

TEST_CASE(a_tap_during_the_search_ends_it_and_leaves_the_draft)
{
    moving();
    g_drv.d_block_lo = BLOCK_L;
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    for (unsigned i = 0; i < 10000u && !model()->mv.find_go; ++i) {
        pms();
    }
    prun(1000u);
    hitec_prog_find_stop(model());
    until_msg(HITEC_PROG_MSG_FIND_HALT, 2000u);
    CHECK_EQ(hitec_prog_level(msg()), HITEC_PROG_INFO);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
    CHECK_EQ(hitec_prog_find_side(model()), -1);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK(hitec_prog_move_on(model()));
}

TEST_CASE(a_stop_asked_before_the_search_begins_drops_it)
{
    unsigned m0;

    moving();
    m0 = msg();
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    hitec_prog_find_stop(model());
    CHECK_EQ(hitec_prog_find_side(model()), -1);
    prun(1000u);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
    CHECK_EQ(msg(), m0);
    /* Nothing to end: a stop with no search does nothing. */
    hitec_prog_find_stop(model());
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
}

TEST_CASE(find_stop_is_refused_without_move_servo_on_and_while_one_runs)
{
    rig_from(0u, SUPPLY_ON, false);
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_find_start(model(), HITEC_FIND_LEFT),
             HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_find_side(model()), -1);
    moving();
    CHECK_EQ(hitec_prog_find_start(model(), 2u), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_find_start(model(), HITEC_FIND_LEFT),
             HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_find_start(model(), HITEC_FIND_RIGHT),
             HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
}

TEST_CASE(the_positions_take_no_edit_while_the_search_runs)
{
    hitec_move_view_t v;

    moving();
    g_drv.d_block_lo = BLOCK_L;
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    CHECK(!hitec_prog_move_live(model(), HITEC_P_D_LEFT_END));
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_NEUTRAL, 8000),
             HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_BUSY);
    until_move(HITEC_MOVE_FIND);
    for (unsigned i = 0; i < 10000u && !model()->mv.find_go; ++i) {
        pms();
    }
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 4000),
             HITEC_PROG_EDIT_REFUSED);
    CHECK(hitec_prog_move_view(model(), &v));
    CHECK_EQ(v.find, (int8_t)HITEC_FIND_LEFT);
    CHECK(v.find_go);
    CHECK_EQ(v.find_p, model()->mv.fs.p);
    CHECK_EQ(draft(HITEC_P_D_NEUTRAL), 8192);
    CHECK(hitec_prog_move_on(model()));
}

TEST_CASE(a_bind_on_the_way_to_the_neutral_is_the_loops)
{
    unsigned reboots;
    uint32_t f0;

    moving();
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 3351),
             HITEC_PROG_EDIT_OK);
    for (unsigned i = 0; i < 20000u
         && g_drv.dreg[HITEC_D_REG_POSITION] != 3351u; ++i) {
        pms();
    }
    reboots = g_drv.d_reboots;
    f0 = faults();
    (void)hitec_prog_find_start(model(), HITEC_FIND_RIGHT);
    until_move(HITEC_MOVE_FIND);
    g_out_ma = 800u;
    until_msg(HITEC_PROG_MSG_MOVE_CURRENT, 5000u);
    g_out_ma = 41u;
    CHECK(!model()->mv.find_go);
    CHECK_EQ(hitec_prog_find_side(model()), -1);
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_CURRENT);
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_EQ(faults(), f0);
}

TEST_CASE(move_off_during_the_search_runs_the_end)
{
    unsigned reboots;

    moving();
    g_drv.d_block_lo = BLOCK_L;
    reboots = g_drv.d_reboots;
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    for (unsigned i = 0; i < 10000u && !model()->mv.find_go; ++i) {
        pms();
    }
    hitec_prog_move_stop(model());
    CHECK(!hitec_prog_move_on(model()));
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_DONE);
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_EQ(model()->mv.fs.state, (uint8_t)HITEC_FIND_ABORTED);
}

TEST_CASE(abort_during_the_search_runs_the_end)
{
    unsigned reboots;
    uint32_t f0;

    moving();
    g_drv.d_block_lo = BLOCK_L;
    reboots = g_drv.d_reboots;
    f0 = faults();
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    for (unsigned i = 0; i < 10000u && !model()->mv.find_go; ++i) {
        pms();
    }
    hitec_prog_abort(model());
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_EQ(faults(), f0);
    CHECK(!model()->must_read);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

TEST_CASE(a_stop_during_the_search_ends_it_hard_with_a_fault)
{
    unsigned reboots;
    uint32_t f0;

    moving();
    g_drv.d_block_lo = BLOCK_L;
    (void)hitec_prog_find_start(model(), HITEC_FIND_LEFT);
    for (unsigned i = 0; i < 10000u && !model()->mv.find_go; ++i) {
        pms();
    }
    reboots = g_drv.d_reboots;
    f0 = faults();
    press_stop();
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK_EQ(faults(), f0 + 1u);
    CHECK_EQ(g_drv.d_reboots, reboots);
    CHECK(model()->must_read);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

int main(void)
{
    RUN(find_stop_left_sets_left_end_75_counts_inside_the_block_and_writes_nothing);
    RUN(find_stop_right_sets_right_end_75_counts_inside_the_block);
    RUN(the_search_moves_37_counts_every_2_passes_from_the_neutral);
    RUN(no_block_up_to_the_reach_edge_says_so_and_leaves_the_draft);
    RUN(the_search_ends_at_40s_with_the_position_said);
    RUN(the_cap_counts_the_way_to_the_neutral_too);
    RUN(a_stop_too_close_to_the_neutral_leaves_the_draft);
    RUN(a_tap_during_the_search_ends_it_and_leaves_the_draft);
    RUN(a_stop_asked_before_the_search_begins_drops_it);
    RUN(find_stop_is_refused_without_move_servo_on_and_while_one_runs);
    RUN(the_positions_take_no_edit_while_the_search_runs);
    RUN(a_bind_on_the_way_to_the_neutral_is_the_loops);
    RUN(move_off_during_the_search_runs_the_end);
    RUN(abort_during_the_search_runs_the_end);
    RUN(a_stop_during_the_search_ends_it_hard_with_a_fault);
    return test_summary("hitec_find");
}
