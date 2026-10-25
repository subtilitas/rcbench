/*
 * The HITEC page's hold of the SUPPLY page (shared/outputs/hitec_port.c,
 * protocol 4.29): held in every state from the OPEN that takes the port to
 * the CLOSE that gives it back, and released by every way the port ends.
 *
 * The SUPPLY page is the real one (shared/outputs/supply_page.c), told of
 * the hold as the IOmcu tells it (firmware/iomcu/src/iomcu.h,
 * ports_hold_supply()): after each write of the HITEC page and after each
 * pass.  The panel's writes are its OUTPUT, SET_MV and SET_MA frame.
 *
 * Under test: the minor, 4.29; the hold through STOPPING, OPEN, SEND, LISTEN, LEVEL, POWER,
 * a stop, an unsafe bench, ABORT and the 10s abort; a panel ON or set point
 * refused while held, its OFF taken, both taken again after CLOSE; the hold
 * ended by CLOSE, by an ABORT or a stop in STOPPING, by an OPEN without a
 * line and by a restart; an OPEN refused holds nothing; the hold across the
 * wrap of the millisecond clock.  The rig is hitec_port_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_port_rig.h"

#include "link_msg.h"
#include "outputs.h"
#include "supply_page.h"

static outputs_t     g_out;
static supply_page_t g_sp;

/* --------------------------------------------------------------- the rig */

/* The SUPPLY page told what the HITEC page holds, as the IOmcu does after
 * each write of HITEC and after each pass. */
static void sync(void)
{
    supply_page_hold(&g_sp, hitec_port_supply_hold(&g_port));
}

/* Whether the SUPPLY page says a port holds it (FLAGS bit 11). */
static bool sp_held(void)
{
    uint16_t f = 0u;

    supply_page_read(&g_sp, LINK_SP_FLAGS, 1u, &f);
    return (f & LINK_SP_PORT) != 0u;
}

static uint16_t sp_reg(unsigned i)
{
    uint16_t v = 0xFFFFu;

    supply_page_read(&g_sp, (uint8_t)i, 1u, &v);
    return v;
}

/* The panel's frame of OUTPUT, SET_MV and SET_MA, with a live heartbeat. */
static uint8_t panel(uint16_t on, uint16_t mv, uint16_t ma)
{
    const uint16_t r[3] = { on, mv, ma };

    return supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, r, &g_out, true);
}

/* A HITEC command, and the SUPPLY page told at once. */
static uint8_t hcmd(link_hitec_op_t o, unsigned port, uint16_t key)
{
    const uint8_t nack = cmd(o, port, key);

    sync();
    return nack;
}

static void hpass(bool safe, bool stop)
{
    tick_as(safe, stop);
    sync();
}

static void hrun(unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) {
        hpass(true, false);
    }
}

/* A fresh port at @p t0, the SUPPLY page wired and on at 5000mV and
 * 1000mA from the panel. */
static void bench_at(uint32_t t0)
{
    static const uint16_t wiring[4] = { 1u, 8u, 9u, 1u };

    fresh_at(t0, SUPPLY_ON);
    outputs_init(&g_out, 0u);
    outputs_reserve_pins(&g_out, (uint64_t)1u << 3);
    supply_page_init(&g_sp);
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_ENABLE, 4u, wiring, &g_out,
                               true),
             0u);
    CHECK_EQ(panel(1u, MV, MA), 0u);
    sync();
    CHECK(!sp_held());
}

static void bench(void)
{
    bench_at(0u);
}

/* While held: the panel's ON at another voltage and another current limit
 * are refused, the set points in force are taken, nothing moves. */
static void check_held(void)
{
    CHECK(hitec_port_supply_hold(&g_port));
    CHECK(sp_held());
    CHECK_EQ(panel(1u, MV + 100u, MA), LINK_NACK_BAD_VALUE);
    CHECK_EQ(panel(1u, MV - 100u, MA), LINK_NACK_BAD_VALUE);
    CHECK_EQ(panel(1u, MV, MA + 100u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sp_reg(LINK_SP_SET_MV), MV);
    CHECK_EQ(sp_reg(LINK_SP_SET_MA), MA);
}

/* Not held: the panel moves a set point and back. */
static void check_free(void)
{
    CHECK(!hitec_port_supply_hold(&g_port));
    CHECK(!sp_held());
    CHECK_EQ(panel(1u, MV + 100u, MA), 0u);
    CHECK_EQ(sp_reg(LINK_SP_SET_MV), MV + 100u);
    CHECK_EQ(panel(1u, MV, MA), 0u);
}

static void open_held(uint8_t family, uint8_t drive)
{
    line(family, drive);
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    for (unsigned i = 0; i < 1000u && state() != LINK_HITEC_OPEN; ++i) {
        hpass(true, false);
    }
    CHECK_EQ(state(), LINK_HITEC_OPEN);
}

/* ===================================================================== cases */

/* Protocol 4.29 adds no register: the hold is said in the SUPPLY FLAGS bit
 * KST has set since 4.13. */
TEST_CASE(the_hold_is_4_29s_and_adds_no_register)
{
    CHECK_EQ(LINK_MINOR_HITEC_HOLD, 29u);
    CHECK(LINK_MINOR_HITEC_HOLD > LINK_MINOR_PROG_DSHOT);
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_HITEC_HOLD);
    CHECK_EQ(LINK_SP_PORT, 0x800u);
    CHECK_EQ(LINK_HT_COUNT, 32);
    CHECK_EQ(LINK_HT_W_COUNT, 24);
    CHECK(!hitec_port_supply_hold(NULL));
}

/* From the frame of the OPEN, through every transaction, to the frame of
 * the CLOSE. */
TEST_CASE(the_supply_is_held_from_open_to_close)
{
    bench();
    check_free();
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    /* STOPPING, in the frame of the OPEN. */
    CHECK_EQ(state(), LINK_HITEC_STOPPING);
    check_held();
    hrun(HITEC_PORT_STOP_MS + 1u);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    check_held();
    /* SEND, while it runs and after it. */
    stage_classic_read(0x01u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    check_held();
    settle();
    sync();
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    check_held();
    /* LISTEN. */
    stage_listen(20000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    check_held();
    settle();
    sync();
    check_held();
    /* LEVEL. */
    st32(LINK_HT_W_LEVEL_LO, 1000u);
    SST(LINK_HT_W_ANSWER) = 0u;
    SST(LINK_HT_W_DECODE) = 0u;
    CHECK_EQ(hcmd(LINK_HITEC_OP_LEVEL, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    check_held();
    settle();
    sync();
    check_held();
    /* CLOSE: released in its own frame, and the panel commands again. */
    CHECK_EQ(hcmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    check_free();
    hrun(100u);
    check_free();
}

/* POWER switches the supply itself while the panel's writes are refused,
 * and the hold outlasts it: the set points of the POWER stay. */
TEST_CASE(a_power_switches_the_supply_and_the_hold_outlasts_it)
{
    bench();
    open_held(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    stage_listen(100000u, 1u, false, 0u, LINK_HITEC_DEC_LISTEN, 0x5Au);
    SST(LINK_HT_W_SUPPLY_MV) = 5500u;
    SST(LINK_HT_W_SUPPLY_MA) = 800u;
    SST(LINK_HT_W_OFF_MS) = 500u;
    CHECK_EQ(hcmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_POWER_OFF);
    check_held();
    /* The port's own switching is not held by its own hold. */
    CHECK(supply_page_port_set(&g_sp, false, 0u, 0u, true));
    CHECK(sp_held());
    CHECK_EQ(panel(1u, MV, MA), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sp_reg(LINK_SP_OUTPUT), 0u);
    for (unsigned i = 0; i < 5000u && state() != LINK_HITEC_BUSY; ++i) {
        hpass(true, false);
    }
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK(supply_page_port_set(&g_sp, true, 5500u, 800u, true));
    CHECK(sp_held());
    CHECK_EQ(panel(1u, MV, MA), LINK_NACK_BAD_VALUE);
    settle();
    sync();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    /* Between two POWERs the panel moves no set point. */
    CHECK(sp_held());
    CHECK_EQ(panel(1u, MV, MA), LINK_NACK_BAD_VALUE);
    CHECK_EQ(panel(1u, 5500u, 900u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(panel(1u, 5500u, 800u), 0u);
    CHECK_EQ(sp_reg(LINK_SP_SET_MV), 5500u);
    CHECK_EQ(sp_reg(LINK_SP_SET_MA), 800u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK(!sp_held());
    /* CLOSE leaves the supply as it is: on at the POWER's set points. */
    CHECK_EQ(sp_reg(LINK_SP_OUTPUT), 1u);
    CHECK_EQ(sp_reg(LINK_SP_SET_MV), 5500u);
    CHECK_EQ(panel(1u, MV, MA), 0u);
}

/* The panel's OFF is taken while held, and its ON is not until CLOSE. */
TEST_CASE(an_off_is_taken_while_held_and_an_on_after_close)
{
    bench();
    open_held(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    check_held();
    CHECK_EQ(panel(0u, 0u, 0u), 0u);
    CHECK_EQ(sp_reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(sp_reg(LINK_SP_SET_MV), MV);
    CHECK_EQ(panel(1u, MV, MA), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sp_reg(LINK_SP_OUTPUT), 0u);
    hrun(1000u);
    CHECK_EQ(panel(1u, MV, MA), LINK_NACK_BAD_VALUE);
    CHECK_EQ(hcmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK(!sp_held());
    CHECK_EQ(panel(1u, MV, MA), 0u);
    CHECK_EQ(sp_reg(LINK_SP_OUTPUT), 1u);
}

/* ABORT keeps the port and the hold, but in STOPPING it frees both. */
TEST_CASE(an_abort_keeps_the_hold_but_one_in_stopping_ends_it)
{
    bench();
    open_held(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    /* In OPEN. */
    CHECK_EQ(hcmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    check_held();
    /* In BUSY. */
    stage_listen(4000000u, 8u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    hrun(5u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    hpass(true, false);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    check_held();
    /* In a POWER's off time. */
    stage_listen(100000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    SST(LINK_HT_W_SUPPLY_MV) = MV;
    SST(LINK_HT_W_SUPPLY_MA) = MA;
    SST(LINK_HT_W_OFF_MS) = 2000u;
    CHECK_EQ(hcmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    hrun(100u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK(sp_held());
    CHECK_EQ(hcmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK(!sp_held());
    /* In STOPPING: the port is free again, and so is the supply. */
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK(sp_held());
    hrun(HITEC_PORT_STOP_MS - 1u);
    CHECK_EQ(state(), LINK_HITEC_STOPPING);
    CHECK_EQ(hcmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    check_free();
}

/* A stop keeps the port and the hold, an unsafe bench too, until CLOSE;
 * a stop in STOPPING frees both. */
TEST_CASE(a_stop_keeps_the_hold_until_close)
{
    bench();
    open_held(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    /* On an idle port, and in a transaction. */
    hpass(true, true);
    CHECK(flag(LINK_HT_F_STOPPED));
    check_held();
    CHECK_EQ(hcmd(LINK_HITEC_OP_CLEAR, CH, 0), 0);
    stage_listen(4000000u, 8u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    hrun(5u);
    hpass(true, true);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    check_held();
    /* The heartbeat lost or the link silent: every pass a stop.  The
     * SUPPLY page itself switches the output off on a lost heartbeat
     * (supply_page_step()); the hold is the port's. */
    for (unsigned i = 0; i < 100u; ++i) {
        hpass(false, false);
    }
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK(sp_held());
    /* CLOSE is taken on an unsafe bench and ends the hold. */
    CHECK_EQ(hcmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK(!sp_held());
    hpass(true, false);
    /* A stop in STOPPING. */
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    hrun(10u);
    CHECK(sp_held());
    hpass(true, true);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    check_free();
}

/* An OPEN whose driver gets no line ends free, and the hold with it; an
 * OPEN refused never holds. */
TEST_CASE(an_open_that_fails_or_is_refused_holds_nothing_after_it)
{
    bench();
    g_hw.take_ok = false;
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK(sp_held());
    hrun(HITEC_PORT_STOP_MS + 1u);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(result(), LINK_HITEC_RESULT_NO_LINE);
    check_free();
    g_hw.take_ok = true;
    /* Refused: no pin, the KST page holds the port, the bank ARMED. */
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, OTHER, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_PIN);
    CHECK(!sp_held());
    hitec_port_other_mask(&g_port, (uint8_t)(1u << CH));
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_HELD);
    CHECK(!sp_held());
    hitec_port_other_mask(&g_port, 0u);
    hitec_port_bench_armed(&g_port, true);
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_ARMED);
    check_free();
    hitec_port_bench_armed(&g_port, false);
    open_held(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    check_held();
    /* A second OPEN on the held port is refused and keeps the hold. */
    CHECK_EQ(hcmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_BUSY);
    check_held();
}

/* A restart of the IOmcu: both pages start again, free. */
TEST_CASE(a_restart_starts_free_and_unheld)
{
    bench();
    open_held(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    check_held();
    bench();
    CHECK_EQ(state(), LINK_HITEC_FREE);
    check_free();
    hrun(100u);
    check_free();
}

/* The 10s abort of a transaction that does not end keeps the port, and the
 * hold; the times hold across the wrap of the millisecond clock. */
static void hold_across_at(uint32_t t0)
{
    bench_at(t0);
    open_held(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    g_drv.hang = true;
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(hcmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    hrun(HITEC_PORT_BUSY_MS + 2u);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    check_held();
    CHECK_EQ(hcmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    check_free();
}

TEST_CASE(the_hold_lasts_across_the_10_s_abort_and_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(hold_across_at, 5000u);
}

int main(void)
{
    RUN(the_hold_is_4_29s_and_adds_no_register);
    RUN(the_supply_is_held_from_open_to_close);
    RUN(a_power_switches_the_supply_and_the_hold_outlasts_it);
    RUN(an_off_is_taken_while_held_and_an_on_after_close);
    RUN(an_abort_keeps_the_hold_but_one_in_stopping_ends_it);
    RUN(a_stop_keeps_the_hold_until_close);
    RUN(an_open_that_fails_or_is_refused_holds_nothing_after_it);
    RUN(a_restart_starts_free_and_unheld);
    RUN(the_hold_lasts_across_the_10_s_abort_and_the_tick_wrap);
    return test_summary("hitec_port_hold");
}
