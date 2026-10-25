/*
 * The HITEC page's stops and supply (shared/outputs/hitec_port.c): a stop
 * in each state and what it blocks until CLEAR, an unsafe bench, CLOSE, the
 * POWER cycle with its 4800 to 6000mV bounds, a supply that does not read
 * off or refuses the ON, the 10s abort, and each timer across the wrap of
 * the millisecond clock.  The rig is hitec_port_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_port_rig.h"

/* ===================================================================== cases */

/* A stop in a transaction: cut, the port held with the pin released, and
 * no transaction until CLEAR. */
TEST_CASE(a_stop_aborts_the_transaction_and_keeps_the_port_held)
{
    open_classic();
    stage_listen(4000000u, 8u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    run_ms(5u);
    tick_as(true, true);
    CHECK_EQ(g_drv.aborts, 1);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK_EQ(g_hw.gives, 0);
    CHECK(g_drv.open);
    CHECK(!g_drv.busy);
    CHECK_EQ(hitec_port_pin_mask(&g_port), 1u << CH);
    CHECK_EQ(hitec_port_quiet_mask(&g_port), 1u << CH);
    /* Every transaction is refused until CLEAR. */
    stage_classic_read(0x01u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_STOPPED);
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_STOPPED);
    st32(LINK_HT_W_LEVEL_LO, 1000u);
    SST(LINK_HT_W_ANSWER) = 0u;
    CHECK_EQ(cmd(LINK_HITEC_OP_LEVEL, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_STOPPED);
    SST(LINK_HT_W_SUPPLY_MV) = MV;
    SST(LINK_HT_W_SUPPLY_MA) = MA;
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_STOPPED);
    run_ms(1000u);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK_EQ(g_drv.starts, 1);
    CHECK_EQ(g_hw.sets, 0);
    /* CLEAR, and the port runs again. */
    CHECK_EQ(cmd(LINK_HITEC_OP_CLEAR, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK(!flag(LINK_HT_F_STOPPED));
    stage_classic_read(0x01u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    CHECK_EQ(rx_code(), HITEC_RX_OK);
}

TEST_CASE(a_stop_on_an_idle_port_blocks_transactions_until_clear)
{
    open_classic();
    tick_as(true, true);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(g_drv.aborts, 0);
    stage_classic_read(0x01u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_STOPPED);
    /* CLOSE and OPEN clear it as well. */
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK(!flag(LINK_HT_F_STOPPED));
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    stage_classic_read(0x01u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
}

/* While the heartbeat is not trusted or the link is silent: every pass is
 * a stop, and only ABORT and CLOSE are taken. */
TEST_CASE(an_unsafe_bench_takes_only_abort_and_close)
{
    open_classic();
    tick_as(false, false);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK_EQ(cmd(LINK_HITEC_OP_CLEAR, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_UNSAFE);
    tick_as(false, false);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK_EQ(cmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(hitec_port_quiet_mask(&g_port), 0);
    CHECK_EQ(hitec_port_pin_mask(&g_port), 0);
    tick_as(false, false);
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_UNSAFE);
    /* Safe again: a free port opens. */
    tick();
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
}

TEST_CASE(a_stop_in_the_stop_time_frees_the_port)
{
    fresh();
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    run_ms(HITEC_PORT_STOP_MS - 1u);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(!flag(LINK_HT_F_STOPPED));
    run_ms(100u);
    CHECK_EQ(g_hw.takes, 0);
}

TEST_CASE(close_cuts_a_running_transaction_and_leaves_the_supply)
{
    open_classic();
    g_drv.listen_n = 4u;
    stage_listen(4000000u, 8u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    run_ms(2u);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(LINK_HT_OP_OF(reg(LINK_HT_OP)), LINK_HITEC_OP_CLOSE);
    CHECK_EQ(g_drv.aborts, 1);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_hw.sets, 0);
    CHECK(g_hw.v.asked_on);
    /* The bytes so far stay on the page. */
    CHECK_EQ(cap_n(), 2);
    CHECK((reg(LINK_HT_CAP) & LINK_HT_CAP_ABORTED) != 0u);
    /* A CLOSE of a free port is refused. */
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NOT_OPEN);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLEAR, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NOT_OPEN);
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NOT_OPEN);
}

/* POWER: the supply off, read off, held off, on, and the listen from the
 * ON, with the SUPPLY page held throughout and after it. */
static void stage_power(uint16_t mv, uint16_t ma, uint16_t off_ms)
{
    stage_listen(100000u, 1u, false, 0u, LINK_HITEC_DEC_LISTEN, 0x5Au);
    SST(LINK_HT_W_SUPPLY_MV) = mv;
    SST(LINK_HT_W_SUPPLY_MA) = ma;
    SST(LINK_HT_W_OFF_MS) = off_ms;
}

static void power_cycle_at(uint32_t t0)
{
    fresh_at(t0, SUPPLY_ON);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    stage_power(5500u, 800u, 1000u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_POWER_OFF);
    CHECK(hitec_port_supply_hold(&g_port));
    CHECK_EQ(g_hw.offs, 1);
    CHECK(!g_hw.v.asked_on);
    /* Read off LAG_MS later; on 1000ms after that read. */
    for (unsigned i = 0; i < 5000u && g_hw.ons == 0u; ++i) {
        tick();
    }
    CHECK_EQ(g_hw.ons, 1);
    CHECK_EQ((uint32_t)(g_hw.on_ms - g_hw.off_read_ms), 1000u);
    CHECK_EQ(g_hw.v.set_mv, 5500);
    CHECK_EQ(g_hw.v.set_ma, 800);
    /* The listen started in the pass of the ON. */
    CHECK_EQ(g_drv.starts, 1);
    CHECK_EQ(g_drv.t_start, g_hw.listen_at_on_us);
    CHECK_EQ(g_drv.x.kind, HITEC_XFER_LISTEN);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK(hitec_port_supply_hold(&g_port));
    settle();
    /* The port is OPEN again and still holds the supply (4.29). */
    CHECK(hitec_port_supply_hold(&g_port));
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(LINK_HT_OP_OF(reg(LINK_HT_OP)), LINK_HITEC_OP_POWER);
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    CHECK_EQ(cap_byte(0), 0x5A);
    CHECK_EQ(count(), 1);
    CHECK(g_hw.v.asked_on);
}

TEST_CASE(a_power_cycle_switches_off_waits_and_listens_from_the_on)
{
    power_cycle_at(0u);
}

TEST_CASE(the_times_of_a_power_cycle_hold_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(power_cycle_at, 600u);
}

TEST_CASE(power_takes_4800_to_6000_mv_only)
{
    static const uint16_t bad[][3] = {
        { 4799u, MA, 0u }, { 6001u, MA, 0u }, { 0u, MA, 0u },
        { MV, PDMINI_I_MIN_MA - 1u, 0u }, { MV, PDMINI_I_MAX_MA + 1u, 0u },
        { MV, MA, LINK_HITEC_OFF_MS_MAX + 1u },
    };
    static const uint16_t good[][3] = {
        { 4800u, MA, 0u }, { 6000u, PDMINI_I_MAX_MA, LINK_HITEC_OFF_MS_MAX },
        { MV, PDMINI_I_MIN_MA, 0u },
    };

    open_classic();
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        stage_power(bad[i][0], bad[i][1], bad[i][2]);
        CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
        CHECK_EQ(refusal(), LINK_HITEC_REF_SUPPLY_RANGE);
        CHECK_EQ(state(), LINK_HITEC_OPEN);
    }
    /* Under the input with the module's cap: 6000mV needs 6700mV in. */
    g_hw.v.vin_mv = 6699u;
    stage_power(6000u, MA, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_SUPPLY_RANGE);
    CHECK_EQ(g_hw.sets, 0);
    g_hw.v.vin_mv = 6700u;
    for (unsigned i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
        stage_power(good[i][0], good[i][1], good[i][2]);
        CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
        CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
        CHECK_EQ(state(), LINK_HITEC_POWER_OFF);
        CHECK_EQ(cmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
        CHECK_EQ(state(), LINK_HITEC_OPEN);
    }
}

TEST_CASE(power_without_a_pd_mini_is_refused)
{
    fresh_at(0u, SUPPLY_NONE);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    stage_power(MV, MA, 100u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_SUPPLY);
    CHECK(!flag(LINK_HT_F_RAIL));
    fresh_at(0u, SUPPLY_OFFLINE);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    stage_power(MV, MA, 100u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_SUPPLY);
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
}

TEST_CASE(power_answering_the_first_byte_needs_the_key)
{
    open_classic();
    stage_power(MV, MA, 100u);
    SST(LINK_HT_W_RX) |= 0x0100u;
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_KEY);
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK(flag(LINK_HT_F_RAIL));
}

static void supply_not_off_at(uint32_t t0)
{
    fresh_at(t0, SUPPLY_ON);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    g_hw.follow = false;
    stage_power(MV, MA, 100u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    run_ms(HITEC_PORT_SUPPLY_MS);
    CHECK_EQ(state(), LINK_HITEC_POWER_OFF);
    tick();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_SUPPLY);
    CHECK_EQ(g_hw.ons, 0);
    CHECK_EQ(g_drv.starts, 0);
    CHECK(hitec_port_supply_hold(&g_port));
}

TEST_CASE(a_supply_that_does_not_read_off_ends_the_power_in_3_s)
{
    at_tick_0_and_before_the_wrap(supply_not_off_at, 1000u);
}

TEST_CASE(a_supply_that_refuses_the_on_ends_the_power)
{
    open_classic();
    g_hw.set_ok = false;
    stage_power(MV, MA, 100u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    run_ms(LAG_MS + 102u);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_SUPPLY);
    CHECK_EQ(g_drv.starts, 0);
    CHECK(!g_hw.v.asked_on);
}

TEST_CASE(a_listen_the_driver_refuses_after_the_on_leaves_the_supply_on)
{
    open_classic();
    g_drv.start_ok = false;
    stage_power(MV, MA, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    run_ms(LAG_MS + 2u);
    CHECK_EQ(g_hw.ons, 1);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_START);
    CHECK(g_hw.v.asked_on);
}

TEST_CASE(a_stop_in_the_off_time_leaves_the_supply_off)
{
    open_classic();
    stage_power(MV, MA, 2000u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    run_ms(500u);
    tick_as(false, false);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK(hitec_port_supply_hold(&g_port));
    run_ms(5000u);
    CHECK_EQ(g_hw.ons, 0);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_drv.starts, 0);
}

TEST_CASE(an_abort_in_the_off_time_leaves_the_supply_off)
{
    open_classic();
    stage_power(MV, MA, 2000u);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    run_ms(500u);
    CHECK_EQ(cmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(!flag(LINK_HT_F_STOPPED));
    run_ms(5000u);
    CHECK_EQ(g_hw.ons, 0);
    /* And a CLOSE in it gives the port back with the supply off. */
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    run_ms(100u);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    run_ms(5000u);
    CHECK_EQ(g_hw.ons, 0);
}

TEST_CASE(a_stop_in_the_listen_of_a_power_cuts_it)
{
    open_classic();
    g_drv.boot_byte = 0x00u;
    stage_power(MV, MA, 10u);
    /* A listen of 4s that hears its byte at 30ms. */
    st32(LINK_HT_W_WINDOW_LO, 4000000u);
    SST(LINK_HT_W_RX) = 2u;
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    for (unsigned i = 0; i < 5000u && state() != LINK_HITEC_BUSY; ++i) {
        tick();
    }
    run_ms(10u);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK_EQ(rx_code(), HITEC_RX_ERR_ABORTED);
    CHECK(hitec_port_supply_hold(&g_port));
    /* A disarm alone: the supply is as the SUPPLY page has it, on. */
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(g_hw.offs, 1);
}

static void hang_at(uint32_t t0)
{
    fresh_at(t0, SUPPLY_ON);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    g_drv.hang = true;
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    run_ms(HITEC_PORT_BUSY_MS);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK_EQ(g_drv.aborts, 0);
    tick();
    CHECK_EQ(g_drv.aborts, 1);
    tick();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(!flag(LINK_HT_F_STOPPED));
}

TEST_CASE(a_transaction_that_does_not_end_is_aborted_after_10_s)
{
    at_tick_0_and_before_the_wrap(hang_at, 5000u);
}

static void stop_time_at(uint32_t t0)
{
    fresh_at(t0, SUPPLY_ON);
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    run_ms(HITEC_PORT_STOP_MS);
    CHECK_EQ(state(), LINK_HITEC_STOPPING);
    tick();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
}

TEST_CASE(the_stop_time_holds_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(stop_time_at, 20u);
}

int main(void)
{
    RUN(a_stop_aborts_the_transaction_and_keeps_the_port_held);
    RUN(a_stop_on_an_idle_port_blocks_transactions_until_clear);
    RUN(an_unsafe_bench_takes_only_abort_and_close);
    RUN(a_stop_in_the_stop_time_frees_the_port);
    RUN(close_cuts_a_running_transaction_and_leaves_the_supply);
    RUN(a_power_cycle_switches_off_waits_and_listens_from_the_on);
    RUN(the_times_of_a_power_cycle_hold_across_the_tick_wrap);
    RUN(power_takes_4800_to_6000_mv_only);
    RUN(power_without_a_pd_mini_is_refused);
    RUN(power_answering_the_first_byte_needs_the_key);
    RUN(a_supply_that_does_not_read_off_ends_the_power_in_3_s);
    RUN(a_supply_that_refuses_the_on_ends_the_power);
    RUN(a_listen_the_driver_refuses_after_the_on_leaves_the_supply_on);
    RUN(a_stop_in_the_off_time_leaves_the_supply_off);
    RUN(an_abort_in_the_off_time_leaves_the_supply_off);
    RUN(a_stop_in_the_listen_of_a_power_cuts_it);
    RUN(a_transaction_that_does_not_end_is_aborted_after_10_s);
    RUN(the_stop_time_holds_across_the_tick_wrap);
    return test_summary("hitec_port_stop");
}
