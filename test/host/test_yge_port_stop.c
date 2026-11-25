/*
 * The YGE_PROG page when something ends a session, shared/outputs/yge_port.c:
 * a stop, an abort and a lost link in each state, a stop and a lost link in a
 * write, CLOSE and LEAVE while writing, every error switching the supply off,
 * a supply gone under the session, and an IOmcu restart freeing the port.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_port_rig.h"

/* To each state of a session. */
static void to_state(link_yge_state_t st)
{
    rig_up();
    g.on_lag_ms = st == LINK_YGE_POWER_ON ? 1000u : 0u;
    open_port(0u);
    ST_REG(LINK_YP_W_POWER) = st == LINK_YGE_HAND_WAIT ? LINK_YGE_BY_HAND
                                                       : LINK_YGE_SWITCHED;
    (void)stage();
    (void)cmd(LINK_YGE_OP_ENTER);
    for (int i = 0; i < 100000 && yge_port_state(&g_p) != st; ++i) {
        pass();
    }
}

static const link_yge_state_t k_session[] = {
    LINK_YGE_POWER_OFF, LINK_YGE_POWER_ON, LINK_YGE_HAND_WAIT, LINK_YGE_RUN,
    LINK_YGE_READY,
};

/* Ready, read, and a write of the pinion started, to its first write frame. */
static void to_write(void)
{
    rig_up();
    ready_on(0u);
    read_now();
    draft_of(21u, 13u);
    CHECK_EQ(write_table(g_draft), 0u);
    for (int i = 0; i < 30000 && !yge_port_writing(&g_p); ++i) {
        pass();
    }
    CHECK(yge_port_writing(&g_p));
    CHECK(flag(LINK_YP_F_WRITING));
}

TEST_CASE(a_stop_in_each_state_parks_and_switches_off_at_once)
{
    for (size_t i = 0u; i < sizeof(k_session) / sizeof(k_session[0]); ++i) {
        for (int how = 0; how < 2; ++how) {
            to_state(k_session[i]);
            CHECK_EQ(yge_port_state(&g_p), k_session[i]);
            const int offs = g.offs;
            const int ons = g.ons;
            const int others = g.others_off;

            if (how == 0) {
                pass_as(true, true);
            } else {
                CHECK_EQ(cmd(LINK_YGE_OP_STOP), 0u);
            }
            CHECK_EQ(g.offs, offs + 1);
            CHECK_EQ(g.others_off, others + 1);
            CHECK_ALL_OFF();
            CHECK(flag(LINK_YP_F_STOPPED));
            CHECK(!flag(LINK_YP_F_FAULT));
            CHECK_EQ(result(), LINK_YGE_RESULT_CUT);
            /* Held: a stop is not a give. */
            CHECK_EQ(g.gives, 0);
            CHECK_EQ(yge_port_quiet_mask(&g_p), 1u);
            CHECK_EQ(yge_port_pin_mask(&g_p), 1u);
            /* Nothing comes back by itself, nor through a command. */
            run_ms(2000u);
            CHECK_ALL_OFF();
            (void)cmd(LINK_YGE_OP_ENTER);
            CHECK_EQ(refusal(), LINK_YGE_REF_STOPPED);
            CHECK_EQ(g.ons, ons);
        }
    }
}

TEST_CASE(a_stop_on_an_open_or_free_page)
{
    rig_up();
    open_port(0u);
    const int offs = g.offs;

    (void)cmd(LINK_YGE_OP_STOP);
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_EQ(g.offs, offs);
    CHECK_EQ(g.others_off, 1);
    (void)cmd(LINK_YGE_OP_CLOSE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    (void)cmd(LINK_YGE_OP_STOP);
    pass_as(true, true);
    CHECK(!flag(LINK_YP_F_STOPPED));
    CHECK_EQ(g.others_off, 1);
}

TEST_CASE(a_stop_or_abort_in_stopping_frees_the_pin_untouched)
{
    for (int how = 0; how < 4; ++how) {
        rig_up();
        stage_defaults(0u);
        (void)stage();
        pass();
        (void)cmd(LINK_YGE_OP_OPEN);
        CHECK_EQ(yge_port_state(&g_p), LINK_YGE_STOPPING);
        if (how == 0) {
            pass_as(true, true);
        } else if (how == 1) {
            (void)cmd(LINK_YGE_OP_ABORT);
        } else if (how == 2) {
            (void)cmd(LINK_YGE_OP_STOP);
        } else {
            pass_as(false, false);
        }
        CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
        CHECK_EQ(result(), LINK_YGE_RESULT_CUT);
        CHECK_EQ(g.takes, 0);
        CHECK_EQ(g.gives, 0);
        CHECK_EQ(yge_port_quiet_mask(&g_p), 0u);
        CHECK(!flag(LINK_YP_F_STOPPED));
        CHECK_EQ(g.ons + g.offs, 0);
    }
}

TEST_CASE(a_stop_in_a_write_cuts_it_at_once)
{
    for (int how = 0; how < 3; ++how) {
        to_write();
        const unsigned n = sim.n_sent;

        if (how == 0) {
            pass_as(true, true);
        } else if (how == 1) {
            (void)cmd(LINK_YGE_OP_STOP);
        } else {
            (void)cmd(LINK_YGE_OP_ABORT);
        }
        CHECK_ALL_OFF();
        CHECK_EQ(result(), LINK_YGE_RESULT_CUT);
        CHECK(flag(LINK_YP_F_CUT_WRITE));
        CHECK_EQ(flag(LINK_YP_F_STOPPED), how != 2);
        CHECK(!flag(LINK_YP_F_FAULT));
        /* Not a frame more. */
        run_ms(1000u);
        CHECK_EQ(sim.n_sent, n);
    }
}

TEST_CASE(a_link_or_heartbeat_lost_parks_at_once_also_in_a_write)
{
    for (size_t i = 0u; i < sizeof(k_session) / sizeof(k_session[0]); ++i) {
        to_state(k_session[i]);
        pass_as(false, false);
        CHECK_ALL_OFF();
        CHECK(flag(LINK_YP_F_STOPPED));
        CHECK_EQ(result(), LINK_YGE_RESULT_CUT);
        /* Each pass of it stays so, and asks nothing more. */
        const int offs = g.offs;

        pass_as(false, false);
        CHECK_EQ(g.offs, offs);
    }
    to_write();
    const unsigned n = sim.n_sent;

    pass_as(false, false);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_YP_F_CUT_WRITE));
    run_ms(1000u);
    CHECK_EQ(sim.n_sent, n);
    /* An open page: the stop stands.  A free one: nothing. */
    rig_up();
    open_port(0u);
    pass_as(false, false);
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_EQ(g.ons + g.offs, 0);
    pass();
    (void)cmd(LINK_YGE_OP_CLOSE);
    pass_as(false, false);
    CHECK(!flag(LINK_YP_F_STOPPED));
}

TEST_CASE(close_and_leave_while_writing)
{
    to_write();
    (void)cmd(LINK_YGE_OP_CLOSE);
    CHECK_EQ(refusal(), LINK_YGE_REF_WRITING);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    CHECK(g.v.asked_on);
    CHECK_EQ(g.gives, 0);
    (void)cmd(LINK_YGE_OP_LEAVE);
    CHECK_EQ(refusal(), LINK_YGE_REF_BUSY);
    CHECK(yge_port_writing(&g_p));
    /* The write runs to its end; then CLOSE is taken. */
    run_while(LINK_YGE_RUN, 30000u);
    CHECK_EQ(result(), LINK_YGE_RESULT_DONE);
    CHECK_EQ(sim.table[21], 13u);
    CHECK_EQ(cmd(LINK_YGE_OP_CLOSE), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK(!g.v.asked_on);
    CHECK_EQ(g.gives, 1);
    /* A CLOSE in a read cuts it and gives the pin back. */
    rig_up();
    ready_on(0u);
    (void)cmd(LINK_YGE_OP_READ);
    run_ms(100u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    CHECK_EQ(cmd(LINK_YGE_OP_CLOSE), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK(!g.v.asked_on);
}

/* Each error: what it left. */
static void check_error(unsigned want_result)
{
    CHECK_EQ(result(), want_result);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_ALL_OFF();
    CHECK(g.others_off >= 1);
    CHECK(!prog_supply_step(&g.psup, true));
    /* Held; ENTER and READ refused until CLEAR. */
    CHECK_EQ(yge_port_quiet_mask(&g_p), 1u);
    (void)cmd(LINK_YGE_OP_ENTER);
    CHECK_EQ(refusal(), LINK_YGE_REF_STOPPED);
    (void)cmd(LINK_YGE_OP_READ);
    CHECK_EQ(refusal(), LINK_YGE_REF_STOPPED);
}

/* The rig up with PROG_SUPPLY on, as a panel may have left it. */
static void rig_with_psup(void)
{
    const uint16_t on = 1u;
    const uint16_t panel = 1u;

    rig_up();
    prog_supply_init(&g.psup, 21u, PROG_SUPPLY_CARRIER_MV);
    (void)prog_supply_write(&g.psup, LINK_PS_PANEL, 1u, &panel, true);
    (void)prog_supply_write(&g.psup, LINK_PS_OUTPUT, 1u, &on, true);
    CHECK(prog_supply_step(&g.psup, true));
}

/* A write of @p draft from a fresh read, to its end. */
static void write_to_end(void)
{
    (void)write_table(g_draft);
    run_while(LINK_YGE_RUN, 30000u);
}

TEST_CASE(every_error_switches_the_supply_off)
{
    /* SILENT: no ESC answers the takeover. */
    rig_with_psup();
    sim.silent = true;
    sim.auto_us = 0u;
    ready_on(0u);
    check_error(LINK_YGE_RESULT_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_SILENT);
    /* SILENT: the ESC goes quiet in READY, between operations. */
    rig_with_psup();
    ready_on(0u);
    sim.silent = true;
    run_while(LINK_YGE_READY, 5000u);
    check_error(LINK_YGE_RESULT_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_SILENT);
    /* NO_TABLE: a count of 65. */
    rig_with_psup();
    sim.table[0] = 65u;
    ready_on(0u);
    read_now();
    check_error(LINK_YGE_RESULT_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_NO_TABLE);
    /* CHANGED: the table moved between the read and the write. */
    rig_with_psup();
    ready_on(0u);
    read_now();
    sim.table[22] = 121u;
    draft_of(21u, 13u);
    write_to_end();
    check_error(LINK_YGE_RESULT_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_CHANGED);
    CHECK_EQ(sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u), 0u);
    /* NOT_TAKEN: the ESC answers and stores nothing. */
    rig_with_psup();
    sim.ignore_writes = true;
    ready_on(0u);
    read_now();
    draft_of(21u, 13u);
    write_to_end();
    check_error(LINK_YGE_RESULT_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_NOT_TAKEN);
    /* PARTIAL: one of two stored. */
    rig_with_psup();
    sim.store_max = 1;
    ready_on(0u);
    read_now();
    draft_of(21u, 13u);
    g_draft[22] = 130u;
    write_to_end();
    check_error(LINK_YGE_RESULT_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_PARTIAL);
    /* GUARD: a draft that changes the identity; nothing sent. */
    rig_with_psup();
    ready_on(0u);
    read_now();
    draft_of(14u, 0x4321u);
    CHECK_EQ(write_table(g_draft), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_SESSION);
    check_error(LINK_YGE_RESULT_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_GUARD);
    CHECK_EQ(reg(LINK_YP_SES) >> 8, OY_REF_IDENTITY);
    CHECK_EQ(reg(LINK_YP_BAD_INDEX), 14u);
    CHECK_EQ(sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u), 0u);
    /* The supply's own: refused ON, never on.  BY HAND's timeout is
     * test_yge_port.c's. */
    rig_with_psup();
    g.refuse_on = true;
    ready_on(0u);
    check_error(LINK_YGE_RESULT_SUPPLY);
    rig_with_psup();
    g.never_on = true;
    ready_on(0u);
    check_error(LINK_YGE_RESULT_SUPPLY);
}

TEST_CASE(a_supply_gone_under_the_session)
{
    /* The panel's OFF, the PD mini silent, the output read off after it read
     * on: in READY and in a write. */
    for (int how = 0; how < 3; ++how) {
        for (int in_write = 0; in_write < 2; ++in_write) {
            if (in_write != 0) {
                to_write();
            } else {
                rig_up();
                ready_on(0u);
            }
            if (how == 0) {
                g.v.asked_on = false;
            } else if (how == 1) {
                g.v.online = false;
            } else {
                g.dropped = true;
                g.v.on = false;
                g.v.off = true;
            }
            pass();
            CHECK_EQ(result(), LINK_YGE_RESULT_SUPPLY);
            CHECK(flag(LINK_YP_F_FAULT));
            CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
            CHECK(!g.listen);
            CHECK(!g.v.asked_on);
            CHECK_EQ(flag(LINK_YP_F_CUT_WRITE), in_write != 0);
        }
    }
    /* BY HAND: the PD mini is none of the session's business. */
    rig_up();
    open_port(0u);
    ST_REG(LINK_YP_W_POWER) = LINK_YGE_BY_HAND;
    (void)stage();
    (void)cmd(LINK_YGE_OP_ENTER);
    g.battery = true;
    (void)cmd(LINK_YGE_OP_HAND_DONE);
    run_while(LINK_YGE_RUN, 10000u);
    g.v.online = false;
    run_ms(100u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    /* And a battery the operator takes away: the ESC goes silent. */
    g.battery = false;
    run_while(LINK_YGE_READY, 5000u);
    CHECK_EQ(result(), LINK_YGE_RESULT_SESSION);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK(!g.listen);
    CHECK_EQ(g.ons, 0);
}

TEST_CASE(an_iomcu_restart_frees_the_port)
{
    to_state(LINK_YGE_READY);
    const yge_port_hw_t hw = g_p.hw;

    /* yge_init() on the way up, with the pad in its reset state. */
    CHECK(yge_port_init(&g_p, &hw));
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(yge_port_quiet_mask(&g_p), 0u);
    CHECK_EQ(yge_port_pin_mask(&g_p), 0u);
    CHECK(!yge_port_supply_hold(&g_p));
    CHECK(!yge_port_writing(&g_p));
    CHECK_EQ(reg(LINK_YP_FLAGS) & ~(uint16_t)LINK_YP_F_RAIL, 0u);
    CHECK_EQ(reg(LINK_YP_PORT), YGE_PORT_NO_PIN);
    /* And a new OPEN takes the port. */
    open_port(0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
}

TEST_CASE(the_bank_stays_held_in_every_state)
{
    for (size_t i = 0u; i < sizeof(k_session) / sizeof(k_session[0]); ++i) {
        to_state(k_session[i]);
        CHECK_EQ(yge_port_quiet_mask(&g_p), 1u);
        CHECK_EQ(yge_port_pin_mask(&g_p), 1u);
        CHECK(yge_port_supply_hold(&g_p));
    }
}

int main(void)
{
    RUN(a_stop_in_each_state_parks_and_switches_off_at_once);
    RUN(a_stop_on_an_open_or_free_page);
    RUN(a_stop_or_abort_in_stopping_frees_the_pin_untouched);
    RUN(a_stop_in_a_write_cuts_it_at_once);
    RUN(a_link_or_heartbeat_lost_parks_at_once_also_in_a_write);
    RUN(close_and_leave_while_writing);
    RUN(every_error_switches_the_supply_off);
    RUN(a_supply_gone_under_the_session);
    RUN(an_iomcu_restart_frees_the_port);
    RUN(the_bank_stays_held_in_every_state);
    return test_summary("yge_port_stop");
}
