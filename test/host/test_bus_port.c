/*
 * The BUS page on a programming port (shared/outputs/bus_port.c) against
 * the simulated Feetech servos of feetech_sim.h: the way out of PWM
 * (pulse-width modulation) and back, every refusal, the command frame and
 * its SEQ, the staged registers and their CRC (cyclic redundancy check),
 * each operation through the page, the key of an EEPROM write, STOP and
 * CLOSE during a scan and an EEPROM write, the link lost, a driver that
 * hangs or starts no torque-off frame, ARM refused while the port is held,
 * and the opening wait across the wrap of the microsecond clock.
 *
 * The clock: a pass is 100 us.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bus_port.h"
#include "feetech_sim.h"

#define PORT  0u
#define PIN   13u
#define STS   true
#define SCS   false

static ftsim_bus_t sim;
static bus_port_t g_port;
static uint32_t now;

static struct {
    uint8_t  pin[LINK_PG_PORTS_MAX];
    bool     held;
    bool     take_ok;
    bool     taken;
    unsigned takes, gives;
    uint8_t  take_pin;
    uint32_t take_baud;
} g_hw;

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return g_hw.pin[port];
}

static bool hw_held(void *ctx, uint8_t port)
{
    (void)ctx;
    (void)port;
    return g_hw.held;
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin, uint32_t baud)
{
    (void)ctx;
    (void)port;
    g_hw.takes++;
    g_hw.take_pin = pin;
    g_hw.take_baud = baud;
    g_hw.taken = g_hw.take_ok;
    if (g_hw.take_ok) {
        sim.baud = baud;
    }
    return g_hw.take_ok;
}

static void hw_give(void *ctx)
{
    (void)ctx;
    g_hw.gives++;
    g_hw.taken = false;
}

static void setup_at(uint32_t start_us)
{
    bus_port_hw_t hw;

    memset(&g_hw, 0, sizeof(g_hw));
    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        g_hw.pin[i] = 0xFFu;
    }
    g_hw.pin[PORT] = PIN;
    g_hw.take_ok = true;
    ftsim_init(&sim, 1000000u);
    memset(&hw, 0, sizeof(hw));
    hw.ports = 2;
    hw.pin = hw_pin;
    hw.held = hw_held;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.driver = ftsim_driver(&sim);
    now = start_us;
    CHECK(bus_port_init(&g_port, &hw));
}

static void setup(void)
{
    setup_at(0);
}

static uint16_t reg(uint8_t r)
{
    uint16_t v = 0;

    bus_port_read(&g_port, r, 1, &v);
    return v;
}

static uint8_t refusal(void)
{
    return (uint8_t)(reg(LINK_BS_RESULT) >> 8);
}

static uint8_t result(void)
{
    return (uint8_t)(reg(LINK_BS_RESULT) & 0xFFu);
}

static uint8_t state(void)
{
    return LINK_BS_STATE_OF(reg(LINK_BS_STATE));
}

/* A command with the next SEQ: the NACK, 0 when taken. */
static uint8_t cmd_full(link_bus_op_t op, uint8_t port, uint8_t proto,
                        bool sts, uint16_t arg, uint16_t aux)
{
    uint16_t f[LINK_BS_W_CMD_FRAME];

    f[LINK_BS_W_CMD] = LINK_BS_W_CMD_OF(op, port, proto, sts);
    f[LINK_BS_W_SEQ] = (uint16_t)(reg(LINK_BS_SEQ) + 1u);
    f[LINK_BS_W_ARG] = arg;
    f[LINK_BS_W_AUX] = aux;
    return bus_port_write(&g_port, 0, LINK_BS_W_CMD_FRAME, f);
}

static uint8_t cmd(link_bus_op_t op, bool sts, uint16_t arg, uint16_t aux)
{
    return cmd_full(op, PORT, LINK_BUS_PROTO_FEETECH, sts, arg, aux);
}

/* Stage @p n bytes with @p key; returns the CRC a command names. */
static uint16_t stage(const uint8_t *bytes, uint8_t n, uint16_t key)
{
    uint16_t s[LINK_BS_W_STAGED];

    memset(s, 0, sizeof(s));
    s[0] = n;
    s[1] = key;
    for (unsigned i = 0; i < n && i < LINK_BS_BYTES; ++i) {
        s[2u + i / 2u] |= (uint16_t)(bytes[i] << (8u * (i % 2u)));
    }
    CHECK_EQ(bus_port_write(&g_port, LINK_BS_W_N, LINK_BS_W_STAGED, s), 0);
    return link_kst_crc(s, LINK_BS_W_STAGED);
}

static void pass(bool safe, bool stop)
{
    bus_port_step(&g_port, safe, stop, now);
    now += 100u;
}

/* Passes until the page is not busy, at most @p max. */
static void run(unsigned max)
{
    for (unsigned i = 0; i < max; ++i) {
        pass(true, false);
        if ((reg(LINK_BS_STATE) & LINK_BS_F_BUSY) == 0u && !sim.open) {
            return;
        }
    }
}

/* Safe, and the bus open at 1,000,000 baud. */
static void open_bus(void)
{
    pass(true, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    run(1000);
    CHECK_EQ(state(), LINK_BUS_OPEN);
}

/* Whether a logged frame is torque off by broadcast. */
static bool is_torque_off(const feetech_frame_t *f)
{
    return ftsim_is_write1(f, FEETECH_ID_BROADCAST, FEETECH_REG_TORQUE, 0);
}

static unsigned torque_offs(void)
{
    unsigned k = 0;

    for (unsigned i = 0; i < sim.n_log; ++i) {
        k += is_torque_off(&sim.log[i]) ? 1u : 0u;
    }
    return k;
}

/* ------------------------------------------------------------- the way out */

TEST_CASE(init_refuses_missing_hardware_and_starts_closed)
{
    bus_port_hw_t hw;
    bus_port_t p;

    setup();
    CHECK(!bus_port_init(NULL, &g_port.hw));
    CHECK(!bus_port_init(&p, NULL));
    hw = g_port.hw;
    hw.held = NULL;
    CHECK(!bus_port_init(&p, &hw));
    hw = g_port.hw;
    hw.driver.set_baud = NULL;
    CHECK(!bus_port_init(&p, &hw));
    hw = g_port.hw;
    hw.ports = 0;
    CHECK(!bus_port_init(&p, &hw));
    hw.ports = LINK_PG_PORTS_MAX + 1u;
    CHECK(!bus_port_init(&p, &hw));
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(reg(LINK_BS_PIN), LINK_PG_PIN_NONE);
    CHECK_EQ(reg(LINK_BS_WATCH) & 0xFFu, 0xFF);
    CHECK_EQ(reg(LINK_BS_SEQ), 0);
    CHECK_EQ(bus_port_quiet_mask(&g_port), 0);
    CHECK_EQ(bus_port_pin_mask(&g_port), 0);
}

static void open_waits_50_ms(uint32_t t0)
{
    setup_at(t0);
    pass(true, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 3, 0), 0);
    /* The pulse ends from this frame on; the pin stays the generator's. */
    CHECK_EQ(bus_port_quiet_mask(&g_port), 1u << PORT);
    CHECK_EQ(bus_port_pin_mask(&g_port), 0);
    CHECK_EQ(state(), LINK_BUS_OPENING);
    CHECK_EQ(result(), FEETECH_RES_BUSY);
    CHECK(reg(LINK_BS_STATE) & LINK_BS_F_BUSY);
    for (unsigned i = 0; i < BUS_PORT_STOP_MS * 10u; ++i) {
        pass(true, false);
    }
    CHECK_EQ(g_hw.takes, 0);
    pass(true, false);
    CHECK_EQ(g_hw.takes, 1);
    CHECK_EQ(g_hw.take_pin, PIN);
    CHECK_EQ(g_hw.take_baud, 128000u);
    CHECK_EQ(state(), LINK_BUS_OPEN);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(reg(LINK_BS_PIN), PIN);
    CHECK_EQ(reg(LINK_BS_OP), LINK_BUS_OP_OPEN | (3u << 8));
    CHECK_EQ(bus_port_pin_mask(&g_port), 1u << PORT);
    CHECK_EQ(reg(LINK_BS_STATE) & LINK_BS_F_BUSY, 0);
    CHECK(reg(LINK_BS_STATE) & LINK_BS_F_SAFE);
}

TEST_CASE(open_quiets_the_port_50_ms_then_takes_the_pin)
{
    open_waits_50_ms(0);
    /* And with the microsecond clock wrapping inside the wait. */
    open_waits_50_ms(0u - 20000u);
}

TEST_CASE(open_is_refused_for_each_reason)
{
    setup();
    pass(true, false);
    bus_port_bench_armed(&g_port, true);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_ARMED);
    bus_port_bench_armed(&g_port, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 8, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd_full(LINK_BUS_OP_OPEN, 1, 0, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NO_PIN);   /* port 1 has no pin */
    CHECK_EQ(cmd_full(LINK_BUS_OP_OPEN, 2, 0, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_PORT);     /* the build has 2 */
    CHECK_EQ(cmd_full(LINK_BUS_OP_OPEN, PORT, 1, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_PROTOCOL);
    g_hw.held = true;
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NO_PIN);   /* the KST page has it */
    g_hw.held = false;
    pass(false, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_UNSAFE);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(bus_port_quiet_mask(&g_port), 0);
    pass(true, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_BUSY);
}

TEST_CASE(open_checks_the_whole_baud_argument)
{
    setup();
    pass(true, false);
    /* A high byte with a valid low byte is out of range. */
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0x0100, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0xFF07, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, FEETECH_BAUD_COUNT, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(bus_port_quiet_mask(&g_port), 0);
    /* The last index opens. */
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, FEETECH_BAUD_COUNT - 1u, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    run(1000);
    CHECK_EQ(state(), LINK_BUS_OPEN);
    CHECK_EQ(g_hw.take_baud, 38400u);
}

TEST_CASE(a_driver_that_does_not_open_returns_the_pin)
{
    setup();
    g_hw.take_ok = false;
    pass(true, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    run(1000);
    CHECK_EQ(g_hw.takes, 1);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(result(), FEETECH_RES_DRIVER);
    CHECK_EQ(bus_port_quiet_mask(&g_port), 0);
    CHECK_EQ(bus_port_pin_mask(&g_port), 0);
}

TEST_CASE(nothing_but_open_is_taken_while_closed)
{
    setup();
    pass(true, false);
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 0x1401, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NOT_OPEN);
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NOT_OPEN);
    CHECK_EQ(cmd(LINK_BUS_OP_CLOSE, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NOT_OPEN);
    CHECK_EQ(cmd(LINK_BUS_OP_TORQUE_OFF, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NOT_OPEN);
    CHECK_EQ(sim.n_start, 0);
}

/* ------------------------------------------------------- the command frame */

TEST_CASE(a_command_frame_is_taken_whole_and_once)
{
    uint16_t f[LINK_BS_W_CMD_FRAME] = { 0 };
    uint16_t seq;

    setup();
    pass(true, false);
    CHECK_EQ(bus_port_write(&g_port, 0, 3, f), LINK_NACK_BAD_RANGE);
    CHECK_EQ(bus_port_write(&g_port, 1, 4, f), LINK_NACK_BAD_RANGE);
    CHECK_EQ(bus_port_write(&g_port, 0, 0, f), LINK_NACK_BAD_RANGE);
    CHECK_EQ(bus_port_write(&g_port, 40, 6, f), LINK_NACK_BAD_RANGE);
    CHECK_EQ(bus_port_write(&g_port, 14, 1, f), LINK_NACK_READ_ONLY);
    CHECK_EQ(bus_port_write(&g_port, 13, 2, f), LINK_NACK_READ_ONLY);
    CHECK_EQ(bus_port_write(NULL, 0, 4, f), LINK_NACK_BAD_RANGE);
    /* Op 0, an op beyond the last, a spare bit. */
    f[LINK_BS_W_SEQ] = 1;
    CHECK_EQ(bus_port_write(&g_port, 0, 4, f), LINK_NACK_BAD_VALUE);
    f[LINK_BS_W_CMD] = LINK_BUS_OP_COUNT;
    CHECK_EQ(bus_port_write(&g_port, 0, 4, f), LINK_NACK_BAD_VALUE);
    f[LINK_BS_W_CMD] = LINK_BUS_OP_OPEN | 0x0020u;
    CHECK_EQ(bus_port_write(&g_port, 0, 4, f), LINK_NACK_BAD_VALUE);
    /* A SEQ that is not the next one. */
    f[LINK_BS_W_CMD] = LINK_BUS_OP_OPEN;
    f[LINK_BS_W_SEQ] = 2;
    CHECK_EQ(bus_port_write(&g_port, 0, 4, f), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_BS_SEQ), 0);
    f[LINK_BS_W_SEQ] = 1;
    CHECK_EQ(bus_port_write(&g_port, 0, 4, f), 0);
    CHECK_EQ(reg(LINK_BS_SEQ), 1);
    CHECK_EQ(state(), LINK_BUS_OPENING);
    /* The same frame again: acknowledged, nothing done. */
    CHECK_EQ(bus_port_write(&g_port, 0, 4, f), 0);
    CHECK_EQ(reg(LINK_BS_SEQ), 1);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    /* SEQ wraps from 65535 to 0. */
    g_port.seq = 0xFFFFu;
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    seq = reg(LINK_BS_SEQ);
    CHECK_EQ(seq, 0);
}

TEST_CASE(the_staged_registers_are_taken_in_any_windows)
{
    const uint16_t a[3] = { 2, 0, 0x0800 };
    const uint16_t b[7] = { 0, 0, 0, 0, 0, 0, 0 };
    uint16_t all[LINK_BS_W_STAGED];

    setup();
    open_bus();
    CHECK_EQ(bus_port_write(&g_port, LINK_BS_W_N, 3, a), 0);
    CHECK_EQ(bus_port_write(&g_port, LINK_BS_W_N + 3u, 7, b), 0);
    memcpy(all, a, sizeof(a));
    memcpy(&all[3], b, sizeof(b));
    ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(cmd(LINK_BUS_OP_WRITE, STS, 1u | (FEETECH_REG_GOAL << 8),
                 link_kst_crc(all, LINK_BS_W_STAGED)), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    run(1000);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(feetech_get16(FEETECH_STS,
                           &ftsim_find(&sim, 1)->regs[FEETECH_REG_GOAL]), 2048);
}

/* ----------------------------------------------------------- the operations */

TEST_CASE(a_scan_through_the_page_finds_ids_0_and_253)
{
    setup();
    open_bus();
    ftsim_add(&sim, 0, FEETECH_STS);
    ftsim_add(&sim, 17, FEETECH_SCS);
    ftsim_add(&sim, 253, FEETECH_STS);
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 254u << 8, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 5u | (4u << 8), 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 253u << 8, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(result(), FEETECH_RES_BUSY);
    /* Busy: everything but STOP and CLOSE is refused. */
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 1u, 1), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_BUSY);
    CHECK_EQ(cmd(LINK_BUS_OP_WATCH, STS, 1u, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_BUSY);
    run(20000);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_SCAN);
    CHECK_EQ(reg(LINK_BS_SCAN) >> 8, 3);
    CHECK_EQ(reg(LINK_BS_RANGE), 253u << 8);
    {
        uint16_t ids[LINK_BI_COUNT + 1];

        ids[LINK_BI_COUNT] = 0xBEEFu;
        bus_port_read_ids(&g_port, 0, LINK_BI_COUNT, ids);
        CHECK_EQ(ids[0], 0x0001);
        CHECK_EQ(ids[1], 0x0002);                    /* 17 */
        CHECK_EQ(ids[15], 0x2000);                   /* 253 */
        CHECK_EQ(ids[LINK_BI_COUNT], 0xBEEFu);
        /* A window past the 16 registers gives nothing. */
        ids[0] = 0xBEEFu;
        bus_port_read_ids(&g_port, 15, 2, ids);
        CHECK_EQ(ids[0], 0xBEEFu);
        bus_port_read_ids(&g_port, 15, 1, ids);
        CHECK_EQ(ids[0], 0x2000);
    }
}

TEST_CASE(a_read_puts_the_bytes_on_the_page)
{
    setup();
    open_bus();
    ftsim_add(&sim, 3, FEETECH_STS);
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 3u | (62u << 8), 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 3u | (62u << 8), 17), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 254u | (62u << 8), 2), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 3u | (250u << 8), 7), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 3u | (62u << 8), 2), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    run(1000);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(reg(LINK_BS_DATA), 74u | (31u << 8));
    CHECK_EQ(reg(LINK_BS_COUNTS) >> 8, 2);
    CHECK_EQ(reg(LINK_BS_AT) & 0xFFu, 62);
    CHECK_EQ(reg(LINK_BS_REPLY) >> 8, 3);
    /* No answer: the session's result. */
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 4u | (62u << 8), 2), 0);
    run(1000);
    CHECK_EQ(result(), FEETECH_RES_NO_REPLY);
    CHECK_EQ(reg(LINK_BS_REPLY) >> 8, 0xFF);
}

TEST_CASE(a_write_takes_ram_only_and_never_the_lock)
{
    const uint8_t two[2] = { 0, 0 };
    uint16_t crc;

    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_SCS);
    crc = stage(two, 2, 0);
    CHECK_EQ(cmd(LINK_BUS_OP_WRITE, SCS, 1u | (38u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_EEPROM);
    CHECK_EQ(cmd(LINK_BUS_OP_WRITE, SCS, 1u | (47u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_EEPROM);   /* covers SCS lock 48 */
    CHECK_EQ(cmd(LINK_BUS_OP_WRITE, STS, 1u | (54u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_EEPROM);   /* covers STS lock 55 */
    CHECK_EQ(cmd(LINK_BUS_OP_WRITE, SCS, 1u | (42u << 8), crc ^ 1u), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_STAGED);
    CHECK_EQ(cmd(LINK_BUS_OP_WRITE, SCS, 254u | (42u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    CHECK_EQ(sim.n_start, 0);
    {
        const uint8_t goal[2] = { 0x01, 0xF4 };  /* 500, high byte first */

        crc = stage(goal, 2, 0);
        CHECK_EQ(cmd(LINK_BUS_OP_WRITE, SCS, 1u | (42u << 8), crc), 0);
        CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
        run(1000);
        CHECK_EQ(result(), FEETECH_RES_OK);
        CHECK_EQ(feetech_get16(FEETECH_SCS,
                               &ftsim_find(&sim, 1)->regs[42]), 500);
    }
    /* A count of 0 or above 16 staged. */
    crc = stage(two, 0, 0);
    CHECK_EQ(cmd(LINK_BUS_OP_WRITE, SCS, 1u | (42u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
}

TEST_CASE(an_eeprom_write_needs_the_key_and_the_eeprom_area)
{
    const uint8_t id5[1] = { 5 };
    const uint8_t bad[1] = { 254 };
    uint16_t crc;

    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_STS);
    /* The key is judged first, before the CRC. */
    crc = stage(id5, 1, 0);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (5u << 8), crc ^ 1u), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NO_KEY);
    crc = stage(id5, 1, LINK_BUS_WRITE_KEY ^ 1u);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (5u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NO_KEY);
    crc = stage(id5, 1, LINK_BUS_WRITE_KEY);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (5u << 8), crc ^ 1u), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_STAGED);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (40u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_EEPROM);
    {
        const uint8_t four[4] = { 0, 0, 0, 0 };
        const uint16_t c4 = stage(four, 4, LINK_BUS_WRITE_KEY);

        CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (37u << 8), c4), 0);
        CHECK_EQ(refusal(), LINK_BUS_REF_EEPROM);
        CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (55u << 8), c4), 0);
        CHECK_EQ(refusal(), LINK_BUS_REF_EEPROM);  /* the lock alone */
    }
    crc = stage(bad, 1, LINK_BUS_WRITE_KEY);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (5u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);    /* ID 254 */
    CHECK_EQ(sim.n_start, 0);
    /* ID 1 to 5: unlocked while it runs, the new ID read back. */
    crc = stage(id5, 1, LINK_BUS_WRITE_KEY);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (5u << 8), crc), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    {
        bool seen_unlocked = false;

        for (unsigned i = 0; i < 1000u; ++i) {
            pass(true, false);
            seen_unlocked |= (reg(LINK_BS_STATE) & LINK_BS_F_UNLOCKED) != 0u;
            if (result() != FEETECH_RES_BUSY && !sim.open) {
                break;
            }
        }
        CHECK(seen_unlocked);
    }
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(reg(LINK_BS_STATE) & LINK_BS_F_UNLOCKED, 0);
    CHECK(ftsim_find(&sim, 5) != NULL);
    CHECK_EQ(ftsim_find(&sim, 5)->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(reg(LINK_BS_AT) & 0xFFu, 5);
    CHECK_EQ(reg(LINK_BS_DATA) & 0xFFu, 5);
    /* The lock flag alone. */
    {
        const uint8_t zero[1] = { 0 };

        crc = stage(zero, 1, LINK_BUS_WRITE_KEY);
        CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 5u | (55u << 8), crc), 0);
        CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
        run(1000);
        CHECK_EQ(result(), FEETECH_RES_OK);
        CHECK_EQ(ftsim_find(&sim, 5)->regs[FEETECH_REG_LOCK_STS], 0);
    }
}

TEST_CASE(a_baud_write_moves_the_page_with_the_line)
{
    const uint8_t idx2[1] = { 2 };
    uint16_t crc;

    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_STS);
    crc = stage(idx2, 1, LINK_BUS_WRITE_KEY);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (6u << 8), crc), 0);
    run(1000);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(sim.baud, 250000u);
    CHECK_EQ(reg(LINK_BS_OP) >> 8, 2);
    /* BAUD: torque off to every servo at the old rate, then the line;
     * the servos keep theirs. */
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 8, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    ftsim_find(&sim, 1)->regs[FEETECH_REG_TORQUE] = 1;
    sim.n_log = 0;
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 7, 0), 0);
    CHECK_EQ(result(), FEETECH_RES_BUSY);
    CHECK(reg(LINK_BS_STATE) & LINK_BS_F_BUSY);
    run(100);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(sim.n_log, 1u);
    CHECK(is_torque_off(&sim.log[0]));
    CHECK_EQ(sim.log_baud[0], 250000u);
    CHECK_EQ(ftsim_find(&sim, 1)->regs[FEETECH_REG_TORQUE], 0);
    CHECK_EQ(sim.baud, 38400u);
    CHECK_EQ(reg(LINK_BS_OP), LINK_BUS_OP_BAUD | (7u << 8));
    sim.refuse_baud = true;
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    run(100);
    CHECK_EQ(result(), FEETECH_RES_DRIVER);
    CHECK_EQ(reg(LINK_BS_OP) >> 8, 7);
    /* A torque off that never starts: DRIVER, the line stays. */
    sim.refuse_baud = false;
    sim.refuse_start = true;
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    run(100);
    CHECK_EQ(result(), FEETECH_RES_DRIVER);
    CHECK_EQ(g_port.baud_due, 0);
    CHECK_EQ(sim.baud, 38400u);
    sim.refuse_start = false;
    /* A lock owed: BAUD refused with nothing sent, until STOP sent it. */
    g_port.bus.unlocked = 1;
    g_port.bus.unlocked_id = 1;
    g_port.bus.unlocked_series = FEETECH_STS;
    run(10);
    CHECK(reg(LINK_BS_STATE) & LINK_BS_F_UNLOCKED);
    sim.n_log = 0;
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_LOCK);
    run(100);
    CHECK_EQ(sim.n_log, 0u);
    CHECK_EQ(sim.baud, 38400u);
    /* The servo answers at 250000 baud, the line runs at 38400: the
     * STOP's lock goes unanswered and stays owed. */
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    run(100);
    CHECK_EQ(result(), FEETECH_RES_NO_REPLY);
    CHECK_EQ(g_port.bus.unlocked, 1);
    CHECK(reg(LINK_BS_STATE) & LINK_BS_F_UNLOCKED);
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_LOCK);
    /* At the servo's rate the lock is answered and paid. */
    ftsim_find(&sim, 1)->regs[FEETECH_REG_BAUD] = 7;
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    run(100);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(g_port.bus.unlocked, 0);
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    run(100);
    CHECK_EQ(sim.baud, 1000000u);
}

/* Passes until a watch read is on the wire. */
static void until_a_watch_read_is_on_the_wire(void)
{
    for (unsigned i = 0; i < 1000u && !feetech_bus_on_wire(&g_port.bus);
         ++i) {
        pass(true, false);
    }
    CHECK(feetech_bus_on_wire(&g_port.bus));
}

TEST_CASE(a_baud_taken_during_a_watch_read_waits_for_its_end)
{
    setup();
    open_bus();
    ftsim_add(&sim, 2, FEETECH_STS);
    CHECK_EQ(cmd(LINK_BUS_OP_WATCH, STS, 2u, 0), 0);
    until_a_watch_read_is_on_the_wire();
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 4, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(result(), FEETECH_RES_BUSY);
    CHECK(reg(LINK_BS_STATE) & LINK_BS_F_BUSY);
    CHECK_EQ(sim.baud, 1000000u);
    /* Nothing else is taken until the rate moved. */
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 2u | (56u << 8), 2), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_BUSY);
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 4, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_BUSY);
    run(100);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(reg(LINK_BS_STATE) & LINK_BS_F_BUSY, 0);
    CHECK_EQ(sim.baud, 115200u);
    CHECK_EQ(reg(LINK_BS_OP), LINK_BUS_OP_BAUD | (4u << 8));
    /* The torque off went at the old rate, after the read. */
    CHECK(is_torque_off(&sim.log[sim.n_log - 1u])
          || is_torque_off(&sim.log[sim.n_log - 2u]));
    /* A driver that refuses the rate then: DRIVER, the rate stays. */
    until_a_watch_read_is_on_the_wire();
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    sim.refuse_baud = true;
    run(100);
    CHECK_EQ(result(), FEETECH_RES_DRIVER);
    CHECK_EQ(reg(LINK_BS_OP) >> 8, 4);
    sim.refuse_baud = false;
    /* STOP before the read ends: the rate never moves. */
    until_a_watch_read_is_on_the_wire();
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    run(1000);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_STOP);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(sim.baud, 115200u);
    CHECK_EQ(g_port.baud_due, 0);
    /* CLOSE likewise. */
    CHECK_EQ(cmd(LINK_BUS_OP_WATCH, STS, 2u, 0), 0);
    until_a_watch_read_is_on_the_wire();
    CHECK_EQ(cmd(LINK_BUS_OP_BAUD, STS, 0, 0), 0);
    CHECK_EQ(cmd(LINK_BUS_OP_CLOSE, STS, 0, 0), 0);
    run(1000);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(g_port.baud_due, 0);
    CHECK_EQ(sim.baud, 115200u);
}

TEST_CASE(the_watch_puts_the_live_block_on_the_page)
{
    setup();
    open_bus();
    ftsim_add(&sim, 2, FEETECH_STS);
    CHECK_EQ(cmd(LINK_BUS_OP_WATCH, STS, 254u, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_RANGE);
    /* After a read that failed: RESULT at the WATCH's SEQ is the
     * watch's, not the scan's. */
    sim.bad_checksum = true;
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 2u | (56u << 8), 2), 0);
    run(1000);
    CHECK_EQ(result(), FEETECH_RES_CHECKSUM);
    CHECK_EQ(cmd(LINK_BUS_OP_WATCH, STS, 2u, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_WATCH);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK(reg(LINK_BS_STATE) & LINK_BS_F_WATCH);
    for (unsigned i = 0; i < 1200u; ++i) {    /* 120 ms */
        pass(true, false);
    }
    CHECK_EQ(reg(LINK_BS_WATCH) & 0xFFu, 2);
    CHECK(reg(LINK_BS_WATCH_OK) >= 2u);
    CHECK_EQ(reg(LINK_BS_WATCH_BAD), 0);
    CHECK_EQ(reg(LINK_BS_COUNTS) & 0xFFu, FEETECH_LIVE_STS);
    /* Bytes 6 and 7 of the block: register 62 (7.4 V) and 63 (31 C). */
    CHECK_EQ(reg(LINK_BS_LIVE + 3), 74u | (31u << 8));
    /* The servo goes: the block empties, the result says why. */
    ftsim_find(&sim, 2)->present = false;
    for (unsigned i = 0; i < 600u; ++i) {     /* 60 ms */
        pass(true, false);
    }
    CHECK(reg(LINK_BS_WATCH_BAD) >= 1u);
    CHECK_EQ(reg(LINK_BS_WATCH) >> 8, FEETECH_RES_NO_REPLY);
    CHECK_EQ(reg(LINK_BS_COUNTS) & 0xFFu, 0);
    CHECK_EQ(cmd(LINK_BUS_OP_WATCH, STS, 0xFFu, 0), 0);
    CHECK_EQ(reg(LINK_BS_STATE) & LINK_BS_F_WATCH, 0);
}

/* ------------------------------------------------------------ the stops */

TEST_CASE(stop_during_a_scan_sends_torque_off_and_the_bus_stays_open)
{
    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(cmd(LINK_BUS_OP_WATCH, STS, 1u, 0), 0);
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 253u << 8, 0), 0);
    for (unsigned i = 0; i < 50u; ++i) {
        pass(true, false);
    }
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(result(), FEETECH_RES_BUSY);
    run(1000);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_STOP);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(torque_offs(), 1);
    CHECK(is_torque_off(&sim.log[sim.n_log - 1u]));
    CHECK_EQ(state(), LINK_BUS_OPEN);
    CHECK_EQ(reg(LINK_BS_STATE) & LINK_BS_F_WATCH, 0);
    /* The bus takes the next operation. */
    CHECK_EQ(cmd(LINK_BUS_OP_TORQUE_OFF, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    run(1000);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(torque_offs(), 2);
}

TEST_CASE(close_during_an_eeprom_write_relocks_then_gives_the_pin)
{
    const uint8_t ang[2] = { 0x00, 0x01 };
    uint16_t crc;

    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_STS);
    crc = stage(ang, 2, LINK_BUS_WRITE_KEY);
    CHECK_EQ(cmd(LINK_BUS_OP_EEPROM, STS, 1u | (9u << 8), crc), 0);
    /* Until the unlock is through. */
    while (sim.n_log < 1u || sim.open) {
        pass(true, false);
    }
    pass(true, false);
    CHECK_EQ(ftsim_find(&sim, 1)->regs[FEETECH_REG_LOCK_STS], 0);
    CHECK_EQ(cmd(LINK_BUS_OP_CLOSE, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(state(), LINK_BUS_CLOSING);
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 0x1401, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_BUSY);
    run(1000);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_CLOSE);
    CHECK_EQ(ftsim_find(&sim, 1)->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(torque_offs(), 1);
    CHECK_EQ(g_hw.gives, 1);
    CHECK(!g_hw.taken);
    CHECK_EQ(bus_port_quiet_mask(&g_port), 0);
    CHECK_EQ(reg(LINK_BS_PIN), LINK_PG_PIN_NONE);
}

TEST_CASE(a_lost_link_closes_the_bus_with_torque_off)
{
    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 20u << 8, 0), 0);
    for (unsigned i = 0; i < 30u; ++i) {
        pass(true, false);
    }
    pass(false, false);
    CHECK_EQ(state(), LINK_BUS_CLOSING);
    CHECK_EQ(result(), FEETECH_RES_ABORTED);
    CHECK_EQ(reg(LINK_BS_STATE) & LINK_BS_F_SAFE, 0);
    /* Commands are refused; the closing frames go out all the same. */
    CHECK_EQ(cmd(LINK_BUS_OP_SCAN, STS, 0x1401, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_UNSAFE);
    for (unsigned i = 0; i < 1000u && state() != LINK_BUS_CLOSED; ++i) {
        pass(false, false);
    }
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(result(), FEETECH_RES_ABORTED);
    CHECK_EQ(torque_offs(), 1);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_UNSAFE);
}

TEST_CASE(a_stop_of_the_bank_closes_the_bus)
{
    setup();
    open_bus();
    pass(true, true);
    CHECK_EQ(state(), LINK_BUS_CLOSING);
    run(1000);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(result(), FEETECH_RES_ABORTED);
    CHECK_EQ(torque_offs(), 1);
}

TEST_CASE(a_stop_while_opening_ends_with_nothing_sent)
{
    setup();
    pass(true, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    pass(true, false);
    pass(false, false);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(result(), FEETECH_RES_ABORTED);
    CHECK_EQ(g_hw.takes, 0);
    CHECK_EQ(g_hw.gives, 0);
    CHECK_EQ(bus_port_quiet_mask(&g_port), 0);
    /* And a CLOSE while opening. */
    pass(true, false);
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(cmd(LINK_BUS_OP_CLOSE, STS, 0, 0), 0);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_CLOSE);
    /* A STOP while opening closes as well and reads as the STOP. */
    CHECK_EQ(cmd(LINK_BUS_OP_OPEN, STS, 0, 0), 0);
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_NONE);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_STOP);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(sim.n_start, 0);
}

TEST_CASE(a_driver_that_hangs_still_closes)
{
    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_STS);
    sim.hang = true;
    CHECK_EQ(cmd(LINK_BUS_OP_READ, STS, 1u | (56u << 8), 2), 0);
    for (unsigned i = 0; i < 20u; ++i) {
        pass(true, false);
    }
    CHECK_EQ(cmd(LINK_BUS_OP_CLOSE, STS, 0, 0), 0);
    for (unsigned i = 0; i < 2000u && state() != LINK_BUS_CLOSED; ++i) {
        pass(true, false);
    }
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK_EQ(g_hw.gives, 1);
    /* The torque-off frame never started: the CLOSE says so. */
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_CLOSE);
    CHECK_EQ(result(), FEETECH_RES_DRIVER);
    CHECK_EQ(torque_offs(), 0);
}

TEST_CASE(a_stop_whose_torque_off_never_starts_says_driver)
{
    setup();
    open_bus();
    ftsim_add(&sim, 1, FEETECH_STS);
    sim.refuse_start = true;
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    run(1000);
    CHECK_EQ(reg(LINK_BS_OP) & 0xFFu, LINK_BUS_OP_STOP);
    CHECK_EQ(result(), FEETECH_RES_DRIVER);
    CHECK_EQ(state(), LINK_BUS_OPEN);
    /* Started on the second try: OK. */
    sim.refuse_start = false;
    sim.refuse_n = 1;
    CHECK_EQ(cmd(LINK_BUS_OP_STOP, STS, 0, 0), 0);
    run(1000);
    CHECK_EQ(result(), FEETECH_RES_OK);
    CHECK_EQ(torque_offs(), 1);
}

TEST_CASE(arm_is_refused_while_the_bus_holds_the_port)
{
    const uint16_t arm[3] = { LINK_ARM_ALL, 0, 0 };
    const uint16_t disarm[3] = { 0, 0, 0 };
    const uint16_t throttle[1] = { 0 };

    setup();
    /* Closed: nothing refused. */
    CHECK(!bus_port_refuses_arm(&g_port, LINK_CT_ARM, 3, arm));
    open_bus();
    CHECK(bus_port_refuses_arm(&g_port, LINK_CT_ARM, 3, arm));
    CHECK(bus_port_refuses_arm(&g_port, LINK_CT_ARM, 1, arm));
    /* A disarm, a frame without ARM, an empty one: taken. */
    CHECK(!bus_port_refuses_arm(&g_port, LINK_CT_ARM, 3, disarm));
    CHECK(!bus_port_refuses_arm(&g_port, 1, 1, throttle));
    CHECK(!bus_port_refuses_arm(&g_port, LINK_CT_ARM, 0, arm));
    CHECK(!bus_port_refuses_arm(&g_port, LINK_CT_ARM, 3, NULL));
    CHECK(!bus_port_refuses_arm(NULL, LINK_CT_ARM, 3, arm));
    /* Closing still holds the port. */
    CHECK_EQ(cmd(LINK_BUS_OP_CLOSE, STS, 0, 0), 0);
    CHECK_EQ(state(), LINK_BUS_CLOSING);
    CHECK(bus_port_refuses_arm(&g_port, LINK_CT_ARM, 3, arm));
    run(1000);
    CHECK_EQ(state(), LINK_BUS_CLOSED);
    CHECK(!bus_port_refuses_arm(&g_port, LINK_CT_ARM, 3, arm));
}

TEST_CASE(another_port_is_refused_while_one_is_open)
{
    setup();
    g_hw.pin[1] = 14;
    open_bus();
    CHECK_EQ(cmd_full(LINK_BUS_OP_SCAN, 1, 0, STS, 0x1401, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_PORT);
    CHECK_EQ(cmd_full(LINK_BUS_OP_STOP, 1, 0, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_PORT);
    CHECK_EQ(cmd_full(LINK_BUS_OP_OPEN, 1, 0, STS, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_BUS_REF_BUSY);
    CHECK_EQ(bus_port_quiet_mask(&g_port), 1u << PORT);
}

int main(void)
{
    RUN(init_refuses_missing_hardware_and_starts_closed);
    RUN(open_quiets_the_port_50_ms_then_takes_the_pin);
    RUN(open_is_refused_for_each_reason);
    RUN(open_checks_the_whole_baud_argument);
    RUN(a_driver_that_does_not_open_returns_the_pin);
    RUN(nothing_but_open_is_taken_while_closed);
    RUN(a_command_frame_is_taken_whole_and_once);
    RUN(the_staged_registers_are_taken_in_any_windows);
    RUN(a_scan_through_the_page_finds_ids_0_and_253);
    RUN(a_read_puts_the_bytes_on_the_page);
    RUN(a_write_takes_ram_only_and_never_the_lock);
    RUN(an_eeprom_write_needs_the_key_and_the_eeprom_area);
    RUN(a_baud_write_moves_the_page_with_the_line);
    RUN(a_baud_taken_during_a_watch_read_waits_for_its_end);
    RUN(the_watch_puts_the_live_block_on_the_page);
    RUN(stop_during_a_scan_sends_torque_off_and_the_bus_stays_open);
    RUN(close_during_an_eeprom_write_relocks_then_gives_the_pin);
    RUN(a_lost_link_closes_the_bus_with_torque_off);
    RUN(a_stop_of_the_bank_closes_the_bus);
    RUN(a_stop_while_opening_ends_with_nothing_sent);
    RUN(a_driver_that_hangs_still_closes);
    RUN(a_stop_whose_torque_off_never_starts_says_driver);
    RUN(arm_is_refused_while_the_bus_holds_the_port);
    RUN(another_port_is_refused_while_one_is_open);
    return test_summary("bus_port");
}
