/*
 * The sensor bus.  See sense_bus.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_bus.h"

#include <string.h>

void sense_bus_init(sense_bus_t *b, const sense_i2c_t *io)
{
    memset(b, 0, sizeof *b);
    b->io = *io;
}

/* Every transaction that reaches the wire comes back through here: the
 * failure count, and the stuck decision. */
static sense_err_t settle(sense_bus_t *b, sense_err_t e)
{
    if (e != SENSE_OK) {
        ++b->errors;              /* modulo 65536, as the register reports */
    }
    b->trial = false;
    switch (e) {
    case SENSE_OK:
    case SENSE_NACK:
        /* The address byte went out and the lines came back. */
        b->timeouts  = 0u;
        b->stuck     = false;
        b->recovered = false;
        break;
    case SENSE_TIMEOUT:
        if (b->timeouts < SENSE_STUCK_TIMEOUTS) {
            ++b->timeouts;
        }
        if (b->timeouts >= SENSE_STUCK_TIMEOUTS) {
            b->stuck = true;
        }
        break;
    default:
        b->stuck = true;          /* SENSE_BUS_LOW */
        break;
    }
    return e;
}

/* Whether a transaction may go out: not while stuck, except the one test
 * after a recovery. */
static bool may_send(const sense_bus_t *b)
{
    return !b->stuck || b->trial;
}

sense_err_t sense_bus_read(sense_bus_t *b, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n)
{
    if (!may_send(b)) {
        return SENSE_STUCK;
    }
    return settle(b, b->io.read(b->io.ctx, addr, reg, buf, n));
}

sense_err_t sense_bus_write16(sense_bus_t *b, uint8_t addr, uint8_t reg,
                              uint16_t value)
{
    if (!may_send(b)) {
        return SENSE_STUCK;
    }
    const uint8_t buf[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFFu) };
    return settle(b, b->io.write(b->io.ctx, addr, reg, buf, sizeof buf));
}

void sense_bus_note(sense_bus_t *b, sense_err_t e)
{
    switch (e) {
    case SENSE_OK:
    case SENSE_NACK:
        /* The address went out and the lines came back: the bus works. */
        (void)settle(b, SENSE_OK);
        break;
    case SENSE_TIMEOUT:
    case SENSE_BUS_LOW:
        (void)settle(b, e);
        break;
    default:
        break;                    /* nothing reached the wire */
    }
}

bool sense_bus_recovery_due(const sense_bus_t *b, uint32_t now_ms)
{
    if (!b->stuck) {
        return false;
    }
    return !b->recovered || (uint32_t)(now_ms - b->recover_at) >= SENSE_RECOVER_MS;
}

void sense_bus_recovered(sense_bus_t *b, uint32_t now_ms)
{
    ++b->recoveries;
    if (!b->stuck) {
        return;                   /* nothing to test, nothing to time */
    }
    b->recovered  = true;
    b->recover_at = now_ms;
    b->trial      = true;
}

/* ----------------------------------------------------------- one part */

void sense_part_init(sense_part_t *p, sense_bus_t *bus, uint8_t addr)
{
    memset(p, 0, sizeof *p);
    p->bus   = bus;
    p->addr  = addr;
    p->state = SENSE_PART_UNPROBED;
}

bool sense_part_probe_due(sense_part_t *p, uint32_t now_ms)
{
    p->now = now_ms;
    switch (p->state) {
    case SENSE_PART_UNPROBED:
        return true;
    case SENSE_PART_ABSENT:
    case SENSE_PART_WRONG_ID:
    case SENSE_PART_REFUSED:
    case SENSE_PART_OFFLINE:
        return (int32_t)(now_ms - p->probe_at) >= 0;
    default:
        return false;             /* online, or no set-up */
    }
}

void sense_part_probed(sense_part_t *p, sense_state_t state)
{
    p->state    = state;
    p->fails    = 0u;
    p->probe_at = p->now + SENSE_RETRY_MS;
}

static sense_err_t count(sense_part_t *p, sense_err_t e)
{
    if (e == SENSE_OK) {
        p->fails = 0u;
        return e;
    }
    p->last_err = e;
    if (++p->fails >= SENSE_FAILS) {
        p->state    = SENSE_PART_OFFLINE;
        p->fails    = 0u;
        p->probe_at = p->now + SENSE_RETRY_MS;
    }
    return e;
}

sense_err_t sense_part_read(sense_part_t *p, uint8_t reg,
                            uint8_t *buf, size_t n)
{
    if (p->state != SENSE_PART_ONLINE) {
        return SENSE_OFFLINE;
    }
    return count(p, sense_bus_read(p->bus, p->addr, reg, buf, n));
}

sense_err_t sense_part_write16(sense_part_t *p, uint8_t reg, uint16_t value)
{
    if (p->state != SENSE_PART_ONLINE) {
        return SENSE_OFFLINE;
    }
    return count(p, sense_bus_write16(p->bus, p->addr, reg, value));
}

void sense_part_sample_failed(sense_part_t *p, uint8_t before)
{
    if (p->state != SENSE_PART_ONLINE) {
        return;
    }
    p->fails = (uint8_t)(before + 1u);
    if (p->fails >= SENSE_FAILS) {
        p->state    = SENSE_PART_OFFLINE;
        p->fails    = 0u;
        p->probe_at = p->now + SENSE_RETRY_MS;
    }
}

void sense_part_lost(sense_part_t *p)
{
    if (p->state != SENSE_PART_ONLINE) {
        return;
    }
    p->state    = SENSE_PART_OFFLINE;
    p->fails    = 0u;
    p->probe_at = p->now + SENSE_RETRY_MS;
}

/* ------------------------------------------------------------- codec */

uint16_t sense_be16(const uint8_t *b)
{
    return (uint16_t)(((unsigned)b[0] << 8) | b[1]);
}

uint32_t sense_be24(const uint8_t *b)
{
    return ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
}

uint64_t sense_be40(const uint8_t *b)
{
    return ((uint64_t)b[0] << 32) | ((uint64_t)b[1] << 24)
         | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 8) | b[4];
}

/* Two's complement by subtraction rather than a signed shift, which C
 * leaves to the implementation for negative numbers. */
int32_t sense_sext32(uint32_t v, unsigned bits)
{
    const uint32_t sign = 1u << (bits - 1u);
    const uint32_t mask = (bits >= 32u) ? 0xFFFFFFFFu : ((sign << 1) - 1u);
    v &= mask;
    if ((v & sign) == 0u) {
        return (int32_t)v;
    }
    return -(int32_t)((mask - v) & mask) - 1;
}

int64_t sense_sext64(uint64_t v, unsigned bits)
{
    const uint64_t sign = (uint64_t)1u << (bits - 1u);
    const uint64_t mask = (bits >= 64u) ? ~(uint64_t)0u : ((sign << 1) - 1u);
    v &= mask;
    if ((v & sign) == 0u) {
        return (int64_t)v;
    }
    return -(int64_t)((mask - v) & mask) - 1;
}

int64_t sense_muldiv(int64_t a, uint32_t mul, uint64_t div)
{
    const int64_t d = (int64_t)div;
    return (a / d) * (int64_t)mul + ((a % d) * (int64_t)mul) / d;
}
