/*
 * The HITEC page's receive mode (shared/outputs/hitec_port.c, protocol
 * 4.24): write register 23, what an OPEN takes from it, the transactions a
 * PRECHARGE line refuses, and the supply going off under a PRECHARGE
 * receive, POWER's ramp included.  The driver is the rig's model (hitec_port_rig.h); the sampler
 * itself is test_hitec_pre.c's.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_port_rig.h"

#include "hitec_pre.h"

static uint8_t rx_mode_write(uint16_t v)
{
    return hitec_port_write(&g_port, LINK_HT_W_RX_MODE, 1, &v, g_ms);
}

/* The line register of VIEW 2. */
static uint16_t line_reg(void)
{
    const uint16_t v = LINK_HITEC_VIEW_RUN;

    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 1, &v, g_ms), 0);
    return reg(LINK_HT_I_LINE);
}

/* ===================================================================== cases */

TEST_CASE(the_receive_mode_starts_pull_up_and_takes_0_and_1_only)
{
    uint16_t in[2];

    fresh();
    CHECK(!flag(LINK_HT_F_PRECHARGE));
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    CHECK(flag(LINK_HT_F_PRECHARGE));
    /* 2 and above: refused, nothing changed. */
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_COUNT), LINK_NACK_BAD_VALUE);
    CHECK_EQ(rx_mode_write(0xFFFFu), LINK_NACK_BAD_VALUE);
    CHECK(flag(LINK_HT_F_PRECHARGE));
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PULL_UP), 0);
    CHECK(!flag(LINK_HT_F_PRECHARGE));
    /* With VIEW in one window; a bad VIEW keeps the mode as it was. */
    in[0] = LINK_HITEC_VIEW_RUN;
    in[1] = LINK_HITEC_RX_PRECHARGE;
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 2, in, g_ms), 0);
    CHECK(flag(LINK_HT_F_PRECHARGE));
    CHECK_EQ(reg(LINK_HT_VIEW), LINK_HITEC_VIEW_RUN);
    in[0] = LINK_HITEC_VIEW_COUNT;
    in[1] = LINK_HITEC_RX_PULL_UP;
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 2, in, g_ms),
             LINK_NACK_BAD_VALUE);
    CHECK(flag(LINK_HT_F_PRECHARGE));
    /* A bad mode keeps VIEW as it was. */
    in[0] = LINK_HITEC_VIEW_TIMES_LO;
    in[1] = 7u;
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 2, in, g_ms),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_HT_VIEW), LINK_HITEC_VIEW_RUN);
    /* Past it: read only. */
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_COUNT, 1, in, g_ms),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_RX_MODE, 2, in, g_ms),
             LINK_NACK_READ_ONLY);
    /* Not a command: no SEQ moves. */
    CHECK_EQ(reg(LINK_HT_SEQ), 0);
}

TEST_CASE(an_open_takes_the_mode_written_before_it)
{
    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(g_hw.take_rx, HITEC_RX_PRECHARGE);
    CHECK_EQ(line_reg(), LINK_HT_LINE_OF(HITEC_D, HITEC_DRIVE_OPEN_DRAIN)
                             | LINK_HT_LINE_PRECHARGE);
    /* A change while the line is open waits for the next OPEN. */
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PULL_UP), 0);
    CHECK((line_reg() & LINK_HT_LINE_PRECHARGE) != 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    open_line(HITEC_D, HITEC_DRIVE_PUSH_PULL);
    CHECK_EQ(g_hw.take_rx, HITEC_RX_PULL_UP);
    CHECK_EQ(line_reg(), LINK_HT_LINE_OF(HITEC_D, HITEC_DRIVE_PUSH_PULL));
    /* Written in the stop time: the OPEN had taken PULL-UP already. */
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_STOPPING);
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    until_state(LINK_HITEC_OPEN);
    CHECK_EQ(g_hw.take_rx, HITEC_RX_PULL_UP);
}

TEST_CASE(a_classic_open_ignores_precharge)
{
    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(g_hw.take_rx, HITEC_RX_PULL_UP);
    CHECK_EQ(line_reg(), LINK_HT_LINE_OF(HITEC_CLASSIC,
                                         HITEC_DRIVE_OPEN_DRAIN));
    /* And reads with it as before. */
    stage_classic_read(0x12u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    CHECK(flag(LINK_HT_F_PRECHARGE));
}

TEST_CASE(a_precharge_line_refuses_an_answer_and_a_window_past_30_ms)
{
    hitec_xfer_t x;

    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    /* A read: taken and decoded as on a PULL-UP line. */
    hitec_d_read_xfer(0x00u, 0x32u, &x);
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    CHECK_EQ(reg(LINK_HT_VALUE), 0x1232u);
    /* The window at 30 ms and one past it. */
    x.window_us = HITEC_PRE_WINDOW_MAX_US;
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    x.window_us = HITEC_PRE_WINDOW_MAX_US + 1u;
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    CHECK_EQ(g_drv.starts, 2);
    /* A write has no window: any lead. */
    hitec_d_write_xfer(0x00u, 0x32u, 0x1234u, &x);
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    /* A listen that answers; one that does not. */
    line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    stage_listen(1000u, 1u, true, 0x2Bu, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    /* A POWER whose listen answers. */
    line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    stage_listen(1000u, 1u, true, 0x2Bu, LINK_HITEC_DEC_NONE, 0u);
    SST(LINK_HT_W_SUPPLY_MV) = MV;
    SST(LINK_HT_W_SUPPLY_MA) = MA;
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    CHECK_EQ(g_hw.offs, 0);
}

TEST_CASE(a_pull_up_line_takes_what_a_precharge_line_refuses)
{
    hitec_xfer_t x;

    fresh();
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    hitec_d_read_xfer(0x00u, 0x32u, &x);
    x.window_us = HITEC_PRE_WINDOW_MAX_US + 1u;
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    stage_listen(1000u, 1u, true, 0x2Bu, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
}

/* Start a 25 ms D read on a line of @p rx with the supply as it is, and
 * switch the supply off 2 ms in. */
static void read_then_supply_off(uint8_t rx)
{
    hitec_xfer_t x;

    CHECK_EQ(rx_mode_write(rx), 0);
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    hitec_d_read_xfer(0x00u, 0x32u, &x);
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    run_ms(2u);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    (void)hw_supply_set(NULL, false, 0u, 0u);
}

TEST_CASE(the_supply_off_cuts_a_precharge_receive)
{
    fresh();
    read_then_supply_off(LINK_HITEC_RX_PRECHARGE);
    tick();
    CHECK_EQ(g_drv.aborts, 1);
    tick();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK((reg(LINK_HT_CAP) & LINK_HT_CAP_ABORTED) != 0u);
    CHECK(!flag(LINK_HT_F_STOPPED));
    /* One cut only. */
    run_ms(5u);
    CHECK_EQ(g_drv.aborts, 1);

    /* A PD mini that stops answering cuts it as well. */
    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    {
        hitec_xfer_t x;

        hitec_d_read_xfer(0x00u, 0x32u, &x);
        stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    }
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    run_ms(2u);
    g_hw.v.online = false;
    tick();
    CHECK_EQ(g_drv.aborts, 1);

    /* The output read off with OUTPUT still asked on (the PD mini's
     * protection) cuts it too. */
    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    {
        hitec_xfer_t x;

        hitec_d_read_xfer(0x00u, 0x32u, &x);
        stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    }
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    run_ms(2u);
    CHECK_EQ(g_drv.aborts, 0);
    g_hw.v.on = false;
    g_hw.v.off = true;
    tick();
    CHECK_EQ(g_drv.aborts, 1);
    CHECK(g_hw.v.asked_on);
}

TEST_CASE(the_supply_off_leaves_a_pull_up_receive_and_an_unpowered_start)
{
    /* PULL-UP: the receive runs to its end, as before 4.24. */
    fresh();
    read_then_supply_off(LINK_HITEC_RX_PULL_UP);
    settle();
    CHECK_EQ(g_drv.aborts, 0);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    /* PRECHARGE started with the supply not asked on: nothing to see go
     * off, and a servo on another supply is read to the end. */
    fresh_at(0u, SUPPLY_OFFLINE);
    read_then_supply_off(LINK_HITEC_RX_PRECHARGE);
    settle();
    CHECK_EQ(g_drv.aborts, 0);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    /* A build without a supply. */
    fresh_at(0u, SUPPLY_NONE);
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    {
        hitec_xfer_t x;

        hitec_d_read_xfer(0x00u, 0x32u, &x);
        stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    }
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    settle();
    CHECK_EQ(g_drv.aborts, 0);
    CHECK_EQ(rx_code(), HITEC_RX_OK);
}

/* POWER on a PRECHARGE line: the supply off, 100 ms off, on at MV and MA,
 * and a 20 ms listen for one byte from the ON.  Returns in the pass after
 * the ON with the listen running, the output still read off and the PD
 * mini's read the test's from here. */
static void power_listen_precharge(uint32_t boot_ns)
{
    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_PUSH_PULL);
    g_drv.boot_ns = boot_ns;
    stage_listen(20000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    SST(LINK_HT_W_SUPPLY_MV) = MV;
    SST(LINK_HT_W_SUPPLY_MA) = MA;
    SST(LINK_HT_W_OFF_MS) = 100u;
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    for (unsigned i = 0; i < 5000u && g_hw.ons == 0u; ++i) {
        tick();
    }
    CHECK_EQ(g_hw.ons, 1);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    g_hw.follow = false;
    CHECK(g_hw.v.asked_on);
    CHECK(g_hw.v.off);
}

static void read_on(bool on)
{
    g_hw.v.on = on;
    g_hw.v.off = !on;
}

TEST_CASE(a_power_listen_rides_the_ramp_that_reads_off_then_on)
{
    power_listen_precharge(15000000u);
    /* The ramp: 3 passes read off, then on. */
    run_ms(3u);
    CHECK_EQ(g_drv.aborts, 0);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    read_on(true);
    settle();
    CHECK_EQ(g_drv.aborts, 0);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(LINK_HT_OP_OF(reg(LINK_HT_OP)), LINK_HITEC_OP_POWER);
    CHECK_EQ(reg(LINK_HT_CAP) & LINK_HT_CAP_ABORTED, 0u);
    CHECK_EQ(cap_n(), 1u);
    CHECK_EQ(cap_byte(0), 0x5A);
    /* A read neither on nor off (LIVE, not ON) counts as on as well. */
    power_listen_precharge(15000000u);
    run_ms(2u);
    g_hw.v.off = false;
    tick();
    g_hw.v.off = true;
    tick();
    CHECK_EQ(g_drv.aborts, 1);
}

TEST_CASE(a_power_listen_is_cut_when_the_output_reads_off_after_on)
{
    /* The PD mini's protection: OUTPUT stays asked on. */
    power_listen_precharge(15000000u);
    run_ms(2u);
    read_on(true);
    run_ms(3u);
    CHECK_EQ(g_drv.aborts, 0);
    read_on(false);
    tick();
    CHECK_EQ(g_drv.aborts, 1);
    CHECK(g_hw.v.asked_on);
    tick();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK((reg(LINK_HT_CAP) & LINK_HT_CAP_ABORTED) != 0u);
    CHECK_EQ(cap_n(), 0u);
    run_ms(5u);
    CHECK_EQ(g_drv.aborts, 1);
}

TEST_CASE(a_power_listen_is_cut_when_the_output_is_asked_off)
{
    /* In the ramp, before it read on. */
    power_listen_precharge(15000000u);
    run_ms(2u);
    (void)hw_supply_set(NULL, false, 0u, 0u);
    tick();
    CHECK_EQ(g_drv.aborts, 1);
    tick();
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    /* After it read on. */
    power_listen_precharge(15000000u);
    run_ms(2u);
    read_on(true);
    run_ms(2u);
    (void)hw_supply_set(NULL, false, 0u, 0u);
    tick();
    CHECK_EQ(g_drv.aborts, 1);
    tick();
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    /* A PD mini that stops answering in the ramp. */
    power_listen_precharge(15000000u);
    run_ms(2u);
    g_hw.v.online = false;
    tick();
    CHECK_EQ(g_drv.aborts, 1);
}

TEST_CASE(a_power_listen_whose_output_never_reads_on_runs_its_window)
{
    /* The output reads off for the whole 20 ms window: no cut, the
     * capture as heard, DONE. */
    power_listen_precharge(30000000u);
    settle();
    CHECK_EQ(g_drv.aborts, 0);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(reg(LINK_HT_CAP) & LINK_HT_CAP_ABORTED, 0u);
    CHECK_EQ(cap_n(), 0u);
    CHECK(g_hw.v.off);
    /* The same with the boot byte in the window. */
    power_listen_precharge(15000000u);
    settle();
    CHECK_EQ(g_drv.aborts, 0);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(cap_n(), 1u);
}

TEST_CASE(a_listen_on_an_output_read_off_is_cut_at_once)
{
    /* Not POWER: the supply is on before the start, so off at the start
     * is off. */
    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_PUSH_PULL);
    g_hw.follow = false;
    read_on(false);
    stage_listen(20000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    tick();
    CHECK_EQ(g_drv.aborts, 1);
    tick();
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
}

/* The rig's window, as the driver's: waiting for a start bit only.  Three
 * bytes back to back from 1 ms: a 1,100 us window hears all three, the
 * last starting 1,174 us in; a 990 us window hears none. */
TEST_CASE(the_window_counts_the_waiting_only)
{
    fresh();
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    g_drv.listen_n = 3u;
    stage_listen(1100u, 3u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    settle();
    CHECK_EQ(cap_n(), 3u);
    CHECK_EQ(cap_byte(2), 0xA2);
    stage_listen(990u, 3u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    settle();
    CHECK_EQ(cap_n(), 0u);
}

TEST_CASE(a_stop_close_and_abort_cut_a_precharge_receive_as_any_other)
{
    hitec_xfer_t x;

    fresh();
    CHECK_EQ(rx_mode_write(LINK_HITEC_RX_PRECHARGE), 0);
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    hitec_d_read_xfer(0x00u, 0x32u, &x);
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    /* A stop. */
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    run_ms(3u);
    tick_as(true, true);
    CHECK_EQ(g_drv.aborts, 1);
    tick();
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(flag(LINK_HT_F_STOPPED));
    CHECK_EQ(cmd(LINK_HITEC_OP_CLEAR, CH, 0), 0);
    /* An unsafe pass. */
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    run_ms(3u);
    tick_as(false, false);
    CHECK_EQ(g_drv.aborts, 2);
    tick();
    CHECK_EQ(cmd(LINK_HITEC_OP_CLEAR, CH, 0), 0);
    /* ABORT, and a new request while it runs. */
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    run_ms(3u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_BUSY);
    CHECK_EQ(cmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(g_drv.aborts, 3);
    tick();
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    /* CLOSE: the line closed, the pin given back. */
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    run_ms(3u);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_drv.aborts, 4);
}

int main(void)
{
    RUN(the_receive_mode_starts_pull_up_and_takes_0_and_1_only);
    RUN(an_open_takes_the_mode_written_before_it);
    RUN(a_classic_open_ignores_precharge);
    RUN(a_precharge_line_refuses_an_answer_and_a_window_past_30_ms);
    RUN(a_pull_up_line_takes_what_a_precharge_line_refuses);
    RUN(the_supply_off_cuts_a_precharge_receive);
    RUN(the_supply_off_leaves_a_pull_up_receive_and_an_unpowered_start);
    RUN(a_power_listen_rides_the_ramp_that_reads_off_then_on);
    RUN(a_power_listen_is_cut_when_the_output_reads_off_after_on);
    RUN(a_power_listen_is_cut_when_the_output_is_asked_off);
    RUN(a_power_listen_whose_output_never_reads_on_runs_its_window);
    RUN(a_listen_on_an_output_read_off_is_cut_at_once);
    RUN(the_window_counts_the_waiting_only);
    RUN(a_stop_close_and_abort_cut_a_precharge_receive_as_any_other);
    return test_summary("hitec_port_rx");
}
