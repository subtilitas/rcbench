/*
 * SPDX-License-Identifier: MIT
 */

#include "bus_port.h"

#include <stddef.h>
#include <string.h>

/* The page carries the session's results as the core numbers them. */
_Static_assert(FEETECH_RES_OK == 0 && FEETECH_RES_BUSY == 1
               && FEETECH_RES_NO_REPLY == 2 && FEETECH_RES_TRUNCATED == 3
               && FEETECH_RES_CHECKSUM == 4 && FEETECH_RES_WRONG_ID == 5
               && FEETECH_RES_LENGTH == 6 && FEETECH_RES_NOISE == 7
               && FEETECH_RES_FRAMING == 8 && FEETECH_RES_VERIFY == 9
               && FEETECH_RES_DRIVER == 10 && FEETECH_RES_ABORTED == 11
               && FEETECH_RES_ARG == 12, "feetech_res_t on the page");
_Static_assert(FEETECH_DATA_MAX == LINK_BS_BYTES
               && FEETECH_LIVE_STS <= LINK_BS_BYTES
               && 2u * LINK_BS_BYTE_REGS == LINK_BS_BYTES,
               "the bytes on the page");
_Static_assert(LINK_BS_W_N + LINK_BS_W_STAGED == LINK_BS_W_COUNT,
               "staged registers");
_Static_assert(LINK_BI_COUNT * 16 >= 254, "the scan's IDs");
_Static_assert(LINK_BUS_OP_COUNT <= 16, "op in 4 bits");

#define NO_ID 0xFFu

/* Bits of the command register no operation uses. */
#define CMD_SPARE 0x00E0u

static uint8_t baud_index(uint32_t baud)
{
    for (uint8_t i = 0; i < FEETECH_BAUD_COUNT; ++i) {
        if (feetech_baud(i) == baud) {
            return i;
        }
    }
    return FEETECH_BAUD_DEFAULT;
}

static void pack(const uint8_t *bytes, unsigned n, uint16_t *regs)
{
    for (unsigned k = 0; k < LINK_BS_BYTE_REGS; ++k) {
        const unsigned lo = 2u * k;

        regs[k] = (uint16_t)((lo < n ? bytes[lo] : 0u)
                             | ((lo + 1u < n ? (unsigned)bytes[lo + 1u] : 0u)
                                << 8));
    }
}

static void unpack(const uint16_t *regs, uint8_t *bytes)
{
    for (unsigned k = 0; k < LINK_BS_BYTE_REGS; ++k) {
        bytes[(size_t)k * 2u] = (uint8_t)(regs[k] & 0xFFu);
        bytes[(size_t)k * 2u + 1u] = (uint8_t)(regs[k] >> 8);
    }
}

/* --- the way out of PWM and back ----------------------------------------- */

static void go_closed(bus_port_t *p)
{
    const bool had_pin = p->has_pin != 0u;

    p->state = (uint8_t)LINK_BUS_CLOSED;
    p->has_pin = 0;
    p->running = 0;
    p->timed = 0;
    if (had_pin) {
        p->hw.give(p->hw.ctx);
    }
}

/* The session's stop, then the pin back.  @p asked: by CLOSE; else by a
 * stop or an unsafe link, which the result says. */
static void begin_close(bus_port_t *p, bool asked)
{
    p->op = (uint8_t)LINK_BUS_OP_CLOSE;
    p->result = asked ? (uint8_t)FEETECH_RES_BUSY
                      : (uint8_t)FEETECH_RES_ABORTED;
    if (p->state == (uint8_t)LINK_BUS_OPENING) {
        /* The pin is still the generator's: nothing went out. */
        go_closed(p);
        p->result = asked ? (uint8_t)FEETECH_RES_OK
                          : (uint8_t)FEETECH_RES_ABORTED;
        return;
    }
    feetech_bus_stop(&p->bus);
    p->running = asked ? 1u : 0u;
    p->state = (uint8_t)LINK_BUS_CLOSING;
}

static void step_opening(bus_port_t *p, uint32_t now_us)
{
    if (p->timed == 0u) {
        p->t0_us = now_us;
        p->timed = 1;
    }
    if ((uint32_t)(now_us - p->t0_us) < BUS_PORT_STOP_MS * 1000u) {
        return;
    }
    p->timed = 0;
    /* The port holds the pin from here, so the caller's binding leaves it
     * unbound. */
    p->has_pin = 1;
    if (!p->hw.take(p->hw.ctx, p->port, p->pin, feetech_baud(p->baud_idx))
        || !feetech_bus_init(&p->bus, &p->hw.driver,
                             feetech_baud(p->baud_idx))) {
        go_closed(p);
        p->result = (uint8_t)FEETECH_RES_DRIVER;
        return;
    }
    p->state = (uint8_t)LINK_BUS_OPEN;
    p->result = (uint8_t)FEETECH_RES_OK;
}

/* --- commands ------------------------------------------------------------- */

static void started(bus_port_t *p, link_bus_op_t op, feetech_res_t r)
{
    p->op = (uint8_t)op;
    if (r == FEETECH_RES_OK) {
        p->result = (uint8_t)FEETECH_RES_BUSY;
        p->running = 1;
    } else {
        p->result = (uint8_t)r;
        p->running = 0;
    }
}

/* Whether @p addr to @p addr + @p n covers @p reg. */
static bool covers(uint8_t addr, uint8_t n, uint8_t reg)
{
    return addr <= reg && (unsigned)reg < (unsigned)addr + n;
}

static link_bus_refusal_t write_op(bus_port_t *p, link_bus_op_t op,
                                   feetech_series_t series, const uint16_t *in)
{
    const uint8_t id = (uint8_t)(in[LINK_BS_W_ARG] & 0xFFu);
    const uint8_t addr = (uint8_t)(in[LINK_BS_W_ARG] >> 8);
    const uint16_t n16 = p->staged[0];      /* LINK_BS_W_N */
    const uint8_t lock = feetech_lock_addr(series);
    uint8_t bytes[LINK_BS_BYTES];

    if (op == LINK_BUS_OP_EEPROM
        && p->staged[LINK_BS_W_KEY - LINK_BS_W_N] != LINK_BUS_WRITE_KEY) {
        return LINK_BUS_REF_NO_KEY;
    }
    if (link_kst_crc(p->staged, LINK_BS_W_STAGED) != in[LINK_BS_W_AUX]) {
        return LINK_BUS_REF_STAGED;
    }
    if (id > FEETECH_ID_MAX || n16 == 0u || n16 > LINK_BS_BYTES
        || (unsigned)addr + n16 > 256u) {
        return LINK_BUS_REF_RANGE;
    }
    unpack(&p->staged[LINK_BS_W_DATA - LINK_BS_W_N], bytes);
    if (op == LINK_BUS_OP_WRITE) {
        /* RAM only, and never the series' lock flag. */
        if (addr < FEETECH_RAM_FIRST || covers(addr, (uint8_t)n16, lock)) {
            return LINK_BUS_REF_EEPROM;
        }
        started(p, op, feetech_bus_write(&p->bus, id, addr, bytes,
                                         (uint8_t)n16));
        return LINK_BUS_REF_NONE;
    }
    if (addr == lock ? n16 != 1u
                     : (unsigned)addr + n16 > FEETECH_RAM_FIRST) {
        return LINK_BUS_REF_EEPROM;
    }
    {
        const feetech_res_t r = feetech_bus_write_ee(&p->bus, series, id, addr,
                                                     bytes, (uint8_t)n16);

        if (r == FEETECH_RES_ARG) {
            return LINK_BUS_REF_RANGE;    /* a new ID or baud index */
        }
        p->at = addr;
        started(p, op, r);
    }
    return LINK_BUS_REF_NONE;
}

static link_bus_refusal_t open_op(bus_port_t *p, uint8_t port, uint8_t idx)
{
    uint8_t pin;

    if (p->state != (uint8_t)LINK_BUS_CLOSED) {
        return LINK_BUS_REF_BUSY;
    }
    if (p->armed != 0u) {
        return LINK_BUS_REF_ARMED;
    }
    if (idx >= FEETECH_BAUD_COUNT) {
        return LINK_BUS_REF_RANGE;
    }
    pin = p->hw.pin(p->hw.ctx, port);
    if (pin > OUT_MAX_PIN || p->hw.held(p->hw.ctx, port)) {
        return LINK_BUS_REF_NO_PIN;
    }
    p->port = port;
    p->pin = pin;
    p->baud_idx = idx;
    p->state = (uint8_t)LINK_BUS_OPENING;
    p->timed = 0;
    p->op = (uint8_t)LINK_BUS_OP_OPEN;
    p->result = (uint8_t)FEETECH_RES_BUSY;
    p->running = 0;
    return LINK_BUS_REF_NONE;
}

static link_bus_refusal_t dispatch(bus_port_t *p, link_bus_op_t op,
                                   uint8_t port, uint8_t proto,
                                   feetech_series_t series,
                                   const uint16_t *in)
{
    const link_bus_state_t state = (link_bus_state_t)p->state;
    const uint16_t arg = in[LINK_BS_W_ARG];

    if (proto != LINK_BUS_PROTO_FEETECH) {
        return LINK_BUS_REF_PROTOCOL;
    }
    if (port >= p->hw.ports) {
        return LINK_BUS_REF_PORT;
    }
    /* The ways to quiet are taken whatever else holds. */
    if (op == LINK_BUS_OP_STOP || op == LINK_BUS_OP_CLOSE) {
        if (state == LINK_BUS_CLOSED || state == LINK_BUS_CLOSING) {
            return LINK_BUS_REF_NOT_OPEN;
        }
        if (port != p->port) {
            return LINK_BUS_REF_PORT;
        }
        if (op == LINK_BUS_OP_CLOSE || state == LINK_BUS_OPENING) {
            begin_close(p, true);
            return LINK_BUS_REF_NONE;
        }
        feetech_bus_stop(&p->bus);
        p->op = (uint8_t)LINK_BUS_OP_STOP;
        p->result = (uint8_t)FEETECH_RES_BUSY;
        p->running = 1;
        return LINK_BUS_REF_NONE;
    }
    if (p->safe == 0u) {
        return LINK_BUS_REF_UNSAFE;
    }
    if (op == LINK_BUS_OP_OPEN) {
        return open_op(p, port, (uint8_t)arg);
    }
    if (state == LINK_BUS_CLOSED) {
        return LINK_BUS_REF_NOT_OPEN;
    }
    if (state != LINK_BUS_OPEN || feetech_bus_busy(&p->bus)) {
        return LINK_BUS_REF_BUSY;
    }
    if (port != p->port) {
        return LINK_BUS_REF_PORT;
    }
    switch (op) {
    case LINK_BUS_OP_SCAN: {
        const uint8_t from = (uint8_t)(arg & 0xFFu);
        const uint8_t to = (uint8_t)(arg >> 8);

        if (to > FEETECH_ID_MAX || from > to) {
            return LINK_BUS_REF_RANGE;
        }
        started(p, op, feetech_bus_scan(&p->bus, from, to));
        return LINK_BUS_REF_NONE;
    }
    case LINK_BUS_OP_READ: {
        const uint8_t id = (uint8_t)(arg & 0xFFu);
        const uint8_t addr = (uint8_t)(arg >> 8);
        const uint16_t n = in[LINK_BS_W_AUX];

        if (id > FEETECH_ID_MAX || n == 0u || n > LINK_BS_BYTES
            || (unsigned)addr + n > 256u) {
            return LINK_BUS_REF_RANGE;
        }
        p->at = addr;
        started(p, op, feetech_bus_read(&p->bus, id, addr, (uint8_t)n));
        return LINK_BUS_REF_NONE;
    }
    case LINK_BUS_OP_WRITE:
    case LINK_BUS_OP_EEPROM:
        return write_op(p, op, series, in);
    case LINK_BUS_OP_WATCH:
        if (arg != NO_ID && arg > FEETECH_ID_MAX) {
            return LINK_BUS_REF_RANGE;
        }
        feetech_bus_watch(&p->bus, (uint8_t)arg, series);
        return LINK_BUS_REF_NONE;
    case LINK_BUS_OP_TORQUE_OFF:
        started(p, op, feetech_bus_torque_off(&p->bus));
        return LINK_BUS_REF_NONE;
    case LINK_BUS_OP_BAUD:
        if (arg >= FEETECH_BAUD_COUNT) {
            return LINK_BUS_REF_RANGE;
        }
        p->op = (uint8_t)op;
        p->running = 0;
        p->result = (uint8_t)feetech_bus_set_baud(&p->bus,
                                                  feetech_baud(arg));
        if (p->result == (uint8_t)FEETECH_RES_OK) {
            p->baud_idx = (uint8_t)arg;
        }
        return LINK_BUS_REF_NONE;
    default:
        return LINK_BUS_REF_RANGE;
    }
}

static uint8_t command(bus_port_t *p, const uint16_t *in)
{
    const uint16_t cmd = in[LINK_BS_W_CMD];
    const uint8_t op = (uint8_t)(cmd & 0xFu);

    if ((cmd & CMD_SPARE) != 0u || op == (uint8_t)LINK_BUS_OP_NONE
        || op >= (uint8_t)LINK_BUS_OP_COUNT) {
        return LINK_NACK_BAD_VALUE;
    }
    /* The frame just taken, sent again: its answer was lost. */
    if (memcmp(in, p->last_cmd, sizeof(p->last_cmd)) == 0) {
        return 0u;
    }
    if (in[LINK_BS_W_SEQ] != (uint16_t)(p->seq + 1u)) {
        return LINK_NACK_BAD_VALUE;
    }
    p->seq = in[LINK_BS_W_SEQ];
    memcpy(p->last_cmd, in, sizeof(p->last_cmd));
    p->refusal = (uint8_t)dispatch(
        p, (link_bus_op_t)op, LINK_BS_PORT_OF(cmd), LINK_BS_PROTO_OF(cmd),
        (cmd & LINK_BS_W_CMD_STS) != 0u ? FEETECH_STS : FEETECH_SCS, in);
    return 0u;
}

uint8_t bus_port_write(bus_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_BS_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (off < LINK_BS_W_CMD_FRAME) {
        if (off != 0u || n != LINK_BS_W_CMD_FRAME) {
            return LINK_NACK_BAD_RANGE;
        }
        return command(p, in);
    }
    if ((unsigned)off + n > LINK_BS_W_COUNT) {
        return LINK_NACK_READ_ONLY;
    }
    for (unsigned i = 0; i < n; ++i) {
        p->staged[off + i - LINK_BS_W_N] = in[i];
    }
    return 0u;
}

/* --- the page --------------------------------------------------------------- */

static bool busy(const bus_port_t *p)
{
    const link_bus_state_t s = (link_bus_state_t)p->state;

    return s == LINK_BUS_OPENING || s == LINK_BUS_CLOSING
           || (s == LINK_BUS_OPEN && feetech_bus_busy(&p->bus));
}

static uint16_t state_reg(const bus_port_t *p)
{
    const bool live = p->state == (uint8_t)LINK_BUS_OPEN
                      || p->state == (uint8_t)LINK_BUS_CLOSING;
    uint16_t r = (uint16_t)(p->state | ((uint16_t)p->port << 8)
                            | ((uint16_t)p->proto << 12));

    r |= busy(p) ? LINK_BS_F_BUSY : 0u;
    r |= live && p->bus.watch_id != NO_ID ? LINK_BS_F_WATCH : 0u;
    r |= live && p->bus.unlocked != 0u ? LINK_BS_F_UNLOCKED : 0u;
    r |= p->safe != 0u ? LINK_BS_F_SAFE : 0u;
    return r;
}

void bus_port_read(const bus_port_t *p, uint8_t off, uint8_t n,
                   uint16_t *out)
{
    uint16_t all[LINK_BS_COUNT];
    const feetech_bus_t *b;
    bool live;

    if (p == NULL || out == NULL || (unsigned)off + n > LINK_BS_COUNT) {
        return;
    }
    b = &p->bus;
    live = p->state == (uint8_t)LINK_BUS_OPEN
           || p->state == (uint8_t)LINK_BUS_CLOSING;
    memset(all, 0, sizeof(all));
    all[LINK_BS_STATE] = state_reg(p);
    all[LINK_BS_SEQ] = p->seq;
    all[LINK_BS_OP] = (uint16_t)(p->op
                                 | ((uint16_t)(live ? baud_index(b->baud)
                                                    : p->baud_idx) << 8));
    all[LINK_BS_RESULT] = (uint16_t)(p->result | ((uint16_t)p->refusal << 8));
    all[LINK_BS_REPLY] = (uint16_t)(b->rx_error | ((uint16_t)b->rx_id << 8));
    all[LINK_BS_SCAN] = (uint16_t)(b->scan_at | ((uint16_t)b->found_n << 8));
    all[LINK_BS_RANGE] = (uint16_t)(b->scan_from
                                    | ((uint16_t)b->scan_to << 8));
    all[LINK_BS_WATCH] = (uint16_t)((live ? b->watch_id : NO_ID)
                                    | ((uint16_t)b->watch_res << 8));
    all[LINK_BS_WATCH_OK] = b->watch_ok;
    all[LINK_BS_WATCH_BAD] = b->watch_bad;
    pack(b->live, b->live_n, &all[LINK_BS_LIVE]);
    all[LINK_BS_COUNTS] = (uint16_t)(b->live_n | ((uint16_t)b->read_n << 8));
    all[LINK_BS_AT] = (uint16_t)(p->at | ((uint16_t)b->bad_n << 8));
    pack(b->read, b->read_n, &all[LINK_BS_DATA]);
    all[LINK_BS_PIN] = p->has_pin != 0u ? (uint16_t)p->pin
                                        : (uint16_t)LINK_PG_PIN_NONE;
    for (unsigned i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

void bus_port_read_ids(const bus_port_t *p, uint8_t off, uint8_t n,
                       uint16_t *out)
{
    if (p == NULL || out == NULL || (unsigned)off + n > LINK_BI_COUNT) {
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned k = off + i;

        out[i] = (uint16_t)(p->bus.found[(size_t)k * 2u]
                            | ((uint16_t)p->bus.found[(size_t)k * 2u + 1u]
                               << 8));
    }
}

/* --- the pass ----------------------------------------------------------------- */

void bus_port_step(bus_port_t *p, bool safe, bool stop, uint32_t now_us)
{
    if (p == NULL) {
        return;
    }
    p->safe = safe ? 1u : 0u;
    if ((stop || !safe)
        && (p->state == (uint8_t)LINK_BUS_OPENING
            || p->state == (uint8_t)LINK_BUS_OPEN)) {
        begin_close(p, false);
    }
    switch ((link_bus_state_t)p->state) {
    case LINK_BUS_OPENING:
        step_opening(p, now_us);
        break;
    case LINK_BUS_OPEN: {
        const feetech_res_t r = feetech_bus_step(&p->bus, now_us);

        if (p->running != 0u && r != FEETECH_RES_BUSY) {
            p->running = 0;
            /* A STOP ends as asked; the operation it cut reads ABORTED
             * until then. */
            p->result = p->op == (uint8_t)LINK_BUS_OP_STOP
                            ? (uint8_t)FEETECH_RES_OK : (uint8_t)r;
        }
        break;
    }
    case LINK_BUS_CLOSING:
        (void)feetech_bus_step(&p->bus, now_us);
        if (!feetech_bus_busy(&p->bus) && !feetech_bus_on_wire(&p->bus)) {
            const uint8_t asked = p->running;

            go_closed(p);
            if (asked != 0u) {
                p->result = (uint8_t)FEETECH_RES_OK;
            }
        }
        break;
    case LINK_BUS_CLOSED:
    default:
        break;
    }
}

bool bus_port_init(bus_port_t *p, const bus_port_hw_t *hw)
{
    if (p == NULL) {
        return false;
    }
    memset(p, 0, sizeof(*p));
    p->bus.watch_id = NO_ID;
    p->bus.rx_id = NO_ID;
    if (hw == NULL || hw->ports == 0u || hw->ports > LINK_PG_PORTS_MAX
        || hw->pin == NULL || hw->held == NULL || hw->take == NULL
        || hw->give == NULL || hw->driver.start == NULL
        || hw->driver.poll == NULL || hw->driver.set_baud == NULL) {
        return false;
    }
    p->hw = *hw;
    p->proto = (uint8_t)LINK_BUS_PROTO_FEETECH;
    return true;
}

link_bus_state_t bus_port_state(const bus_port_t *p)
{
    return p != NULL ? (link_bus_state_t)p->state : LINK_BUS_CLOSED;
}

uint8_t bus_port_quiet_mask(const bus_port_t *p)
{
    return (uint8_t)(p != NULL && p->state != (uint8_t)LINK_BUS_CLOSED
                         ? 1u << p->port : 0u);
}

uint8_t bus_port_pin_mask(const bus_port_t *p)
{
    return (uint8_t)(p != NULL && p->has_pin != 0u ? 1u << p->port : 0u);
}

void bus_port_bench_armed(bus_port_t *p, bool armed)
{
    if (p != NULL) {
        p->armed = armed ? 1u : 0u;
    }
}
