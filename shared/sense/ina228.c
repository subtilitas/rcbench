/*
 * The INA228.  See ina228.h; tables and equations are TI SLYS021A's.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ina228.h"

#include <string.h>

/* Ends of a 20-bit two's complement register. */
#define CODE20_MAX   524287
#define CODE20_MIN (-524288)

/* Full-scale shunt voltages as max mA × µΩ, which is nV (Table 8-1). */
#define FS_NARROW_NV  40960000u    /* ±40.96 mV, ADCRANGE 1  */
#define FS_WIDE_NV   163840000u    /* ±163.84 mV, ADCRANGE 0 */

_Static_assert(INA228_SHUNT_CAL_ADC <= 0x7FFFu,
               "SHUNT_CAL is a 15-bit field (Table 7-7)");

uint16_t ina228_adc_config(uint8_t mode, uint8_t vbusct, uint8_t vshct,
                           uint8_t vtct, uint8_t avg)
{
    return (uint16_t)(((unsigned)(mode & 0xFu) << 12)
                    | ((unsigned)(vbusct & 7u) << 9)
                    | ((unsigned)(vshct & 7u) << 6)
                    | ((unsigned)(vtct & 7u) << 3)
                    | (avg & 7u));
}

uint32_t ina228_ct_us(uint8_t code)
{
    static const uint16_t k_us[8] = { 50, 84, 150, 280, 540, 1052, 2074, 4120 };
    return k_us[code & 7u];
}

uint32_t ina228_averages(uint8_t code)
{
    static const uint16_t k_avg[8] = { 1, 4, 16, 64, 128, 256, 512, 1024 };
    return k_avg[code & 7u];
}

uint32_t ina228_cycle_us(uint16_t adc_config)
{
    const unsigned mode = (unsigned)adc_config >> 12;
    if ((mode & 8u) == 0u || mode == 8u) {
        return 0u;                     /* shut down, or triggered */
    }
    uint32_t us = 0u;
    if ((mode & 1u) != 0u) {
        us += ina228_ct_us((uint8_t)((adc_config >> 9) & 7u));
    }
    if ((mode & 2u) != 0u) {
        us += ina228_ct_us((uint8_t)((adc_config >> 6) & 7u));
    }
    if ((mode & 4u) != 0u) {
        us += ina228_ct_us((uint8_t)((adc_config >> 3) & 7u));
    }
    return us * ina228_averages((uint8_t)(adc_config & 7u));
}

bool ina228_identity_ok(uint16_t maker, uint16_t device)
{
    return maker == INA228_MAKER_ID && (device >> 4) == INA228_DIE_ID;
}

/* ------------------------------------------------------- calibration */

ina228_setup_err_t ina228_calibrate(uint32_t shunt_uohm, uint32_t max_ma,
                                    ina228_cal_t *out)
{
    if (shunt_uohm == 0u) {
        return INA228_SETUP_NO_SHUNT;
    }
    if (max_ma == 0u) {
        return INA228_SETUP_NO_MAX;
    }
    const uint64_t nv = (uint64_t)max_ma * shunt_uohm;
    uint8_t range;
    uint64_t fs_nv;
    if (nv <= FS_NARROW_NV) {
        range = 1u;
        fs_nv = FS_NARROW_NV;
    } else if (nv <= FS_WIDE_NV) {
        range = 0u;
        fs_nv = FS_WIDE_NV;
    } else {
        return INA228_SETUP_OVER_RANGE;
    }
    /* Full scale in mA is nV over µΩ. */
    if (fs_nv > (uint64_t)INA228_FS_MA_LIMIT * shunt_uohm) {
        return INA228_SETUP_OVER_RANGE;
    }
    out->shunt_uohm = shunt_uohm;
    out->max_ma     = max_ma;
    out->adcrange   = range;
    out->shunt_cal  = INA228_SHUNT_CAL_ADC;
    return INA228_SETUP_OK;
}

/* R × 4 at ADCRANGE 1: the denominator every quantity scaled by
 * CURRENT_LSB = SHUNT_CAL / (13107.2 × R[µΩ] × (4 at ADCRANGE 1)) shares. */
static uint64_t r_k(const ina228_cal_t *cal)
{
    return (uint64_t)cal->shunt_uohm * (cal->adcrange != 0u ? 4u : 1u);
}

/* ------------------------------------------------------------ decoding */

int32_t ina228_code20(uint32_t raw24)
{
    return sense_sext32(raw24 >> 4, 20u);
}

static sense_clip_t clip20(int32_t code)
{
    if (code >= CODE20_MAX) {
        return SENSE_CLIP_HIGH;
    }
    if (code <= CODE20_MIN) {
        return SENSE_CLIP_LOW;
    }
    return SENSE_CLIP_NONE;
}

sense_value_t ina228_current_ua(const ina228_cal_t *cal, int32_t code)
{
    sense_value_t v = { 0, clip20(code) };
    if (v.clip == SENSE_CLIP_NONE) {
        /* µA = CURRENT × SHUNT_CAL × 10^7 / (131072 × R × k)
         *    = CURRENT × SHUNT_CAL × 78125 / (1024 × R × k)  (Eq. 4). */
        v.value = (int32_t)sense_muldiv((int64_t)code * cal->shunt_cal,
                                        78125u, 1024u * r_k(cal));
    }
    return v;
}

sense_value_t ina228_vshunt_nv(const ina228_cal_t *cal, int32_t code)
{
    sense_value_t v = { 0, clip20(code) };
    if (v.clip == SENSE_CLIP_NONE) {
        /* 312.5 nV = 625 / 2; 78.125 nV = 625 / 8. */
        v.value = (int32_t)((int64_t)code * 625 / (cal->adcrange != 0u ? 8 : 2));
    }
    return v;
}

int32_t ina228_vbus_uv(int32_t code)
{
    return (int32_t)((int64_t)code * 3125 / 16);     /* 195.3125 µV */
}

int32_t ina228_dietemp_mdegc(uint16_t raw)
{
    return sense_sext32(raw, 16u) * 125 / 16;        /* 7.8125 m°C */
}

uint64_t ina228_power_uw(const ina228_cal_t *cal, uint32_t raw24)
{
    /* µW = POWER × 3.2 × CURRENT_LSB × 10^6
     *    = POWER × SHUNT_CAL × 15625 / (64 × R × k). */
    return (uint64_t)sense_muldiv((int64_t)(raw24 & 0xFFFFFFu) * cal->shunt_cal,
                                  15625u, 64u * r_k(cal));
}

uint64_t ina228_energy_mj(const ina228_cal_t *cal, uint64_t raw40)
{
    /* mJ = ENERGY × 51.2 × CURRENT_LSB × 10^3
     *    = ENERGY × SHUNT_CAL × 125 / (32 × R × k). */
    const int64_t e = (int64_t)(raw40 & 0xFFFFFFFFFFu);
    return (uint64_t)sense_muldiv(e * cal->shunt_cal, 125u, 32u * r_k(cal));
}

int64_t ina228_charge_uc(const ina228_cal_t *cal, uint64_t raw40)
{
    /* µC = CHARGE × CURRENT_LSB × 10^6
     *    = CHARGE × SHUNT_CAL × 78125 / (1024 × R × k). */
    const int64_t q = sense_sext64(raw40, 40u);
    return sense_muldiv(q * cal->shunt_cal, 78125u, 1024u * r_k(cal));
}

/* -------------------------------------------------------------- driver */

ina228_setup_err_t ina228_init(ina228_t *d, sense_bus_t *bus, uint8_t addr,
                               uint32_t shunt_uohm, uint32_t max_ma,
                               uint16_t adc_config)
{
    memset(d, 0, sizeof *d);
    d->part.bus  = bus;
    d->part.addr = addr;
    if (addr < INA228_ADDR_MIN || addr > INA228_ADDR_MAX) {
        return INA228_SETUP_BAD_ADDR;
    }
    const ina228_setup_err_t e = ina228_calibrate(shunt_uohm, max_ma, &d->cal);
    if (e != INA228_SETUP_OK) {
        return e;
    }
    sense_part_init(&d->part, bus, addr);
    d->config     = (d->cal.adcrange != 0u) ? INA228_CONFIG_ADCRANGE : 0u;
    d->adc_config = adc_config;
    return INA228_SETUP_OK;
}

static bool read16(ina228_t *d, uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    if (sense_bus_read(d->part.bus, d->part.addr, reg, b, sizeof b) != SENSE_OK) {
        return false;
    }
    *v = sense_be16(b);
    return true;
}

static bool write16(ina228_t *d, uint8_t reg, uint16_t v)
{
    return sense_bus_write16(d->part.bus, d->part.addr, reg, v) == SENSE_OK;
}

static sense_state_t probe(ina228_t *d)
{
    if (!read16(d, INA228_MANUFACTURER, &d->part.id_maker)
        || !read16(d, INA228_DEVICE_ID, &d->part.id_device)) {
        return SENSE_PART_ABSENT;
    }
    if (!ina228_identity_ok(d->part.id_maker, d->part.id_device)) {
        return SENSE_PART_WRONG_ID;
    }
    /* ADC_CONFIG last but the accumulator clear: writing MODE restarts the
     * conversion under way (§7.3.4), now with the range and SHUNT_CAL in
     * place. */
    if (!write16(d, INA228_CONFIG, d->config)
        || !write16(d, INA228_SHUNT_CAL, d->cal.shunt_cal)
        || !write16(d, INA228_ADC_CONFIG, d->adc_config)
        || !write16(d, INA228_CONFIG, (uint16_t)(d->config | INA228_CONFIG_RSTACC))
        || !write16(d, INA228_CONFIG, d->config)) {
        return SENSE_PART_ABSENT;
    }
    uint16_t config;
    uint16_t cal;
    uint16_t adc;
    if (!read16(d, INA228_CONFIG, &config)
        || !read16(d, INA228_SHUNT_CAL, &cal)
        || !read16(d, INA228_ADC_CONFIG, &adc)) {
        return SENSE_PART_ABSENT;
    }
    if (config != d->config || cal != d->cal.shunt_cal || adc != d->adc_config) {
        return SENSE_PART_REFUSED;
    }
    return SENSE_PART_ONLINE;
}

bool ina228_step(ina228_t *d, uint32_t now_ms)
{
    if (sense_part_probe_due(&d->part, now_ms)) {
        sense_part_probed(&d->part, probe(d));
    }
    return d->part.state == SENSE_PART_ONLINE;
}

sense_state_t ina228_state(const ina228_t *d)
{
    return d->part.state;
}

static sense_err_t read24(ina228_t *d, uint8_t reg, uint32_t *raw)
{
    uint8_t b[3];
    const sense_err_t e = sense_part_read(&d->part, reg, b, sizeof b);
    if (e == SENSE_OK) {
        *raw = sense_be24(b);
    }
    return e;
}

static sense_err_t read40(ina228_t *d, uint8_t reg, uint64_t *raw)
{
    uint8_t b[5];
    const sense_err_t e = sense_part_read(&d->part, reg, b, sizeof b);
    if (e == SENSE_OK) {
        *raw = sense_be40(b);
    }
    return e;
}

static sense_err_t read_reg16(ina228_t *d, uint8_t reg, uint16_t *raw)
{
    uint8_t b[2];
    const sense_err_t e = sense_part_read(&d->part, reg, b, sizeof b);
    if (e == SENSE_OK) {
        *raw = sense_be16(b);
    }
    return e;
}

sense_err_t ina228_read_current(ina228_t *d, sense_value_t *ua)
{
    uint32_t raw = 0u;
    const sense_err_t e = read24(d, INA228_CURRENT, &raw);
    if (e == SENSE_OK) {
        *ua = ina228_current_ua(&d->cal, ina228_code20(raw));
    }
    return e;
}

sense_err_t ina228_read_vshunt(ina228_t *d, sense_value_t *nv)
{
    uint32_t raw = 0u;
    const sense_err_t e = read24(d, INA228_VSHUNT, &raw);
    if (e == SENSE_OK) {
        *nv = ina228_vshunt_nv(&d->cal, ina228_code20(raw));
    }
    return e;
}

sense_err_t ina228_read_vbus(ina228_t *d, int32_t *uv)
{
    uint32_t raw = 0u;
    const sense_err_t e = read24(d, INA228_VBUS, &raw);
    if (e == SENSE_OK) {
        *uv = ina228_vbus_uv(ina228_code20(raw));
    }
    return e;
}

sense_err_t ina228_read_power(ina228_t *d, uint64_t *uw)
{
    uint32_t raw = 0u;
    const sense_err_t e = read24(d, INA228_POWER, &raw);
    if (e == SENSE_OK) {
        *uw = ina228_power_uw(&d->cal, raw);
    }
    return e;
}

sense_err_t ina228_read_dietemp(ina228_t *d, int32_t *mdegc)
{
    uint16_t raw = 0u;
    const sense_err_t e = read_reg16(d, INA228_DIETEMP, &raw);
    if (e == SENSE_OK) {
        *mdegc = ina228_dietemp_mdegc(raw);
    }
    return e;
}

sense_err_t ina228_read_diag(ina228_t *d, uint16_t *diag)
{
    return read_reg16(d, INA228_DIAG_ALRT, diag);
}

sense_err_t ina228_read_energy(ina228_t *d, uint64_t *mj)
{
    uint64_t raw = 0u;
    const sense_err_t e = read40(d, INA228_ENERGY, &raw);
    if (e == SENSE_OK) {
        *mj = ina228_energy_mj(&d->cal, raw);
    }
    return e;
}

sense_err_t ina228_read_charge(ina228_t *d, int64_t *uc)
{
    uint64_t raw = 0u;
    const sense_err_t e = read40(d, INA228_CHARGE, &raw);
    if (e == SENSE_OK) {
        *uc = ina228_charge_uc(&d->cal, raw);
    }
    return e;
}

sense_err_t ina228_clear_totals(ina228_t *d)
{
    const sense_err_t e = sense_part_write16(&d->part, INA228_CONFIG,
                                             (uint16_t)(d->config | INA228_CONFIG_RSTACC));
    if (e != SENSE_OK) {
        return e;
    }
    return sense_part_write16(&d->part, INA228_CONFIG, d->config);
}

sense_err_t ina228_verify(ina228_t *d, bool *lost)
{
    *lost = false;
    for (unsigned k = 0; k < 2u; ++k) {
        uint16_t back = 0u;
        const sense_err_t e = read_reg16(d, INA228_ADC_CONFIG, &back);
        if (e != SENSE_OK || back == d->adc_config) {
            return e;
        }
    }
    sense_part_lost(&d->part);
    *lost = true;
    return SENSE_OK;
}
