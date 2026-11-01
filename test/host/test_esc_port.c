/*
 * The ESC_WIRE page, shared/outputs/esc_port.c: the way in and out of a slot and a
 * port, every refusal of OPEN and ENTER at its limit, the supply's order, BY HAND,
 * what the bank's masks say in each state.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_port_rig.h"

TEST_CASE(init_needs_every_hook)
{
    sim_init_am(&esc, 4u, 2u);
    rig_up();
    esc_port_hw_t hw = g_p.hw;

    CHECK(!esc_port_init(NULL, &hw));
    CHECK(!esc_port_init(&g_p, NULL));
    hw.ports = 0u;
    CHECK(!esc_port_init(&g_p, &hw));
    hw.ports = LINK_PG_PORTS_MAX + 1u;
    CHECK(!esc_port_init(&g_p, &hw));
    hw = (esc_port_hw_t){ .ports = 1u };
    CHECK(!esc_port_init(&g_p, &hw));
    rig_up();
    hw = g_p.hw;
    hw.others_off = NULL;
    CHECK(!esc_port_init(&g_p, &hw));
    CHECK_EQ(esc_port_state(NULL), LINK_ESC_FREE);
    CHECK_EQ(esc_port_quiet_mask(NULL), 0u);
    CHECK_EQ(esc_port_pin_mask(NULL), 0u);
    CHECK_EQ(esc_port_may_bind(NULL, 0xA5u), 0xA5u);
    CHECK_EQ(esc_port_pins(NULL), 0u);
    CHECK(!esc_port_supply_hold(NULL));
    CHECK(!esc_port_writing(NULL));
    esc_port_bench_armed(NULL, true);
    esc_port_other(NULL, 1u);
    esc_port_step(NULL, true, false, 0u);
    CHECK_EQ(esc_port_write(NULL, 0u, 4u, g_st, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(esc_port_data_write(NULL, 0u, 1u, g_st), LINK_NACK_BAD_RANGE);
}

TEST_CASE(a_slot_goes_to_the_line_and_comes_back)
{
    up_am_rig();
    stage_defaults(2u);
    CHECK_EQ(stage(), 0u);
    pass();
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NONE);
    /* From the frame on the bank is held, the pin not yet taken. */
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_STOPPING);
    CHECK_EQ(esc_port_quiet_mask(&g_p), ESC_PORT_QUIET_SLOT);
    CHECK(esc_port_supply_hold(&g_p));
    CHECK_EQ(esc_port_may_bind(&g_p, 0xFFu), 0xFFu);
    CHECK_EQ(esc_port_pins(&g_p), 0u);
    CHECK_EQ(g.takes, 0);
    /* 50ms to the ms. */
    const uint32_t t0 = esc.now / 1000u;

    while (esc.now / 1000u < t0 + ESC_PORT_STOP_MS - 1u) {
        pass();
    }
    CHECK_EQ(g.takes, 0);
    run_ms(2u);
    CHECK_EQ(g.takes, 1);
    CHECK_EQ(g.last_pin, 5u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    /* Held: the slot may not bind, the pin is reserved; no port is named. */
    CHECK_EQ(esc_port_may_bind(&g_p, 0xFFu), (uint8_t)~(1u << 2));
    CHECK_EQ(esc_port_pins(&g_p), (uint64_t)1u << 5);
    CHECK_EQ(esc_port_pin_mask(&g_p), 0u);
    CHECK_EQ(reg(LINK_EW_LINE), (uint16_t)(LINK_ESC_LINE_PARK | (5u << 8)));
    CHECK_EQ(reg(LINK_EW_TARGET), 2u);
    /* CLOSE: back. */
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    CHECK_EQ(g.gives, 1);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(esc_port_quiet_mask(&g_p), 0u);
    CHECK_EQ(esc_port_may_bind(&g_p, 0xFFu), 0xFFu);
    CHECK_EQ(esc_port_pins(&g_p), 0u);
    CHECK(!esc_port_supply_hold(&g_p));
    CHECK_EQ(reg(LINK_EW_LINE) >> 8, ESC_PORT_NO_PIN);
    CHECK_EQ(reg(LINK_EW_TARGET), 0u);
    /* The supply was never touched by a session with no ENTER. */
    CHECK_EQ(g.ons + g.offs, 0);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
}

TEST_CASE(an_output_that_does_not_take_its_pin_back_keeps_the_hold)
{
    up_am_rig();
    open_target(2u);
    g.give_refused = true;
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NONE);
    CHECK_EQ(result(), LINK_ESC_RESULT_NOT_BACK);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_NOT_BACK);
    CHECK_EQ(g.gives, 1);
    /* The bank stays down, the target stays named, the rest of the page refuses;
     * the line is closed and any apply may bind it. */
    CHECK_EQ(esc_port_quiet_mask(&g_p), ESC_PORT_QUIET_SLOT);
    CHECK(esc_port_supply_hold(&g_p));
    CHECK_EQ(reg(LINK_EW_TARGET), 2u);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK_EQ(esc_port_may_bind(&g_p, 0xFFu), 0xFFu);
    CHECK_EQ(esc_port_pins(&g_p), 0u);
    (void)cmd(LINK_ESC_OP_ENTER);
    CHECK_EQ(refusal(), LINK_ESC_REF_BUSY);
    (void)cmd(LINK_ESC_OP_OPEN);
    CHECK_EQ(refusal(), LINK_ESC_REF_BUSY);
    (void)cmd(LINK_ESC_OP_CLEAR);
    CHECK_EQ(refusal(), LINK_ESC_REF_BUSY);
    /* A stop, an abort, a lost link change nothing. */
    pass_as(true, true);
    (void)cmd(LINK_ESC_OP_ABORT);
    pass_as(false, false);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_NOT_BACK);
    pass();
    /* CLOSE tries again and keeps it until the output takes the pin. */
    const uint16_t f[LINK_EW_W_CMD_FRAME] = {
        LINK_ESC_OP_CLOSE, (uint16_t)(reg(LINK_EW_SEQ) + 1u), 0u, 0u };

    CHECK_EQ(esc_port_write(&g_p, 0u, LINK_EW_W_CMD_FRAME, f, 0u), 0u);
    CHECK_EQ(g.gives, 2);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_NOT_BACK);
    g.give_refused = false;
    CHECK_EQ(cmd_full(LINK_ESC_OP_CLOSE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(g.gives, 3);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK_EQ(esc_port_quiet_mask(&g_p), 0u);
    CHECK_EQ(reg(LINK_EW_TARGET), 0u);
    /* A line with no resources whose output then refuses the pin: the same. */
    g.take_ok = false;
    g.give_refused = true;
    open_target(2u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_NOT_BACK);
    CHECK_EQ(result(), LINK_ESC_RESULT_NOT_BACK);
    /* An IOmcu restart clears it. */
    const esc_port_hw_t hw = g_p.hw;

    CHECK(esc_port_init(&g_p, &hw));
    CHECK_EQ(esc_port_quiet_mask(&g_p), 0u);
}

TEST_CASE(a_port_goes_to_the_line_and_comes_back)
{
    up_am_rig();
    open_target(LINK_EW_TARGET_PORT);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    CHECK_EQ(g.last_pin, 13u);
    CHECK_EQ(esc_port_quiet_mask(&g_p), 1u);
    CHECK_EQ(esc_port_pin_mask(&g_p), 1u);
    /* A port's session leaves every slot to bind. */
    CHECK_EQ(esc_port_may_bind(&g_p, 0xFFu), 0xFFu);
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    CHECK_EQ(g.gives, 1);
    CHECK_EQ(esc_port_pin_mask(&g_p), 0u);
    /* A port other than 0 has its own bits. */
    g.pin[1][3] = 14u;
    open_target(LINK_EW_TARGET_PORT | 3u);
    CHECK_EQ(esc_port_quiet_mask(&g_p), 8u);
    CHECK_EQ(esc_port_pin_mask(&g_p), 8u);
}

TEST_CASE(a_line_with_no_resources_leaves_the_output_free)
{
    up_am_rig();
    g.take_ok = false;
    open_target(2u);
    CHECK_EQ(g.takes, 1);
    CHECK_EQ(g.gives, 1);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(result(), LINK_ESC_RESULT_NO_LINE);
    CHECK_EQ(esc_port_quiet_mask(&g_p), 0u);
    CHECK_EQ(esc_port_pins(&g_p), 0u);
}

TEST_CASE(every_refusal_of_an_open)
{
    up_am_rig();
    stage_defaults(2u);
    CHECK_EQ(stage(), 0u);
    /* Before the first pass nothing says the link is safe. */
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_UNSAFE);
    pass();
    esc_port_other(&g_p, 1u);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    /* The frame repeated: acknowledged, nothing done. */
    CHECK_EQ(refusal(), LINK_ESC_REF_HELD);
    esc_port_other(&g_p, 0u);
    esc_port_bench_armed(&g_p, true);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_ARMED);
    esc_port_bench_armed(&g_p, false);
    CHECK_EQ(cmd_full(LINK_ESC_OP_OPEN, 0u, true), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_STAGED);
    /* A slot past the last, a port past the last, a stray bit. */
    ST_REG(LINK_EW_W_TARGET) = OUT_MAX_SLOTS;
    CHECK_EQ(stage(), 0u);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_TARGET);
    ST_REG(LINK_EW_W_TARGET) = LINK_EW_TARGET_PORT | 4u;
    CHECK_EQ(stage(), 0u);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_TARGET);
    ST_REG(LINK_EW_W_TARGET) = 0x0202u;
    CHECK_EQ(stage(), 0u);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_TARGET);
    /* The last ones, with no pin. */
    ST_REG(LINK_EW_W_TARGET) = OUT_MAX_SLOTS - 1u;
    CHECK_EQ(stage(), 0u);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NO_PIN);
    ST_REG(LINK_EW_W_TARGET) = LINK_EW_TARGET_PORT | 3u;
    CHECK_EQ(stage(), 0u);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NO_PIN);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    /* And a second OPEN on a held one. */
    open_target(2u);
    CHECK_EQ(cmd(LINK_ESC_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_BUSY);
}

TEST_CASE(a_frame_or_window_the_page_does_not_take)
{
    uint16_t f[LINK_EW_W_CMD_FRAME] = { LINK_ESC_OP_ABORT, 1u, 0u, 0u };

    up_am_rig();
    CHECK_EQ(esc_port_write(&g_p, 0u, 3u, f, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(esc_port_write(&g_p, 1u, 4u, f, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(esc_port_write(&g_p, 0u, 0u, f, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(esc_port_write(&g_p, LINK_EW_COUNT - 1u, 2u, f, 0u),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(esc_port_write(&g_p, LINK_EW_W_COUNT, 1u, f, 0u),
             LINK_NACK_READ_ONLY);
    f[0] = LINK_ED_BLOCKS;
    CHECK_EQ(esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, f, 0u),
             LINK_NACK_BAD_VALUE);
    f[0] = LINK_ED_BLOCKS - 1u;
    CHECK_EQ(esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, f, 0u), 0u);
    CHECK_EQ(reg(LINK_EW_BLOCK), LINK_ED_BLOCKS - 1u);
    /* Bad commands. */
    f[0] = LINK_ESC_OP_NONE;
    CHECK_EQ(esc_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    f[0] = LINK_ESC_OP_COUNT;
    CHECK_EQ(esc_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    f[0] = LINK_ESC_OP_ABORT;
    f[2] = 1u;
    CHECK_EQ(esc_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    f[2] = 0u;
    f[1] = 7u;
    CHECK_EQ(esc_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_EW_SEQ), 0u);
    /* The next SEQ, then the same frame again: one operation. */
    f[1] = 1u;
    CHECK_EQ(esc_port_write(&g_p, 0u, 4u, f, 0u), 0u);
    CHECK_EQ(reg(LINK_EW_SEQ), 1u);
    CHECK_EQ(esc_port_write(&g_p, 0u, 4u, f, 0u), 0u);
    CHECK_EQ(reg(LINK_EW_SEQ), 1u);
    /* A read past the page leaves the buffer alone. */
    uint16_t v = 0x1234u;

    esc_port_read(&g_p, LINK_EW_COUNT, 1u, &v);
    CHECK_EQ(v, 0x1234u);
    esc_port_read(NULL, 0u, 1u, &v);
    esc_port_read(&g_p, 0u, 1u, NULL);
    CHECK_EQ(v, 0x1234u);
}

TEST_CASE(every_refusal_of_an_enter)
{
    up_am_rig();
    stage_defaults(2u);
    (void)stage();
    pass();
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NOT_OPEN);
    open_target(2u);
    /* The limits, one past each. */
    static const struct { unsigned reg; uint16_t bad; uint16_t good; } k[] = {
        { LINK_EW_W_POWER, LINK_ESC_POWER_COUNT, LINK_ESC_SWITCHED },
        { LINK_EW_W_MV, LINK_ESC_SUPPLY_MIN_MV - 1u, 7400u },
        { LINK_EW_W_MV, PDMINI_V_MAX_MV + 1u, 7400u },
        { LINK_EW_W_MA, PDMINI_I_MIN_MA - 1u, 1000u },
        { LINK_EW_W_MA, PDMINI_I_MAX_MA + 1u, 1000u },
        { LINK_EW_W_OFF_MS, LINK_ESC_OFF_MS_MAX + 1u, 1000u },
        { LINK_EW_W_HOLD_LO, EW_HOLD_MIN_MS - 1u, 3000u },
    };

    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        ST_REG(k[i].reg) = k[i].bad;
        (void)stage();
        CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
        CHECK_EQ(refusal(), LINK_ESC_REF_RANGE);
        ST_REG(k[i].reg) = k[i].good;
    }
    /* The hold's high word: 120000ms and one past. */
    ST_REG(LINK_EW_W_HOLD_LO) = (uint16_t)((EW_HOLD_MAX_MS + 1u) & 0xFFFFu);
    ST_REG(LINK_EW_W_HOLD_HI) = (uint16_t)((EW_HOLD_MAX_MS + 1u) >> 16);
    (void)stage();
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_RANGE);
    ST_REG(LINK_EW_W_HOLD_LO) = 3000u;
    ST_REG(LINK_EW_W_HOLD_HI) = 0u;
    /* 700mV under the input, and 10mV more. */
    g.v.vin_mv = 15000u;
    ST_REG(LINK_EW_W_MV) = 14310u;
    (void)stage();
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_RANGE);
    ST_REG(LINK_EW_W_MV) = 7400u;
    (void)stage();
    /* No PD mini. */
    g.v.online = false;
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NO_SUPPLY);
    CHECK(!flag(LINK_EW_F_RAIL));
    g.v.online = true;
    CHECK(flag(LINK_EW_F_RAIL));
    CHECK_EQ(cmd_full(LINK_ESC_OP_ENTER, 0u, true), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_STAGED);
    CHECK_EQ(g.ons + g.offs, 0);
    /* Unsafe. */
    pass_as(false, false);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_STOPPED);
    CHECK_EQ(cmd(LINK_ESC_OP_CLEAR), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_UNSAFE);
    pass();
    CHECK_EQ(cmd(LINK_ESC_OP_CLEAR), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NONE);
    CHECK(!flag(LINK_EW_F_STOPPED));
    /* A stale safe: the flag from the last pass, in the same batch of frames. */
    g_p.safe = false;
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_UNSAFE);
    pass();
    /* A second ENTER while one runs. */
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_POWER_OFF);
    CHECK_EQ(cmd_full(LINK_ESC_OP_ENTER, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_BUSY);
    CHECK_EQ(cmd(LINK_ESC_OP_CLEAR), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_BUSY);
    /* CLEAR on a free page. */
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    CHECK_EQ(cmd_full(LINK_ESC_OP_CLEAR, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NOT_OPEN);
    CHECK_EQ(cmd_full(LINK_ESC_OP_CLOSE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NOT_OPEN);
}

TEST_CASE(a_switched_entry_in_its_order)
{
    up_am_rig();
    open_target(2u);
    ST_REG(LINK_EW_W_OFF_MS) = 400u;
    ST_REG(LINK_EW_W_HOLD_LO) = 700u;
    (void)stage();
    uint32_t t0 = esc.now;

    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(g.offs, 1);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_POWER_OFF);
    /* The line stays parked: nothing feeds an ESC without its power. */
    CHECK_EQ(line_word(), LINK_ESC_LINE_PARK);
    /* 250ms at least off, then the time off. */
    run_while(LINK_ESC_POWER_OFF, 5000u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_LEAD);
    const uint32_t off_us = esc.now - t0;

    CHECK(off_us >= (ESC_PORT_READ_OFF_MIN_MS + 400u) * 1000u);
    CHECK(off_us <= (ESC_PORT_READ_OFF_MIN_MS + 400u + 2u) * 1000u);
    CHECK_EQ(esc.line, EW_LINE_HIGH);
    CHECK_EQ(line_word(), LINK_ESC_LINE_HIGH);
    CHECK_EQ(g.ons, 0);
    t0 = esc.now;
    run_while(LINK_ESC_LEAD, 200u);
    CHECK_EQ(g.ons, 1);
    /* The line was driven high 50ms before ON. */
    CHECK_EQ(g.line_at_on, EW_LINE_HIGH);
    CHECK(g.on_at - t0 >= (ESC_PORT_LEAD_MS - 1u) * 1000u);
    CHECK(g.on_at - t0 <= (ESC_PORT_LEAD_MS + 1u) * 1000u);
    CHECK_EQ(g.on_mv, 7400u);
    CHECK_EQ(g.on_ma, 1000u);
    CHECK(flag(LINK_EW_F_SUPPLY));
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    /* No frame before the hold is over. */
    CHECK_EQ(esc.starts, 0);
    const uint32_t on = esc.now;

    while (esc.starts == 0 && esc.now - on < 2000000u) {
        pass();
    }
    CHECK(esc.first_tx_at - on >= 699000u);
    CHECK_EQ(line_word(), LINK_ESC_LINE_SEND);
    run_while(LINK_ESC_RUN, 5000u);
    CHECK_EQ(line_word(), LINK_ESC_LINE_UP);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK(flag(LINK_EW_F_IN_MODE));
    CHECK_EQ(reg(LINK_EW_ID) & 0xFFu, EW_FW_AM);
    CHECK_EQ(reg(LINK_EW_LEN), EW_MAP_LEN_AM);
    CHECK_EQ(reg(LINK_EW_CONTACT) & 0xFFu, esc.answer[3]);
    CHECK_EQ(reg(LINK_EW_PROTOCOL), 2u);
    CHECK(flag(LINK_EW_F_WRITABLE));
}

TEST_CASE(the_hold_counts_from_the_supply_reading_on)
{
    up_am_rig();
    open_target(2u);
    ST_REG(LINK_EW_W_OFF_MS) = 0u;
    ST_REG(LINK_EW_W_HOLD_LO) = 500u;
    (void)stage();
    g.on_lag_ms = 800u;
    (void)cmd(LINK_ESC_OP_ENTER);
    run_while(LINK_ESC_POWER_OFF, 5000u);
    run_while(LINK_ESC_LEAD, 200u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_POWER_ON);
    CHECK_EQ(line_word(), LINK_ESC_LINE_HIGH);
    const uint32_t ask = g.on_at;

    while (esc.starts == 0 && esc.now - ask < 5000000u) {
        pass();
    }
    /* 800ms to read on, 500ms held after that. */
    CHECK(esc.first_tx_at - ask >= 1299000u);
    CHECK(esc.first_tx_at - ask <= 1302000u);
    /* Read off from then on is a loss at once. */
    g.on_lag_ms = 0u;
    g.never_on = true;
    g.v.off = true;
    pass();
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(result(), LINK_ESC_RESULT_SUPPLY);
    /* Never reads on: 3000ms, and no frame. */
    up_am_rig();
    open_target(2u);
    ST_REG(LINK_EW_W_OFF_MS) = 0u;
    (void)stage();
    g.never_on = true;
    (void)cmd(LINK_ESC_OP_ENTER);
    run_while(LINK_ESC_POWER_OFF, 5000u);
    run_while(LINK_ESC_LEAD, 200u);
    const uint32_t t0 = g.on_at;

    run_while(LINK_ESC_POWER_ON, 5000u);
    CHECK(esc.now - t0 >= (ESC_PORT_READ_ON_MAX_MS - 1u) * 1000u);
    CHECK(esc.now - t0 <= (ESC_PORT_READ_ON_MAX_MS + 1u) * 1000u);
    CHECK_EQ(result(), LINK_ESC_RESULT_SUPPLY);
    CHECK_EQ(esc.starts, 0);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK(!g.v.asked_on);
    /* The panel's OFF while it waits. */
    up_am_rig();
    open_target(2u);
    ST_REG(LINK_EW_W_OFF_MS) = 0u;
    (void)stage();
    g.never_on = true;
    (void)cmd(LINK_ESC_OP_ENTER);
    run_while(LINK_ESC_POWER_OFF, 5000u);
    run_while(LINK_ESC_LEAD, 200u);
    g.v.asked_on = false;
    pass();
    CHECK_EQ(result(), LINK_ESC_RESULT_SUPPLY);
}

TEST_CASE(a_supply_that_will_not_read_off_or_refuses_on)
{
    up_am_rig();
    open_target(2u);
    g.stuck_on = true;
    (void)cmd(LINK_ESC_OP_ENTER);
    const uint32_t t0 = esc.now;

    run_while(LINK_ESC_POWER_OFF, 5000u);
    CHECK(esc.now - t0 >= (ESC_PORT_READ_OFF_MAX_MS - 1u) * 1000u);
    CHECK(esc.now - t0 <= (ESC_PORT_READ_OFF_MAX_MS + 1u) * 1000u);
    CHECK_EQ(result(), LINK_ESC_RESULT_SUPPLY);
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(esc.starts, 0);
    CHECK_EQ(g.ons, 0);
    CHECK_EQ(esc.line, EW_LINE_PARK);
    /* The ON refused: the line it raised goes back at once. */
    up_am_rig();
    open_target(2u);
    g.refuse_on = true;
    enter_now();
    CHECK_EQ(result(), LINK_ESC_RESULT_SUPPLY);
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK_EQ(esc.starts, 0);
}

TEST_CASE(by_hand_waits_for_done_and_for_no_longer)
{
    up_am_rig();
    open_target(2u);
    ST_REG(LINK_EW_W_POWER) = LINK_ESC_BY_HAND;
    /* Voltage and limit are not looked at, nor a PD mini. */
    ST_REG(LINK_EW_W_MV) = 0u;
    ST_REG(LINK_EW_W_MA) = 0u;
    ST_REG(LINK_EW_W_HOLD_LO) = 500u;
    (void)stage();
    g.v.online = false;
    CHECK_EQ(cmd(LINK_ESC_OP_DONE), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NOT_OPEN);
    CHECK_EQ(cmd(LINK_ESC_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_NONE);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_HAND_WAIT);
    CHECK_EQ(esc.line, EW_LINE_HIGH);
    CHECK_EQ(g.ons + g.offs, 0);
    run_ms(100u);
    g_p.safe = false;
    CHECK_EQ(cmd_full(LINK_ESC_OP_DONE, LINK_ESC_WRITE_KEY, false), 0u);
    CHECK_EQ(refusal(), LINK_ESC_REF_UNSAFE);
    pass();
    CHECK_EQ(cmd(LINK_ESC_OP_DONE), 0u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    run_while(LINK_ESC_RUN, 5000u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK(!flag(LINK_EW_F_SUPPLY));
    /* The supply never counted: READY does not watch it. */
    pass();
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    /* No DONE in 60s. */
    (void)cmd(LINK_ESC_OP_CLOSE);
    open_target(2u);
    ST_REG(LINK_EW_W_POWER) = LINK_ESC_BY_HAND;
    (void)stage();
    (void)cmd(LINK_ESC_OP_ENTER);
    const uint32_t t0 = esc.now;

    run_while(LINK_ESC_HAND_WAIT, 70000u);
    CHECK(esc.now - t0 >= (ESC_PORT_HAND_MS - 1u) * 1000u);
    CHECK(esc.now - t0 <= (ESC_PORT_HAND_MS + 1u) * 1000u);
    CHECK_EQ(result(), LINK_ESC_RESULT_TIMEOUT);
    CHECK(flag(LINK_EW_F_FAULT));
    CHECK_EQ(esc.line, EW_LINE_PARK);
}

TEST_CASE(a_leave_with_the_supply_after_it)
{
    up_am_rig();
    ready_on(2u);
    read_now();
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    const int offs = g.offs;

    CHECK_EQ(cmd(LINK_ESC_OP_LEAVE), 0u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    CHECK(g.v.asked_on);
    /* Line low 300ms; the supply stays till then. */
    run_ms(200u);
    CHECK_EQ(esc.line, EW_LINE_LOW);
    CHECK_EQ(line_word(), LINK_ESC_LINE_LOW);
    CHECK(g.v.asked_on);
    run_while(LINK_ESC_RUN, 500u);
    CHECK_EQ(g.offs, offs + 1);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    CHECK_EQ(result(), LINK_ESC_RESULT_DONE);
    CHECK(!flag(LINK_EW_F_FAULT));
    CHECK_EQ(esc.line, EW_LINE_PARK);
    CHECK_EQ(cmd(LINK_ESC_OP_CLOSE), 0u);
    CHECK_EQ(g.gives, 1);
    /* A READ on a page with no ESC. */
    (void)cmd_full(LINK_ESC_OP_READ, LINK_ESC_WRITE_KEY, false);
    CHECK_EQ(refusal(), LINK_ESC_REF_NOT_OPEN);
}

TEST_CASE(a_slot_pin_is_a_bound_throttle)
{
    outputs_t o;

    memset(&o, 0, sizeof(o));
    o.slot[1].driver = OUT_DRIVER_DSHOT;
    o.slot[1].first_channel = 3u;
    o.slot[1].pin = 9u;
    o.channel[3].role = OUT_ROLE_THROTTLE;
    CHECK_EQ(esc_port_slot_pin(&o, 1u, 0x02u), 9u);
    /* PPM counts: the rule is the role, not the driver. */
    o.slot[1].driver = OUT_DRIVER_PPM;
    CHECK_EQ(esc_port_slot_pin(&o, 1u, 0x02u), 9u);
    CHECK_EQ(esc_port_slot_pin(&o, 1u, 0x01u), ESC_PORT_NO_PIN);
    CHECK_EQ(esc_port_slot_pin(&o, OUT_MAX_SLOTS, 0xFFu), ESC_PORT_NO_PIN);
    CHECK_EQ(esc_port_slot_pin(NULL, 1u, 0x02u), ESC_PORT_NO_PIN);
    o.channel[3].role = OUT_ROLE_SURFACE;
    CHECK_EQ(esc_port_slot_pin(&o, 1u, 0x02u), ESC_PORT_NO_PIN);
    o.channel[3].role = OUT_ROLE_THROTTLE;
    o.slot[1].driver = OUT_DRIVER_NONE;
    CHECK_EQ(esc_port_slot_pin(&o, 1u, 0x02u), ESC_PORT_NO_PIN);
    o.slot[1].driver = OUT_DRIVER_DSHOT;
    o.slot[1].first_channel = OUT_MAX_CHANNELS;
    CHECK_EQ(esc_port_slot_pin(&o, 1u, 0x02u), ESC_PORT_NO_PIN);
}

int main(void)
{
    RUN(init_needs_every_hook);
    RUN(a_slot_goes_to_the_line_and_comes_back);
    RUN(an_output_that_does_not_take_its_pin_back_keeps_the_hold);
    RUN(a_port_goes_to_the_line_and_comes_back);
    RUN(a_line_with_no_resources_leaves_the_output_free);
    RUN(every_refusal_of_an_open);
    RUN(a_frame_or_window_the_page_does_not_take);
    RUN(every_refusal_of_an_enter);
    RUN(a_switched_entry_in_its_order);
    RUN(the_hold_counts_from_the_supply_reading_on);
    RUN(a_supply_that_will_not_read_off_or_refuses_on);
    RUN(by_hand_waits_for_done_and_for_no_longer);
    RUN(a_leave_with_the_supply_after_it);
    RUN(a_slot_pin_is_a_bound_throttle);
    return test_summary("esc_port");
}
