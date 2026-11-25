/*
 * The YGE_PROG page with the safety gate and PROG_SUPPLY beside it, as
 * firmware/iomcu/src calls them: an ARM asked during the hold is refused and
 * none arms the bank at the CLOSE; the link silent for 199ms leaves the
 * session and for 200ms cuts it, also in a write; the heartbeat lost cuts it;
 * an error switches PROG_SUPPLY off, whose ON the hold refuses.  The calls are
 * control_write(), yge_hw_give(), yge_hw_others_off(), ports_tell_supply(),
 * ports_hold_follow() and the main loop's port_safe.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_port_rig.h"

static link_dev_t      g_dev;
static heartbeat_mon_t g_beat;

/* What a pass of the main loop does, 1ms on: the gate with a heartbeat edge
 * every 20ms while @p beat and a request while @p heard, then the page with
 * the gate's port_safe. */
static void loop_ms(uint32_t ms, bool beat, bool heard)
{
    for (uint32_t i = 0u; i < ms; ++i) {
        sim.us += MS;
        const uint32_t t = now_ms();

        if (heard) {
            g_dev.last_request_ms = t;
            g_dev.heard = true;
        }
        (void)safety_gate_step(&g_beat, beat && (t % 20u) == 0u, &g_dev,
                               g.control, t);
        yge_port_step(&g_p, safety_gate_supply_ok(&g_beat, &g_dev), false, t);
    }
}

/* control_write() as far as ARM goes. */
static uint8_t control(uint16_t arm)
{
    if (safety_gate_hold_refuses_arm(yge_port_quiet_mask(&g_p) != 0u, LINK_CT_ARM,
                                     1u, &arm)) {
        return LINK_NACK_NOT_ARMED;
    }
    return safety_gate_control_write(g.control, LINK_CT_ARM, 1u, &arm, &g_beat,
                                     &g_dev);
}

/* The bench up, the start's latch lifted, the gate trusting the heartbeat. */
static void bench_up(void)
{
    uint16_t clear = LINK_CLEAR_MAGIC;

    rig_up();
    link_dev_init(&g_dev, NULL, 0, NULL, now_ms());
    heartbeat_mon_init(&g_beat);
    loop_ms(300u, true, true);
    CHECK(safety_gate_supply_ok(&g_beat, &g_dev));
    CHECK_EQ(safety_gate_control_write(g.control, LINK_CT_CLEAR, 1u, &clear,
                                       &g_beat, &g_dev), 0u);
}

/* The page held and the ESC taken over, on the gate's clock. */
static void ready_on_gate(void)
{
    stage_defaults(0u);
    (void)stage();
    (void)cmd(LINK_YGE_OP_OPEN);
    loop_ms(100u, true, true);
    (void)cmd(LINK_YGE_OP_ENTER);
    for (int i = 0; i < 20000 && yge_port_state(&g_p) != LINK_YGE_READY; ++i) {
        loop_ms(1u, true, true);
    }
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
}

TEST_CASE(an_arm_during_the_hold_is_refused_and_none_arms_at_the_close)
{
    bench_up();
    stage_defaults(0u);
    (void)stage();
    (void)cmd(LINK_YGE_OP_OPEN);
    /* From the OPEN's frame, in STOPPING already. */
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_STOPPING);
    CHECK_EQ(control(1u), LINK_NACK_NOT_ARMED);
    CHECK_EQ(g.control[LINK_CT_ARM], 0u);
    loop_ms(100u, true, true);
    CHECK_EQ(control(1u), LINK_NACK_NOT_ARMED);
    /* The worst case: a value that reached the page some other way. */
    g.control[LINK_CT_ARM] = 1u;
    g.control[LINK_CT_THROTTLE] = 500u;
    CHECK(!safety_gate_arm_now(true, yge_port_quiet_mask(&g_p), false));
    CHECK_EQ(cmd(LINK_YGE_OP_CLOSE), 0u);
    /* The give cleared it: the next ARM is a new ask. */
    CHECK_EQ(yge_port_quiet_mask(&g_p), 0u);
    CHECK_EQ(g.control[LINK_CT_ARM], 0u);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0u);
    CHECK_EQ(control(1u), 0u);
    /* And an OPEN then is refused. */
    yge_port_bench_armed(&g_p, g.control[LINK_CT_ARM] != 0u);
    (void)cmd(LINK_YGE_OP_OPEN);
    CHECK_EQ(refusal(), LINK_YGE_REF_ARMED);
}

TEST_CASE(the_page_is_cleared_on_both_edges_of_the_hold)
{
    bool was = false;

    bench_up();
    g.control[LINK_CT_THROTTLE] = 500u;
    safety_gate_hold_follow(g.control, &was, yge_port_quiet_mask(&g_p) != 0u);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 500u);
    open_port(0u);
    safety_gate_hold_follow(g.control, &was, yge_port_quiet_mask(&g_p) != 0u);
    CHECK(was);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0u);
    CHECK_EQ(cmd(LINK_YGE_OP_CLOSE), 0u);
    g.control[LINK_CT_THROTTLE] = 500u;
    safety_gate_hold_follow(g.control, &was, yge_port_quiet_mask(&g_p) != 0u);
    CHECK(!was);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0u);
}

TEST_CASE(the_link_silent_199ms_keeps_the_session_200ms_cuts_it)
{
    bench_up();
    ready_on_gate();
    loop_ms(LINK_DEV_SILENCE_MS - 1u, true, false);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK(g.listen);
    loop_ms(1u, true, false);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_EQ(result(), LINK_YGE_RESULT_CUT);
    /* In a write the same: the write is cut. */
    bench_up();
    ready_on_gate();
    (void)cmd(LINK_YGE_OP_READ);
    for (int i = 0; i < 20000 && yge_port_state(&g_p) == LINK_YGE_RUN; ++i) {
        loop_ms(1u, true, true);
    }
    draft_of(21u, 13u);
    (void)write_table(g_draft);
    for (int i = 0; i < 20000 && !yge_port_writing(&g_p); ++i) {
        loop_ms(1u, true, true);
    }
    CHECK(yge_port_writing(&g_p));
    loop_ms(LINK_DEV_SILENCE_MS, true, false);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_YP_F_CUT_WRITE));
    /* The link back: the stop stands and the port stays held. */
    loop_ms(100u, true, true);
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_EQ(yge_port_quiet_mask(&g_p), 1u);
}

TEST_CASE(the_heartbeat_lost_cuts_the_session)
{
    bench_up();
    ready_on_gate();
    /* Edges stop: no later than HEARTBEAT_MAX_GAP_MS after the last one the
     * gate stops trusting the line, and the page is cut in that pass. */
    uint32_t ms = 0u;

    while (safety_gate_supply_ok(&g_beat, &g_dev) && ms < 1000u) {
        CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
        loop_ms(1u, false, true);
        ++ms;
    }
    CHECK(ms <= HEARTBEAT_MAX_GAP_MS + 20u);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_YP_F_STOPPED));
    /* Refused until it is back. */
    (void)cmd(LINK_YGE_OP_CLEAR);
    CHECK_EQ(refusal(), LINK_YGE_REF_UNSAFE);
}

TEST_CASE(an_error_switches_prog_supply_off_and_the_hold_refuses_its_on)
{
    const uint16_t on = 1u;
    const uint16_t panel = 1u;

    rig_up();
    prog_supply_init(&g.psup, 21u, PROG_SUPPLY_CARRIER_MV);
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_PANEL, 1u, &panel, true), 0u);
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true), 0u);
    CHECK(prog_supply_step(&g.psup, true));
    open_port(0u);
    /* ports_tell_supply(): the page's hold.  ON refused, as under a port's. */
    prog_supply_hold(&g.psup, yge_port_supply_hold(&g_p));
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true),
             LINK_NACK_BAD_VALUE);
    /* An error: all off. */
    sim.silent = true;
    sim.auto_us = 0u;
    enter_now();
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK_EQ(g.others_off, 1);
    CHECK(!prog_supply_step(&g.psup, true));
    CHECK_ALL_OFF();
    /* After the give the ON is taken again. */
    (void)cmd(LINK_YGE_OP_CLOSE);
    prog_supply_hold(&g.psup, yge_port_supply_hold(&g_p));
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true), 0u);
}

int main(void)
{
    RUN(an_arm_during_the_hold_is_refused_and_none_arms_at_the_close);
    RUN(the_page_is_cleared_on_both_edges_of_the_hold);
    RUN(the_link_silent_199ms_keeps_the_session_200ms_cuts_it);
    RUN(the_heartbeat_lost_cuts_the_session);
    RUN(an_error_switches_prog_supply_off_and_the_hold_refuses_its_on);
    return test_summary("yge_port_gate");
}
