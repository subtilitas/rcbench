/*
 * The INA3221.  See ina3221.h; tables and sections are TI SBOS576C's.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ina3221.h"

#include <string.h>

/* Ends of the 13-bit range: 7FF8h and 8000h (§8.2.2). */
#define CODE13_MAX   4095
#define CODE13_MIN (-4096)

uint16_t ina3221_config(uint8_t channels, uint8_t avg, uint8_t vbusct,
                        uint8_t vshct, uint8_t mode)
{
    unsigned v = 0u;
    if ((channels & 1u) != 0u) {
        v |= INA3221_CONFIG_CH1EN;
    }
    if ((channels & 2u) != 0u) {
        v |= INA3221_CONFIG_CH2EN;
    }
    if ((channels & 4u) != 0u) {
        v |= INA3221_CONFIG_CH3EN;
    }
    v |= ((unsigned)(avg & 7u) << 9) | ((unsigned)(vbusct & 7u) << 6)
       | ((unsigned)(vshct & 7u) << 3) | (mode & 7u);
    return (uint16_t)v;
}

uint8_t ina3221_channels(uint16_t config)
{
    uint8_t ch = 0u;
    if ((config & INA3221_CONFIG_CH1EN) != 0u) {
        ch |= 1u;
    }
    if ((config & INA3221_CONFIG_CH2EN) != 0u) {
        ch |= 2u;
    }
    if ((config & INA3221_CONFIG_CH3EN) != 0u) {
        ch |= 4u;
    }
    return ch;
}

uint32_t ina3221_ct_us(uint8_t code)
{
    static const uint16_t k_us[8] = { 140, 204, 332, 588, 1100, 2116, 4156, 8244 };
    return k_us[code & 7u];
}

uint32_t ina3221_cycle_us(uint16_t config)
{
    const unsigned mode = config & 7u;
    if ((mode & 4u) == 0u || mode == 4u) {
        return 0u;            /* 000b and 100b power down; 001b to 011b one shot */
    }
    uint32_t per = 0u;
    if ((mode & 1u) != 0u) {
        per += ina3221_ct_us((uint8_t)((config >> 3) & 7u));
    }
    if ((mode & 2u) != 0u) {
        per += ina3221_ct_us((uint8_t)((config >> 6) & 7u));
    }
    const uint8_t ch = ina3221_channels(config);
    const uint32_t n = (uint32_t)(ch & 1u) + ((ch >> 1) & 1u) + ((ch >> 2) & 1u);
    return per * n;
}

bool ina3221_identity_ok(uint16_t maker, uint16_t die)
{
    return maker == INA3221_MAKER_ID && die == INA3221_DIE;
}

int32_t ina3221_code13(uint16_t raw)
{
    return sense_sext32((uint32_t)raw >> 3, 13u);
}

sense_value_t ina3221_current_ua(uint32_t shunt_uohm, int32_t code)
{
    sense_value_t v = { 0, SENSE_CLIP_NONE };
    if (code >= CODE13_MAX) {
        v.clip = SENSE_CLIP_HIGH;
    } else if (code <= CODE13_MIN) {
        v.clip = SENSE_CLIP_LOW;
    } else {
        /* 40 µV / R[µΩ] = 40 × 10^6 / R µA a step. */
        v.value = (int32_t)((int64_t)code * 40000000 / (int64_t)shunt_uohm);
    }
    return v;
}

int32_t ina3221_end_ua(uint32_t shunt_uohm, sense_clip_t end)
{
    if (end == SENSE_CLIP_NONE || shunt_uohm == 0u) {
        return 0;
    }
    const int64_t code = (end == SENSE_CLIP_HIGH) ? CODE13_MAX : CODE13_MIN;
    return (int32_t)(code * 40000000 / (int64_t)shunt_uohm);
}

int32_t ina3221_bus_mv(int32_t code)
{
    return code * 8;
}

/* -------------------------------------------------------------- driver */

ina3221_setup_err_t ina3221_init(ina3221_t *d, sense_bus_t *bus, uint8_t addr,
                                 uint32_t shunt_uohm, uint16_t config)
{
    memset(d, 0, sizeof *d);
    d->part.bus  = bus;
    d->part.addr = addr;
    if (addr < INA3221_ADDR_MIN || addr > INA3221_ADDR_MAX) {
        return INA3221_SETUP_BAD_ADDR;
    }
    if (shunt_uohm < INA3221_SHUNT_MIN_UOHM) {
        return INA3221_SETUP_NO_SHUNT;
    }
    if (ina3221_channels(config) == 0u) {
        return INA3221_SETUP_NO_CHANNEL;
    }
    sense_part_init(&d->part, bus, addr);
    d->shunt_uohm = shunt_uohm;
    d->config     = (uint16_t)(config & (uint16_t)~INA3221_CONFIG_RST);
    return INA3221_SETUP_OK;
}

static bool read16(ina3221_t *d, uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    if (sense_bus_read(d->part.bus, d->part.addr, reg, b, sizeof b) != SENSE_OK) {
        return false;
    }
    *v = sense_be16(b);
    return true;
}

static sense_state_t probe(ina3221_t *d)
{
    if (!read16(d, INA3221_MANUFACTURER, &d->part.id_maker)
        || !read16(d, INA3221_DIE_ID, &d->part.id_device)) {
        return SENSE_PART_ABSENT;
    }
    if (!ina3221_identity_ok(d->part.id_maker, d->part.id_device)) {
        return SENSE_PART_WRONG_ID;
    }
    uint16_t back;
    if (sense_bus_write16(d->part.bus, d->part.addr, INA3221_CONFIG,
                          d->config) != SENSE_OK
        || !read16(d, INA3221_CONFIG, &back)) {
        return SENSE_PART_ABSENT;
    }
    return (back == d->config) ? SENSE_PART_ONLINE : SENSE_PART_REFUSED;
}

bool ina3221_step(ina3221_t *d, uint32_t now_ms)
{
    if (sense_part_probe_due(&d->part, now_ms)) {
        sense_part_probed(&d->part, probe(d));
    }
    return d->part.state == SENSE_PART_ONLINE;
}

sense_state_t ina3221_state(const ina3221_t *d)
{
    return d->part.state;
}

/* Channel @p ch's register (shunt or bus, by @p first) when the set-up
 * converts it; 0 when it does not. */
static uint8_t channel_reg(const ina3221_t *d, uint8_t ch, uint8_t first,
                           unsigned mode_bit)
{
    if (ch < 1u || ch > INA3221_CHANNELS
        || (ina3221_channels(d->config) & (1u << (ch - 1u))) == 0u
        || (d->config & mode_bit) == 0u) {
        return 0u;
    }
    return (uint8_t)(first + 2u * (ch - 1u));
}

static sense_err_t read_reg(ina3221_t *d, uint8_t reg, uint16_t *raw)
{
    uint8_t b[2];
    const sense_err_t e = sense_part_read(&d->part, reg, b, sizeof b);
    if (e == SENSE_OK) {
        *raw = sense_be16(b);
    }
    return e;
}

sense_err_t ina3221_read_current(ina3221_t *d, uint8_t ch, sense_value_t *ua)
{
    const uint8_t reg = channel_reg(d, ch, INA3221_SHUNT1, 1u);
    if (reg == 0u) {
        return SENSE_BAD_ARG;
    }
    uint16_t raw = 0u;
    const sense_err_t e = read_reg(d, reg, &raw);
    if (e == SENSE_OK) {
        *ua = ina3221_current_ua(d->shunt_uohm, ina3221_code13(raw));
    }
    return e;
}

sense_err_t ina3221_read_bus(ina3221_t *d, uint8_t ch, int32_t *mv)
{
    const uint8_t reg = channel_reg(d, ch, INA3221_BUS1, 2u);
    if (reg == 0u) {
        return SENSE_BAD_ARG;
    }
    uint16_t raw = 0u;
    const sense_err_t e = read_reg(d, reg, &raw);
    if (e == SENSE_OK) {
        *mv = ina3221_bus_mv(ina3221_code13(raw));
    }
    return e;
}

sense_err_t ina3221_read_flags(ina3221_t *d, uint16_t *mask_enable)
{
    return read_reg(d, INA3221_MASK_ENABLE, mask_enable);
}

sense_err_t ina3221_verify(ina3221_t *d, bool *lost)
{
    *lost = false;
    for (unsigned k = 0; k < 2u; ++k) {
        uint16_t back = 0u;
        const sense_err_t e = read_reg(d, INA3221_CONFIG, &back);
        if (e != SENSE_OK || back == d->config) {
            return e;
        }
    }
    sense_part_lost(&d->part);
    *lost = true;
    return SENSE_OK;
}
