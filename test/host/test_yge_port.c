/*
 * The YGE_PROG and YGE_DATA pages, shared/outputs/yge_port.c: the port's way to
 * the line and back, every refusal of OPEN and ENTER, the switched entry in its
 * order, BY HAND at 60000ms and 60001ms, a read, a write, LEAVE, and the frames
 * and windows the pages do not take.  On the simulated ESC of oy_sim.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_port_rig.h"

TEST_CASE(init_needs_every_hook)
{
    rig_up();
    const yge_port_hw_t good = g_p.hw;
    yge_port_hw_t hw;

    CHECK(!yge_port_init(NULL, &good));
    CHECK(!yge_port_init(&g_p, NULL));
    for (int i = 0; i < 12; ++i) {
        hw = good;
        switch (i) {
        case 0: hw.ports = 0u; break;
        case 1: hw.ports = LINK_PG_PORTS_MAX + 1u; break;
        case 2: hw.pin = NULL; break;
        case 3: hw.take = NULL; break;
        case 4: hw.give = NULL; break;
        case 5: hw.line = NULL; break;
        case 6: hw.supply = NULL; break;
        case 7: hw.supply_set = NULL; break;
        case 8: hw.others_off = NULL; break;
        case 9: hw.driver.tx = NULL; break;
        case 10: hw.driver.rx = NULL; break;
        default: hw.driver.now_us = NULL; break;
        }
        CHECK(!yge_port_init(&g_p, &hw));
        CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
        CHECK_EQ(yge_port_quiet_mask(&g_p), 0u);
    }
    CHECK(yge_port_init(&g_p, &good));
    CHECK_EQ(reg(LINK_YP_PORT), YGE_PORT_NO_PIN);
    CHECK_EQ(reg(LINK_YP_LINE), (uint16_t)(YGE_PORT_NO_PIN << 8));
    /* NULL is a free page everywhere. */
    CHECK_EQ(yge_port_state(NULL), LINK_YGE_FREE);
    CHECK_EQ(yge_port_quiet_mask(NULL), 0u);
    CHECK_EQ(yge_port_pin_mask(NULL), 0u);
    CHECK(!yge_port_supply_hold(NULL));
    CHECK(!yge_port_writing(NULL));
    yge_port_step(NULL, true, false, 0u);
    yge_port_bench_armed(NULL, true);
    yge_port_other(NULL, 0xFFu);
}

TEST_CASE(a_port_goes_to_the_line_and_comes_back)
{
    rig_up();
    stage_defaults(2u);
    CHECK_EQ(stage(), 0u);
    pass();
    CHECK_EQ(cmd(LINK_YGE_OP_OPEN), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    /* From the OPEN's frame: the port is held and renders nothing. */
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_STOPPING);
    CHECK_EQ(yge_port_quiet_mask(&g_p), 1u << 2);
    CHECK(yge_port_supply_hold(&g_p));
    CHECK_EQ(yge_port_pin_mask(&g_p), 0u);
    CHECK_EQ(reg(LINK_YP_PORT), 2u);
    /* STOPPING: 49ms is not enough, 50ms is. */
    run_ms(YGE_PORT_STOP_MS - 1u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_STOPPING);
    CHECK_EQ(g.takes, 0);
    pass();
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
    CHECK_EQ(g.takes, 1);
    CHECK_EQ(g.last_port, 2u);
    CHECK_EQ(g.last_pin, 15u);
    CHECK_EQ(result(), LINK_YGE_RESULT_DONE);
    CHECK_EQ(yge_port_pin_mask(&g_p), 1u << 2);
    /* The active output: the GPIO on the page, the line parked. */
    CHECK_EQ(reg(LINK_YP_LINE), (uint16_t)(15u << 8 | LINK_YGE_LINE_PARK));
    CHECK(!g.listen);
    CHECK_EQ(g.ons, 0);
    /* And back. */
    CHECK_EQ(cmd(LINK_YGE_OP_CLOSE), 0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(g.gives, 1);
    CHECK_EQ(result(), LINK_YGE_RESULT_DONE);
    CHECK_EQ(yge_port_quiet_mask(&g_p), 0u);
    CHECK_EQ(yge_port_pin_mask(&g_p), 0u);
    CHECK(!yge_port_supply_hold(&g_p));
    CHECK_EQ(reg(LINK_YP_PORT), YGE_PORT_NO_PIN);
    CHECK_EQ(reg(LINK_YP_LINE) >> 8, YGE_PORT_NO_PIN);
    /* A CLOSE of a free page is refused. */
    CHECK_EQ(cmd(LINK_YGE_OP_CLOSE), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NOT_OPEN);
}

TEST_CASE(a_port_that_does_not_take_its_pin_back_keeps_the_hold)
{
    rig_up();
    open_port(0u);
    g.give_refused = true;
    CHECK_EQ(cmd(LINK_YGE_OP_CLOSE), 0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_NOT_BACK);
    CHECK_EQ(result(), LINK_YGE_RESULT_NOT_BACK);
    CHECK_EQ(yge_port_quiet_mask(&g_p), 1u);
    CHECK(flag(LINK_YP_F_STOPPED));
    /* Nothing but a CLOSE moves it; that one tries again. */
    (void)cmd(LINK_YGE_OP_ENTER);
    CHECK_EQ(refusal(), LINK_YGE_REF_BUSY);
    g.give_refused = false;
    CHECK_EQ(cmd_full(LINK_YGE_OP_CLOSE, LINK_YGE_KEY, false), 0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(g.gives, 2);
}

TEST_CASE(a_line_with_no_resources_leaves_the_port_free)
{
    rig_up();
    g.take_ok = false;
    open_port(0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(result(), LINK_YGE_RESULT_NO_LINE);
    CHECK_EQ(g.gives, 1);
    CHECK_EQ(yge_port_quiet_mask(&g_p), 0u);
}

/* OPEN of @p port as staged, from a free page; its refusal. */
static unsigned open_refusal(uint16_t port)
{
    stage_defaults(port);
    (void)stage();
    (void)cmd(LINK_YGE_OP_OPEN);
    return refusal();
}

TEST_CASE(every_refusal_of_an_open)
{
    rig_up();
    pass();
    /* Armed or asked to arm. */
    yge_port_bench_armed(&g_p, true);
    CHECK_EQ(open_refusal(0u), LINK_YGE_REF_ARMED);
    yge_port_bench_armed(&g_p, false);
    /* KST, HITEC, ESC_WIRE, an attach or an update: every port. */
    const uint8_t all[] = { YGE_PORT_OTHER_SERVO, YGE_PORT_OTHER_ESC,
                            YGE_PORT_OTHER_ATTACH, YGE_PORT_OTHER_UPDATE };

    for (size_t i = 0u; i < sizeof(all); ++i) {
        yge_port_other(&g_p, all[i]);
        CHECK_EQ(open_refusal(0u), LINK_YGE_REF_HELD);
        CHECK_EQ(open_refusal(2u), LINK_YGE_REF_HELD);
    }
    /* The node's rail on port 2: that port only. */
    yge_port_other(&g_p, 1u << 2);
    CHECK_EQ(open_refusal(2u), LINK_YGE_REF_HELD);
    yge_port_other(&g_p, 0u);
    /* Not safe. */
    pass_as(false, false);
    CHECK_EQ(open_refusal(0u), LINK_YGE_REF_UNSAFE);
    pass();
    /* The CRC. */
    stage_defaults(0u);
    (void)stage();
    (void)cmd_full(LINK_YGE_OP_OPEN, 0u, true);
    CHECK_EQ(refusal(), LINK_YGE_REF_STAGED);
    /* No such port, a port with no pin. */
    CHECK_EQ(open_refusal(4u), LINK_YGE_REF_TARGET);
    CHECK_EQ(open_refusal(0x0100u), LINK_YGE_REF_TARGET);
    CHECK_EQ(open_refusal(1u), LINK_YGE_REF_NO_PIN);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(yge_port_quiet_mask(&g_p), 0u);
    /* Nothing was taken by any of them. */
    CHECK_EQ(g.takes, 0);
    /* Held: a second OPEN is BUSY. */
    yge_port_other(&g_p, 1u << 2);
    CHECK_EQ(open_refusal(0u), LINK_YGE_REF_NONE);
    CHECK_EQ(open_refusal(0u), LINK_YGE_REF_BUSY);
}

TEST_CASE(the_mask_of_what_else_holds_the_bench)
{
    CHECK_EQ(yge_port_others(0u, false, false, false, 0u), 0u);
    CHECK_EQ(yge_port_others(1u << 3, false, false, false, 0u),
             YGE_PORT_OTHER_SERVO);
    CHECK_EQ(yge_port_others(0u, true, false, false, 0u), YGE_PORT_OTHER_ESC);
    CHECK_EQ(yge_port_others(0u, false, true, false, 0u), YGE_PORT_OTHER_ATTACH);
    CHECK_EQ(yge_port_others(0u, false, false, true, 0u), YGE_PORT_OTHER_UPDATE);
    /* The rail's bits are per port, and no rail bit reaches the others. */
    CHECK_EQ(yge_port_others(0u, false, false, false, 0x05u), 0x05u);
    CHECK_EQ(yge_port_others(0u, false, false, false, 0xF0u), 0u);
}

/* An open page and ENTER as staged; its refusal. */
static unsigned enter_refusal(void)
{
    (void)stage();
    (void)cmd(LINK_YGE_OP_ENTER);
    return refusal();
}

TEST_CASE(every_refusal_of_an_enter)
{
    rig_up();
    pass();
    stage_defaults(0u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_NOT_OPEN);
    open_port(0u);
    /* No key: the panel's hold did not end. */
    (void)cmd_full(LINK_YGE_OP_ENTER, 0u, false);
    CHECK_EQ(refusal(), LINK_YGE_REF_NO_KEY);
    (void)cmd_full(LINK_YGE_OP_ENTER, LINK_YGE_KEY, true);
    CHECK_EQ(refusal(), LINK_YGE_REF_STAGED);
    /* Values. */
    ST_REG(LINK_YP_W_POWER) = LINK_YGE_POWER_COUNT;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    stage_defaults(0u);
    ST_REG(LINK_YP_W_BOOT_MS) = (uint16_t)(OY_BOOT_MS_MIN - 1u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    ST_REG(LINK_YP_W_BOOT_MS) = (uint16_t)(OY_BOOT_MS_MAX + 1u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    /* BY HAND judges the boot wait too. */
    ST_REG(LINK_YP_W_POWER) = LINK_YGE_BY_HAND;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    stage_defaults(0u);
    ST_REG(LINK_YP_W_MV) = (uint16_t)(LINK_YGE_SUPPLY_MIN_MV - 1u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    ST_REG(LINK_YP_W_MV) = (uint16_t)(PDMINI_V_MAX_MV + 1u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    stage_defaults(0u);
    ST_REG(LINK_YP_W_MA) = (uint16_t)(PDMINI_I_MIN_MA - 1u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    ST_REG(LINK_YP_W_MA) = (uint16_t)(PDMINI_I_MAX_MA + 1u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    /* The board's ceiling and the input's cap. */
    stage_defaults(0u);
    g.v.max_mv = 7399u;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    g.v.max_mv = 0u;
    g.v.vin_mv = (uint16_t)(7400u + PDMINI_CAP_MV - 1u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_RANGE);
    g.v.vin_mv = 0u;
    /* The supply: none, or its OFF latched. */
    g.v.online = false;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_NO_SUPPLY);
    g.v.online = true;
    g.v.latched = true;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_SUPPLY_OFF);
    /* BY HAND needs no supply at all. */
    ST_REG(LINK_YP_W_POWER) = LINK_YGE_BY_HAND;
    g.v.online = false;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_HAND_WAIT);
    (void)cmd(LINK_YGE_OP_ABORT);
    g.v.online = true;
    g.v.latched = false;
    /* A stop stands: CLEAR first. */
    CHECK(!flag(LINK_YP_F_STOPPED));
    (void)cmd(LINK_YGE_OP_STOP);
    stage_defaults(0u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_STOPPED);
    (void)cmd(LINK_YGE_OP_CLEAR);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    /* Not safe. */
    pass_as(false, false);
    (void)cmd(LINK_YGE_OP_CLEAR);
    CHECK_EQ(refusal(), LINK_YGE_REF_UNSAFE);
    pass();
    (void)cmd(LINK_YGE_OP_CLEAR);
    g_p.safe = false;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_UNSAFE);
    pass();
    /* None of them switched anything on. */
    CHECK_EQ(g.ons, 0);
    /* The limits taken: 1000ms, 20000ms, 3000mV. */
    ST_REG(LINK_YP_W_MV) = (uint16_t)LINK_YGE_SUPPLY_MIN_MV;
    ST_REG(LINK_YP_W_BOOT_MS) = (uint16_t)OY_BOOT_MS_MAX;
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_NONE);
    (void)cmd(LINK_YGE_OP_ABORT);
    (void)cmd(LINK_YGE_OP_CLEAR);
    stage_defaults(0u);
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_NONE);
    /* Held in an entry: BUSY. */
    CHECK_EQ(enter_refusal(), LINK_YGE_REF_BUSY);
}

TEST_CASE(a_switched_entry_in_its_order)
{
    rig_up();
    g.v.asked_on = true;    /* the panel left the PD mini on */
    g.v.on = true;
    g.v.off = false;
    open_port(0u);
    CHECK_EQ(cmd(LINK_YGE_OP_ENTER), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_POWER_OFF);
    CHECK_EQ(g.offs, 1);
    CHECK_EQ(g.ons, 0);
    /* Off at least 250ms: 249ms is not enough. */
    run_ms(YGE_PORT_READ_OFF_MIN_MS - 1u);
    CHECK_EQ(g.ons, 0);
    pass();
    CHECK_EQ(g.ons, 1);
    CHECK_EQ(g.on_mv, 7400u);
    CHECK_EQ(g.on_ma, 1000u);
    CHECK(flag(LINK_YP_F_SUPPLY));
    /* Parked when ON is asked: nothing feeds the ESC through the line. */
    CHECK(!g.listen_at_on);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_POWER_ON);
    run_while(LINK_YGE_POWER_ON, 100u);
    /* It read on: the line listens, the boot wait runs. */
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    CHECK(g.listen);
    CHECK_EQ(line_word(), LINK_YGE_LINE_LISTEN);
    CHECK_EQ(g_p.ses.state, OY_ST_WAIT);
    run_while(LINK_YGE_RUN, 10000u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK_EQ(result(), LINK_YGE_RESULT_DONE);
    CHECK(flag(LINK_YP_F_TAKEN));
    CHECK(!flag(LINK_YP_F_BY_HAND));
    CHECK(!flag(LINK_YP_F_HAVE_READ));
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_OK);
    /* The boot wait: nothing sent before 1000ms after it read on. */
    CHECK(sim.n_sent > 0u);
    CHECK(sim.sent[0].t - g.on_at_us >= OY_BOOT_MS_MIN * MS);
    /* READY keeps the ESC with requests. */
    const unsigned n = sim.n_sent;

    run_ms(1000u);
    CHECK(sim.n_sent > n);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
}

TEST_CASE(the_boot_wait_counts_from_the_supply_reading_on)
{
    rig_up();
    g.on_lag_ms = 2000u;
    open_port(0u);
    (void)cmd(LINK_YGE_OP_ENTER);
    run_while(LINK_YGE_POWER_OFF, 1000u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_POWER_ON);
    run_ms(1990u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_POWER_ON);
    CHECK(!g.listen);
    run_while(LINK_YGE_POWER_ON, 100u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    /* 3000ms with no ON read: an error. */
    rig_up();
    g.never_on = true;
    open_port(0u);
    (void)cmd(LINK_YGE_OP_ENTER);
    run_while(LINK_YGE_POWER_OFF, 1000u);
    run_ms(YGE_PORT_READ_ON_MAX_MS - 1u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_POWER_ON);
    pass();
    CHECK_EQ(result(), LINK_YGE_RESULT_SUPPLY);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK_ALL_OFF();
}

TEST_CASE(a_supply_that_will_not_read_off_or_refuses_on)
{
    rig_up();
    g.v.asked_on = true;
    g.v.on = true;
    g.v.off = false;
    g.stuck_on = true;
    open_port(0u);
    (void)cmd(LINK_YGE_OP_ENTER);
    run_ms(YGE_PORT_READ_OFF_MAX_MS - 1u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_POWER_OFF);
    pass();
    CHECK_EQ(result(), LINK_YGE_RESULT_SUPPLY);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK_EQ(g.ons, 0);
    rig_up();
    g.refuse_on = true;
    open_port(0u);
    (void)cmd(LINK_YGE_OP_ENTER);
    run_while(LINK_YGE_POWER_OFF, 1000u);
    CHECK_EQ(result(), LINK_YGE_RESULT_SUPPLY);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK_ALL_OFF();
    CHECK_EQ(g.others_off, 1);
}

/* BY HAND, to HAND_WAIT. */
static void to_hand_wait(void)
{
    rig_up();
    open_port(0u);
    ST_REG(LINK_YP_W_POWER) = LINK_YGE_BY_HAND;
    (void)stage();
    CHECK_EQ(cmd(LINK_YGE_OP_ENTER), 0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_HAND_WAIT);
}

TEST_CASE(by_hand_at_60000ms_and_60001ms)
{
    /* HAND_DONE at 60000ms is taken. */
    to_hand_wait();
    CHECK(flag(LINK_YP_F_BY_HAND));
    run_ms(YGE_PORT_HAND_MS);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_HAND_WAIT);
    CHECK_EQ(reg(LINK_YP_T_MS), YGE_PORT_HAND_MS);
    /* No supply in all that time, the line parked. */
    CHECK_EQ(g.ons + g.offs, 0);
    CHECK(!g.listen);
    g.battery = true;
    CHECK_EQ(cmd(LINK_YGE_OP_HAND_DONE), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    CHECK(g.listen);
    run_while(LINK_YGE_RUN, 10000u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK_EQ(g.ons, 0);
    /* At 60001ms the wait is over: an error, nothing switched. */
    to_hand_wait();
    run_ms(YGE_PORT_HAND_MS + 1u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
    CHECK_EQ(result(), LINK_YGE_RESULT_TIMEOUT);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK_EQ(g.ons, 0);
    CHECK_EQ(g.others_off, 1);
    CHECK_ALL_OFF();
    (void)cmd(LINK_YGE_OP_HAND_DONE);
    CHECK_EQ(refusal(), LINK_YGE_REF_NOT_OPEN);
    /* HAND_DONE elsewhere and when not safe. */
    to_hand_wait();
    g_p.safe = false;
    (void)cmd(LINK_YGE_OP_HAND_DONE);
    CHECK_EQ(refusal(), LINK_YGE_REF_UNSAFE);
}

TEST_CASE(a_read_and_a_write)
{
    rig_up();
    ready_on(0u);
    /* Nothing to write before a read. */
    draft_of(21u, 13u);
    CHECK_EQ(write_table(g_draft), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_NO_TABLE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK(!flag(LINK_YP_F_FAULT));
    read_now();
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK_EQ(result(), LINK_YGE_RESULT_DONE);
    CHECK_EQ(reg(LINK_YP_ENTRIES), 32u);
    CHECK_EQ(reg(LINK_YP_TYPE), 5712u);
    CHECK_EQ((uint32_t)reg(LINK_YP_FW_LO) | ((uint32_t)reg(LINK_YP_FW_HI) << 16),
             SIM_FW);
    CHECK_EQ(reg(LINK_YP_SERIAL_LO), 0x1234u);
    CHECK_EQ(reg(LINK_YP_READS), 1u);
    CHECK(flag(LINK_YP_F_HAVE_READ));
    CHECK(flag(LINK_YP_F_WRITABLE));
    CHECK_EQ(reg(LINK_YP_PROGRESS), (uint16_t)(32u | 0xFF00u));
    /* The read copy through YGE_DATA. */
    uint16_t b[LINK_YD_COUNT];
    const uint16_t which = LINK_YD_READ;

    CHECK_EQ(yge_port_write(&g_p, LINK_YP_W_BLOCK, 1u, &which, now_ms()), 0u);
    yge_port_data_read(&g_p, 0u, LINK_YD_COUNT, b);
    CHECK_EQ(memcmp(b, sim.table, sizeof(b)), 0);
    /* A write of the pinion. */
    draft_of(21u, 13u);
    const uint16_t crc = reg(LINK_YP_READ_CRC);

    CHECK_EQ(write_table(g_draft), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    run_while(LINK_YGE_RUN, 30000u);
    CHECK_EQ(result(), LINK_YGE_RESULT_DONE);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_OK);
    CHECK_EQ(sim.table[21], 13u);
    CHECK_EQ(sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u), 1u);
    /* The write's read after is the read copy now. */
    CHECK(reg(LINK_YP_READS) > 1u);
    CHECK(reg(LINK_YP_READ_CRC) != crc);
    CHECK_EQ(g_p.ses.orig[21], 13u);
    CHECK(!flag(LINK_YP_F_FAULT));
    CHECK(flag(LINK_YP_F_SUPPLY));
    /* A draft equal to the table: done with no frame. */
    const unsigned n = sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u);

    draft_of(21u, 13u);
    CHECK_EQ(write_table(g_draft), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK_EQ(sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u), n);
}

TEST_CASE(every_refusal_of_a_write)
{
    rig_up();
    ready_on(0u);
    read_now();
    draft_of(21u, 14u);
    draft_put(g_draft);
    ST_REG(LINK_YP_W_READS) = reg(LINK_YP_READS);
    ST_REG(LINK_YP_W_READ_CRC) = reg(LINK_YP_READ_CRC);
    ST_REG(LINK_YP_W_DRAFT_CRC) = oy_guard_table_crc(g_draft, 32u);
    (void)stage();
    /* No key. */
    (void)cmd_full(LINK_YGE_OP_WRITE, 0u, false);
    CHECK_EQ(refusal(), LINK_YGE_REF_NO_KEY);
    (void)cmd_full(LINK_YGE_OP_WRITE, LINK_YGE_KEY, true);
    CHECK_EQ(refusal(), LINK_YGE_REF_STAGED);
    /* Another read. */
    ST_REG(LINK_YP_W_READS) = (uint16_t)(reg(LINK_YP_READS) + 1u);
    (void)stage();
    (void)cmd(LINK_YGE_OP_WRITE);
    CHECK_EQ(refusal(), LINK_YGE_REF_STALE);
    ST_REG(LINK_YP_W_READS) = reg(LINK_YP_READS);
    ST_REG(LINK_YP_W_READ_CRC) = (uint16_t)(reg(LINK_YP_READ_CRC) ^ 1u);
    (void)stage();
    (void)cmd(LINK_YGE_OP_WRITE);
    CHECK_EQ(refusal(), LINK_YGE_REF_STALE);
    /* A draft that is not the one named. */
    ST_REG(LINK_YP_W_READ_CRC) = reg(LINK_YP_READ_CRC);
    ST_REG(LINK_YP_W_DRAFT_CRC) ^= 1u;
    (void)stage();
    (void)cmd(LINK_YGE_OP_WRITE);
    CHECK_EQ(refusal(), LINK_YGE_REF_DRAFT);
    CHECK_EQ(sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u), 0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK(!flag(LINK_YP_F_FAULT));
    /* Busy while a read runs; not open after a LEAVE. */
    (void)cmd(LINK_YGE_OP_READ);
    (void)cmd(LINK_YGE_OP_READ);
    CHECK_EQ(refusal(), LINK_YGE_REF_BUSY);
    run_while(LINK_YGE_RUN, 30000u);
    (void)cmd(LINK_YGE_OP_LEAVE);
    (void)cmd(LINK_YGE_OP_READ);
    CHECK_EQ(refusal(), LINK_YGE_REF_NOT_OPEN);
    /* A read-only table: a refusal, nothing sent. */
    rig_up();
    sim.table[12] = 3546u;     /* V1.03546, below the lowest writable */
    sim.table[13] = 1u;
    ready_on(0u);
    read_now();
    CHECK(!flag(LINK_YP_F_WRITABLE));
    CHECK_EQ(reg(LINK_YP_SES_STATE) >> 8, OY_REF_RO_FIRMWARE);
    draft_of(21u, 14u);
    CHECK_EQ(write_table(g_draft), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_SESSION);
    CHECK_EQ(reg(LINK_YP_SES) & 0xFFu, OY_NOT_KNOWN);
    CHECK(!flag(LINK_YP_F_FAULT));
    CHECK_EQ(sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u), 0u);
}

TEST_CASE(a_leave_with_the_supply_after_it)
{
    rig_up();
    ready_on(0u);
    CHECK(g.v.asked_on);
    const unsigned n = sim.n_sent;

    CHECK_EQ(cmd(LINK_YGE_OP_LEAVE), 0u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(result(), LINK_YGE_RESULT_DONE);
    CHECK_ALL_OFF();
    CHECK(!flag(LINK_YP_F_FAULT));
    CHECK(!flag(LINK_YP_F_STOPPED));
    /* Requests stop. */
    run_ms(1000u);
    CHECK_EQ(sim.n_sent, n);
    /* Still held; a new ENTER works. */
    CHECK_EQ(yge_port_quiet_mask(&g_p), 1u);
    enter_now();
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    /* LEAVE outside READY. */
    (void)cmd(LINK_YGE_OP_CLOSE);
    (void)cmd(LINK_YGE_OP_LEAVE);
    CHECK_EQ(refusal(), LINK_YGE_REF_NOT_OPEN);
}

TEST_CASE(a_frame_or_window_the_page_does_not_take)
{
    uint16_t f[LINK_YP_W_CMD_FRAME] = { LINK_YGE_OP_OPEN, 1u, 0u, 0u };
    const uint16_t v = 0u;

    rig_up();
    pass();
    /* Windows. */
    CHECK_EQ(yge_port_write(NULL, 0u, 4u, f, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, NULL, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_write(&g_p, 0u, 0u, f, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_write(&g_p, 0u, 3u, f, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_write(&g_p, 1u, 3u, f, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_write(&g_p, LINK_YP_W_COUNT, 1u, &v, 0u),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(yge_port_write(&g_p, LINK_YP_COUNT - 1u, 2u, f, 0u),
             LINK_NACK_BAD_RANGE);
    const uint16_t blk = LINK_YD_BLOCKS;

    CHECK_EQ(yge_port_write(&g_p, LINK_YP_W_BLOCK, 1u, &blk, 0u),
             LINK_NACK_BAD_VALUE);
    /* Commands: none, past the last, a key that is neither, a SEQ skipped. */
    f[LINK_YP_W_CMD] = LINK_YGE_OP_NONE;
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    f[LINK_YP_W_CMD] = LINK_YGE_OP_COUNT;
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    f[LINK_YP_W_CMD] = LINK_YGE_OP_CLOSE;
    f[LINK_YP_W_KEY] = (uint16_t)(LINK_YGE_KEY + 1u);
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    f[LINK_YP_W_KEY] = 0u;
    f[LINK_YP_W_SEQ] = 2u;
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, f, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_YP_SEQ), 0u);
    /* SEQ + 1 is taken; the same frame again is acknowledged and does nothing. */
    f[LINK_YP_W_SEQ] = 1u;
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, f, 0u), 0u);
    CHECK_EQ(reg(LINK_YP_SEQ), 1u);
    CHECK_EQ(refusal(), LINK_YGE_REF_NOT_OPEN);
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, f, 0u), 0u);
    CHECK_EQ(reg(LINK_YP_SEQ), 1u);
    /* SEQ wraps at 65536. */
    g_p.seq = 0xFFFFu;
    f[LINK_YP_W_SEQ] = 0u;
    CHECK_EQ(yge_port_write(&g_p, 0u, 4u, f, 0u), 0u);
    CHECK_EQ(reg(LINK_YP_SEQ), 0u);
    /* A read past the page leaves the output alone. */
    uint16_t out = 0xBEEFu;

    yge_port_read(&g_p, LINK_YP_COUNT, 1u, &out);
    CHECK_EQ(out, 0xBEEFu);
    yge_port_read(NULL, 0u, 1u, &out);
    CHECK_EQ(out, 0xBEEFu);
}

TEST_CASE(the_data_page_blocks)
{
    uint16_t in[LINK_YD_COUNT];
    uint16_t out[LINK_YD_COUNT];

    rig_up();
    ready_on(0u);
    read_now();
    for (unsigned i = 0u; i < LINK_YD_COUNT; ++i) {
        in[i] = (uint16_t)(0x100u + i);
    }
    /* Read-only blocks. */
    for (uint16_t b = 0u; b < (uint16_t)LINK_YD_BLOCKS; ++b) {
        CHECK_EQ(yge_port_write(&g_p, LINK_YP_W_BLOCK, 1u, &b, now_ms()), 0u);
        CHECK_EQ(reg(LINK_YP_BLOCK), b);
        const bool draft = b == LINK_YD_DRAFT || b == LINK_YD_DRAFT + 1u;

        CHECK_EQ(yge_port_data_write(&g_p, 0u, LINK_YD_COUNT, in),
                 draft ? 0u : LINK_NACK_READ_ONLY);
    }
    /* The draft's second block holds indices 32 to 63. */
    const uint16_t hi = LINK_YD_DRAFT + 1u;

    (void)yge_port_write(&g_p, LINK_YP_W_BLOCK, 1u, &hi, now_ms());
    yge_port_data_read(&g_p, 0u, LINK_YD_COUNT, out);
    CHECK_EQ(memcmp(in, out, sizeof(in)), 0);
    CHECK_EQ(g_p.draft[32], 0x100u);
    CHECK_EQ(g_p.draft[63], 0x11Fu);
    /* Windows. */
    CHECK_EQ(yge_port_data_write(&g_p, 31u, 2u, in), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_data_write(&g_p, 0u, 0u, in), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_data_write(NULL, 0u, 1u, in), LINK_NACK_BAD_RANGE);
    CHECK_EQ(yge_port_data_write(&g_p, 31u, 1u, in), 0u);
    out[0] = 0xBEEFu;
    yge_port_data_read(&g_p, 31u, 2u, out);
    CHECK_EQ(out[0], 0xBEEFu);
    /* The last received table: the read copy after a read. */
    const uint16_t last = LINK_YD_LAST;

    (void)yge_port_write(&g_p, LINK_YP_W_BLOCK, 1u, &last, now_ms());
    yge_port_data_read(&g_p, 0u, LINK_YD_COUNT, out);
    CHECK_EQ(memcmp(out, g_p.ses.orig, sizeof(out)), 0);
    /* BUSY while an operation runs. */
    (void)yge_port_write(&g_p, LINK_YP_W_BLOCK, 1u, &hi, now_ms());
    (void)cmd(LINK_YGE_OP_READ);
    CHECK_EQ(yge_port_data_write(&g_p, 0u, 1u, in), LINK_NACK_BUSY);
}

int main(void)
{
    RUN(init_needs_every_hook);
    RUN(a_port_goes_to_the_line_and_comes_back);
    RUN(a_port_that_does_not_take_its_pin_back_keeps_the_hold);
    RUN(a_line_with_no_resources_leaves_the_port_free);
    RUN(every_refusal_of_an_open);
    RUN(the_mask_of_what_else_holds_the_bench);
    RUN(every_refusal_of_an_enter);
    RUN(a_switched_entry_in_its_order);
    RUN(the_boot_wait_counts_from_the_supply_reading_on);
    RUN(a_supply_that_will_not_read_off_or_refuses_on);
    RUN(by_hand_at_60000ms_and_60001ms);
    RUN(a_read_and_a_write);
    RUN(every_refusal_of_a_write);
    RUN(a_leave_with_the_supply_after_it);
    RUN(a_frame_or_window_the_page_does_not_take);
    RUN(the_data_page_blocks);
    return test_summary("yge_port");
}
