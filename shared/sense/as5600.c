/*
 * The AS5600 codec and driver.  See as5600.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "as5600.h"

uint16_t as5600_u12(const uint8_t *b)
{
    return (uint16_t)(sense_be16(b) & 0x0FFFu);
}

bool as5600_status_possible(uint8_t status)
{
    const uint8_t both = (uint8_t)(AS5600_STATUS_ML | AS5600_STATUS_MH);
    return (status & both) != both;
}

bool as5600_md(uint8_t status)
{
    return (status & AS5600_STATUS_MD) != 0u;
}

bool as5600_ml(uint8_t status)
{
    return (status & AS5600_STATUS_ML) != 0u;
}

bool as5600_mh(uint8_t status)
{
    return (status & AS5600_STATUS_MH) != 0u;
}

int16_t as5600_delta(uint16_t a, uint16_t b)
{
    const int32_t d = (int32_t)((uint32_t)(a - b) & (AS5600_COUNTS - 1u));
    return (int16_t)((d >= (int32_t)(AS5600_COUNTS / 2u))
                         ? d - (int32_t)AS5600_COUNTS
                         : d);
}

int32_t as5600_cdeg(int32_t counts)
{
    /* 64 bits: counts * 36000 leaves 32 bits above |counts| 59,652. */
    const int64_t n = (int64_t)counts * AS5600_CDEG_TURN;
    const int64_t h = (int64_t)(AS5600_COUNTS / 2u);
    const int64_t r = (n >= 0) ? (n + h) / (int64_t)AS5600_COUNTS
                               : -((-n + h) / (int64_t)AS5600_COUNTS);
    /* The result passes 32 bits above |counts| 244,335,917: saturate. */
    if (r > INT32_MAX) {
        return INT32_MAX;
    }
    return (r < -INT32_MAX) ? -INT32_MAX : (int32_t)r;
}

/* -------------------------------------------------------------- driver */

void as5600_init(as5600_t *d, sense_bus_t *bus)
{
    sense_part_init(&d->part, bus, AS5600_ADDR);
}

static sense_state_t probe(as5600_t *d)
{
    uint8_t b[1];
    if (sense_bus_read(d->part.bus, d->part.addr, AS5600_REG_STATUS, b,
                       sizeof b) != SENSE_OK) {
        return SENSE_PART_ABSENT;
    }
    d->part.id_device = b[0];
    return as5600_status_possible(b[0]) ? SENSE_PART_ONLINE
                                        : SENSE_PART_WRONG_ID;
}

bool as5600_step(as5600_t *d, uint32_t now_ms)
{
    if (sense_part_probe_due(&d->part, now_ms)) {
        sense_part_probed(&d->part, probe(d));
    }
    return d->part.state == SENSE_PART_ONLINE;
}

sense_state_t as5600_state(const as5600_t *d)
{
    return d->part.state;
}

sense_err_t as5600_read_status(as5600_t *d, uint8_t *status)
{
    uint8_t b[1];
    const sense_err_t e = sense_part_read(&d->part, AS5600_REG_STATUS, b,
                                          sizeof b);
    if (e == SENSE_OK) {
        *status = b[0];
    }
    return e;
}

sense_err_t as5600_read_raw(as5600_t *d, uint16_t *raw)
{
    uint8_t b[2];
    const sense_err_t e = sense_part_read(&d->part, AS5600_REG_RAW_ANGLE, b,
                                          sizeof b);
    if (e == SENSE_OK) {
        *raw = as5600_u12(b);
    }
    return e;
}

sense_err_t as5600_read_agc(as5600_t *d, uint8_t *agc)
{
    uint8_t b[1];
    const sense_err_t e = sense_part_read(&d->part, AS5600_REG_AGC, b,
                                          sizeof b);
    if (e == SENSE_OK) {
        *agc = b[0];
    }
    return e;
}

sense_err_t as5600_read_mag(as5600_t *d, uint16_t *magnitude)
{
    uint8_t b[2];
    const sense_err_t e = sense_part_read(&d->part, AS5600_REG_MAGNITUDE, b,
                                          sizeof b);
    if (e == SENSE_OK) {
        *magnitude = as5600_u12(b);
    }
    return e;
}

sense_err_t as5600_read_angle(as5600_t *d, uint8_t *status, uint16_t *raw)
{
    uint8_t st = 0u;
    uint16_t r = 0u;
    sense_err_t e = as5600_read_status(d, &st);
    if (e == SENSE_OK) {
        e = as5600_read_raw(d, &r);
    }
    if (e == SENSE_OK) {
        *status = st;
        *raw    = r;
    }
    return e;
}

sense_err_t as5600_read_magnitude(as5600_t *d, uint8_t *agc,
                                  uint16_t *magnitude)
{
    uint8_t a = 0u;
    uint16_t m = 0u;
    sense_err_t e = as5600_read_agc(d, &a);
    if (e == SENSE_OK) {
        e = as5600_read_mag(d, &m);
    }
    if (e == SENSE_OK) {
        *agc       = a;
        *magnitude = m;
    }
    return e;
}
