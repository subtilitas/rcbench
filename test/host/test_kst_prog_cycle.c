/*
 * The servo programmer's supply cycles (shared/bench/kst_prog.c), end to
 * end on the rig of kst_prog_fix.h: CONNECT switches a PD mini's output
 * off and on before the sync, LEAVE after a write switches it off and on
 * before PWM, and without a PD mini neither is switched.  The servo is the
 * simulated one with needs_power_up: it enters programming mode only on
 * the first sync after a power-up and acts on a written register only
 * from the next power-up on, as the bench measured a Chaservo.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_prog_fix.h"

/* The servo on the PD mini's output, on at @p mv and @p ma for a while:
 * the sync after its power-up is long past. */
static void on_pd_at(uint32_t t0, uint16_t mv, uint16_t ma)
{
    fresh_at(t0);
    g_rail = true;
    g_on_rail = true;
    g_sim.needs_power_up = true;
    g_sup_on = true;
    g_sup_mv = mv;
    g_sup_ma = ma;
    link_up();
    CHECK(g_sim.powered);
    CHECK(!g_sim.fresh);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
}

static void on_pd(void)
{
    on_pd_at(0u, 5000u, 1000u);
}

/* Frames until the PD mini's output has gone off @p n times; 10s bound. */
static void until_offs(unsigned n)
{
    for (unsigned i = 0; i < 10000u && g_sup_offs < n; ++i) {
        frame();
    }
    CHECK_EQ(g_sup_offs, n);
}

/* CONNECT with the output on, through the cycle to the servo read. */
static void connect_cycled(void)
{
    CHECK(kst_prog_connect_cycles(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(g_sim.in_mode);
    CHECK_EQ(kst_prog_type_set(&g_p, kst_prog_type_guess(&g_p)),
             KST_PROG_MSG_NONE);
}

/* LEFT ANGLE to @p raw, written and read back. */
static void write_left(uint16_t raw)
{
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_LEFT_ANGLE, raw);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_field_get(kst_prog_servo(&g_p), KST_F_LEFT_ANGLE), raw);
}

/* What the servo acts on in LEFT ANGLE. */
static uint16_t acting_left(void)
{
    kst_image_t img;

    memcpy(img.r, g_sim.acting, sizeof(img.r));
    return kst_field_get(&img, KST_F_LEFT_ANGLE);
}

/* --------------------------------------------------------------- CONNECT */

TEST_CASE(connect_with_the_output_on_switches_it_off_1s_then_on_then_syncs)
{
    on_pd();
    /* The programming values above the output's: the session runs at the
     * output's, and no value is raised by the cycle. */
    kst_prog_supply_set(&g_p, 6000u, 1500u);
    CHECK(kst_prog_connect_cycles(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_REBOOT);
    CHECK(kst_prog_holds_port(&g_p));
    until_offs(1u);
    /* 999ms after the read that shows it off: off still, no sync. */
    frames(KST_PORT_RAIL_OFF_MS - 1u);
    CHECK(!g_sim.powered);
    CHECK(!g_sup_on);
    CHECK_EQ(g_sup_ons, 0u);
    CHECK_EQ(g_sim.n_syncs, 0u);
    CHECK(kst_prog_holds_port(&g_p));
    CHECK(kst_prog_port_taken(&g_p.f.rd));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    settle();
    CHECK_EQ(g_sup_offs, 1u);
    CHECK_EQ(g_sup_ons, 1u);
    CHECK((uint32_t)(g_on_ms - g_off_ms) >= KST_PORT_RAIL_OFF_MS);
    /* The ENTER follows the off time at once: the panel's exchanges, the
     * port's 50ms without a pulse, nothing more. */
    CHECK((uint32_t)(g_on_ms - g_off_ms) < KST_PORT_RAIL_OFF_MS + 400u);
    CHECK_EQ(g_sim.power_ups, 1u);
    /* The sync came after the servo's start time. */
    CHECK(g_sim.in_mode);
    CHECK((uint32_t)(g_sim.sync_taken_us - g_on_us)
          >= KST_PORT_RAIL_WAIT_MS * 1000u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK_EQ(g_sup_mv, 5000u);
    CHECK_EQ(g_sup_ma, 1000u);
    /* REBOOT, ENTER and READ_ALL; the ABORT that kept the output off. */
    CHECK_EQ(g_starts, 3u);
    CHECK_EQ(g_aborts, 1u);
}

TEST_CASE(connect_lowers_the_programming_values_and_raises_none)
{
    /* The output on above the programming values: the ENTER after the
     * cycle switches it on at the programming values. */
    on_pd_at(0u, 6000u, 2000u);
    kst_prog_supply_set(&g_p, 5200u, 800u);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(g_sup_mv, 5200u);
    CHECK_EQ(g_sup_ma, 800u);
    CHECK_EQ(g_sup_offs, 1u);
}

TEST_CASE(connect_with_the_output_off_powers_the_servo_up_without_a_cycle)
{
    on_pd();
    g_sup_on = false;
    frames(5);
    CHECK(!g_sim.powered);
    CHECK(!kst_prog_connect_cycles(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_ENTER);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(g_sup_offs, 0u);
    CHECK_EQ(g_sup_ons, 1u);
    CHECK_EQ(g_sim.power_ups, 1u);
    CHECK_EQ(g_starts, 2u);
    CHECK_EQ(g_aborts, 0u);
}

TEST_CASE(connect_without_a_pd_mini_switches_nothing_and_needs_a_power_up)
{
    fresh_at(0u);
    g_sim.needs_power_up = true;     /* fed from elsewhere, on for long */
    link_up();
    CHECK(!kst_prog_connect_cycles(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_ENTER);
    settle();
    /* The sync went and was ignored: no servo answers. */
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_NO_SERVO);
    CHECK_EQ(g_sim.n_syncs, 1u);
    CHECK(!g_sim.in_mode);
    CHECK_EQ(g_sup_offs + g_sup_ons, 0u);
    /* The operator switches the servo off and on, leaves and connects
     * again: the sync after the power-up is taken. */
    sim_power(&g_sim, false);
    sim_power(&g_sim, true);
    if (kst_prog_state(&g_p) == KST_PROG_PROGRAMMING) {
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    }
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(g_sim.in_mode);
    CHECK_EQ(g_sup_offs + g_sup_ons, 0u);
}

TEST_CASE(stop_in_the_off_time_leaves_the_output_off_and_the_port_released)
{
    on_pd();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    until_offs(1u);
    frames(KST_PORT_RAIL_OFF_MS / 2u);
    /* STOP: latched on the panel, the bank switched off at the IOmcu, and
     * the control task's abort. */
    g_stopped = true;
    g_stop = true;
    frame();
    g_stop = false;
    kst_link_abort(&g_k);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_holds_port(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);
    CHECK(!g_sup_on);
    CHECK_EQ(g_sup_ons, 0u);
    CHECK_EQ(g_sim.n_syncs, 0u);
    /* No ENTER after the REBOOT. */
    CHECK_EQ(g_starts, 1u);
    frames(3000);
    CHECK(!g_sup_on);
    CHECK_EQ(kst_port_state(&g_port), LINK_KST_PWM);
}

TEST_CASE(arm_in_the_off_time_is_refused_and_one_at_its_end_sends_no_enter)
{
    on_pd();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    until_offs(1u);
    frames(100);
    /* The control task takes no arm: the port reads out of PWM. */
    CHECK(kst_prog_port_taken(&g_p.f.rd));
    CHECK(kst_prog_holds_port(&g_p));
    /* An arm that lands at the end of the off time: no ENTER, the output
     * stays off. */
    g_armed = true;
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_DISARM);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!g_sup_on);
    CHECK_EQ(g_sup_ons, 0u);
    CHECK_EQ(g_starts, 1u);
}

TEST_CASE(abort_in_the_off_time_keeps_the_output_off_and_sends_no_enter)
{
    on_pd();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    until_offs(1u);
    kst_prog_abort(&g_p);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_CYCLE_CUT);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!g_sup_on);
    CHECK_EQ(g_starts, 1u);
    CHECK_EQ(g_sim.n_syncs, 0u);
}

TEST_CASE(a_target_changed_in_the_off_time_sends_no_enter)
{
    on_pd();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    until_offs(1u);
    g_target = 0u;
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_TARGET);
    CHECK(!g_sup_on);
    CHECK_EQ(g_starts, 1u);
}

TEST_CASE(a_link_lost_in_the_off_time_sends_no_enter_after_it)
{
    on_pd();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    until_offs(1u);
    frames(100);
    /* The link goes: no heartbeat at the IOmcu, nothing answers here. */
    g_safe = false;
    kst_link_lost(&g_k);
    frames(KST_PORT_RAIL_OFF_MS + 500u);
    CHECK_EQ(kst_port_state(&g_port), LINK_KST_PWM);
    CHECK(!g_sup_on);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    /* It comes back: the port is in PWM with the output off, and no
     * ENTER was sent. */
    g_safe = true;
    link_up();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!g_sup_on);
    CHECK_EQ(g_starts, 1u);
    CHECK_EQ(g_sim.n_syncs, 0u);
}

TEST_CASE(a_reboot_refused_for_an_output_off_goes_on_with_the_enter)
{
    /* The panel reads the output on; the IOmcu has it asked off. */
    fresh_at(0u);
    g_rail = true;
    g_set_read = true;
    g_set_mv = 5000u;
    g_set_ma = 1000u;
    link_up();
    CHECK(kst_prog_connect_cycles(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(g_starts, 3u);
    CHECK(g_sup_on);
    CHECK_EQ(g_sup_offs, 0u);
}

TEST_CASE(a_cycle_across_the_wrap_of_the_clock_takes_its_times)
{
    on_pd_at(0xFFFFFF00u, 5000u, 1000u);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    until_offs(1u);
    frames(KST_PORT_RAIL_OFF_MS - 1u);
    CHECK(!g_sup_on);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK((uint32_t)(g_on_ms - g_off_ms) >= KST_PORT_RAIL_OFF_MS);
    CHECK(g_ms < 0x1000u);
    write_left(100u);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_CYCLED);
    CHECK_EQ(acting_left(), 100u);
}

/* ----------------------------------------------------------------- LEAVE */

TEST_CASE(leave_after_a_write_cycles_the_output_before_pwm)
{
    uint32_t pwm_ms = 0;

    on_pd();
    connect_cycled();
    CHECK(!kst_prog_cycle_owed(&g_p));
    CHECK(!kst_prog_leave_cycles(&g_p));
    write_left(75u);
    /* Read back, and not acted on before a power-up. */
    CHECK_EQ(acting_left(), 150u);
    CHECK(kst_prog_cycle_owed(&g_p));
    CHECK(kst_prog_leave_cycles(&g_p));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_REBOOT);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    until_offs(2u);
    frames(KST_PORT_RAIL_OFF_MS - 1u);
    CHECK(!g_sup_on);
    CHECK_EQ(g_sup_ons, 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    for (unsigned i = 0; i < 10000u && kst_port_state(&g_port) != LINK_KST_PWM;
         ++i) {
        frame();
        /* No pulse before PWM: the port holds the pin. */
        CHECK(kst_port_state(&g_port) == LINK_KST_PWM
              || kst_port_quiet_mask(&g_port) != 0u);
    }
    pwm_ms = g_ms;
    CHECK_EQ(g_sup_ons, 2u);
    CHECK((uint32_t)(g_on_ms - g_off_ms) >= KST_PORT_RAIL_OFF_MS);
    CHECK((uint32_t)(pwm_ms - g_on_ms) >= KST_PORT_RAIL_WAIT_MS);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_CYCLED);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK_EQ(kst_prog_type(&g_p), 0u);
    /* On again at the session's values, and the written value acts. */
    CHECK(g_sup_on);
    CHECK_EQ(g_sup_mv, 5000u);
    CHECK_EQ(g_sup_ma, 1000u);
    CHECK_EQ(acting_left(), 75u);
    CHECK(!g_sim.in_mode);
}

TEST_CASE(leave_without_a_write_does_not_cycle)
{
    unsigned starts;

    on_pd();
    connect_cycled();
    CHECK(!kst_prog_leave_cycles(&g_p));
    starts = g_starts;
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_LEAVE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(g_starts, starts + 1u);
    CHECK_EQ(g_sup_offs, 1u);
    CHECK(g_sup_on);
}

TEST_CASE(leave_after_a_write_without_a_pd_mini_says_the_values_wait)
{
    fresh_at(0u);
    g_sim.needs_power_up = true;
    sim_power(&g_sim, false);
    sim_power(&g_sim, true);     /* the operator powers it up */
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(kst_prog_type_set(&g_p, 6u), KST_PROG_MSG_NONE);
    write_left(80u);
    CHECK(kst_prog_cycle_owed(&g_p));
    CHECK(!kst_prog_leave_cycles(&g_p));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_LEAVE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_NOTE);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(acting_left(), 150u);
    CHECK_EQ(g_sup_offs + g_sup_ons, 0u);
    /* A leave with no write says nothing of it. */
    sim_power(&g_sim, false);
    sim_power(&g_sim, true);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
}

TEST_CASE(abort_or_stop_in_a_leaves_cycle_ends_the_session_with_the_output_off)
{
    for (int stop = 0; stop < 2; ++stop) {
        on_pd();
        connect_cycled();
        write_left(90u);
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        until_offs(2u);
        frames(200);
        if (stop != 0) {
            g_stopped = true;
            g_stop = true;
            frame();
            g_stop = false;
            kst_link_abort(&g_k);
        } else {
            kst_prog_abort(&g_p);
        }
        settle();
        CHECK_EQ(msg(), stop != 0 ? KST_PROG_MSG_STOPPED
                                  : KST_PROG_MSG_CYCLE_CUT);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
        CHECK(!kst_prog_have_servo(&g_p));
        CHECK(!g_sup_on);
        CHECK_EQ(g_sup_ons, 1u);
        /* Off, and never on again: the value acts at the operator's
         * next power-up. */
        CHECK_EQ(acting_left(), 150u);
        CHECK(!g_sim.powered);
    }
}

TEST_CASE(a_link_lost_in_a_leaves_cycle_leaves_the_output_off)
{
    on_pd();
    connect_cycled();
    write_left(95u);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    until_offs(2u);
    g_safe = false;
    kst_link_lost(&g_k);
    frames(KST_PORT_RAIL_OFF_MS + KST_PORT_RAIL_WAIT_MS + 500u);
    CHECK(!g_sup_on);
    CHECK_EQ(kst_port_state(&g_port), LINK_KST_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    g_safe = true;
    link_up();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!g_sup_on);
}

TEST_CASE(an_iomcu_without_leave_cycles_the_output_after_a_write)
{
    fresh_at(0u);
    g_rail = true;
    g_on_rail = true;
    g_sim.needs_power_up = true;
    g_sup_on = true;
    g_sup_mv = 5000u;
    g_sup_ma = 1000u;
    kst_link_came_up(&g_k, LINK_MINOR_KST_LEAVE - 1u);
    settle();
    connect_cycled();
    /* No write: LEAVE is what such an IOmcu lacks, and a PD mini keeps
     * the session. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_SUPPLY);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    write_left(85u);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_CYCLED);
    CHECK_EQ(acting_left(), 85u);
    CHECK(g_sup_on);
}

TEST_CASE(a_session_the_panel_did_not_see_begin_cycles_at_leave)
{
    on_pd();
    connect_cycled();
    /* The panel starts again under the held port. */
    prog_fresh();
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_cycle_owed(&g_p));
    CHECK(kst_prog_leave_cycles(&g_p));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_REBOOT);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_CYCLED);
}

TEST_CASE(a_port_in_doubt_with_a_pd_mini_is_not_left_by_a_cycle)
{
    on_pd();
    connect_cycled();
    write_left(70u);
    /* STOP: the port asks for a read before anything else. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    frames(3);
    g_stop = true;
    frame();
    g_stop = false;
    settle();
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_leave_cycles(&g_p));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_LEAVE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_MUST_READ);
    CHECK(g_sup_on);
}

int main(void)
{
    RUN(connect_with_the_output_on_switches_it_off_1s_then_on_then_syncs);
    RUN(connect_lowers_the_programming_values_and_raises_none);
    RUN(connect_with_the_output_off_powers_the_servo_up_without_a_cycle);
    RUN(connect_without_a_pd_mini_switches_nothing_and_needs_a_power_up);
    RUN(stop_in_the_off_time_leaves_the_output_off_and_the_port_released);
    RUN(arm_in_the_off_time_is_refused_and_one_at_its_end_sends_no_enter);
    RUN(abort_in_the_off_time_keeps_the_output_off_and_sends_no_enter);
    RUN(a_target_changed_in_the_off_time_sends_no_enter);
    RUN(a_link_lost_in_the_off_time_sends_no_enter_after_it);
    RUN(a_reboot_refused_for_an_output_off_goes_on_with_the_enter);
    RUN(a_cycle_across_the_wrap_of_the_clock_takes_its_times);
    RUN(leave_after_a_write_cycles_the_output_before_pwm);
    RUN(leave_without_a_write_does_not_cycle);
    RUN(leave_after_a_write_without_a_pd_mini_says_the_values_wait);
    RUN(abort_or_stop_in_a_leaves_cycle_ends_the_session_with_the_output_off);
    RUN(a_link_lost_in_a_leaves_cycle_leaves_the_output_off);
    RUN(an_iomcu_without_leave_cycles_the_output_after_a_write);
    RUN(a_session_the_panel_did_not_see_begin_cycles_at_leave);
    RUN(a_port_in_doubt_with_a_pd_mini_is_not_left_by_a_cycle);
    return test_summary("kst_prog_cycle");
}
