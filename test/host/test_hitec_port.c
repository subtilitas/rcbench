/*
 * The HITEC page on a programming port (shared/outputs/hitec_port.c): the
 * way in, every refusal of OPEN and of a transaction, each transaction kind,
 * the decoders on the page, the capture's 16 bytes and their times, the
 * write key, the staged CRC (cyclic redundancy check) and SEQ, ABORT, the
 * transaction count and the frame a SEND has on the wire.  The rig is
 * hitec_port_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_port_rig.h"

/* ===================================================================== cases */


TEST_CASE(init_refuses_hardware_without_its_functions)
{
    hitec_port_hw_t hw;
    hitec_port_t p;

    fresh();
    hw = g_port.hw;
    CHECK(hitec_port_init(&p, &hw));
    hw.abort = NULL;
    CHECK(!hitec_port_init(&p, &hw));
    hw = g_port.hw;
    hw.ports = 0u;
    CHECK(!hitec_port_init(&p, &hw));
    hw.ports = LINK_PG_PORTS_MAX + 1u;
    CHECK(!hitec_port_init(&p, &hw));
    hw = g_port.hw;
    hw.supply = NULL;
    CHECK(!hitec_port_init(&p, &hw));
    hw.supply_set = NULL;
    CHECK(hitec_port_init(&p, &hw));
    hw.run = NULL;
    CHECK(hitec_port_init(&p, &hw));
    CHECK(!hitec_port_init(NULL, &hw));
    CHECK(!hitec_port_init(&p, NULL));
}

TEST_CASE(open_takes_the_pin_after_the_stop_time_released)
{
    fresh();
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(hitec_port_quiet_mask(&g_port), 0);
    line(HITEC_D, HITEC_DRIVE_PUSH_PULL);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_STOPPING);
    CHECK_EQ(result(), LINK_HITEC_RESULT_RUNNING);
    CHECK_EQ(hitec_port_quiet_mask(&g_port), 1u << CH);
    CHECK_EQ(hitec_port_pin_mask(&g_port), 0);
    run_ms(HITEC_PORT_STOP_MS);
    CHECK_EQ(g_hw.takes, 0);
    tick();
    CHECK_EQ(g_hw.takes, 1);
    CHECK_EQ(g_hw.take_port, CH);
    CHECK_EQ(g_hw.take_pin, PIN);
    CHECK_EQ(g_hw.take_family, HITEC_D);
    CHECK_EQ(g_hw.take_drive, HITEC_DRIVE_PUSH_PULL);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(hitec_port_pin_mask(&g_port), 1u << CH);
    CHECK_EQ(LINK_HT_PORT_OF(reg(LINK_HT_STATE)), CH);
    CHECK_EQ(LINK_HT_OP_OF(reg(LINK_HT_OP)), LINK_HITEC_OP_OPEN);
    /* The run view names the line and its pin. */
    {
        const uint16_t two = LINK_HITEC_VIEW_RUN;

        CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 1, &two, g_ms), 0);
    }
    CHECK_EQ(reg(LINK_HT_I_LINE),
             LINK_HT_LINE_OF(HITEC_D, HITEC_DRIVE_PUSH_PULL));
    CHECK_EQ(reg(LINK_HT_I_PIN), PIN);
    CHECK_EQ(reg(LINK_HT_I_BIT_NS), hitec_bits_ns(HITEC_D, 1u));
    CHECK_EQ(g_drv.starts, 0);
}

TEST_CASE(open_is_refused_for_each_reason)
{
    fresh();
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    /* No such port. */
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, 2u, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_PORT);
    /* No pin on the other port. */
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, OTHER, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_PIN);
    /* The KST page holds it. */
    hitec_port_other_mask(&g_port, 1u << CH);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_HELD);
    hitec_port_other_mask(&g_port, 1u << OTHER);
    /* The bank is armed. */
    hitec_port_bench_armed(&g_port, true);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_ARMED);
    hitec_port_bench_armed(&g_port, false);
    /* Not safe. */
    tick_as(false, false);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_UNSAFE);
    tick();
    /* A CRC that is not the staged block's. */
    (void)stage();
    CHECK_EQ(cmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_OPEN, CH), 0,
                     (uint16_t)(link_kst_crc(g_st, LINK_HT_W_STAGED) ^ 1u)),
             0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_STAGED);
    /* Classic is open drain only, and the other bits are 0. */
    line(HITEC_CLASSIC, HITEC_DRIVE_PUSH_PULL);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    SST(LINK_HT_W_LINE) |= 0x0002u;
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(hitec_port_quiet_mask(&g_port), 0);
    run_ms(100u);
    CHECK_EQ(g_hw.takes, 0);
    /* And with every reason gone, it opens; a second OPEN is busy. */
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_BUSY);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
}

TEST_CASE(a_driver_without_resources_leaves_the_port_free)
{
    fresh();
    g_hw.take_ok = false;
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    run_ms(HITEC_PORT_STOP_MS + 1u);
    CHECK_EQ(g_hw.takes, 1);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(result(), LINK_HITEC_RESULT_NO_LINE);
    CHECK_EQ(hitec_port_quiet_mask(&g_port), 0);
    CHECK_EQ(hitec_port_pin_mask(&g_port), 0);
}

TEST_CASE(a_classic_read_is_sent_and_decoded)
{
    open_classic();
    stage_classic_read(0x12u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK_EQ(result(), LINK_HITEC_RESULT_RUNNING);
    CHECK_EQ(g_drv.starts, 1);
    CHECK_EQ(g_drv.x.n_tx, 4);
    CHECK_EQ(g_drv.x.tx[0], HITEC_C_READ_EEPROM);
    CHECK_EQ(g_drv.x.tx[1], 0x12);
    CHECK_EQ(g_drv.x.tx[3], hitec_c_sum(HITEC_C_READ_EEPROM, 0x12u, 0u));
    CHECK_EQ(g_drv.x.gap_bits, HITEC_C_GAP_BITS);
    CHECK_EQ(g_drv.x.window_us, HITEC_C_WINDOW_US);
    settle();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK(flag(LINK_HT_F_CAPTURE));
    CHECK(flag(LINK_HT_F_KEY));
    CHECK_EQ(cap_n(), 2);
    CHECK((reg(LINK_HT_CAP) & LINK_HT_CAP_FULL) != 0u);
    CHECK_EQ(reg(LINK_HT_CAP) >> 12, LINK_HITEC_DEC_C);
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    CHECK_EQ(reg(LINK_HT_VALUE), 0x12u ^ 0x3Cu);
    CHECK_EQ(cap_byte(0), 0x12u ^ 0x3Cu);
    CHECK_EQ(cap_byte(1), HITEC_C_ACK);
    CHECK_EQ(cap_byte(2), 0);
    CHECK_EQ(count(), 1);
    /* The start bits' times, ns from the receiver's start. */
    {
        const uint32_t at0 = 520000u - hitec_bits_ns(HITEC_CLASSIC, 9u);
        const uint32_t at1 = at0 + hitec_bits_ns(HITEC_CLASSIC, 10u);

        CHECK_EQ(reg(LINK_HT_DATA) | ((uint32_t)reg(LINK_HT_DATA + 1u) << 16),
                 at0);
        CHECK_EQ(reg(LINK_HT_DATA + 2u)
                     | ((uint32_t)reg(LINK_HT_DATA + 3u) << 16), at1);
        CHECK_EQ(reg(LINK_HT_DATA + 4u), 0);
    }
    /* The run view: the decoder's delay and the window asked. */
    {
        const uint16_t two = LINK_HITEC_VIEW_RUN;

        CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 1, &two, g_ms), 0);
        CHECK_EQ(reg(LINK_HT_VIEW), LINK_HITEC_VIEW_RUN);
        CHECK_EQ(reg(LINK_HT_I_DELAY_LO)
                     | ((uint32_t)reg(LINK_HT_I_DELAY_HI) << 16), 520000u);
        CHECK_EQ(reg(LINK_HT_I_WINDOW_LO), HITEC_C_WINDOW_US);
        CHECK_EQ(reg(LINK_HT_I_SPAN_LO), 1234u);
        CHECK(reg(LINK_HT_I_T_LO) > 0u);
    }
    /* A write reads back. */
    {
        hitec_xfer_t x;

        hitec_c_xfer(HITEC_C_WRITE_EEPROM, 0x30u, 0x77u, &x);
        stage_send(&x, LINK_HITEC_DEC_C, HITEC_C_WRITE_EEPROM);
        CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
        settle();
        CHECK_EQ(rx_code(), HITEC_RX_OK);
        CHECK_EQ(g_drv.eeprom[0x30], 0x77);
        CHECK_EQ(count(), 2);
    }
}

TEST_CASE(a_d_series_read_is_sent_and_decoded)
{
    hitec_xfer_t x;

    fresh();
    open_line(HITEC_D, HITEC_DRIVE_PUSH_PULL);
    hitec_d_read_xfer(0x00u, 0x32u, &x);
    stage_send(&x, LINK_HITEC_DEC_D, 0x32u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(g_drv.x.lead_us, HITEC_D_LEAD_US);
    settle();
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    CHECK_EQ(reg(LINK_HT_VALUE), 0x1232u);
    CHECK_EQ(cap_n(), 8);
    CHECK_EQ(cap_byte(0), HITEC_D_HEADER_ANSWER);
    CHECK_EQ(cap_byte(2), 0x32);
    /* A decoder for the other family is out of range. */
    stage_send(&x, LINK_HITEC_DEC_C, HITEC_C_READ_EEPROM);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    CHECK_EQ(g_drv.starts, 1);
}

TEST_CASE(a_send_without_the_key_is_refused_before_anything_else)
{
    fresh();
    stage_classic_read(0x01u);
    /* Free port: still NO_KEY. */
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_KEY);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    stage_classic_read(0x01u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_KEY);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(g_drv.starts, 0);
    /* A key that is not the key is no command at all. */
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, (uint16_t)(KEY ^ 1u)),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(g_drv.starts, 0);
}

TEST_CASE(a_listen_needs_the_key_only_to_answer)
{
    open_classic();
    g_drv.listen_n = 1u;
    stage_listen(5000u, 1u, false, 0u, LINK_HITEC_DEC_LISTEN, 0xA0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    settle();
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    CHECK(!flag(LINK_HT_F_KEY));
    /* The key on a listen that answers nothing puts no byte out: the flag
     * stays clear.  The capture of the last one is gone while it runs. */
    CHECK(flag(LINK_HT_F_CAPTURE));
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK(!flag(LINK_HT_F_CAPTURE));
    CHECK_EQ(cap_n(), 0);
    CHECK(!flag(LINK_HT_F_KEY));
    settle();
    CHECK(flag(LINK_HT_F_CAPTURE));
    CHECK(!flag(LINK_HT_F_KEY));
    stage_listen(5000u, 1u, true, 0x2Bu, LINK_HITEC_DEC_LISTEN, 0xA0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_KEY);
    CHECK_EQ(g_drv.starts, 2);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(g_drv.x.answer, 1);
    CHECK_EQ(g_drv.x.answer_byte, 0x2B);
    settle();
    CHECK((reg(LINK_HT_CAP) & LINK_HT_CAP_ANSWERED) != 0u);
    CHECK(flag(LINK_HT_F_KEY));
    /* A listen for a byte that does not come reads ACK; one that hears
     * more than the byte reads LENGTH. */
    stage_listen(5000u, 1u, false, 0u, LINK_HITEC_DEC_LISTEN, 0x55u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    settle();
    CHECK_EQ(rx_code(), HITEC_RX_ERR_ACK);
    g_drv.listen_n = 3u;
    stage_listen(5000u, 3u, false, 0u, LINK_HITEC_DEC_LISTEN, 0xA0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    settle();
    CHECK_EQ(rx_code(), HITEC_RX_ERR_LENGTH);
    CHECK_EQ(cap_n(), 3);
}

TEST_CASE(an_empty_capture_reads_silent)
{
    open_classic();
    stage_listen(3000u, 2u, false, 0u, LINK_HITEC_DEC_LISTEN, 0x2Bu);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    run_ms(2u);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    settle();
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(cap_n(), 0);
    CHECK_EQ(rx_code(), HITEC_RX_ERR_SILENT);
    CHECK_EQ(cap_byte(0), 0);
    CHECK_EQ(reg(LINK_HT_DATA), 0);
}

TEST_CASE(the_capture_holds_16_bytes_and_the_full_flag)
{
    open_classic();
    g_drv.listen_n = 16u;
    stage_listen(4000000u, 16u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    settle();
    CHECK_EQ(cap_n(), 16);
    CHECK((reg(LINK_HT_CAP) & LINK_HT_CAP_FULL) != 0u);
    CHECK_EQ(rx_code(), HITEC_RX_OK);
    CHECK_EQ(cap_byte(0), 0xA0);
    CHECK_EQ(cap_byte(15), 0xAF);
    CHECK_EQ(reg(LINK_HT_BYTES + 7u), 0xAFAEu);
    {
        const uint16_t one = LINK_HITEC_VIEW_TIMES_HI;
        const uint32_t at15 = 1000000u + 15u * hitec_bits_ns(HITEC_CLASSIC,
                                                            10u);

        CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 1, &one, g_ms), 0);
        CHECK_EQ(reg(LINK_HT_DATA + 14u)
                     | ((uint32_t)reg(LINK_HT_DATA + 15u) << 16), at15);
    }
    /* 17 bytes is not a capture the line holds. */
    stage_listen(4000000u, 17u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
}

TEST_CASE(a_staged_register_changed_after_the_crc_is_refused)
{
    uint16_t crc;
    const uint16_t other = 0x0042u;

    open_classic();
    stage_classic_read(0x05u);
    crc = stage();
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_TX + 1u, 1, &other, g_ms), 0);
    CHECK_EQ(cmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_SEND, CH), KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_STAGED);
    CHECK_EQ(g_drv.starts, 0);
}

TEST_CASE(transactions_outside_the_line_are_refused)
{
    hitec_xfer_t x;

    open_classic();
    hitec_c_xfer(HITEC_C_READ_EEPROM, 0x01u, 0u, &x);
    /* 0 and 9 bytes; a gap of 7; an answer on a SEND. */
    x.n_tx = 0u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    x.n_tx = 9u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    x.n_tx = 4u;
    x.gap_bits = HITEC_GAP_MAX + 1u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    x.gap_bits = 1u;
    x.answer = 1u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    x.answer = 0u;
    /* A lead and a window of 4 s are taken; one more us is not. */
    x.lead_us = HITEC_TIME_MAX_US + 1u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    x.lead_us = 0u;
    x.window_us = HITEC_TIME_MAX_US + 1u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    /* A window without bytes to receive. */
    x.window_us = 1500u;
    x.max_rx = 0u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    x.max_rx = 2u;
    /* A decoder for a command the module does not know, and the bits
     * between the decoder and its argument. */
    stage_send(&x, LINK_HITEC_DEC_C, 0x70u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    SST(LINK_HT_W_DECODE) = 0x0004u;
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    SST(LINK_HT_W_DECODE) = 0u;
    SST(LINK_HT_W_RX) |= 0x0200u;
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    SST(LINK_HT_W_RX) = 2u;
    /* A listen with no window; a level of 0 us; a level with a decoder. */
    stage_listen(0u, 0u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    st32(LINK_HT_W_LEVEL_LO, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LEVEL, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    st32(LINK_HT_W_LEVEL_LO, 1000u);
    SST(LINK_HT_W_DECODE) = LINK_HITEC_DEC_LISTEN;
    CHECK_EQ(cmd(LINK_HITEC_OP_LEVEL, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_RANGE);
    SST(LINK_HT_W_DECODE) = 0u;
    CHECK_EQ(g_drv.starts, 0);
    /* The limits themselves are taken: 8 bytes, a gap of 6, 4 s. */
    x.n_tx = 8u;
    x.gap_bits = HITEC_GAP_MAX;
    x.window_us = HITEC_TIME_MAX_US;
    x.lead_us = HITEC_TIME_MAX_US;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    settle();
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
}

TEST_CASE(a_transaction_the_driver_refuses_ends_with_start)
{
    open_classic();
    /* An open-drain line makes no high level. */
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    SST(LINK_HT_W_ANSWER) = 0x0100u;
    st32(LINK_HT_W_LEVEL_LO, 1000u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LEVEL, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_START);
    CHECK_EQ(count(), 0);
    /* A low level is made. */
    SST(LINK_HT_W_ANSWER) = 0u;
    CHECK_EQ(cmd(LINK_HITEC_OP_LEVEL, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK_EQ(g_drv.x.kind, HITEC_XFER_LEVEL);
    CHECK_EQ(g_drv.x.level_us, 1000u);
    settle();
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(count(), 1);
}

TEST_CASE(a_command_while_a_transaction_runs_is_busy)
{
    open_classic();
    stage_listen(100000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_BUSY);
    CHECK_EQ(cmd(LINK_HITEC_OP_POWER, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_BUSY);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_BUSY);
    CHECK_EQ(g_drv.starts, 1);
    /* A command for the other port is refused as that. */
    run_ms(200u);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, OTHER, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_PORT);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, OTHER, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_PORT);
    CHECK_EQ(cmd(LINK_HITEC_OP_CLEAR, OTHER, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_PORT);
}

TEST_CASE(abort_cuts_the_transaction_and_keeps_the_port)
{
    open_classic();
    g_drv.listen_n = 4u;
    stage_listen(4000000u, 8u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    /* The first two bytes have come at 1.0 and 1.5 ms. */
    run_ms(2u);
    CHECK_EQ(cmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(g_drv.aborts, 1);
    tick();
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK((reg(LINK_HT_CAP) & LINK_HT_CAP_ABORTED) != 0u);
    CHECK_EQ(cap_n(), 2);
    CHECK(!flag(LINK_HT_F_STOPPED));
    CHECK_EQ(g_hw.gives, 0);
    CHECK_EQ(LINK_HT_OP_OF(reg(LINK_HT_OP)), LINK_HITEC_OP_LISTEN);
    /* An abort with nothing running does nothing; the port goes on. */
    CHECK_EQ(cmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(g_drv.aborts, 1);
    stage_classic_read(0x02u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    settle();
    CHECK_EQ(rx_code(), HITEC_RX_OK);
}

TEST_CASE(an_abort_in_the_stop_time_frees_the_port)
{
    fresh();
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    run_ms(10u);
    CHECK_EQ(cmd(LINK_HITEC_OP_ABORT, CH, 0), 0);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    run_ms(100u);
    CHECK_EQ(g_hw.takes, 0);
    CHECK_EQ(g_hw.gives, 0);
}

TEST_CASE(the_seq_is_checked_repeated_and_wraps)
{
    uint16_t in[4];

    fresh();
    /* SEQ 2 when 1 is next. */
    in[0] = LINK_HT_W_CMD_OF(LINK_HITEC_OP_CLEAR, CH);
    in[1] = 2u;
    in[2] = 0u;
    in[3] = 0u;
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_HT_SEQ), 0);
    in[1] = 1u;
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK_EQ(reg(LINK_HT_SEQ), 1);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NOT_OPEN);
    /* The same frame again: acknowledged, nothing done. */
    g_port.refusal = 0u;
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK_EQ(reg(LINK_HT_SEQ), 1);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    /* Across 65535. */
    g_port.seq = 0xFFFEu;
    in[1] = 0xFFFFu;
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK_EQ(reg(LINK_HT_SEQ), 0xFFFFu);
    in[1] = 0u;
    in[0] = LINK_HT_W_CMD_OF(LINK_HITEC_OP_ABORT, CH);
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK_EQ(reg(LINK_HT_SEQ), 0);
}

TEST_CASE(frames_and_windows_that_are_no_command_are_nacked)
{
    uint16_t in[LINK_HT_COUNT];

    fresh();
    memset(in, 0, sizeof(in));
    in[1] = 1u;
    /* Op 0, op past the last, bits outside op and port. */
    in[0] = LINK_HITEC_OP_NONE;
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), LINK_NACK_BAD_VALUE);
    in[0] = LINK_HITEC_OP_COUNT;
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), LINK_NACK_BAD_VALUE);
    in[0] = (uint16_t)(LINK_HITEC_OP_CLEAR | 0x0010u);
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), LINK_NACK_BAD_VALUE);
    in[0] = (uint16_t)(LINK_HITEC_OP_CLEAR | 0x1000u);
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), LINK_NACK_BAD_VALUE);
    in[0] = LINK_HITEC_OP_CLEAR;
    in[2] = 0x1234u;
    CHECK_EQ(hitec_port_write(&g_port, 0, 4, in, g_ms), LINK_NACK_BAD_VALUE);
    in[2] = 0u;
    /* A command is the 4 registers exactly. */
    CHECK_EQ(hitec_port_write(&g_port, 0, 3, in, g_ms), LINK_NACK_BAD_RANGE);
    CHECK_EQ(hitec_port_write(&g_port, 0, 5, in, g_ms), LINK_NACK_BAD_RANGE);
    CHECK_EQ(hitec_port_write(&g_port, 1, 3, in, g_ms), LINK_NACK_BAD_RANGE);
    CHECK_EQ(hitec_port_write(&g_port, 0, 0, in, g_ms), LINK_NACK_BAD_RANGE);
    CHECK_EQ(hitec_port_write(&g_port, 30, 3, in, g_ms), LINK_NACK_BAD_RANGE);
    /* Registers past VIEW are read only; a view past the last refused. */
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_COUNT, 1, in, g_ms),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 2, in, g_ms),
             LINK_NACK_READ_ONLY);
    in[0] = LINK_HITEC_VIEW_COUNT;
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 1, in, g_ms),
             LINK_NACK_BAD_VALUE);
    in[0] = 0u;
    CHECK_EQ(reg(LINK_HT_SEQ), 0);
    /* The whole staged block and VIEW in one window. */
    in[LINK_HT_W_STAGED] = LINK_HITEC_VIEW_RUN;
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_LINE,
                              LINK_HT_W_STAGED + 1u, in, g_ms), 0);
    CHECK_EQ(reg(LINK_HT_VIEW), LINK_HITEC_VIEW_RUN);
    /* A read past the page is not served; a whole read is. */
    {
        uint16_t out[LINK_HT_COUNT];

        memset(out, 0xEE, sizeof(out));
        hitec_port_read(&g_port, 1, LINK_HT_COUNT, out);
        CHECK_EQ(out[0], 0xEEEEu);
        hitec_port_read(&g_port, 0, LINK_HT_COUNT, out);
        CHECK_EQ(out[LINK_HT_I_PIN], 0xFFFFu);
    }
}

TEST_CASE(the_transaction_count_holds_at_4095)
{
    open_classic();
    g_port.count = 4094u;
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    settle();
    CHECK_EQ(count(), 4095);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    settle();
    CHECK_EQ(count(), 4095);
    CHECK_EQ(LINK_HT_OP_OF(reg(LINK_HT_OP)), LINK_HITEC_OP_LISTEN);
    /* A new OPEN counts from 0. */
    CHECK_EQ(cmd(LINK_HITEC_OP_CLOSE, CH, 0), 0);
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(count(), 0);
}

TEST_CASE(a_send_says_while_its_lead_and_bytes_are_out)
{
    hitec_xfer_t x;

    fresh();
    open_line(HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    CHECK(!hitec_port_frame_out(&g_port, g_ms));
    hitec_d_read_xfer(0u, 0x10u, &x);
    x.lead_us = 3000u;
    stage_send(&x, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_SEND, CH, KEY), 0);
    /* 3 ms of lead and 51 bit times of 8.68 us: 3.443 ms, counted as the
     * whole 3 ms and 1 more. */
    for (unsigned i = 0; i <= 4u; ++i) {
        CHECK(hitec_port_frame_out(&g_port, g_ms + i));
    }
    CHECK(!hitec_port_frame_out(&g_port, g_ms + 5u));
    /* A listen is no frame. */
    settle();
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, 0), 0);
    CHECK(!hitec_port_frame_out(&g_port, g_ms));
    /* One that answers is, for as long as it runs: the answer goes out
     * when the first byte has come, which the port does not see. */
    settle();
    stage_listen(20000u, 1u, true, 0x2Bu, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(cmd(LINK_HITEC_OP_LISTEN, CH, KEY), 0);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK(hitec_port_frame_out(&g_port, g_ms));
    run_ms(15u);
    CHECK(hitec_port_frame_out(&g_port, g_ms));
    settle();
    CHECK(!hitec_port_frame_out(&g_port, g_ms));
}

TEST_CASE(the_read_registers_say_nothing_before_a_capture)
{
    fresh();
    CHECK_EQ(reg(LINK_HT_STATE) & 0x0FF8u, LINK_HT_F_RAIL);
    CHECK_EQ(reg(LINK_HT_OP), 0);
    CHECK_EQ(reg(LINK_HT_RESULT), 0);
    CHECK_EQ(reg(LINK_HT_CAP), 0);
    CHECK_EQ(reg(LINK_HT_DEC), 0);
    CHECK_EQ(reg(LINK_HT_VALUE), 0);
    for (unsigned i = LINK_HT_BYTES; i < LINK_HT_COUNT; ++i) {
        CHECK_EQ(reg(i), 0);
    }
    CHECK_EQ(hitec_port_state(NULL), LINK_HITEC_FREE);
    CHECK_EQ(hitec_port_quiet_mask(NULL), 0);
    CHECK_EQ(hitec_port_pin_mask(NULL), 0);
    CHECK(!hitec_port_supply_hold(NULL));
    CHECK(!hitec_port_frame_out(NULL, 0u));
    CHECK_EQ(hitec_port_write(NULL, 0, 4, g_st, 0u), LINK_NACK_BAD_RANGE);
    hitec_port_step(NULL, true, false, 0u);
    hitec_port_bench_armed(NULL, true);
    hitec_port_other_mask(NULL, 1u);
}

int main(void)
{
    RUN(init_refuses_hardware_without_its_functions);
    RUN(open_takes_the_pin_after_the_stop_time_released);
    RUN(open_is_refused_for_each_reason);
    RUN(a_driver_without_resources_leaves_the_port_free);
    RUN(a_classic_read_is_sent_and_decoded);
    RUN(a_d_series_read_is_sent_and_decoded);
    RUN(a_send_without_the_key_is_refused_before_anything_else);
    RUN(a_listen_needs_the_key_only_to_answer);
    RUN(an_empty_capture_reads_silent);
    RUN(the_capture_holds_16_bytes_and_the_full_flag);
    RUN(a_staged_register_changed_after_the_crc_is_refused);
    RUN(transactions_outside_the_line_are_refused);
    RUN(a_transaction_the_driver_refuses_ends_with_start);
    RUN(a_command_while_a_transaction_runs_is_busy);
    RUN(abort_cuts_the_transaction_and_keeps_the_port);
    RUN(an_abort_in_the_stop_time_frees_the_port);
    RUN(the_seq_is_checked_repeated_and_wraps);
    RUN(frames_and_windows_that_are_no_command_are_nacked);
    RUN(the_transaction_count_holds_at_4095);
    RUN(a_send_says_while_its_lead_and_bytes_are_out);
    RUN(the_read_registers_say_nothing_before_a_capture);
    return test_summary("hitec_port");
}
