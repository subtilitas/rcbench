/*
 * SPDX-License-Identifier: MIT
 */

#include "bench_state.h"

#include <string.h>

/* The scales, in one place.  A number that disagrees with the wire is a wrong
 * reading rather than a crash, which is the kind that ships. */
#define CV_PER_V   100.0f   /* 10 mV steps  */
#define CA_PER_A   100.0f   /* 10 mA steps  */
#define DC_PER_C    10.0f   /* 0.1 C steps  */
#define DWH_PER_WH  10.0f   /* 0.1 Wh steps */

static float from_u16(uint16_t v, float per_unit)
{
    return (float)v / per_unit;
}

static uint16_t to_u16(float v, float per_unit)
{
    if (!(v > 0.0f)) {
        return 0;
    }
    const float scaled = v * per_unit + 0.5f;
    return (scaled >= 65535.0f) ? 65535u : (uint16_t)scaled;
}

static float from_i16(uint16_t v, float per_unit)
{
    return (float)(int16_t)v / per_unit;
}

static uint16_t to_i16(float v, float per_unit)
{
    float scaled = v * per_unit;
    scaled += (scaled >= 0.0f) ? 0.5f : -0.5f;
    if (scaled > 32767.0f)  { scaled = 32767.0f; }
    if (scaled < -32768.0f) { scaled = -32768.0f; }
    return (uint16_t)(int16_t)scaled;
}

void bench_state_from_regs(bench_state_t *b, const uint16_t *regs,
                           uint8_t offset, uint8_t count)
{
    if (b == NULL || regs == NULL) {
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t reg = (uint8_t)(offset + i);
        const uint16_t v = regs[i];
        switch (reg) {
        case LINK_BN_VOLTAGE_CV:  b->voltage     = from_u16(v, CV_PER_V);  break;
        case LINK_BN_CURRENT_CA:  b->current     = from_u16(v, CA_PER_A);  break;
        case LINK_BN_POWER_W:     b->power       = (float)v;               break;
        case LINK_BN_RPM:         b->rpm         = (float)v;               break;
        case LINK_BN_TEMP_ESC_DC: b->temp_esc    = from_i16(v, DC_PER_C);  break;
        case LINK_BN_TEMP_MOT_DC: b->temp_motor  = from_i16(v, DC_PER_C);  break;
        case LINK_BN_CHARGE_MAH:  b->charge_mah  = (float)v;               break;
        case LINK_BN_ENERGY_DWH:  b->energy_wh   = from_u16(v, DWH_PER_WH); break;
        case LINK_BN_VOLT_MIN_CV: b->voltage_min = from_u16(v, CV_PER_V);  break;
        case LINK_BN_CURR_MAX_CA: b->current_max = from_u16(v, CA_PER_A);  break;
        case LINK_BN_POWER_MAX_W: b->power_max   = (float)v;               break;
        case LINK_BN_RPM_MAX:     b->rpm_max     = (float)v;               break;
        case LINK_BN_FLAGS:       b->flags       = v;                      break;
        default:                                                           break;
        }
    }
    b->valid = true;
}

void bench_state_to_regs(const bench_state_t *b, uint16_t *regs)
{
    if (b == NULL || regs == NULL) {
        return;
    }
    memset(regs, 0, LINK_BN_COUNT * sizeof(uint16_t));
    regs[LINK_BN_VOLTAGE_CV]  = to_u16(b->voltage, CV_PER_V);
    regs[LINK_BN_CURRENT_CA]  = to_u16(b->current, CA_PER_A);
    regs[LINK_BN_POWER_W]     = to_u16(b->power, 1.0f);
    regs[LINK_BN_RPM]         = to_u16(b->rpm, 1.0f);
    regs[LINK_BN_TEMP_ESC_DC] = to_i16(b->temp_esc, DC_PER_C);
    regs[LINK_BN_TEMP_MOT_DC] = to_i16(b->temp_motor, DC_PER_C);
    regs[LINK_BN_CHARGE_MAH]  = to_u16(b->charge_mah, 1.0f);
    regs[LINK_BN_ENERGY_DWH]  = to_u16(b->energy_wh, DWH_PER_WH);
    regs[LINK_BN_VOLT_MIN_CV] = to_u16(b->voltage_min, CV_PER_V);
    regs[LINK_BN_CURR_MAX_CA] = to_u16(b->current_max, CA_PER_A);
    regs[LINK_BN_POWER_MAX_W] = to_u16(b->power_max, 1.0f);
    regs[LINK_BN_RPM_MAX]     = to_u16(b->rpm_max, 1.0f);
    regs[LINK_BN_FLAGS]       = b->flags;
}

void bench_state_reset_peaks(bench_state_t *b)
{
    if (b == NULL) {
        return;
    }
    /* The minimum resets to the reading rather than to zero: a sag floor of
     * zero volts would read as a pack that had collapsed. */
    b->voltage_min = b->voltage;
    b->current_max = b->current;
    b->power_max   = b->power;
    b->rpm_max     = b->rpm;
}
