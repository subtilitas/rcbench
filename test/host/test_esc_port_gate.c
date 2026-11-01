/*
 * The ESC_WIRE page with the safety gate and PROG_SUPPLY beside it: an ARM asked
 * during the hold is refused and none arms the bank at the CLOSE, a new ARM after
 * it works, and an error switches PROG_SUPPLY off, whose ON the hold refuses.  The
 * calls are the ones firmware/iomcu/src makes: control_write(), esc_hw_give(),
 * esc_hw_others_off(), ports_tell_supply(), ports_hold_follow().
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_port_rig.h"

static link_dev_t      g_dev;
static heartbeat_mon_t g_beat;
static uint32_t        g_t;

/* Passes of the gate, an edge every 20ms and a request every pass. */
static safety_gate_t gate_ms(uint32_t ms)
{
    safety_gate_t r = { false, false, 0u };

    for (uint32_t i = 0u; i < ms; ++i, ++g_t) {
        g_dev.last_request_ms = g_t;
        g_dev.heard = true;
        r = safety_gate_step(&g_beat, (g_t % 20u) == 0u, &g_dev, g.control, g_t);
    }
    return r;
}

/* control_write() as far as ARM goes. */
static uint8_t control(uint16_t arm)
{
    if (safety_gate_hold_refuses_arm(esc_port_quiet_mask(&g_p) != 0u, LINK_CT_ARM,
                                     1u, &arm)) {
        return LINK_NACK_NOT_ARMED;
    }
    return safety_gate_control_write(g.control, LINK_CT_ARM, 1u, &arm, &g_beat,
                                     &g_dev);
}

/* The bench up, the start's latch lifted. */
static void bench_up(void)
{
    uint16_t clear = LINK_CLEAR_MAGIC;

    up_am_rig();
    g_t = 1000u;
    link_dev_init(&g_dev, NULL, 0, NULL, g_t);
    heartbeat_mon_init(&g_beat);
    (void)gate_ms(200u);
    CHECK_EQ(safety_gate_control_write(g.control, LINK_CT_CLEAR, 1u, &clear,
                                       &g_beat, &g_dev), 0u);
}

static uint16_t status(void)
{
    uint16_t st = 0u;
    uint16_t faults = 0u;

    safety_gate_status(&g_beat, &g_dev, g.control, &st, &faults);
    return st;
}

TEST_CASE(an_arm_during_the_hold_is_refused_and_stores_nothing)
{
    bench_up();
    open_target(2u);
    CHECK(esc_port_supply_hold(&g_p));
    CHECK_EQ(control(1u), LINK_NACK_NOT_ARMED);
    CHECK_EQ(g.control[LINK_CT_ARM], 0u);
    CHECK_EQ(status(), LINK_STATE_IDLE);
    /* It moves no latch: a disarm is taken. */
    CHECK(!link_dev_arm_latched(&g_dev));
    CHECK_EQ(control(0u), 0u);
    /* In STOPPING too: from the OPEN's frame. */
    (void)cmd(LINK_ESC_OP_CLOSE);
    stage_defaults(2u);
    (void)stage();
    pass();
    (void)cmd(LINK_ESC_OP_OPEN);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_STOPPING);
    CHECK_EQ(control(1u), LINK_NACK_NOT_ARMED);
    /* Only a frame from register 0 carries ARM; a window of nothing or no values
     * is no ARM. */
    const uint16_t one = 1u;

    CHECK(!safety_gate_hold_refuses_arm(true, LINK_CT_THROTTLE, 1u, &one));
    CHECK(!safety_gate_hold_refuses_arm(true, LINK_CT_ARM, 0u, &one));
    CHECK(!safety_gate_hold_refuses_arm(true, LINK_CT_ARM, 1u, NULL));
    CHECK(!safety_gate_hold_refuses_arm(false, LINK_CT_ARM, 1u, &one));
}

TEST_CASE(nothing_stored_arms_the_bank_at_the_close)
{
    bench_up();
    open_target(2u);
    /* The worst case: a value that reached the page some other way. */
    g.control[LINK_CT_ARM] = 1u;
    g.control[LINK_CT_THROTTLE] = 500u;
    safety_gate_t r = gate_ms(5u);

    CHECK(r.arm);
    CHECK(!safety_gate_arm_now(r.arm, esc_port_quiet_mask(&g_p), false));
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    /* The give cleared it: in the pass after the CLOSE the mask is 0 and nothing
     * arms, and the page says IDLE. */
    CHECK_EQ(esc_port_quiet_mask(&g_p), 0u);
    r = gate_ms(1u);
    CHECK(!r.arm);
    CHECK(!safety_gate_arm_now(r.arm, esc_port_quiet_mask(&g_p), false));
    CHECK_EQ(g.control[LINK_CT_ARM], 0u);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0u);
    CHECK_EQ(status(), LINK_STATE_IDLE);
    safety_gate_hold_ended(NULL);
}

TEST_CASE(a_new_arm_after_the_close_works)
{
    bench_up();
    open_target(2u);
    CHECK_EQ(control(1u), LINK_NACK_NOT_ARMED);
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    CHECK_EQ(control(1u), 0u);
    const safety_gate_t r = gate_ms(1u);

    CHECK(r.arm);
    CHECK(safety_gate_arm_now(r.arm, esc_port_quiet_mask(&g_p), false));
    CHECK_EQ(status(), LINK_STATE_ARMED);
    /* And an OPEN then is refused. */
    esc_port_bench_armed(&g_p, g.control[LINK_CT_ARM] != 0u);
    stage_defaults(2u);
    (void)stage();
    pass();
    (void)cmd(LINK_ESC_OP_OPEN);
    CHECK_EQ(refusal(), LINK_ESC_REF_ARMED);
}

/* PROG_SUPPLY on as the panel left it, the page held and in @p st. */
static void psup_on_held(link_esc_state_t st)
{
    const uint16_t on = 1u;
    const uint16_t panel = 1u;

    up_am_rig();
    prog_supply_init(&g.psup, 21u, PROG_SUPPLY_CARRIER_MV);
    (void)prog_supply_write(&g.psup, LINK_PS_PANEL, 1u, &panel, true);
    (void)prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true);
    open_target(2u);
    if (st == LINK_ESC_READY) {
        ST_REG(LINK_EW_W_HOLD_LO) = 500u;
        ST_REG(LINK_EW_W_OFF_MS) = 0u;
        (void)stage();
        enter_now();
    }
    CHECK_EQ(esc_port_state(&g_p), st);
    CHECK(prog_supply_step(&g.psup, true));
}

TEST_CASE(a_stop_or_abort_switches_prog_supply_off_a_close_does_not)
{
    /* A stop, in READY and on OPEN. */
    psup_on_held(LINK_ESC_READY);
    pass_as(true, true);
    CHECK(!prog_supply_step(&g.psup, true));
    CHECK(!flag(LINK_EW_F_FAULT));
    psup_on_held(LINK_ESC_OPEN);
    pass_as(true, true);
    CHECK(!prog_supply_step(&g.psup, true));
    /* The STOP operation and ABORT. */
    psup_on_held(LINK_ESC_READY);
    (void)cmd(LINK_ESC_OP_STOP);
    CHECK(!prog_supply_step(&g.psup, true));
    psup_on_held(LINK_ESC_READY);
    (void)cmd(LINK_ESC_OP_ABORT);
    CHECK(!prog_supply_step(&g.psup, true));
    /* A stop in a write: it goes at once, the PD mini after the write. */
    sim_init_efm8(&esc, 0xB2u, false);
    rig_up();
    prog_supply_init(&g.psup, 21u, PROG_SUPPLY_CARRIER_MV);
    const uint16_t on = 1u;
    const uint16_t panel = 1u;

    (void)prog_supply_write(&g.psup, LINK_PS_PANEL, 1u, &panel, true);
    (void)prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true);
    ready_on(2u);
    read_now();
    static uint8_t page[EW_PAGE_MAX];

    memcpy(page, g_p.ses.orig, sizeof(page));
    page[0x15] = 2u;
    (void)write_page(page);
    while (esc.erases == 0) {
        pass();
    }
    pass_as(true, true);
    CHECK(!prog_supply_step(&g.psup, true));
    CHECK(g.v.asked_on);
    /* CLOSE with no error leaves it as the panel has it; so does a free page. */
    psup_on_held(LINK_ESC_OPEN);
    (void)cmd(LINK_ESC_OP_CLOSE);
    CHECK_EQ(g.others_off, 0);
    CHECK(prog_supply_step(&g.psup, true));
    pass_as(true, true);
    (void)cmd(LINK_ESC_OP_ABORT);
    CHECK_EQ(g.others_off, 0);
}

TEST_CASE(no_arm_with_no_signal_after_a_pin_that_did_not_come_back)
{
    bench_up();
    open_target(2u);
    g.give_refused = true;
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    CHECK_EQ(result(), LINK_ESC_RESULT_NOT_BACK);
    /* ARM refused, the gate held down. */
    CHECK_EQ(control(1u), LINK_NACK_NOT_ARMED);
    CHECK_EQ(status(), LINK_STATE_IDLE);
    CHECK(!safety_gate_arm_now(true, esc_port_quiet_mask(&g_p), false));
    /* The output takes it at the next CLOSE: a new ARM works. */
    g.give_refused = false;
    CHECK_EQ(cmd_full(LINK_ESC_OP_CLOSE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(control(1u), 0u);
    const safety_gate_t r = gate_ms(1u);

    CHECK(safety_gate_arm_now(r.arm, esc_port_quiet_mask(&g_p), false));
}

TEST_CASE(an_error_switches_prog_supply_off_and_the_hold_refuses_its_on)
{
    const uint16_t on = 1u;
    const uint16_t panel = 1u;

    /* The carrier's supply, the panel's box on before the session. */
    up_am_rig();
    prog_supply_init(&g.psup, 21u, PROG_SUPPLY_CARRIER_MV);
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_PANEL, 1u, &panel, true), 0u);
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true), 0u);
    CHECK(prog_supply_step(&g.psup, true));
    open_target(2u);
    /* ports_tell_supply(): the page's hold.  ON refused, as under a port's. */
    prog_supply_hold(&g.psup, esc_port_supply_hold(&g_p));
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true),
             LINK_NACK_BAD_VALUE);
    /* An error: all off. */
    ST_REG(LINK_EW_W_HOLD_LO) = 500u;
    ST_REG(LINK_EW_W_OFF_MS) = 0u;
    (void)stage();
    sim_script(&esc, F_DEAD, 0, 1, 0u);
    enter_now();
    run_while(LINK_ESC_RUN, 20000u);
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(g.others_off, 1);
    CHECK(!prog_supply_step(&g.psup, true));
    CHECK_ALL_OFF();
    /* After the give the ON is taken again. */
    (void)cmd(LINK_ESC_OP_CLOSE);
    prog_supply_hold(&g.psup, esc_port_supply_hold(&g_p));
    CHECK_EQ(prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true), 0u);
}

/* What ports_hold_follow() does with this page's mask: no ARM or THROTTLE at the
 * hold's start, what a frame stores in the hold stays to its end, and none past it. */
TEST_CASE(the_page_is_cleared_on_both_edges_of_the_hold)
{
    bool was = false;

    bench_up();
    g.control[LINK_CT_THROTTLE] = 500u;
    safety_gate_hold_follow(g.control, &was, esc_port_quiet_mask(&g_p) != 0u);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 500u);
    open_target(2u);
    safety_gate_hold_follow(g.control, &was, esc_port_quiet_mask(&g_p) != 0u);
    CHECK(was);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0u);
    g.control[LINK_CT_THROTTLE] = 500u;
    safety_gate_hold_follow(g.control, &was, esc_port_quiet_mask(&g_p) != 0u);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 500u);
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    /* After the give's own clear, so that this edge is what is measured. */
    g.control[LINK_CT_THROTTLE] = 500u;
    safety_gate_hold_follow(g.control, &was, esc_port_quiet_mask(&g_p) != 0u);
    CHECK(!was);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0u);
}

int main(void)
{
    RUN(an_arm_during_the_hold_is_refused_and_stores_nothing);
    RUN(nothing_stored_arms_the_bank_at_the_close);
    RUN(a_new_arm_after_the_close_works);
    RUN(an_error_switches_prog_supply_off_and_the_hold_refuses_its_on);
    RUN(a_stop_or_abort_switches_prog_supply_off_a_close_does_not);
    RUN(no_arm_with_no_signal_after_a_pin_that_did_not_come_back);
    RUN(the_page_is_cleared_on_both_edges_of_the_hold);
    return test_summary("esc_port_gate");
}
