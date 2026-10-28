/*
 * An error while the Hitec programmer holds a port (error_stop.h): the
 * port's supply off outside a write, on the whole loopback of
 * hitec_prog_rig.h with the panel's cut as panel_supply.c runs it at the
 * top of each control pass (port_cut_follow()) and the steps it blocks as
 * panel_hitec.c does:
 *
 *  - the model says a write runs (hitec_prog_gate_writing()) from before
 *    its first step is offered to the end of its read-back, and not while
 *    its record goes to the card: no write frame reaches the servo while
 *    it says none runs, D series and classic;
 *  - an error during a D series write and during a classic write: the
 *    write ends verified, then the supply goes off, within
 *    ERROR_PORT_CUT_MS;
 *  - an error with no write: off in the same pass; every step after it is
 *    answered BUSY and sends nothing; LEAVE ends in CLOSE and the cut ends
 *    with the port free;
 *  - an error while the write's record waits for the card: off at once,
 *    and the write that follows sends nothing;
 *  - a classic write the servo does not answer is a session fault
 *    (hitec_prog_ses_fault()) and an error: off at once; an ABORT is none;
 *  - which session results are faults;
 *  - the longest write the code allows: a classic one of HITEC_PLAN_MAX
 *    steps and its read-back of 36 cells, a D series one of every register
 *    of the full read with its save, reboot and read-back, at the pace of a
 *    step on this loopback at the panel's 5ms pass and a 16ms frame (32ms
 *    a step, a one-register D series write 2464ms), under
 *    ERROR_PORT_CUT_MS / 2.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

#include "error_stop.h"

/* --------------------------------------------------------- the panel's cut */

static port_cut_t    g_cut;
static error_watch_t g_watch;
static unsigned      g_offs;          /* switch-offs the cut made        */
static uint32_t      g_cut_ms;        /* the first one's time            */
static uint32_t      g_write_end_ms;  /* the last pass that saw a write  */

/* port_cut_follow() as panel_supply.c runs it, the supply as the SUPPLY
 * page takes the panel's OFF. */
static void cut_pump(void)
{
    const bool held = hitec_prog_port_taken(&g_f.rd);
    const bool writing = hitec_prog_gate_writing(&g_gate);
    const error_stop_t e =
        error_stop_take(&g_watch, hitec_prog_gate_faults(&g_gate), false,
                        g_hw.v.asked_on, held);

    if (writing) {
        g_write_end_ms = g_ms;
    }
    if (e.supply_off) {
        (void)hw_supply_set(NULL, false, 0u, 0u);
    }
    if (e.port_cut) {
        port_cut_ask(&g_cut, g_ms);
    }
    if (port_cut_step(&g_cut, held, writing, g_hw.v.asked_on, g_ms)) {
        (void)hw_supply_set(NULL, false, 0u, 0u);
        if (g_offs++ == 0u) {
            g_cut_ms = g_ms;
        }
    }
    g_cut_blocks = port_cut_blocks(&g_cut);
}

/* A write frame that reached the servo while the model said no write ran,
 * before and after the millisecond it went in. */
static unsigned g_unseen;

static unsigned servo_writes(void)
{
    return g_drv.d_writes + g_drv.d_saves + g_drv.d_reboots
           + g_drv.c_writes;
}

static void cms(void)
{
    const bool before = hitec_prog_gate_writing(&g_gate);
    const unsigned n = servo_writes();

    pms();
    if (servo_writes() != n && !before
        && !hitec_prog_gate_writing(&g_gate)) {
        g_unseen++;
    }
}

static void crun(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        cms();
    }
}

/* The bench with the cut in place. */
static void cut_rig(void)
{
    rig();
    memset(&g_cut, 0, sizeof(g_cut));
    error_watch_init(&g_watch, 0u);
    g_offs = 0;
    g_cut_ms = 0;
    g_write_end_ms = 0;
    g_unseen = 0;
    g_pump = cut_pump;
}

/* A D485HW read, the switch on. */
static void d_ready(void)
{
    cut_rig();
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
}

/* An HS-5645MG read, the switch on. */
static void c_ready(void)
{
    cut_rig();
    servo_hs5645();
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
}

/* Until the model is held with nothing running; 60s is the bound. */
static void held(void)
{
    for (unsigned i = 0; i < 60000u && pstate() != HITEC_PROG_HELD; ++i) {
        cms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

/* Until the first write frame has reached the servo; 60s is the bound. */
static void first_write(void)
{
    const unsigned n = servo_writes();

    for (unsigned i = 0; i < 60000u && servo_writes() == n; ++i) {
        cms();
    }
    CHECK(servo_writes() > n);
}

/* ----------------------------------------------------------------- cases */

TEST_CASE(a_write_is_seen_from_its_first_step_to_its_read_back)
{
    d_ready();
    card_in();
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    /* The record goes to the card: nothing is sent, no write runs. */
    crun(100u);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK(!hitec_prog_gate_writing(&g_gate));
    g_card_slow = false;
    first_write();
    CHECK(hitec_prog_gate_writing(&g_gate));
    held();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK(!hitec_prog_gate_writing(&g_gate));
    CHECK_EQ(g_unseen, 0u);
    CHECK_EQ(g_offs, 0u);
    /* Classic: the cell, its balance, the read-back. */
    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    first_write();
    held();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    CHECK(g_drv.c_writes >= 2u);
    CHECK_EQ(g_unseen, 0u);
}

/* An error with the first write frame out; the write ends verified, then
 * the supply goes off. */
static void error_in_the_write(void)
{
    uint32_t asked;

    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    first_write();
    asked = g_ms;
    port_cut_ask(&g_cut, asked);
    held();
    CHECK_EQ(msg(), HITEC_PROG_MSG_WRITTEN);
    crun(10u);
    CHECK_EQ(g_offs, 1u);
    CHECK(!g_hw.v.asked_on);
    /* Off in the pass after the one that last saw the write. */
    CHECK(g_cut_ms > g_write_end_ms);
    CHECK(g_cut_ms - g_write_end_ms <= HITEC_LINK_POLL_MS);
    CHECK(g_cut_ms - asked < ERROR_PORT_CUT_MS);
    CHECK_EQ(g_unseen, 0u);
}

TEST_CASE(an_error_during_a_d_write_cuts_after_its_read_back)
{
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    error_in_the_write();
    CHECK_EQ(g_drv.dreg[0x54], 10u);
    CHECK_EQ(g_drv.d_saves, 1u);
    CHECK_EQ(g_drv.d_reboots, 1u);
}

TEST_CASE(an_error_during_a_classic_write_cuts_after_its_read_back)
{
    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    error_in_the_write();
    CHECK(!hitec_prog_sum_bad(model()));
}

TEST_CASE(an_error_with_no_write_cuts_at_once_and_sends_no_step)
{
    unsigned sets;

    d_ready();
    CHECK(g_hw.v.asked_on);
    port_cut_ask(&g_cut, g_ms);
    crun(HITEC_LINK_POLL_MS);
    CHECK_EQ(g_offs, 1u);
    CHECK(!g_hw.v.asked_on);
    CHECK(g_cut_blocks);
    /* READ ALL: its first step is answered BUSY; nothing goes out. */
    sets = g_hw.sets;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    held();
    CHECK_EQ(msg(), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(g_hw.sets, sets);
    /* LEAVE: its step is refused, and it ends in CLOSE all the same. */
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    crun(2u * HITEC_LINK_POLL_MS);
    /* The port is free with the output off: the cut ends. */
    CHECK(!g_cut.armed);
    CHECK(!g_cut_blocks);
    CHECK_EQ(g_offs, 1u);
}

TEST_CASE(an_error_while_the_record_waits_for_the_card_writes_nothing)
{
    d_ready();
    card_in();
    g_card_slow = true;
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    crun(50u);
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    port_cut_ask(&g_cut, g_ms);
    crun(HITEC_LINK_POLL_MS);
    CHECK_EQ(g_offs, 1u);
    g_card_slow = false;
    held();
    CHECK_EQ(g_drv.d_writes + g_drv.d_saves + g_drv.d_reboots, 0u);
    CHECK(g_drv.dreg[0x54] != 10u);
    /* The record stands: READ ALL before the next write. */
    CHECK(hitec_prog_record(model()));
}

TEST_CASE(a_classic_write_the_servo_does_not_answer_is_an_error)
{
    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    g_drv.c_mute = true;
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    held();
    CHECK_EQ(hitec_prog_gate_faults(&g_gate), 1u);
    crun(2u * HITEC_LINK_POLL_MS);
    CHECK_EQ(g_offs, 1u);
    CHECK(!g_hw.v.asked_on);
    /* An ABORT of a read is no fault: no second cut is asked. */
    g_drv.c_mute = false;
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    crun(2u * HITEC_LINK_POLL_MS);
    CHECK(!g_cut.armed);
    connected();
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    crun(40u);
    hitec_prog_abort(model());
    held();
    CHECK_EQ(hitec_prog_gate_faults(&g_gate), 1u);
    crun(2u * HITEC_LINK_POLL_MS);
    CHECK(!g_cut.armed);
    CHECK(g_hw.v.asked_on);
}

TEST_CASE(which_session_results_are_faults)
{
    static const unsigned k_fault[] = {
        HITEC_SES_ERR_NO_SERVO, HITEC_SES_ERR_ENTRY, HITEC_SES_ERR_READ,
        HITEC_SES_ERR_WRITE, HITEC_SES_ERR_VERIFY, HITEC_SES_ERR_STEP,
    };
    unsigned faults = 0;

    for (unsigned r = 0; r < 64u; ++r) {
        bool want = false;

        for (unsigned i = 0; i < sizeof(k_fault) / sizeof(k_fault[0]); ++i) {
            want = want || k_fault[i] == r;
        }
        CHECK_EQ(hitec_prog_ses_fault(r), want);
        faults += want ? 1u : 0u;
    }
    CHECK_EQ(faults, 6u);
    CHECK(!hitec_prog_ses_fault(HITEC_SES_ERR_ABORTED));
    CHECK(!hitec_prog_ses_fault(HITEC_SES_ERR_CUT));
    CHECK(!hitec_prog_ses_fault(HITEC_SES_ERR_LINK));
    CHECK(!hitec_prog_gate_writing(NULL));
    CHECK_EQ(hitec_prog_gate_faults(NULL), 0u);
}

/* The time from the write's first step to its end, and the steps it took:
 * writes and reads that went out. */
static uint32_t timed_write(unsigned *steps)
{
    const unsigned n0 = servo_writes();
    const uint32_t t0 = g_ms;

    first_write();
    held();
    *steps = servo_writes() - n0;
    return g_ms - t0;
}

TEST_CASE(the_longest_writes_end_within_half_the_bound)
{
    unsigned steps;
    uint32_t c_ms, d_ms, per_step;
    size_t d_regs = 0;

    (void)hitec_d_read_list(&d_regs);

    /* Classic: the read-back is 36 cells; the pace of a step on this
     * loopback, from a write of one cell, its balance and that read-back. */
    c_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    c_ms = timed_write(&steps);
    CHECK_EQ(steps, 2u);
    per_step = (c_ms + 2u + HITEC_C_BLOCK_LEN - 1u)
               / (2u + HITEC_C_BLOCK_LEN);
    /* HITEC_PLAN_MAX writes and the read-back at that pace. */
    CHECK((HITEC_PLAN_MAX + HITEC_C_BLOCK_LEN) * per_step
          < ERROR_PORT_CUT_MS / 2u);
    /* D series: one write, the save, the reboot and the read-back of the
     * full set; every further write adds a step and its 20ms. */
    d_ready();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write(model()), HITEC_PROG_MSG_NONE);
    d_ms = timed_write(&steps);
    CHECK_EQ(steps, 3u);
    CHECK(d_ms + ((unsigned)d_regs - 1u) * (per_step + HITEC_D_WRITE_GAP_MS)
          < ERROR_PORT_CUT_MS / 2u);
}

int main(void)
{
    RUN(a_write_is_seen_from_its_first_step_to_its_read_back);
    RUN(an_error_during_a_d_write_cuts_after_its_read_back);
    RUN(an_error_during_a_classic_write_cuts_after_its_read_back);
    RUN(an_error_with_no_write_cuts_at_once_and_sends_no_step);
    RUN(an_error_while_the_record_waits_for_the_card_writes_nothing);
    RUN(a_classic_write_the_servo_does_not_answer_is_an_error);
    RUN(which_session_results_are_faults);
    RUN(the_longest_writes_end_within_half_the_bound);
    return test_summary("hitec_prog_cut");
}
