/*
 * The panel's half of the BUS page (shared/bench/bus_link.c) against the
 * coprocessor's (shared/outputs/bus_port.c) and the simulated Feetech
 * servos of feetech_sim.h: a command from the screen to the wire and its
 * result back, the staged windows, the SEQ, an answer lost and a frame
 * refused, STOP before and after a command went, the link lost, a
 * coprocessor without the page, the poll intervals, and the table of
 * protocols (shared/bench/bus_proto.c).
 *
 * The clocks: a pass is 1 ms of the panel and 100 us of the coprocessor's
 * microsecond clock times ten steps.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bus_link.h"
#include "bus_port.h"
#include "bus_proto.h"
#include "feetech_sim.h"

#define PORT 0u
#define PIN  13u

static ftsim_bus_t sim;
static bus_port_t g_port;
static bus_link_t g_link;
static uint32_t now_us;
static uint32_t now_ms;
static bool g_safe;

/* The link's faults: the next exchanges lose their answer, and a write
 * that is taken can lose it after the page took it. */
static unsigned g_drop;
static unsigned g_drop_after;
static unsigned g_exchanges;
static unsigned g_cmd_writes;

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return port == PORT ? PIN : 0xFFu;
}

static bool hw_held(void *ctx, uint8_t port)
{
    (void)ctx;
    (void)port;
    return false;
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin, uint32_t baud)
{
    (void)ctx;
    (void)port;
    (void)pin;
    sim.baud = baud;
    return true;
}

static void hw_give(void *ctx)
{
    (void)ctx;
}

static void setup(void)
{
    bus_port_hw_t hw;

    ftsim_init(&sim, 1000000u);
    memset(&hw, 0, sizeof(hw));
    hw.ports = 1;
    hw.pin = hw_pin;
    hw.held = hw_held;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.driver = ftsim_driver(&sim);
    now_us = 0u;
    now_ms = 1000u;
    g_safe = true;
    g_drop = 0u;
    g_drop_after = 0u;
    g_exchanges = 0u;
    g_cmd_writes = 0u;
    CHECK(bus_port_init(&g_port, &hw));
    bus_link_init(&g_link);
    bus_link_came_up(&g_link, BUS_LINK_MINOR);
}

/* One exchange the link module owes, carried to the page. */

/* A read of either page, as the coprocessor's page table serves it. */
static void serve_read(const bus_link_xfer_t *x, uint16_t *regs)
{
    CHECK(x->page == LINK_PAGE_BUS || x->page == LINK_PAGE_BUS_IDS);
    if (x->page == LINK_PAGE_BUS_IDS) {
        CHECK((unsigned)x->off + x->n <= LINK_BI_COUNT);
        bus_port_read_ids(&g_port, x->off, x->n, regs);
    } else {
        CHECK((unsigned)x->off + x->n <= LINK_BS_COUNT);
        bus_port_read(&g_port, x->off, x->n, regs);
    }
}

static bool exchange(void)
{
    bus_link_xfer_t x;
    uint16_t regs[32];

    if (!bus_link_next(&g_link, now_ms, &x)) {
        return false;
    }
    g_exchanges++;
    if (g_drop > 0u) {
        g_drop--;
        bus_link_done(&g_link, BUS_LINK_NO_ANSWER, NULL);
        return true;
    }
    if (x.write) {
        const uint8_t nack = bus_port_write(&g_port, x.off, x.n, x.regs);

        if (x.off == 0u) {
            g_cmd_writes++;
        }
        if (g_drop_after > 0u && x.off == 0u) {
            g_drop_after--;
            bus_link_done(&g_link, BUS_LINK_NO_ANSWER, NULL);
            return true;
        }
        bus_link_done(&g_link, nack == 0u ? BUS_LINK_ACK : (int)nack, NULL);
        return true;
    }
    serve_read(&x, regs);
    bus_link_done(&g_link, BUS_LINK_ACK, regs);
    return true;
}

/* A pass of each end: up to 4 exchanges, then 10 steps of the page. */
static void pass(void)
{
    for (int k = 0; k < 4 && exchange(); ++k) {
    }
    for (int k = 0; k < 10; ++k) {
        bus_port_step(&g_port, g_safe, false, now_us);
        now_us += 100u;
    }
    now_ms += 1u;
}

static bus_link_readout_t rd(void)
{
    bus_link_readout_t r;

    bus_link_readout(&g_link, &r);
    return r;
}

static uint8_t page_state(void)
{
    return LINK_BS_STATE_OF(rd().status[LINK_BS_STATE]);
}

static uint8_t page_result(void)
{
    return (uint8_t)(rd().status[LINK_BS_RESULT] & 0xFFu);
}

static uint8_t page_refusal(void)
{
    return (uint8_t)(rd().status[LINK_BS_RESULT] >> 8);
}

static bus_link_cmd_t cmd_of(link_bus_op_t op, uint16_t arg, uint32_t id)
{
    bus_link_cmd_t c;

    memset(&c, 0, sizeof(c));
    c.op = op;
    c.port = PORT;
    c.proto = LINK_BUS_PROTO_FEETECH;
    c.sts = true;
    c.arg = arg;
    c.id = id;
    return c;
}

/*
 * Send @p c and run until its answer and, when taken, until the page shows
 * it done: SEQ at the answer's, the bus not busy.  The answer.
 */
static bus_link_ans_t run_cmd(const bus_link_cmd_t *c, unsigned max)
{
    if (!bus_link_send(&g_link, c)) {
        return BUS_LINK_ANS_NONE;
    }
    for (unsigned i = 0; i < max; ++i) {
        pass();
        const bus_link_readout_t r = rd();

        if (r.sending || r.ans_id != c->id) {
            continue;
        }
        if (r.ans != BUS_LINK_ANS_TAKEN) {
            return (bus_link_ans_t)r.ans;
        }
        if (r.status[LINK_BS_SEQ] == r.ans_seq
            && (r.status[LINK_BS_STATE] & LINK_BS_F_BUSY) == 0u
            && (r.status[LINK_BS_RESULT] & 0xFFu) != 1u) {
            return BUS_LINK_ANS_TAKEN;
        }
    }
    return (bus_link_ans_t)rd().ans;
}

static void open_bus(void)
{
    const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_OPEN, 0u, 1u);

    for (int i = 0; i < 3; ++i) {
        pass();
    }
    CHECK_EQ(run_cmd(&c, 500), BUS_LINK_ANS_TAKEN);
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
}

static void drain(uint32_t t);

/* The next write owed now, the reads before it made. */
static bool next_write(bus_link_xfer_t *x)
{
    for (int i = 0; i < 8; ++i) {
        uint16_t regs[32];

        if (!bus_link_next(&g_link, now_ms, x)) {
            return false;
        }
        if (x->write) {
            return true;
        }
        serve_read(x, regs);
        bus_link_done(&g_link, BUS_LINK_ACK, regs);
    }
    return false;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(a_coprocessor_without_the_page_is_sent_nothing)
{
    bus_link_xfer_t x;
    const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_OPEN, 0u, 1u);

    setup();
    bus_link_came_up(&g_link, (uint16_t)(BUS_LINK_MINOR - 1u));
    CHECK(!bus_link_has_page(&g_link));
    CHECK(!bus_link_send(&g_link, &c));
    CHECK(!bus_link_next(&g_link, now_ms, &x));
    /* Nor before any coprocessor answered. */
    bus_link_init(&g_link);
    CHECK(!bus_link_send(&g_link, &c));
    CHECK(!bus_link_next(&g_link, now_ms, &x));
}

TEST_CASE(a_command_is_refused_for_none_and_beyond_the_last)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_NONE, 0u, 1u);

    setup();
    CHECK(!bus_link_send(&g_link, &c));
    c.op = LINK_BUS_OP_COUNT;
    CHECK(!bus_link_send(&g_link, &c));
    c.op = LINK_BUS_OP_WRITE;
    c.n = 0u;
    CHECK(!bus_link_send(&g_link, &c));
    c.n = LINK_BS_BYTES + 1u;
    CHECK(!bus_link_send(&g_link, &c));
    c.n = LINK_BS_BYTES;
    c.arg = (uint16_t)(1u | (FEETECH_REG_GOAL << 8));
    CHECK(bus_link_send(&g_link, &c));
    /* One at a time. */
    CHECK(!bus_link_send(&g_link, &c));
}

TEST_CASE(open_and_close_through_the_page)
{
    setup();
    CHECK(!bus_link_holds(&g_link));
    open_bus();
    CHECK(bus_link_holds(&g_link));
    CHECK_EQ(rd().status[LINK_BS_PIN], PIN);
    {
        const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_CLOSE, 0u, 2u);

        CHECK_EQ(run_cmd(&c, 500), BUS_LINK_ANS_TAKEN);
    }
    for (int i = 0; i < 20; ++i) {
        pass();
    }
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
    CHECK(!bus_link_holds(&g_link));
}

TEST_CASE(an_open_in_flight_holds_the_bench)
{
    const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_OPEN, 0u, 1u);

    setup();
    pass();
    CHECK(bus_link_send(&g_link, &c));
    CHECK(bus_link_holds(&g_link));
}

TEST_CASE(a_scan_of_0_to_253_reads_the_ids_found)
{
    bus_link_readout_t r;

    setup();
    (void)ftsim_add(&sim, 0u, FEETECH_STS);
    (void)ftsim_add(&sim, 17u, FEETECH_SCS);
    (void)ftsim_add(&sim, 253u, FEETECH_STS);
    open_bus();
    {
        const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_SCAN,
                                        (uint16_t)(253u << 8), 3u);

        CHECK_EQ(run_cmd(&c, 20000), BUS_LINK_ANS_TAKEN);
    }
    for (int i = 0; i < 4; ++i) {
        pass();
    }
    r = rd();
    CHECK_EQ(page_result(), FEETECH_RES_OK);
    CHECK_EQ(r.status[LINK_BS_SCAN] >> 8, 3);
    CHECK(r.ids_known);
    CHECK_EQ(r.ids[0] & 1u, 1u);
    CHECK_EQ((r.ids[1] >> 1) & 1u, 1u);
    CHECK_EQ((r.ids[15] >> 13) & 1u, 1u);
    CHECK_EQ(r.ids[2], 0u);
    /* CLOSE and OPEN: the new session found nothing, and the IDs of the
     * last one are not offered. */
    {
        const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_CLOSE, 0u, 4u);
        const bus_link_cmd_t o = cmd_of(LINK_BUS_OP_OPEN, 0u, 5u);

        CHECK_EQ(run_cmd(&c, 500), BUS_LINK_ANS_TAKEN);
        CHECK_EQ(run_cmd(&o, 500), BUS_LINK_ANS_TAKEN);
    }
    r = rd();
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
    CHECK_EQ(r.status[LINK_BS_SCAN] >> 8, 0);
    CHECK(!r.ids_known);
    CHECK_EQ(r.ids[0], 0u);
    CHECK_EQ(r.ids[15], 0u);
}

TEST_CASE(a_scan_to_254_is_refused_by_the_page)
{
    setup();
    open_bus();
    {
        const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_SCAN,
                                        (uint16_t)(1u | (254u << 8)), 3u);

        CHECK_EQ(run_cmd(&c, 50), BUS_LINK_ANS_TAKEN);
    }
    CHECK_EQ(page_refusal(), LINK_BUS_REF_RANGE);
}

TEST_CASE(a_write_goes_out_in_three_staged_windows)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_WRITE,
                              (uint16_t)(1u | (FEETECH_REG_TORQUE << 8)), 4u);
    ftsim_servo_t *v;

    setup();
    v = ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    c.n = 1u;
    c.data[0] = 1u;
    g_exchanges = 0u;
    CHECK_EQ(run_cmd(&c, 200), BUS_LINK_ANS_TAKEN);
    CHECK_EQ(page_refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(page_result(), FEETECH_RES_OK);
    CHECK_EQ(v->regs[FEETECH_REG_TORQUE], 1u);
}

TEST_CASE(a_goal_of_16_bytes_reaches_the_servo_in_its_byte_order)
{
    bus_link_cmd_t c;
    uint8_t addr = 0;
    ftsim_servo_t *v;
    const bus_proto_t *p = bus_proto_of(LINK_BUS_PROTO_FEETECH);

    setup();
    v = ftsim_add(&sim, 2u, FEETECH_SCS);
    open_bus();
    c = cmd_of(LINK_BUS_OP_WRITE, 0u, 5u);
    c.sts = false;
    c.n = (uint8_t)bus_proto_goal(p, FEETECH_SCS, 512u, 1000u, 0u, &addr,
                                  c.data, sizeof(c.data));
    CHECK_EQ(c.n, 6);
    CHECK_EQ(addr, FEETECH_REG_GOAL);
    c.arg = (uint16_t)(2u | ((unsigned)addr << 8));
    CHECK_EQ(run_cmd(&c, 200), BUS_LINK_ANS_TAKEN);
    CHECK_EQ(page_result(), FEETECH_RES_OK);
    /* SCS: high byte first. */
    CHECK_EQ(v->regs[FEETECH_REG_GOAL], 0x02u);
    CHECK_EQ(v->regs[FEETECH_REG_GOAL + 1], 0x00u);
    CHECK_EQ(v->regs[FEETECH_REG_SPEED], 0x03u);
    CHECK_EQ(v->regs[FEETECH_REG_SPEED + 1], 0xE8u);
}

TEST_CASE(an_eeprom_write_needs_the_key)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_EEPROM,
                              (uint16_t)(1u | (FEETECH_REG_ID << 8)), 6u);
    ftsim_servo_t *v;

    setup();
    v = ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    c.n = 1u;
    c.data[0] = 9u;
    c.key = false;
    CHECK_EQ(run_cmd(&c, 200), BUS_LINK_ANS_TAKEN);
    CHECK_EQ(page_refusal(), LINK_BUS_REF_NO_KEY);
    CHECK_EQ(v->regs[FEETECH_REG_ID], 1u);
    c.key = true;
    c.id = 7u;
    CHECK_EQ(run_cmd(&c, 2000), BUS_LINK_ANS_TAKEN);
    CHECK_EQ(page_refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(page_result(), FEETECH_RES_OK);
    CHECK_EQ(v->regs[FEETECH_REG_ID], 9u);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1u);
}

TEST_CASE(a_lost_answer_sends_the_same_frame_and_the_page_takes_it_once)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_TORQUE_OFF, 0u, 8u);
    uint16_t seq;

    setup();
    open_bus();
    seq = rd().status[LINK_BS_SEQ];
    g_drop_after = 1u;
    g_cmd_writes = 0u;
    CHECK_EQ(run_cmd(&c, 500), BUS_LINK_ANS_TAKEN);
    CHECK_EQ(g_cmd_writes, 2u);
    CHECK_EQ(rd().status[LINK_BS_SEQ], (uint16_t)(seq + 1u));
    CHECK_EQ(rd().ans_seq, (uint16_t)(seq + 1u));
}

TEST_CASE(three_exchanges_without_an_answer_give_the_command_up)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_TORQUE_OFF, 0u, 9u);

    setup();
    open_bus();
    g_drop = BUS_LINK_RETRIES;
    CHECK_EQ(run_cmd(&c, 50), BUS_LINK_ANS_NO_ANSWER);
    CHECK_EQ(rd().ans_id, 9u);
    /* And the next one goes. */
    c.id = 10u;
    CHECK_EQ(run_cmd(&c, 500), BUS_LINK_ANS_TAKEN);
}

TEST_CASE(a_frame_refused_for_its_seq_goes_again_after_a_read)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_TORQUE_OFF, 0u, 11u);
    uint16_t f[LINK_BS_W_CMD_FRAME];

    setup();
    open_bus();
    CHECK(bus_link_send(&g_link, &c));
    /* Someone else's command moves the page's SEQ before this one goes. */
    f[LINK_BS_W_CMD] = LINK_BS_W_CMD_OF(LINK_BUS_OP_WATCH, PORT,
                                        LINK_BUS_PROTO_FEETECH, true);
    f[LINK_BS_W_SEQ] = (uint16_t)(rd().status[LINK_BS_SEQ] + 1u);
    f[LINK_BS_W_ARG] = 0xFFu;
    f[LINK_BS_W_AUX] = 0u;
    CHECK_EQ(bus_port_write(&g_port, 0, LINK_BS_W_CMD_FRAME, f), 0);
    for (int i = 0; i < 20 && rd().ans_id != 11u; ++i) {
        pass();
    }
    CHECK_EQ(rd().ans, BUS_LINK_ANS_TAKEN);
    CHECK_EQ(rd().ans_seq, (uint16_t)(f[LINK_BS_W_SEQ] + 1u));
}

TEST_CASE(a_refused_window_or_a_frame_refused_twice_ends_nack)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_WRITE,
                              (uint16_t)(1u | (FEETECH_REG_TORQUE << 8)), 12u);
    bus_link_xfer_t x;

    setup();
    open_bus();
    c.n = 1u;
    CHECK(bus_link_send(&g_link, &c));
    CHECK(bus_link_next(&g_link, now_ms, &x));
    CHECK(x.write);
    CHECK_EQ(x.off, LINK_BS_W_N);
    CHECK_EQ(x.n, 4);
    bus_link_done(&g_link, (int)LINK_NACK_BAD_RANGE, NULL);
    CHECK_EQ(rd().ans, BUS_LINK_ANS_NACK);
    CHECK_EQ(rd().ans_nack, LINK_NACK_BAD_RANGE);
    CHECK_EQ(rd().ans_id, 12u);

    /* BAD_VALUE for the command: a read, the frame once more, then NACK. */
    c = cmd_of(LINK_BUS_OP_TORQUE_OFF, 0u, 13u);
    CHECK(bus_link_send(&g_link, &c));
    CHECK(next_write(&x));
    CHECK_EQ(x.off, 0);
    bus_link_done(&g_link, (int)LINK_NACK_BAD_VALUE, NULL);
    CHECK(rd().sending);
    CHECK(bus_link_next(&g_link, now_ms, &x));
    CHECK(!x.write);
    {
        uint16_t regs[32];

        serve_read(&x, regs);
        bus_link_done(&g_link, BUS_LINK_ACK, regs);
    }
    CHECK(next_write(&x));
    CHECK_EQ(x.off, 0);
    bus_link_done(&g_link, (int)LINK_NACK_BAD_VALUE, NULL);
    CHECK(!rd().sending);
    CHECK_EQ(rd().ans, BUS_LINK_ANS_NACK);
    CHECK_EQ(rd().ans_nack, LINK_NACK_BAD_VALUE);
}

TEST_CASE(stop_drops_a_command_not_sent_and_stops_the_bus)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_SCAN, (uint16_t)(253u << 8),
                              13u);
    bus_link_cmd_t w = cmd_of(LINK_BUS_OP_TORQUE_OFF, 0u, 14u);

    setup();
    (void)ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    CHECK(bus_link_send(&g_link, &c));
    for (int i = 0; i < 3; ++i) {
        pass();
    }
    CHECK_EQ(rd().ans, BUS_LINK_ANS_TAKEN);
    CHECK(rd().status[LINK_BS_STATE] & LINK_BS_F_BUSY);
    /* A command queued behind the scan, then STOP. */
    CHECK(bus_link_send(&g_link, &w));
    bus_link_stop(&g_link);
    CHECK_EQ(rd().ans, BUS_LINK_ANS_DROPPED);
    CHECK_EQ(rd().ans_id, 14u);
    for (int i = 0; i < 100; ++i) {
        pass();
    }
    CHECK_EQ((uint8_t)(rd().status[LINK_BS_OP] & 0xFFu), LINK_BUS_OP_STOP);
    CHECK_EQ(page_result(), FEETECH_RES_OK);
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
}

TEST_CASE(stop_goes_before_a_command_whose_frame_went_unanswered)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_SCAN, (uint16_t)(253u << 8),
                              15u);
    bus_link_xfer_t x;

    setup();
    (void)ftsim_add(&sim, 1u, FEETECH_STS);
    open_bus();
    CHECK(bus_link_send(&g_link, &c));
    /* The scan's frame reaches the page; its answer is lost. */
    CHECK(next_write(&x));
    CHECK_EQ(x.off, 0u);
    CHECK_EQ(bus_port_write(&g_port, x.off, x.n, x.regs), 0u);
    bus_link_done(&g_link, BUS_LINK_NO_ANSWER, NULL);
    CHECK(rd().sending);
    bus_link_stop(&g_link);
    /* The next frame is the STOP, not the scan again: the scan ends
     * without an answer. */
    CHECK(next_write(&x));
    CHECK_EQ((x.regs[LINK_BS_W_CMD] & 0xFu), LINK_BUS_OP_STOP);
    CHECK_EQ(rd().ans, BUS_LINK_ANS_NO_ANSWER);
    CHECK_EQ(rd().ans_id, 15u);
    /* Its SEQ went with the scan: refused, read, sent again, taken. */
    CHECK_EQ(bus_port_write(&g_port, x.off, x.n, x.regs),
             LINK_NACK_BAD_VALUE);
    bus_link_done(&g_link, LINK_NACK_BAD_VALUE, NULL);
    CHECK(next_write(&x));
    CHECK_EQ((x.regs[LINK_BS_W_CMD] & 0xFu), LINK_BUS_OP_STOP);
    CHECK_EQ(bus_port_write(&g_port, x.off, x.n, x.regs), 0u);
    bus_link_done(&g_link, BUS_LINK_ACK, NULL);
    for (int i = 0; i < 100; ++i) {
        pass();
    }
    CHECK_EQ((uint8_t)(rd().status[LINK_BS_OP] & 0xFFu), LINK_BUS_OP_STOP);
    CHECK_EQ(page_result(), FEETECH_RES_OK);
    CHECK_EQ(page_state(), LINK_BUS_OPEN);
}

TEST_CASE(stop_closes_an_open_whose_frame_went_unanswered)
{
    const bus_link_cmd_t c = cmd_of(LINK_BUS_OP_OPEN, 0u, 16u);
    bus_link_xfer_t x;

    setup();
    for (int i = 0; i < 3; ++i) {
        pass();
    }
    CHECK(bus_link_send(&g_link, &c));
    CHECK(next_write(&x));
    CHECK_EQ(bus_port_write(&g_port, x.off, x.n, x.regs), 0u);
    bus_link_done(&g_link, BUS_LINK_NO_ANSWER, NULL);
    CHECK_EQ(bus_port_state(&g_port), LINK_BUS_OPENING);
    bus_link_stop(&g_link);
    CHECK(next_write(&x));
    CHECK_EQ((x.regs[LINK_BS_W_CMD] & 0xFu), LINK_BUS_OP_STOP);
    CHECK_EQ(LINK_BS_PORT_OF(x.regs[LINK_BS_W_CMD]), PORT);
    CHECK_EQ(rd().ans, BUS_LINK_ANS_NO_ANSWER);
    for (int i = 0; i < 100; ++i) {
        pass();
    }
    CHECK_EQ(page_state(), LINK_BUS_CLOSED);
    CHECK_EQ(bus_port_state(&g_port), LINK_BUS_CLOSED);
}

TEST_CASE(stop_sends_nothing_to_a_closed_bus)
{
    unsigned before;

    setup();
    for (int i = 0; i < 3; ++i) {
        pass();
    }
    bus_link_stop(&g_link);
    g_cmd_writes = 0u;
    before = rd().answers;
    for (int i = 0; i < 1000; ++i) {
        pass();
    }
    CHECK_EQ(g_cmd_writes, 0u);
    CHECK_EQ(rd().answers, before);
}

TEST_CASE(the_link_lost_ends_a_command_down)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_OPEN, 0u, 15u);
    bus_link_xfer_t x;

    setup();
    pass();
    CHECK(bus_link_send(&g_link, &c));
    bus_link_lost(&g_link);
    CHECK_EQ(rd().ans, BUS_LINK_ANS_DOWN);
    CHECK(!rd().up);
    CHECK(!bus_link_next(&g_link, now_ms, &x));
    CHECK(!bus_link_holds(&g_link));
}

TEST_CASE(a_page_that_goes_ends_a_command_down)
{
    bus_link_cmd_t c = cmd_of(LINK_BUS_OP_OPEN, 0u, 16u);
    bus_link_xfer_t x;

    setup();
    pass();
    CHECK(bus_link_send(&g_link, &c));
    CHECK(bus_link_next(&g_link, now_ms, &x));
    bus_link_done(&g_link, (int)LINK_NACK_BAD_PAGE, NULL);
    CHECK_EQ(rd().ans, BUS_LINK_ANS_DOWN);
    CHECK(!bus_link_has_page(&g_link));
}

/* The reads owed at @p t, made. */
static void drain(uint32_t t)
{
    bus_link_xfer_t x;

    while (bus_link_next(&g_link, t, &x)) {
        uint16_t regs[32];

        CHECK(!x.write);
        serve_read(&x, regs);
        bus_link_done(&g_link, BUS_LINK_ACK, regs);
    }
}

TEST_CASE(the_page_is_read_every_500_ms_closed_and_50_ms_open)
{
    bus_link_xfer_t x;
    uint32_t t;

    setup();
    pass();
    /* Closed: nothing owed for 499 ms after a read, a read at 500. */
    t = now_ms + 1000u;
    drain(t);
    CHECK(!bus_link_next(&g_link, t + 499u, &x));
    CHECK(bus_link_next(&g_link, t + 500u, &x));
    CHECK(!x.write);
    CHECK_EQ(x.off, 0);
    CHECK_EQ(x.n, BUS_LINK_STATUS_N);
    bus_link_done(&g_link, BUS_LINK_NO_ANSWER, NULL);
    /* Open: every 50 ms. */
    setup();
    open_bus();
    t = now_ms + 100u;
    drain(t);
    CHECK(!bus_link_next(&g_link, t + 49u, &x));
    CHECK(bus_link_next(&g_link, t + 50u, &x));
    CHECK(!x.write);
    CHECK_EQ(x.n, BUS_LINK_STATUS_N);
}

TEST_CASE(the_protocol_table_has_feetech_and_nothing_else)
{
    const bus_proto_t *p = bus_proto_get(0);
    bus_reg_t r;
    bus_live_t l;
    uint8_t live[FEETECH_LIVE_STS];
    uint8_t out[16];
    uint8_t addr = 0;

    CHECK_EQ(bus_proto_count(), 1u);
    CHECK(p != NULL);
    CHECK_EQ(bus_proto_get(1), NULL);
    CHECK_EQ(bus_proto_of(LINK_BUS_PROTO_FEETECH), p);
    CHECK_EQ(bus_proto_of(1u), NULL);
    CHECK_STR_EQ(p->name, "FEETECH");
    CHECK_EQ(p->baud_count, 8u);
    CHECK_EQ(p->baud(0), 1000000u);
    CHECK_EQ(p->baud(7), 38400u);
    CHECK_EQ(p->baud(8), 0u);
    CHECK_STR_EQ(p->series_names[0], "SCS");
    CHECK_STR_EQ(p->series_names[1], "STS/SMS");
    CHECK_EQ(p->id_max, 253u);
    /* Mode and acceleration on STS only. */
    CHECK(!p->reg(0, BUS_R_MODE, &r));
    CHECK(!p->reg(0, BUS_R_ACC, &r));
    CHECK(p->reg(1, BUS_R_ACC, &r));
    CHECK_EQ(r.addr, 41u);
    CHECK(p->reg(0, BUS_R_LOCK, &r));
    CHECK_EQ(r.addr, 48u);
    CHECK(r.eeprom);
    CHECK(p->reg(1, BUS_R_LOCK, &r));
    CHECK_EQ(r.addr, 55u);
    CHECK(!p->reg(2, BUS_R_ID, &r));
    CHECK(!p->reg(1, BUS_R_COUNT, &r));
    CHECK_EQ(p->position_max(0), 1023u);
    CHECK_EQ(p->position_max(1), 4095u);
    /* The live block: 7.4 V, 31 deg C, 100 counts of 6.5 mA. */
    memset(live, 0, sizeof(live));
    feetech_put16(FEETECH_STS, &live[0], 2048u);
    live[62 - 56] = 74u;
    live[63 - 56] = 31u;
    live[65 - 56] = FEETECH_ERR_OVERLOAD | FEETECH_ERR_VOLTAGE;
    feetech_put16(FEETECH_STS, &live[69 - 56], 100u);
    CHECK(p->live(1, live, FEETECH_LIVE_STS, &l));
    CHECK_EQ(l.position, 2048u);
    CHECK_EQ(l.mv, 7400u);
    CHECK_EQ(l.temp_c, 31u);
    CHECK_EQ(l.status & p->error_bit[BUS_E_OVERLOAD],
              p->error_bit[BUS_E_OVERLOAD]);
    CHECK_EQ(l.status & p->error_bit[BUS_E_VOLTAGE],
              p->error_bit[BUS_E_VOLTAGE]);
    CHECK(l.has_current);
    CHECK_EQ(l.current_ma, 650u);
    /* A block of the other series' length is not decoded. */
    CHECK(!p->live(0, live, FEETECH_LIVE_STS, &l));
    CHECK(!p->live(1, live, FEETECH_LIVE_STS, NULL));
    /* The goal: STS from 41 with the acceleration, little-endian; the
     * manual's example, 50, 1024, 0, 2000. */
    CHECK_EQ(bus_proto_goal(p, 1, 1024u, 2000u, 50u, &addr, out,
                             sizeof(out)), 7u);
    CHECK_EQ(addr, 41u);
    {
        static const uint8_t want[] = { 0x32, 0x00, 0x04, 0x00, 0x00,
                                        0xD0, 0x07 };

        CHECK(memcmp(want, out, sizeof(want)) == 0);
    }
    /* Limits: the position's and the speed's ends, and the room. */
    CHECK_EQ(bus_proto_goal(p, 1, 4095u, 3400u, 255u, &addr, out,
                             sizeof(out)), 7u);
    CHECK_EQ(bus_proto_goal(p, 1, 4096u, 0u, 0u, &addr, out, sizeof(out)),
              0u);
    CHECK_EQ(bus_proto_goal(p, 1, 0u, 3401u, 0u, &addr, out, sizeof(out)),
              0u);
    CHECK_EQ(bus_proto_goal(p, 0, 1024u, 0u, 0u, &addr, out, sizeof(out)),
              0u);
    CHECK_EQ(bus_proto_goal(p, 0, 1023u, 1001u, 0u, &addr, out,
                             sizeof(out)), 0u);
    CHECK_EQ(bus_proto_goal(p, 1, 0u, 0u, 0u, &addr, out, 6u), 0u);
    CHECK_EQ(bus_proto_goal(p, 0, 0u, 0u, 0u, &addr, out, 6u), 6u);
    CHECK_EQ(bus_proto_goal(NULL, 0, 0u, 0u, 0u, &addr, out, 6u), 0u);
}

TEST_CASE(the_mode_check_reads_sts_mode_and_scs_angle_limits)
{
    const bus_proto_t *p = bus_proto_get(0);
    uint8_t addr = 0;
    static const uint8_t pos[1] = { 0 }, speed[1] = { 1 }, step[1] = { 3 };
    static const uint8_t lim0[4] = { 0, 0, 0, 0 };
    static const uint8_t lim_hi[4] = { 0, 0, 0x03, 0xFF };   /* 0..1023 */
    static const uint8_t lim_lo[4] = { 0, 1, 0, 0 };         /* 1..0    */

    /* STS: register 33, 1 byte; 0 is position. */
    CHECK_EQ(p->goal_check(1, &addr), 1u);
    CHECK_EQ(addr, FEETECH_REG_MODE);
    CHECK(p->goal_mode(1, pos, 1u));
    CHECK(!p->goal_mode(1, speed, 1u));
    CHECK(!p->goal_mode(1, step, 1u));
    CHECK(!p->goal_mode(1, pos, 0u));
    CHECK(!p->goal_mode(1, pos, 2u));
    /* SCS: no mode register; both limits 0 run it as a motor. */
    CHECK_EQ(p->goal_check(0, &addr), 4u);
    CHECK_EQ(addr, FEETECH_REG_MIN_ANGLE);
    CHECK(!p->goal_mode(0, lim0, 4u));
    CHECK(p->goal_mode(0, lim_hi, 4u));
    CHECK(p->goal_mode(0, lim_lo, 4u));
    CHECK(!p->goal_mode(0, lim_hi, 3u));
    CHECK(!p->goal_mode(0, NULL, 4u));
    CHECK_EQ(p->goal_check(0, NULL), 0u);
    CHECK_EQ(p->goal_check(7, &addr), 0u);      /* no such series */
    CHECK(!p->goal_mode(7, lim_hi, 4u));
}

int main(void)
{
    RUN(the_mode_check_reads_sts_mode_and_scs_angle_limits);
    RUN(a_coprocessor_without_the_page_is_sent_nothing);
    RUN(a_command_is_refused_for_none_and_beyond_the_last);
    RUN(open_and_close_through_the_page);
    RUN(an_open_in_flight_holds_the_bench);
    RUN(a_scan_of_0_to_253_reads_the_ids_found);
    RUN(a_scan_to_254_is_refused_by_the_page);
    RUN(a_write_goes_out_in_three_staged_windows);
    RUN(a_goal_of_16_bytes_reaches_the_servo_in_its_byte_order);
    RUN(an_eeprom_write_needs_the_key);
    RUN(a_lost_answer_sends_the_same_frame_and_the_page_takes_it_once);
    RUN(three_exchanges_without_an_answer_give_the_command_up);
    RUN(a_frame_refused_for_its_seq_goes_again_after_a_read);
    RUN(a_refused_window_or_a_frame_refused_twice_ends_nack);
    RUN(stop_drops_a_command_not_sent_and_stops_the_bus);
    RUN(stop_goes_before_a_command_whose_frame_went_unanswered);
    RUN(stop_closes_an_open_whose_frame_went_unanswered);
    RUN(stop_sends_nothing_to_a_closed_bus);
    RUN(the_link_lost_ends_a_command_down);
    RUN(a_page_that_goes_ends_a_command_down);
    RUN(the_page_is_read_every_500_ms_closed_and_50_ms_open);
    RUN(the_protocol_table_has_feetech_and_nothing_else);
    return test_summary("bus_link");
}
