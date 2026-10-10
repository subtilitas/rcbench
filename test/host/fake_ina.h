/*
 * A fake sensor bus for test_ina228 and test_ina3221: an INA228 and an
 * INA3221 that answer as their datasheets say (TI SLYS021A, TI SBOS576C),
 * from currents and voltages a test sets.
 *
 * Modelled: the register map and widths, reset values, the identity
 * registers, CONFIG.RST and RSTACC, SHUNT_CAL's 15-bit field, the shunt
 * ADC's range and step for each ADCRANGE, CURRENT from Equations 2 and 4,
 * POWER from Equation 5, the INA3221's 13-bit registers in bits 15-3 and
 * Mask/Enable cleared by its read.  Chosen where the datasheets are silent:
 * a reading past the end of a range stays at the end code, and RSTACC
 * reads back as written.
 *
 * The INA3221's conversion timing, when a test gives the part a clock
 * (clock_us): the shunt and bus registers hold their values until the
 * cycle of the Configuration in force has passed (ina3221_cycle_us(): 280
 * µs for CH1 alone at 140 µs, 6.6 ms on the reset value 7127h), and a
 * write of Configuration or a reset starts a cycle.  Without a clock every
 * read is a fresh conversion.
 *
 * An AS5600 (ams DS000365) answers at 0x36: STATUS at 0x0B, RAW ANGLE at
 * 0x0C and ANGLE at 0x0E, AGC at 0x1A and MAGNITUDE at 0x1B.  A read is
 * one register: STATUS and AGC 1 byte, RAW ANGLE, ANGLE and MAGNITUDE 2
 * bytes, since a read that runs on into a register that suppresses the
 * address increment is not relied on; any other width or register counts
 * in bad_width.  A test sets status, raw, agc and magnitude.  Each part
 * counts the clocks its reads take: 9 * (3 + bytes) + 3.
 *
 * Faults a test can give it: a part that is not there (NACK), the next n
 * transactions or the nth since the start failing with a chosen code, the lines held low, a part
 * that acknowledges writes and keeps none, other identities, a part reset
 * (fake_reset228(), fake_reset3221(): every register at its power-on
 * value), and the next glitch_n reads of one register answering
 * glitch_value in place of what the register holds.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <string.h>

#include "as5600.h"
#include "ina228.h"
#include "ina3221.h"
#include "sense_bus.h"

typedef enum { FAKE_INA228, FAKE_INA3221, FAKE_AS5600 } fake_kind_t;

typedef struct {
    fake_kind_t kind;
    uint8_t     addr;
    bool        present;
    uint16_t    maker, device;   /* the identity it answers               */
    uint16_t    reg[256];        /* 16-bit registers as held               */
    bool        ignore_writes;   /* acknowledges a write, keeps nothing    */
    double      amps[3];         /* through each shunt; INA228 uses [0]    */
    double      shunt_ohm;
    double      volts[3];        /* at VBUS, or each channel's IN-         */
    double      degc;
    uint64_t    energy;          /* ENERGY and CHARGE, raw 40-bit codes    */
    uint64_t    charge;
    uint8_t     status;          /* AS5600 STATUS                          */
    uint16_t    raw;             /* AS5600 RAW ANGLE and ANGLE, 12 bits    */
    uint8_t     agc;
    uint16_t    magnitude;
    unsigned    reads[256];
    unsigned    writes[256];
    uint64_t    clocks;          /* of the reads: 9 * (3 + n) + 3 each     */
    uint8_t     glitch_reg;      /* the next glitch_n reads of this        */
    unsigned    glitch_n;        /* register answer glitch_value           */
    uint16_t    glitch_value;
    const uint64_t *clock_us;    /* INA3221: conversions take their time   */
    bool        conv_have;       /* held[] holds a conversion              */
    uint64_t    conv_at;         /* when the cycle under way began         */
    uint16_t    held[7];         /* shunt and bus registers 1 to 6 as held */
} fake_part_t;

typedef struct {
    fake_part_t part[4];
    size_t      n;
    sense_err_t fail_with;       /* the next fail_count transactions fail */
    unsigned    fail_count;
    unsigned    fail_at;         /* and transaction number fail_at, from 1 */
    bool        low;             /* SDA held low: every one BUS_LOW        */
    unsigned    transactions;    /* that reached the fake                  */
    unsigned    bad_width;       /* reads of a width the register lacks    */
} fake_bus_t;

static inline void fake_reset228(fake_part_t *p)
{
    memset(p->reg, 0, sizeof p->reg);
    p->reg[INA228_ADC_CONFIG] = INA228_ADC_RESET;
    p->reg[INA228_SHUNT_CAL]  = 0x1000u;
    p->reg[INA228_DIAG_ALRT]  = 0x0001u;
    p->energy = 0u;
    p->charge = 0u;
}

static inline void fake_reset3221(fake_part_t *p)
{
    memset(p->reg, 0, sizeof p->reg);
    p->reg[INA3221_CONFIG]      = INA3221_CONFIG_RESET;
    p->reg[INA3221_MASK_ENABLE] = 0x0002u;
    if (p->clock_us != NULL) {
        p->conv_at = *p->clock_us;           /* a cycle starts */
    }
}

static inline fake_part_t *fake_add(fake_bus_t *b, fake_kind_t kind,
                                    uint8_t addr, double shunt_ohm)
{
    fake_part_t *p = &b->part[b->n++];
    memset(p, 0, sizeof *p);
    p->kind      = kind;
    p->addr      = addr;
    p->present   = true;
    p->shunt_ohm = shunt_ohm;
    p->maker     = 0x5449u;
    if (kind == FAKE_AS5600) {
        p->status = AS5600_STATUS_MD;
    } else if (kind == FAKE_INA228) {
        p->device = 0x2281u;
        fake_reset228(p);
    } else {
        p->device = 0x3220u;
        fake_reset3221(p);
    }
    return p;
}

static inline fake_part_t *fake_find(fake_bus_t *b, uint8_t addr)
{
    for (size_t i = 0; i < b->n; ++i) {
        if (b->part[i].present && b->part[i].addr == addr) {
            return &b->part[i];
        }
    }
    return NULL;
}

static inline int64_t fake_sat(double v, int64_t lo, int64_t hi)
{
    const double r = floor(v + 0.5);
    if (r >= (double)hi) {
        return hi;
    }
    if (r <= (double)lo) {
        return lo;
    }
    return (int64_t)r;
}

static inline void fake_put(uint8_t *buf, size_t n, uint64_t v)
{
    for (size_t i = 0; i < n; ++i) {
        buf[i] = (uint8_t)(v >> (8u * (n - 1u - i)));
    }
}

/* ------------------------------------------------------------- INA228 */

static inline size_t fake_width228(uint8_t reg)
{
    switch (reg) {
    case INA228_VSHUNT: case INA228_VBUS: case INA228_CURRENT: case INA228_POWER:
        return 3u;
    case INA228_ENERGY: case INA228_CHARGE:
        return 5u;
    default:
        return (reg <= 0x11u || reg == 0x3Eu || reg == 0x3Fu) ? 2u : 0u;
    }
}

static inline double fake_lsb228(const fake_part_t *p)   /* CURRENT_LSB, A */
{
    const double k = (p->reg[INA228_CONFIG] & INA228_CONFIG_ADCRANGE) ? 4.0 : 1.0;
    return (double)p->reg[INA228_SHUNT_CAL] / (13107.2e6 * p->shunt_ohm * k);
}

static inline uint64_t fake_value228(const fake_part_t *p, uint8_t reg)
{
    const bool narrow = (p->reg[INA228_CONFIG] & INA228_CONFIG_ADCRANGE) != 0u;
    const double vsh_lsb = narrow ? 78.125e-9 : 312.5e-9;
    const int64_t vsh = fake_sat(p->amps[0] * p->shunt_ohm / vsh_lsb,
                                 -524288, 524287);
    switch (reg) {
    case INA228_VSHUNT:
        return ((uint64_t)vsh & 0xFFFFFu) << 4;
    case INA228_VBUS:
        return ((uint64_t)fake_sat(p->volts[0] / 195.3125e-6, -524288, 524287)
                & 0xFFFFFu) << 4;
    case INA228_DIETEMP:
        return (uint64_t)fake_sat(p->degc / 7.8125e-3, -32768, 32767) & 0xFFFFu;
    case INA228_CURRENT: {
        if (p->reg[INA228_SHUNT_CAL] == 0u) {
            return 0u;                         /* §7.3.2 */
        }
        /* The ADC's reading in CURRENT_LSB steps, held at the register's
         * ends. */
        const double amps = (double)vsh * vsh_lsb / p->shunt_ohm;
        return ((uint64_t)fake_sat(amps / fake_lsb228(p), -524288, 524287)
                & 0xFFFFFu) << 4;
    }
    case INA228_POWER: {
        if (p->reg[INA228_SHUNT_CAL] == 0u) {
            return 0u;
        }
        const double w = fabs(p->amps[0] * p->volts[0]);
        return (uint64_t)fake_sat(w / (3.2 * fake_lsb228(p)), 0, 0xFFFFFF);
    }
    case INA228_ENERGY:
        return p->energy & 0xFFFFFFFFFFu;
    case INA228_CHARGE:
        return p->charge & 0xFFFFFFFFFFu;
    case 0x3E:
        return p->maker;
    case 0x3F:
        return p->device;
    default:
        return p->reg[reg];
    }
}

static inline void fake_write228(fake_part_t *p, uint8_t reg, uint16_t v)
{
    if (reg == INA228_CONFIG) {
        if ((v & INA228_CONFIG_RST) != 0u) {
            fake_reset228(p);                  /* self-clears */
            return;
        }
        if ((v & INA228_CONFIG_RSTACC) != 0u) {
            p->energy = 0u;
            p->charge = 0u;
        }
        p->reg[reg] = (uint16_t)(v & 0x7FF0u); /* bits 3-0 read 0 */
    } else if (reg == INA228_SHUNT_CAL) {
        p->reg[reg] = (uint16_t)(v & 0x7FFFu); /* bit 15 reads 0 */
    } else if (reg <= 0x11u) {
        p->reg[reg] = v;
    }
}

/* ------------------------------------------------------------ INA3221 */

/* A shunt or bus register, 1 to 6, converted now. */
static inline uint16_t fake_convert3221(const fake_part_t *p, uint8_t reg)
{
    const unsigned ch = (unsigned)(reg - 1u) / 2u;
    int64_t code;
    if (((reg - 1u) & 1u) == 0u) {
        code = fake_sat(p->amps[ch] * p->shunt_ohm / 40e-6, -4096, 4095);
    } else {
        code = fake_sat(p->volts[ch] / 8e-3, -4096, 4095);
    }
    return (uint16_t)((uint64_t)(code * 8) & 0xFFFFu);
}

/* With a clock: the six registers as held, converted again once the cycle
 * of the Configuration in force has passed. */
static inline uint16_t fake_held3221(fake_part_t *p, uint8_t reg)
{
    const uint64_t now   = *p->clock_us;
    const uint32_t cycle = ina3221_cycle_us(p->reg[INA3221_CONFIG]);
    if (!p->conv_have
        || (cycle != 0u && now - p->conv_at >= (uint64_t)cycle)) {
        for (uint8_t r = INA3221_SHUNT1; r <= 0x06u; ++r) {
            p->held[r] = fake_convert3221(p, r);
        }
        if (p->conv_have && cycle != 0u) {
            p->conv_at = now - (now - p->conv_at) % cycle;
        } else {
            p->conv_at = now;
        }
        p->conv_have = true;
    }
    return p->held[reg];
}

static inline uint64_t fake_value3221(fake_part_t *p, uint8_t reg)
{
    if (reg >= INA3221_SHUNT1 && reg <= 0x06u) {
        return (p->clock_us != NULL) ? fake_held3221(p, reg)
                                     : fake_convert3221(p, reg);
    }
    if (reg == INA3221_MASK_ENABLE) {
        const uint16_t v = p->reg[reg];
        p->reg[reg] = (uint16_t)(v & ~0x03F9u);  /* flags clear on read */
        return v;
    }
    if (reg == INA3221_MANUFACTURER) {
        return p->maker;
    }
    if (reg == INA3221_DIE_ID) {
        return p->device;
    }
    return p->reg[reg];
}

static inline void fake_write3221(fake_part_t *p, uint8_t reg, uint16_t v)
{
    if (reg == INA3221_CONFIG && p->clock_us != NULL) {
        p->conv_at = *p->clock_us;           /* a cycle starts */
    }
    if (reg == INA3221_CONFIG && (v & INA3221_CONFIG_RST) != 0u) {
        fake_reset3221(p);
        return;
    }
    p->reg[reg] = v;
}

/* -------------------------------------------------------------- wire */

static inline sense_err_t fake_fault(fake_bus_t *b)
{
    ++b->transactions;
    if (b->low) {
        return SENSE_BUS_LOW;
    }
    if (b->fail_count > 0u) {
        --b->fail_count;
        return b->fail_with;
    }
    if (b->fail_at != 0u && b->transactions == b->fail_at) {
        return b->fail_with;
    }
    return SENSE_OK;
}

/* The AS5600's registers as bytes: one register a read. */
static inline bool fake_read5600(const fake_part_t *p, uint8_t reg,
                                 uint8_t *buf, size_t n)
{
    uint16_t v;
    size_t   want;
    switch (reg) {
    case AS5600_REG_STATUS:    v = p->status;    want = 1u; break;
    case AS5600_REG_AGC:       v = p->agc;       want = 1u; break;
    case AS5600_REG_RAW_ANGLE:
    case AS5600_REG_ANGLE:     v = p->raw;       want = 2u; break;
    case AS5600_REG_MAGNITUDE: v = p->magnitude; want = 2u; break;
    default:                   return false;
    }
    if (n != want) {
        return false;
    }
    if (n == 1u) {
        buf[0] = (uint8_t)v;
    } else {
        buf[0] = (uint8_t)(v >> 8);
        buf[1] = (uint8_t)v;
    }
    return true;
}

static inline sense_err_t fake_read(void *ctx, uint8_t addr, uint8_t reg,
                                    uint8_t *buf, size_t n)
{
    fake_bus_t *b = (fake_bus_t *)ctx;
    const sense_err_t e = fake_fault(b);
    if (e != SENSE_OK) {
        return e;
    }
    fake_part_t *p = fake_find(b, addr);
    if (p == NULL) {
        return SENSE_NACK;
    }
    ++p->reads[reg];
    p->clocks += 9u * (3u + (unsigned)n) + 3u;
    if (p->kind == FAKE_AS5600) {
        if (!fake_read5600(p, reg, buf, n)) {
            ++b->bad_width;
        }
        return SENSE_OK;
    }
    const size_t want = (p->kind == FAKE_INA228) ? fake_width228(reg) : 2u;
    if (n != want) {
        ++b->bad_width;
    }
    uint64_t v = (p->kind == FAKE_INA228) ? fake_value228(p, reg)
                                          : fake_value3221(p, reg);
    if (p->glitch_n > 0u && reg == p->glitch_reg) {
        --p->glitch_n;
        v = p->glitch_value;
    }
    fake_put(buf, n, v);
    return SENSE_OK;
}

static inline sense_err_t fake_write(void *ctx, uint8_t addr, uint8_t reg,
                                     const uint8_t *buf, size_t n)
{
    fake_bus_t *b = (fake_bus_t *)ctx;
    const sense_err_t e = fake_fault(b);
    if (e != SENSE_OK) {
        return e;
    }
    fake_part_t *p = fake_find(b, addr);
    if (p == NULL) {
        return SENSE_NACK;
    }
    ++p->writes[reg];
    if (n != 2u) {
        ++b->bad_width;
        return SENSE_OK;
    }
    if (p->ignore_writes) {
        return SENSE_OK;
    }
    const uint16_t v = (uint16_t)((buf[0] << 8) | buf[1]);
    if (p->kind == FAKE_INA228) {
        fake_write228(p, reg, v);
    } else {
        fake_write3221(p, reg, v);
    }
    return SENSE_OK;
}

static inline void fake_bus_init(fake_bus_t *b, sense_bus_t *bus)
{
    memset(b, 0, sizeof *b);
    const sense_i2c_t io = { fake_read, fake_write, b };
    sense_bus_init(bus, &io);
}
