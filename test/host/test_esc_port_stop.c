/*
 * The ESC_WIRE page when something ends a session, shared/outputs/esc_port.c: a
 * stop, an abort and a lost link in each state, a stop in a write, every session
 * error going to all outputs off (TORN and LOST included, which the session itself
 * leaves in its mode), a supply gone under the session, a restart.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_port_rig.h"

static uint8_t g_page[EW_PAGE_MAX];

/* An AM32 or EFM8 read, and a draft of it with one byte changed. */
static void changed_page(unsigned off, uint8_t value)
{
    memcpy(g_page, g_p.ses.orig, sizeof(g_page));
    g_page[off] = value;
}

/* To each state of a session. */
static void to_state(link_esc_state_t st)
{
    up_am_rig();
    open_target(2u);
    ST_REG(LINK_EW_W_OFF_MS) = 300u;
    ST_REG(LINK_EW_W_HOLD_LO) = 500u;
    ST_REG(LINK_EW_W_POWER) = st == LINK_ESC_HAND_WAIT ? LINK_ESC_BY_HAND
                                                       : LINK_ESC_SWITCHED;
    (void)stage();
    (void)cmd(LINK_ESC_OP_ENTER);
    for (int i = 0; i < 100000 && esc_port_state(&g_p) != st; ++i) {
        pass();
    }
}

static const link_esc_state_t k_session[] = {
    LINK_ESC_POWER_OFF, LINK_ESC_LEAD, LINK_ESC_HAND_WAIT, LINK_ESC_RUN,
    LINK_ESC_READY,
};

TEST_CASE(a_stop_in_each_state_parks_and_switches_off_at_once)
{
    for (size_t i = 0u; i < sizeof(k_session) / sizeof(k_session[0]); ++i) {
        to_state(k_session[i]);
        CHECK_EQ(esc_port_state(&g_p), k_session[i]);
        const int offs = g.offs;

        pass_as(true, true);
        CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
        CHECK_EQ(g.offs, offs + 1);
        CHECK_ALL_OFF();
        CHECK(flag(LINK_EW_F_STOPPED));
        CHECK(!flag(LINK_EW_F_FAULT));
        CHECK_EQ(result(), LINK_ESC_RESULT_CUT);
        /* Held: the same pin and the same hold; a stop is not a give. */
        CHECK_EQ(g.gives, 0);
        CHECK(esc_port_pins(&g_p) != 0u);
        CHECK(esc_port_quiet_mask(&g_p) != 0u);
        /* No line comes back by itself, nor anything through a command. */
        run_ms(2000u);
        CHECK_ALL_OFF();
        (void)cmd(LINK_ESC_OP_ENTER);
        CHECK_EQ(refusal(), LINK_ESC_REF_STOPPED);
    }
}

TEST_CASE(a_stop_by_command_is_the_same)
{
    to_state(LINK_ESC_READY);
    CHECK_EQ(cmd(LINK_ESC_OP_STOP), 0u);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_EW_F_STOPPED));
    /* On OPEN it only stands; on a free page it does nothing. */
    up_am_rig();
    open_target(2u);
    const int offs = g.offs;

    (void)cmd(LINK_ESC_OP_STOP);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK_EQ(g.offs, offs);
    (void)cmd(LINK_ESC_OP_CLOSE);
    (void)cmd(LINK_ESC_OP_STOP);
    CHECK(!flag(LINK_EW_F_STOPPED));
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
}

TEST_CASE(a_stop_or_abort_in_stopping_frees_the_pin_untouched)
{
    for (int how = 0; how < 3; ++how) {
        up_am_rig();
        stage_defaults(2u);
        (void)stage();
        pass();
        (void)cmd(LINK_ESC_OP_OPEN);
        CHECK_EQ(esc_port_state(&g_p), LINK_ESC_STOPPING);
        if (how == 0) {
            pass_as(true, true);
        } else if (how == 1) {
            (void)cmd(LINK_ESC_OP_ABORT);
        } else {
            pass_as(false, false);
        }
        CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
        CHECK_EQ(result(), LINK_ESC_RESULT_CUT);
        CHECK_EQ(g.takes, 0);
        CHECK_EQ(g.gives, 0);
        CHECK_EQ(esc_port_quiet_mask(&g_p), 0u);
        CHECK(!flag(LINK_EW_F_STOPPED));
        CHECK_EQ(g.ons + g.offs, 0);
    }
}

TEST_CASE(a_link_lost_parks_at_once_also_in_a_write)
{
    /* In each state. */
    for (size_t i = 0u; i < sizeof(k_session) / sizeof(k_session[0]); ++i) {
        to_state(k_session[i]);
        pass_as(false, false);
        CHECK_ALL_OFF();
        CHECK(flag(LINK_EW_F_STOPPED));
        /* Each pass of it stays so, and asks nothing more. */
        const int offs = g.offs;

        pass_as(false, false);
        CHECK_EQ(g.offs, offs);
    }
    /* In the erase of an EFM8: no wait for the write. */
    up_s_rig();
    ready_on(2u);
    read_now();
    changed_page(0x15u, 2u);
    CHECK_EQ(write_page(g_page), 0u);
    while (esc.erases == 0) {
        pass();
    }
    CHECK(esc_port_writing(&g_p));
    pass_as(false, false);
    CHECK_ALL_OFF();
    CHECK(!esc_port_writing(&g_p));
    CHECK(flag(LINK_EW_F_DIRTY));
    /* An open page: it stands.  A free one: nothing. */
    up_am_rig();
    open_target(2u);
    pass_as(false, false);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK_EQ(g.ons + g.offs, 0);
    (void)cmd(LINK_ESC_OP_CLOSE);
    pass_as(false, false);
    CHECK(!flag(LINK_EW_F_STOPPED));
    /* OPEN and ENTER are refused until it is back. */
    stage_defaults(2u);
    (void)stage();
    (void)cmd(LINK_ESC_OP_OPEN);
    CHECK_EQ(refusal(), LINK_ESC_REF_UNSAFE);
}

TEST_CASE(an_abort_cuts_a_write_a_stop_lets_it_end)
{
    /* Abort: at once, supply off, no stop standing. */
    up_s_rig();
    ready_on(2u);
    read_now();
    changed_page(0x15u, 2u);
    (void)write_page(g_page);
    while (esc.erases == 0) {
        pass();
    }
    (void)cmd(LINK_ESC_OP_ABORT);
    CHECK_ALL_OFF();
    CHECK(!flag(LINK_EW_F_STOPPED));
    CHECK_EQ(result(), LINK_ESC_RESULT_CUT);
    CHECK_EQ(esc.programs, 0);
    /* Stop: the write ends, read back, then line and supply. */
    up_s_rig();
    ready_on(2u);
    read_now();
    changed_page(0x15u, 2u);
    (void)write_page(g_page);
    while (esc.erases == 0) {
        pass();
    }
    pass_as(true, true);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    CHECK(g.v.asked_on);
    CHECK(esc_port_writing(&g_p));
    CHECK(flag(LINK_EW_F_STOPPED));
    /* CLOSE waits as well. */
    (void)cmd(LINK_ESC_OP_CLOSE);
    CHECK_EQ(refusal(), LINK_ESC_REF_WRITING);
    CHECK_EQ(g.gives, 0);
    /* A stop every pass changes nothing. */
    for (int i = 0; i < 100000 && esc_port_state(&g_p) == LINK_ESC_RUN; ++i) {
        pass_as(true, true);
    }
    CHECK_EQ(esc.programs, 1);
    CHECK_ALL_OFF();
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK(!flag(LINK_EW_F_FAULT));
    CHECK_EQ(esc.flash[esc.base + 0x15u], 2u);
    CHECK(!esc_port_writing(&g_p));
}

TEST_CASE(a_stopped_write_that_fails_is_a_fault)
{
    up_s_rig();
    ready_on(2u);
    read_now();
    changed_page(0x15u, 2u);
    (void)write_page(g_page);
    while (esc.erases == 0) {
        pass();
    }
    sim_script(&esc, F_DEAD, 0, 1, 0u);
    for (int i = 0; i < 400000 && esc_port_state(&g_p) == LINK_ESC_RUN; ++i) {
        pass_as(true, true);
    }
    CHECK_ALL_OFF();
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(result(), LINK_ESC_RESULT_SESSION);
}

TEST_CASE(every_session_error_is_all_outputs_off)
{
    /* No answer at all. */
    up_am_rig();
    open_target(2u);
    sim_script(&esc, F_DEAD, 0, 1, 0u);
    ST_REG(LINK_EW_W_HOLD_LO) = 500u;
    ST_REG(LINK_EW_W_OFF_MS) = 0u;
    (void)stage();
    enter_now();
    run_while(LINK_ESC_RUN, 20000u);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(result(), LINK_ESC_RESULT_SESSION);
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_SILENT);
    /* A fault stands like a stop: nothing starts before a CLEAR. */
    CHECK(flag(LINK_EW_F_STOPPED));
    const int starts = esc.starts;

    (void)cmd(LINK_ESC_OP_ENTER);
    CHECK_EQ(refusal(), LINK_ESC_REF_STOPPED);
    CHECK_EQ(esc.starts, starts);
    /* A refused ESC: the session keeps the line up; the port does not. */
    up_am_rig();
    esc.answer[7] = 9u;
    open_target(2u);
    enter_now();
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_NOT_KNOWN);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(reg(LINK_EW_WHY) & 0xFFu, EW_WHY_PROTOCOL);
    /* A read that fails. */
    up_am_rig();
    ready_on(2u);
    sim_script(&esc, F_DEAD, 0, 1, 0u);
    read_now();
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_READ);
    CHECK_ALL_OFF();
    /* A driver that does not end. */
    up_am_rig();
    ready_on(2u);
    sim_script(&esc, F_STUCK, 0, 1, 0u);
    read_now();
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_DRIVER);
    CHECK_ALL_OFF();
    /* CLEAR, and the page is usable; CLOSE gives the pin back as ever. */
    (void)cmd(LINK_ESC_OP_CLEAR);
    CHECK(!flag(LINK_EW_F_FAULT));
    CHECK(!flag(LINK_EW_F_STOPPED));
    (void)cmd(LINK_ESC_OP_CLOSE);
    CHECK_EQ(g.gives, 1);
}

TEST_CASE(torn_and_lost_leave_nothing_on)
{
    /* The session's own policy leaves the ESC in its mode for a repair. */
    up_s_rig();
    ready_on(2u);
    read_now();
    changed_page(0x15u, 2u);
    sim_script(&esc, F_NO_TAKE, 0, 999, 0u);
    (void)write_page(g_page);
    run_while(LINK_ESC_RUN, 60000u);
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_TORN);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK(flag(LINK_EW_F_DIRTY));
    /* What was read stays on the page for the message. */
    CHECK(flag(LINK_EW_F_HAVE_READ));
    CHECK(!esc_port_writing(&g_p));
    /* After CLEAR a new ENTER forgets it: a new read comes first. */
    esc.fail = F_NONE;
    (void)cmd(LINK_ESC_OP_CLEAR);
    enter_now();
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK(!flag(LINK_EW_F_DIRTY));
    CHECK(!flag(LINK_EW_F_HAVE_READ));
    /* The ESC gone in a write. */
    up_am_rig();
    ready_on(2u);
    read_now();
    changed_page(0x18u, 48u);
    sim_script(&esc, F_ERASE_DEAD, 0, 999, 0u);
    (void)write_page(g_page);
    run_while(LINK_ESC_RUN, 60000u);
    CHECK_EQ(reg(LINK_EW_SES) & 0xFFu, EW_SES_ERR_LOST);
    CHECK_ALL_OFF();
    CHECK(flag(LINK_EW_F_FAULT));
}

TEST_CASE(a_supply_gone_under_the_session)
{
    /* The panel's OFF, the PD mini silent, the output read off after it read
     * on: in RUN and in READY. */
    for (int how = 0; how < 3; ++how) {
        for (int ready = 0; ready < 2; ++ready) {
            to_state(ready != 0 ? LINK_ESC_READY : LINK_ESC_RUN);
            pass();
            if (how == 0) {
                g.v.asked_on = false;
            } else if (how == 1) {
                g.v.online = false;
            } else {
                g.v.off = true;
            }
            pass();
            CHECK_ALL_OFF();
            CHECK(flag(LINK_EW_F_FAULT));
            CHECK_EQ(result(), LINK_ESC_RESULT_SUPPLY);
        }
    }
    /* Read off before it ever read on is no loss: the PD mini's report lags. */
    up_am_rig();
    open_target(2u);
    ST_REG(LINK_EW_W_OFF_MS) = 0u;
    (void)stage();
    (void)cmd(LINK_ESC_OP_ENTER);
    run_while(LINK_ESC_POWER_OFF, 5000u);
    run_while(LINK_ESC_LEAD, 200u);
    g.v.off = true;
    run_ms(10u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
}

TEST_CASE(a_restart_finds_everything_free)
{
    to_state(LINK_ESC_READY);
    const esc_port_hw_t hw = g_p.hw;

    /* esc_init() on the way up, with the pad in its reset state. */
    CHECK(esc_port_init(&g_p, &hw));
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(esc_port_quiet_mask(&g_p), 0u);
    CHECK_EQ(esc_port_pins(&g_p), 0u);
    CHECK(!esc_port_supply_hold(&g_p));
    CHECK(!esc_port_writing(&g_p));
    CHECK_EQ(reg(LINK_EW_FLAGS) & ~(uint16_t)LINK_EW_F_RAIL, 0u);
}

TEST_CASE(the_bank_stays_held_in_every_state)
{
    /* A session on the slot in each state: the gate's mask is not 0. */
    for (size_t i = 0u; i < sizeof(k_session) / sizeof(k_session[0]); ++i) {
        to_state(k_session[i]);
        CHECK_EQ(esc_port_quiet_mask(&g_p), ESC_PORT_QUIET_SLOT);
        CHECK_EQ(esc_port_may_bind(&g_p, 1u << 2), 0u);
        CHECK(esc_port_supply_hold(&g_p));
    }
}

int main(void)
{
    RUN(a_stop_in_each_state_parks_and_switches_off_at_once);
    RUN(a_stop_by_command_is_the_same);
    RUN(a_stop_or_abort_in_stopping_frees_the_pin_untouched);
    RUN(a_link_lost_parks_at_once_also_in_a_write);
    RUN(an_abort_cuts_a_write_a_stop_lets_it_end);
    RUN(a_stopped_write_that_fails_is_a_fault);
    RUN(every_session_error_is_all_outputs_off);
    RUN(torn_and_lost_leave_nothing_on);
    RUN(a_supply_gone_under_the_session);
    RUN(a_restart_finds_everything_free);
    RUN(the_bank_stays_held_in_every_state);
    return test_summary("esc_port_stop");
}
