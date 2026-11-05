/*
 * MOVE SERVO on the Hitec programmer's model (shared/bench/hitec_move.c):
 * the gate and each refusal with nothing sent, the passes toward the
 * followed position with the slew and the reach, the end with its reboot
 * and its compare, a bind by each judge with the move back, the current
 * at the stall level just under and over 400ms, the 32-bit ms wrap, and
 * every hard end: STOP, a switch to SAFE at the bench, the link lost, the
 * supply off, the AS5600 gone, a failed read, ABORT.  Every hard end but
 * ABORT counts one fault at the gate, the panel's error path that turns
 * every output off.  The rig is hitec_prog_rig.h, its servo follows the
 * target (hitec_sim.h, d_follow).
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "hitec_move.h"

/* The stall level of the default 1000mA: 800mA. */
#define STALL_MA 800u

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

/* A D485HW whose horn follows its target, at the neutral, connected. */
static void held_from(uint32_t t0)
{
    rig_from(t0, SUPPLY_ON, false);
    servo_d485();
    g_drv.d_follow = true;
    g_drv.dreg[HITEC_D_REG_TARGET] = HITEC_LIVE_D_T_MID;
    sim_d_follow();
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

static void held(void)
{
    held_from(0u);
}

/* MOVE SERVO on and its start's read done. */
static void moving(void)
{
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_MOVING);
    CHECK(hitec_prog_move_on(model()));
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

/* Run until the servo's position lies within @p tol of @p pos. */
static void until_pos(uint16_t pos, uint16_t tol)
{
    for (unsigned i = 0; i < 60000u; ++i) {
        const int d = (int)g_drv.dreg[HITEC_D_REG_POSITION] - (int)pos;

        if (d <= (int)tol && d >= -(int)tol) {
            return;
        }
        pms();
    }
    CHECK_EQ(g_drv.dreg[HITEC_D_REG_POSITION], pos);
}

/* MOVE OFF and the end run to the held port. */
static void ended(void)
{
    hitec_prog_move_stop(model());
    CHECK(!hitec_prog_move_on(model()));
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
}

TEST_CASE(move_servo_follows_neutral_and_ends_with_the_reboot_and_no_save)
{
    const unsigned saves0 = g_drv.d_saves;
    unsigned reboots;
    uint32_t f0;

    held();
    reboots = g_drv.d_reboots;
    f0 = faults();
    moving();
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_NEUTRAL, 7800),
             HITEC_PROG_EDIT_OK);
    until_pos(7800u, 13u);
    /* The edit stands in the draft; nothing is written or saved. */
    CHECK_EQ(draft(HITEC_P_D_NEUTRAL), 7800);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    CHECK_EQ(g_drv.d_saves, saves0);
    CHECK_EQ(g_drv.dreg[0xC2], 8192u);
    ended();
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_DONE);
    CHECK_EQ(hitec_prog_level(msg()), HITEC_PROG_INFO);
    /* The end: 0x1E back to 3000 and one reboot, the horn at the neutral. */
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_EQ(g_drv.dreg[HITEC_D_REG_TARGET], HITEC_LIVE_D_T_MID);
    CHECK_EQ(g_drv.dreg[HITEC_D_REG_POSITION], 8192u);
    CHECK_EQ(g_drv.d_saves, saves0);
    CHECK_EQ(faults(), f0);
    /* The draft keeps the edit; READ ALL is not owed and a write is
     * taken. */
    CHECK_EQ(draft(HITEC_P_D_NEUTRAL), 7800);
    CHECK(!model()->must_read);
    CHECK_EQ(hitec_prog_write_gate(model(), HITEC_PROG_WR_EDITS),
             HITEC_PROG_MSG_WRITE_OFF);
}

TEST_CASE(each_pass_moves_the_command_150_counts_at_most)
{
    uint16_t last;
    unsigned steps = 0;
    unsigned big = 0;

    held();
    moving();
    /* LEFT END 3351 followed: 4841 counts away from the neutral. */
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 3351),
             HITEC_PROG_EDIT_OK);
    last = g_drv.dreg[HITEC_D_REG_POSITION];
    for (unsigned i = 0; i < 60000u && last > 3360u; ++i) {
        const uint16_t now = g_drv.dreg[HITEC_D_REG_POSITION];

        pms();
        if (now != last) {
            const unsigned d = last > now ? last - now : now - last;

            ++steps;
            /* One count of rounding through the target. */
            big += d > HITEC_LIVE_SLEW + 1u ? 1u : 0u;
            last = now;
        }
    }
    CHECK_EQ(big, 0u);
    CHECK(steps >= (8192u - 3351u) / HITEC_LIVE_SLEW);
    CHECK(last <= 3360u);
    ended();
}

TEST_CASE(a_value_outside_the_reach_stops_the_horn_at_the_reach_edge)
{
    hitec_move_view_t v;

    held();
    moving();
    /* 2000 lies below the reach of target 0, 2606 on the factory ends. */
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 2000),
             HITEC_PROG_EDIT_OK);
    until_pos(2606u, 1u);
    prun(500u);
    CHECK_EQ(g_drv.dreg[HITEC_D_REG_TARGET], 0u);
    CHECK(hitec_prog_move_view(model(), &v));
    CHECK(v.clamped);
    CHECK_EQ(v.lo, 2606u);
    CHECK_EQ(v.hi, 13743u);
    CHECK_EQ(v.follow, HITEC_P_D_LEFT_END);
    CHECK(v.has_pos);
    CHECK(v.has_ma);
    CHECK_EQ(v.ma, 41u);
    ended();
}

TEST_CASE(a_blocked_horn_binds_moves_back_and_ends_without_a_fault)
{
    const hitec_prog_msg_t *m;
    uint32_t f0;
    unsigned reboots;

    held();
    f0 = faults();
    reboots = g_drv.d_reboots;
    g_drv.d_block_lo = 6000u;
    moving();
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 3351),
             HITEC_PROG_EDIT_OK);
    for (unsigned i = 0; i < 60000u && msg() != HITEC_PROG_MSG_MOVE_BLOCKED;
         ++i) {
        pms();
    }
    m = hitec_prog_message(model());
    CHECK_EQ(m->id, HITEC_PROG_MSG_MOVE_BLOCKED);
    CHECK_EQ(m->arg, 6000u);
    /* Back from the last free position, 6000 to one slew above it, by 75
     * counts. */
    CHECK(m->reg >= 6000u + HITEC_LIVE_BACK);
    CHECK(m->reg <= 6000u + HITEC_LIVE_SLEW + HITEC_LIVE_BACK);
    CHECK(!hitec_prog_move_on(model()));
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    /* The bind's line stays; the end ran; no error. */
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_BLOCKED);
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_EQ(faults(), f0);
    CHECK(g_hw.v.on);
}

TEST_CASE(a_current_at_the_stall_level_binds_after_400ms_not_before)
{
    uint32_t t_over;

    held();
    moving();
    /* 799mA: under the level, no bind in 3s. */
    g_out_ma = STALL_MA - 1u;
    prun(3000u);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
    /* 800mA for 350ms, then under again: the run starts over. */
    g_out_ma = STALL_MA;
    prun(350u);
    g_out_ma = 41u;
    prun(600u);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
    /* 800mA held: the bind once 400ms passed since its first reading. */
    g_out_ma = STALL_MA;
    t_over = g_ms;
    for (unsigned i = 0; i < 5000u && msg() != HITEC_PROG_MSG_MOVE_CURRENT;
         ++i) {
        pms();
    }
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_CURRENT);
    CHECK_EQ(hitec_prog_message(model())->arg, STALL_MA);
    CHECK(g_ms - t_over >= HITEC_LIVE_STALL_MS);
    CHECK(g_ms - t_over < HITEC_LIVE_STALL_MS + 400u);
    g_out_ma = 41u;
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_CURRENT);
}

TEST_CASE(the_stall_level_is_80_percent_of_the_session_current_limit)
{
    held();
    CHECK_EQ(hitec_prog_move_stall_ma(model()), STALL_MA);
    hitec_prog_init(&g_p);
    hitec_prog_supply_set(&g_p, 5000u, 1500u);
    CHECK_EQ(hitec_prog_move_stall_ma(&g_p), 1200u);
}

TEST_CASE(the_current_judge_counts_across_the_32_bit_ms_wrap)
{
    held_from(0xFFFFFFFFu - 60000u);
    moving();
    /* Run the clock to 200ms before the wrap, then over the stall level. */
    while (g_ms != 0xFFFFFFFFu - 200u) {
        pms();
    }
    g_out_ma = STALL_MA;
    for (unsigned i = 0; i < 5000u && msg() != HITEC_PROG_MSG_MOVE_CURRENT;
         ++i) {
        pms();
    }
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_CURRENT);
    CHECK(g_ms < 0x80000000u);
    CHECK(g_ms + 200u >= HITEC_LIVE_STALL_MS);
    g_out_ma = 41u;
    until(HITEC_PROG_HELD);
}

TEST_CASE(an_as5600_angle_that_does_not_follow_binds)
{
    held();
    g_angle_on = true;
    g_angle = 1000u;
    moving();
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 3351),
             HITEC_PROG_EDIT_OK);
    for (unsigned i = 0; i < 20000u && msg() != HITEC_PROG_MSG_MOVE_ENC;
         ++i) {
        pms();
    }
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_ENC);
    until(HITEC_PROG_HELD);
}

TEST_CASE(an_as5600_angle_that_follows_lets_the_horn_reach_the_end)
{
    held();
    g_angle_on = true;
    g_angle_follow = true;
    moving();
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 3351),
             HITEC_PROG_EDIT_OK);
    until_pos(3351u, 13u);
    CHECK_EQ(phase(), HITEC_MOVE_RUN);
    ended();
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_DONE);
}

TEST_CASE(an_as5600_reading_gone_ends_the_loop_hard_with_a_fault)
{
    uint32_t f0;
    unsigned reboots;

    held();
    g_angle_on = true;
    g_angle_follow = true;
    moving();
    f0 = faults();
    reboots = g_drv.d_reboots;
    g_angle_on = false;
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_ENC_LOST);
    CHECK_EQ(hitec_prog_level(msg()), HITEC_PROG_DANGER);
    CHECK_EQ(faults(), f0 + 1u);
    CHECK_EQ(g_drv.d_reboots, reboots);
    CHECK(model()->must_read);
}

TEST_CASE(a_stop_ends_the_loop_hard_counts_a_fault_and_owes_read_all)
{
    uint32_t f0;
    unsigned reboots, moves;

    held();
    moving();
    CHECK_EQ(hitec_prog_write_enable(model(), true),
             HITEC_PROG_MSG_BUSY);
    f0 = faults();
    reboots = g_drv.d_reboots;
    press_stop();
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK_EQ(faults(), f0 + 1u);
    CHECK_EQ(g_drv.d_reboots, reboots);
    CHECK(model()->must_read);
    CHECK(!hitec_prog_write_on(model()));
    /* Nothing more goes to the servo. */
    moves = g_drv.d_moves;
    prun(2000u);
    CHECK_EQ(g_drv.d_moves, moves);
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_STOP_LATCHED);
}

TEST_CASE(a_step_the_link_does_not_run_ends_the_loop_hard_with_a_fault)
{
    uint32_t f0;
    unsigned reboots;

    held();
    moving();
    f0 = faults();
    reboots = g_drv.d_reboots;
    g_step_busy = true;
    until_move(HITEC_MOVE_OFF);
    g_step_busy = false;
    prun(50u);
    CHECK_EQ(faults(), f0 + 1u);
    CHECK_EQ(g_drv.d_reboots, reboots);
    CHECK(model()->must_read);
    CHECK(model()->mv.owed);
    CHECK(!hitec_prog_write_on(model()));
    CHECK(!hitec_prog_move_on(model()));
}

TEST_CASE(a_switch_to_safe_at_the_bench_ends_the_loop_with_a_fault)
{
    uint32_t f0;

    held();
    moving();
    f0 = faults();
    disarm();
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK_EQ(faults(), f0 + 1u);
    CHECK(model()->must_read);
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_STOPPED);
}

TEST_CASE(a_lost_link_ends_the_loop_with_a_fault)
{
    uint32_t f0;

    held();
    moving();
    f0 = faults();
    link_silent();
    until_move(HITEC_MOVE_OFF);
    CHECK_EQ(faults(), f0 + 1u);
    prun(200u);
    CHECK_EQ(pstate(), HITEC_PROG_DOWN);
    CHECK(!hitec_prog_move_on(model()));
}

TEST_CASE(the_supply_going_off_ends_the_loop_hard_with_a_fault)
{
    uint32_t f0;
    unsigned moves;

    held();
    moving();
    f0 = faults();
    g_hw.v.on = false;
    until_move(HITEC_MOVE_OFF);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_SUPPLY);
    CHECK_EQ(faults(), f0 + 1u);
    moves = g_drv.d_moves;
    prun(1000u);
    CHECK_EQ(g_drv.d_moves, moves);
    CHECK(model()->must_read);
}

TEST_CASE(a_failed_position_read_is_one_fault_and_ends_the_loop)
{
    uint32_t f0;

    held();
    moving();
    f0 = faults();
    g_drv.d_bad_sum_plus1 = (uint16_t)(HITEC_D_REG_POSITION + 1u);
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ_FAIL);
    CHECK_EQ(hitec_prog_message(model())->arg, HITEC_D_REG_POSITION);
    CHECK_EQ(faults(), f0 + 1u);
    CHECK(model()->must_read);
}

TEST_CASE(abort_during_the_loop_runs_the_end_with_no_fault)
{
    uint32_t f0;
    unsigned reboots;

    held();
    moving();
    f0 = faults();
    reboots = g_drv.d_reboots;
    hitec_prog_abort(model());
    CHECK(!hitec_prog_move_on(model()));
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    /* The ABORT's line stays; the end ran and found the servo as it was. */
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_EQ(faults(), f0);
    CHECK(!model()->must_read);
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_NONE);
}

TEST_CASE(abort_during_the_start_moves_nothing)
{
    unsigned moves;

    held();
    moves = g_drv.d_moves;
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_NONE);
    prun(20u);
    hitec_prog_abort(model());
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK_EQ(g_drv.d_moves, moves);
    CHECK(!model()->must_read);
}

TEST_CASE(abort_during_the_end_ends_it_hard)
{
    held();
    moving();
    hitec_prog_move_stop(model());
    until_move(HITEC_MOVE_REBOOT);
    hitec_prog_abort(model());
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK(model()->must_read);
    CHECK(model()->mv.owed);
    /* READ ALL takes what the loop owes. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    prun(20u);
    CHECK(!model()->must_read);
    CHECK(!model()->mv.owed);
}

TEST_CASE(move_off_during_the_start_moves_nothing)
{
    unsigned moves, reboots;

    held();
    moves = g_drv.d_moves;
    reboots = g_drv.d_reboots;
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(phase(), HITEC_MOVE_START);
    hitec_prog_move_stop(model());
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(g_drv.d_moves, moves);
    CHECK_EQ(g_drv.d_reboots, reboots);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

TEST_CASE(a_register_changed_during_the_loop_owes_read_all)
{
    held();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    moving();
    prun(300u);
    /* Not by the loop: a power limit the servo shows other after it. */
    g_drv.dreg[0x56] = 400u;
    ended();
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_CHANGED);
    CHECK_EQ(hitec_prog_level(msg()), HITEC_PROG_DANGER);
    CHECK(model()->must_read);
    CHECK(!hitec_prog_write_on(model()));
    CHECK_EQ(hitec_prog_write_gate(model(), HITEC_PROG_WR_EDITS),
             HITEC_PROG_MSG_MUST_READ);
}

TEST_CASE(a_position_outside_the_reach_at_the_start_moves_nothing)
{
    unsigned moves;

    held();
    moves = g_drv.d_moves;
    /* 2606 - 51: further than 50 counts below the reach. */
    g_drv.d_follow = false;
    g_drv.dreg[HITEC_D_REG_POSITION] = 2555u;
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_NONE);
    until_move(HITEC_MOVE_OFF);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_OUTSIDE);
    CHECK_EQ(hitec_prog_message(model())->arg, 2555u);
    CHECK_EQ(g_drv.d_moves, moves);
    /* 50 below the reach is taken. */
    g_drv.dreg[HITEC_D_REG_POSITION] = 2556u;
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_NONE);
    until_move(HITEC_MOVE_RUN);
    ended();
}

TEST_CASE(edits_while_on_take_the_holds_consent_and_follow_the_edit)
{
    held();
    moving();
    /* LEFT END changed: HITEC_RULE_END, confirmed by the hold. */
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END,
                                  3351 - (int32_t)HITEC_MOVE_STEP),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_pending(model(), NULL, NULL), 0u);
    CHECK_EQ(model()->mv.follow, HITEC_P_D_LEFT_END);
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_RIGHT_END, 13011),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(model()->mv.follow, HITEC_P_D_RIGHT_END);
    /* The order is a hard rule: refused, the draft and the follow kept. */
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_NEUTRAL, 13011),
             HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_EDIT_REFUSED);
    CHECK_EQ(model()->mv.follow, HITEC_P_D_RIGHT_END);
    CHECK_EQ(draft(HITEC_P_D_NEUTRAL), 8192);
    /* Other values and other operations wait for the end. */
    CHECK(!hitec_prog_move_live(model(), HITEC_P_D_SPEED));
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_BUSY);
    ended();
    /* Off: the positions take no edit through the loop. */
    CHECK(!hitec_prog_move_live(model(), HITEC_P_D_LEFT_END));
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 3300),
             HITEC_PROG_EDIT_REFUSED);
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), 3351 - (int32_t)HITEC_MOVE_STEP);
}

TEST_CASE(the_loop_drives_the_held_port_and_its_pin)
{
    held();
    moving();
    prun(300u);
    CHECK_EQ(hitec_prog_port(model()), CH);
    CHECK_EQ(hitec_prog_pin(model()), PIN);
    CHECK_EQ(g_hw.take_pin, PIN);
    CHECK(g_drv.open);
    CHECK(g_drv.d_moves > 0u);
    ended();
}

TEST_CASE(move_servo_is_refused_without_a_pd_mini_and_sends_nothing)
{
    unsigned cmds;

    /* No PD mini answering: the D series runs on the supply as it is. */
    rig_at(SUPPLY_NONE, false);
    servo_d485();
    connected();
    cmds = g_cmds;
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_MOVE_NEED_PD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_NEED_PD);
    prun(200u);
    CHECK_EQ(g_cmds, cmds);
    CHECK_EQ(phase(), HITEC_MOVE_OFF);
}

TEST_CASE(move_servo_is_refused_on_the_carriers_fixed_5v)
{
    rig();
    g_fixed = true;
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_MOVE_NEED_PD);
}

TEST_CASE(move_servo_is_refused_with_the_pd_mini_output_off)
{
    held();
    g_hw.v.on = false;
    prun(50u);
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_MOVE_NEED_PD);
}

TEST_CASE(move_servo_is_refused_for_a_ccw_servo_or_a_direction_edited)
{
    rig();
    servo_d485();
    g_drv.dreg[0x5E] = HITEC_CCW;
    connected();
    CHECK_EQ(hitec_prog_move_start(model()), HITEC_PROG_MSG_MOVE_NEED_DIR);

    held();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_DIRECTION, HITEC_CCW),
             HITEC_PROG_EDIT_CHECK);
    CHECK_EQ(hitec_prog_confirm(model()), HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_MOVE_NEED_DIR);
    hitec_prog_discard(model());
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_NONE);
}

TEST_CASE(move_servo_is_refused_for_stored_positions_out_of_order)
{
    rig();
    servo_d485();
    g_drv.dreg[0xB2] = 9000u;
    connected();
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_MOVE_NO_MAP);
}

TEST_CASE(move_servo_is_refused_for_a_classic_servo)
{
    rig();
    servo_hs5645();
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_MOVE_NO_MAP);
}

TEST_CASE(move_servo_is_refused_before_a_read_with_a_stop_and_while_owed)
{
    rig();
    servo_d485();
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_NOT_IN_MODE);
    connected();
    press_stop();
    prun(50u);
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_STOP_LATCHED);

    held();
    model()->must_read = true;
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_MUST_READ);
    model()->must_read = false;
    model()->rec = true;
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_RECORD_HELD);
    model()->rec = false;
    model()->run = true;
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_BUSY);
    model()->run = false;
    CHECK_EQ(hitec_prog_move_gate(model()), HITEC_PROG_MSG_NONE);
}

int main(void)
{
    RUN(move_servo_follows_neutral_and_ends_with_the_reboot_and_no_save);
    RUN(each_pass_moves_the_command_150_counts_at_most);
    RUN(a_value_outside_the_reach_stops_the_horn_at_the_reach_edge);
    RUN(a_blocked_horn_binds_moves_back_and_ends_without_a_fault);
    RUN(a_current_at_the_stall_level_binds_after_400ms_not_before);
    RUN(the_stall_level_is_80_percent_of_the_session_current_limit);
    RUN(the_current_judge_counts_across_the_32_bit_ms_wrap);
    RUN(an_as5600_angle_that_does_not_follow_binds);
    RUN(an_as5600_angle_that_follows_lets_the_horn_reach_the_end);
    RUN(an_as5600_reading_gone_ends_the_loop_hard_with_a_fault);
    RUN(a_stop_ends_the_loop_hard_counts_a_fault_and_owes_read_all);
    RUN(a_switch_to_safe_at_the_bench_ends_the_loop_with_a_fault);
    RUN(a_lost_link_ends_the_loop_with_a_fault);
    RUN(a_step_the_link_does_not_run_ends_the_loop_hard_with_a_fault);
    RUN(the_supply_going_off_ends_the_loop_hard_with_a_fault);
    RUN(a_failed_position_read_is_one_fault_and_ends_the_loop);
    RUN(abort_during_the_loop_runs_the_end_with_no_fault);
    RUN(abort_during_the_start_moves_nothing);
    RUN(abort_during_the_end_ends_it_hard);
    RUN(move_off_during_the_start_moves_nothing);
    RUN(a_register_changed_during_the_loop_owes_read_all);
    RUN(a_position_outside_the_reach_at_the_start_moves_nothing);
    RUN(edits_while_on_take_the_holds_consent_and_follow_the_edit);
    RUN(the_loop_drives_the_held_port_and_its_pin);
    RUN(move_servo_is_refused_without_a_pd_mini_and_sends_nothing);
    RUN(move_servo_is_refused_on_the_carriers_fixed_5v);
    RUN(move_servo_is_refused_with_the_pd_mini_output_off);
    RUN(move_servo_is_refused_for_a_ccw_servo_or_a_direction_edited);
    RUN(move_servo_is_refused_for_stored_positions_out_of_order);
    RUN(move_servo_is_refused_for_a_classic_servo);
    RUN(move_servo_is_refused_before_a_read_with_a_stop_and_while_owed);
    return test_summary("hitec_move");
}
