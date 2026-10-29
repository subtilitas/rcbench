/*
 * The Hitec programmer's model (shared/bench/hitec_prog.c) where the bench
 * stops: a STOP during READ ALL, a switch to SAFE at the bench alone, an
 * ABORT, a lost link, a panel that starts again with the port held (the
 * station's #606 finding: the IOmcu's failsafe latched, CLEAR refused
 * UNSAFE, ARM refused while the port is held), a port another session
 * opened, an IOmcu that does not trust the heartbeat, an arm asked while
 * the port is held, and a transaction that never ends.  ARM is refused
 * while a port is held; after LEAVE or RELEASE PORT the switch to ARMED
 * clears the failsafe and CONNECT runs again.  The rig is
 * hitec_prog_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

/* A D485HW connected, and READ ALL @p regs registers in. */
static void reading(unsigned regs)
{
    unsigned done = 0, total = 0;

    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    for (unsigned i = 0; i < 20000u; ++i) {
        pms();
        if (hitec_prog_progress(model(), &done, &total) && done >= regs) {
            break;
        }
    }
    CHECK_EQ(pstate(), HITEC_PROG_READING);
    CHECK(done >= regs && done < total);
}

/* After the port went back to PWM: ARM clears the latch and the failsafe,
 * DISARM, and CONNECT reads the servo again. */
static void arm_and_again(void)
{
    CHECK(!hitec_prog_port_taken(&g_f.rd));
    CHECK(arm());
    CHECK(g_safe);
    CHECK(!g_stopped);
    disarm();
    prun(50u);
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

TEST_CASE(a_stop_during_read_all_cuts_holds_the_port_and_leave_frees_it)
{
    unsigned cmds;

    rig();
    reading(5u);
    press_stop();
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK(hitec_prog_stopped(model()));
    /* Held, the pin released, the port this panel's. */
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
    CHECK(hitec_prog_holds_port(model()));
    CHECK(hitec_prog_port_taken(&g_f.rd));
    /* CLEAR and READ ALL: refused here, nothing sent, while the STOP is
     * latched. */
    cmds = g_cmds;
    CHECK_EQ(hitec_prog_clear(model()), HITEC_PROG_MSG_STOP_LATCHED);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_STOP_LATCHED);
    prun(100u);
    CHECK_EQ(g_cmds, cmds);
    /* ARM is refused while the port is held. */
    CHECK(!arm());
    CHECK_EQ(g_arm_refused, 1u);
    CHECK(g_stopped);
    /* LEAVE: the D series reboot is refused by the STOPPED port, CLOSE is
     * taken. */
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_LEFT);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    arm_and_again();
}

TEST_CASE(a_switch_to_safe_at_the_bench_stops_and_clear_goes_on_reading)
{
    rig();
    reading(8u);
    disarm();
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK(hitec_prog_stopped(model()));
    /* No latch here: CLEAR is sent and taken, and the read starts over
     * with the supply still on. */
    CHECK_EQ(hitec_prog_clear(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_CLEARING);
    until(HITEC_PROG_READING);
    CHECK_EQ(msg(), HITEC_PROG_MSG_CLEARED);
    CHECK(!hitec_prog_stopped(model()));
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_EQ(hitec_prog_model(model())->model, HITEC_MODEL_D485HW);
}

TEST_CASE(on_the_carriers_fixed_supply_clear_goes_on_reading_too)
{
    /* The ports' own 5V reads on, with no reading of it: the servo is
     * still powered, and CLEAR reads on without a new entry. */
    rig();
    g_fixed = true;
    reading(8u);
    disarm();
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK_EQ(hitec_prog_clear(model()), HITEC_PROG_MSG_NONE);
    for (unsigned i = 0; i < 20000u && pstate() == HITEC_PROG_CLEARING;
         ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_READING);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

TEST_CASE(clear_with_no_stop_is_refused_busy)
{
    rig();
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_clear(model()), HITEC_PROG_MSG_BUSY);
}

TEST_CASE(abort_during_read_all_keeps_the_port_and_read_all_runs_again)
{
    rig();
    reading(4u);
    hitec_prog_abort(model());
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK(hitec_prog_holds_port(model()));
    CHECK(hitec_prog_in_mode(model()));
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

TEST_CASE(abort_during_the_entry_keeps_the_port_for_leave)
{
    rig();
    servo_d485();
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_ENTERING);
    hitec_prog_abort(model());
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK(!hitec_prog_in_mode(model()));
    /* READ ALL needs the entry: refused. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
}

TEST_CASE(a_link_lost_during_read_all_leaves_a_port_to_release)
{
    unsigned offs, ons;

    rig();
    reading(6u);
    link_silent();
    prun(20u);
    CHECK_EQ(pstate(), HITEC_PROG_DOWN);
    CHECK(!hitec_prog_port_taken(&g_f.rd));
    prun(500u);
    link_back();
    until(HITEC_PROG_OTHER);
    CHECK_EQ(msg(), HITEC_PROG_MSG_HELD_OTHER);
    CHECK_EQ(hitec_prog_message(model())->arg, CH);
    /* The port is held, by no session of this panel. */
    CHECK(hitec_prog_port_taken(&g_f.rd));
    CHECK(!hitec_prog_in_mode(model()));
    CHECK(!arm());
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_HELD_OTHER);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_HELD_OTHER);
    CHECK_EQ(hitec_prog_clear(model()), HITEC_PROG_MSG_HELD_OTHER);
    /* RELEASE PORT: CLOSE, which the page takes without the heartbeat;
     * the supply stays as it is. */
    offs = g_hw.offs;
    ons = g_hw.ons;
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_RELEASING);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RELEASED);
    CHECK_EQ(hitec_prog_message(model())->arg, CH);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    CHECK_EQ(g_hw.offs, offs);
    CHECK_EQ(g_hw.ons, ons);
    arm_and_again();
}

/* The station's #606 finding: after a panel restart with the port held,
 * the IOmcu's failsafe is latched; CLEAR is refused UNSAFE and ARM, which
 * clears the failsafe, is refused while the port is held.  RELEASE PORT
 * frees it, and the switch to ARMED works. */
TEST_CASE(a_panel_restart_with_the_port_held_releases_then_arms)
{
    uint32_t outs;

    rig();
    servo_d485();
    connected();
    panel_restart();
    until(HITEC_PROG_OTHER);
    CHECK(hitec_prog_port_taken(&g_f.rd));
    /* The page's CLEAR is refused UNSAFE: the heartbeat is not trusted. */
    outs = g_outs;
    CHECK_EQ(hitec_link_clear(&g_l, CH, 9000u), HITEC_LINK_START_OK);
    for (unsigned i = 0; i < 5000u && g_outs == outs; ++i) {
        pms();
    }
    CHECK_EQ(g_out.kind, HITEC_LINK_OUT_REFUSED);
    CHECK_EQ(g_out.refusal, LINK_HITEC_REF_UNSAFE);
    CHECK_EQ(g_out.id, 9000u);
    CHECK(!arm());
    CHECK_EQ(g_arm_refused, 1u);
    CHECK(!g_safe);
    /* RELEASE PORT. */
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RELEASED);
    CHECK(g_hw.v.on);
    arm_and_again();
}

TEST_CASE(a_port_another_session_opened_is_released_with_the_supply_as_is)
{
    hitec_link_outcome_t out;
    unsigned sets;

    rig();
    /* The link module's own OPEN, not the model's: the bench console's
     * session as far as the model can tell. */
    out = open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    until(HITEC_PROG_OTHER);
    CHECK(!hitec_prog_series_set(model(), 7u));
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    CHECK(hitec_prog_series_set(model(), HITEC_D));
    CHECK_EQ(hitec_prog_port(model()), CH);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_HELD_OTHER);
    sets = g_hw.sets;
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    /* A second while the first runs. */
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_BUSY);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RELEASED);
    CHECK_EQ(g_hw.sets, sets);
    /* No port held: nothing to release. */
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_BUSY);
}

TEST_CASE(an_iomcu_that_does_not_trust_the_heartbeat_refuses_connect_unsafe)
{
    rig();
    servo_d485();
    g_safe = false;
    prun(20u);
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_UNSAFE);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    CHECK_EQ(g_hw.takes, 0u);
    /* The switch to ARMED clears it. */
    arm_and_again();
}

TEST_CASE(an_arm_asked_while_the_port_is_held_refuses_read_all)
{
    rig();
    servo_d485();
    connected();
    hitec_prog_bench_armed(model(), true);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NEED_DISARM);
    /* LEAVE sends nothing to the servo that moves it: taken. */
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    hitec_prog_bench_armed(model(), false);
}

TEST_CASE(a_transaction_that_never_ends_is_ended_and_the_port_stays_held)
{
    rig();
    reading(3u);
    g_drv.hang = true;
    for (unsigned i = 0; i < HITEC_PROG_RUN_MS + 10000u
                         && pstate() == HITEC_PROG_READING;
         ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
    CHECK(msg() != HITEC_PROG_MSG_NONE && msg() != HITEC_PROG_MSG_READ);
    CHECK(hitec_prog_port_taken(&g_f.rd));
    g_drv.hang = false;
    g_drv.busy = false;
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
}

int main(void)
{
    RUN(a_stop_during_read_all_cuts_holds_the_port_and_leave_frees_it);
    RUN(a_switch_to_safe_at_the_bench_stops_and_clear_goes_on_reading);
    RUN(on_the_carriers_fixed_supply_clear_goes_on_reading_too);
    RUN(clear_with_no_stop_is_refused_busy);
    RUN(abort_during_read_all_keeps_the_port_and_read_all_runs_again);
    RUN(abort_during_the_entry_keeps_the_port_for_leave);
    RUN(a_link_lost_during_read_all_leaves_a_port_to_release);
    RUN(a_panel_restart_with_the_port_held_releases_then_arms);
    RUN(a_port_another_session_opened_is_released_with_the_supply_as_is);
    RUN(an_iomcu_that_does_not_trust_the_heartbeat_refuses_connect_unsafe);
    RUN(an_arm_asked_while_the_port_is_held_refuses_read_all);
    RUN(a_transaction_that_never_ends_is_ended_and_the_port_stays_held);
    return test_summary("hitec_prog_safe");
}
