/*
 * SPDX-License-Identifier: MIT
 */

#include "bench_state.h"

#include <math.h>
#include <string.h>

/* The scales of the BENCH page registers, matching link_pages.h.  A scale
 * that disagrees with the wire produces a wrong reading, not a crash. */
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

void bench_state_run_starts(bench_state_t *b, uint16_t *regs)
{
    if (b != NULL) {
        b->charge_mah = 0.0f;
        b->energy_wh  = 0.0f;
        b->flags &= (uint16_t)~LINK_BN_TOTALS_OK;
    }
    if (regs != NULL) {
        regs[LINK_BN_CHARGE_MAH] = 0u;
        regs[LINK_BN_ENERGY_DWH] = 0u;
        regs[LINK_BN_FLAGS] &= (uint16_t)~LINK_BN_TOTALS_OK;
    }
}

void bench_state_reset_peaks(bench_state_t *b)
{
    if (b == NULL) {
        return;
    }
    /* The minimum resets to the current reading, not to zero: a sag floor
     * of 0 V reads as a collapsed pack. */
    b->voltage_min = b->voltage;
    b->current_max = b->current;
    b->power_max   = b->power;
    b->rpm_max     = b->rpm;
    /* And if that reading was not a measurement, neither is the floor: the
     * run's first valid voltage seeds it instead. */
    b->sag_seeded  = (b->flags & (uint16_t)LINK_BN_VOLTAGE_OK) != 0u;
}

void bench_totals_reset(bench_totals_t *t)
{
    if (t != NULL) {
        t->mah     = 0.0f;
        t->wh      = 0.0f;
        t->counted = 0u;
        t->run_s   = 0.0f;
    }
}

void bench_totals_count(bench_totals_t *t, const bench_state_t *b,
                        float dt_s, bool driving)
{
    if (t == NULL || b == NULL || !driving) {
        return;
    }
    float dt = dt_s;
    if (!(dt > 0.0f)) {
        dt = 0.0f;              /* negative, zero or NaN counts nothing */
    } else if (dt > BENCH_TOTALS_MAX_STEP_S) {
        dt = BENCH_TOTALS_MAX_STEP_S;
    }
    const bool settled = t->run_s >= BENCH_TOTALS_SETTLE_S;
    t->run_s += dt;
    if ((b->flags & (uint16_t)LINK_BN_TOTALS_OK) != 0u && settled) {
        /* The INA228 counts, at every conversion: its totals are the run's,
         * and a reading the panel saw adds nothing to them.  Not in the
         * run's first samples, which can be the last run's page.  A
         * register at either end of its range is a bound, not the total:
         * a count already past it from a finer read stands -- above the
         * ceiling, and for charge below the floor of 0, where a run that
         * gave back more than it took reads 0 on BENCH.  Only the bound
         * itself, as BENCH decodes it: a finer figure beyond it is a
         * reading and is taken, up or down.  Energy has no floor to pass:
         * the INA228 accumulates unsigned power. */
        const bool past_ceiling = b->charge_mah == BENCH_CHARGE_MAH_MAX
                                  && t->mah > b->charge_mah;
        const bool past_floor = b->charge_mah == 0.0f && t->mah < 0.0f;
        if (!past_ceiling && !past_floor) {
            t->mah = b->charge_mah;
        }
        if (!(b->energy_wh == BENCH_ENERGY_WH_MAX && t->wh > b->energy_wh)) {
            t->wh = b->energy_wh;
        }
        t->counted |= (uint8_t)(BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);
        return;
    }
    const uint16_t both = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
    if ((b->flags & (uint16_t)LINK_BN_CURRENT_OK) != 0u) {
        t->mah += b->current * dt * (1000.0f / 3600.0f);
        t->counted |= BENCH_COUNTED_CHARGE;
    }
    if ((b->flags & both) == both) {
        t->wh += b->power * dt * (1.0f / 3600.0f);
        t->counted |= BENCH_COUNTED_ENERGY;
    }
}

void bench_totals_show(const bench_totals_t *t, bench_state_t *b)
{
    if (t == NULL || b == NULL) {
        return;
    }
    b->charge_mah = t->mah;
    b->energy_wh  = t->wh;
    b->counted    = t->counted;
}

void bench_state_set_esc(bench_state_t *b, bool v_ok, float volts,
                         bool i_ok, float amps, bool clipped)
{
    if (b == NULL) {
        return;
    }
    b->esc_voltage = v_ok ? volts : 0.0f;
    b->esc_current = i_ok ? amps : 0.0f;
    b->esc_ok = (uint8_t)((v_ok ? LINK_SN_ESC_VOLTAGE_OK : 0u)
                          | (i_ok ? LINK_SN_ESC_CURRENT_OK : 0u));
    b->clipped = clipped;
}

static bool sensed(const bench_state_t *b)
{
    return (b->flags & (uint16_t)LINK_BN_SENSED) != 0u;
}

/* One of the ESC's figures: from SENSE while the INA228 is the source,
 * from BENCH's own register and flag otherwise. */
static bool esc_figure(const bench_state_t *b, uint8_t esc_bit, float esc,
                       uint16_t bench_bit, float bench, float *out)
{
    if (b == NULL || out == NULL) {
        return false;
    }
    if (sensed(b)) {
        if ((b->esc_ok & esc_bit) == 0u) {
            return false;
        }
        *out = esc;
        return true;
    }
    /* The panel's model is no ESC: its numbers are the bench's when the
     * link is down, and nothing reported them. */
    if ((b->flags & bench_bit) == 0u
        || (b->flags & (uint16_t)LINK_BN_SIMULATED) != 0u) {
        return false;
    }
    *out = bench;
    return true;
}

bool bench_state_esc_voltage(const bench_state_t *b, float *out)
{
    return b != NULL
           && esc_figure(b, LINK_SN_ESC_VOLTAGE_OK, b->esc_voltage,
                         LINK_BN_VOLTAGE_OK, b->voltage, out);
}

bool bench_state_esc_current(const bench_state_t *b, float *out)
{
    return b != NULL
           && esc_figure(b, LINK_SN_ESC_CURRENT_OK, b->esc_current,
                         LINK_BN_CURRENT_OK, b->current, out);
}

static bool ina_figure(const bench_state_t *b, uint16_t bit, float v,
                       float *out)
{
    if (b == NULL || out == NULL || !sensed(b) || (b->flags & bit) == 0u) {
        return false;
    }
    *out = v;
    return true;
}

bool bench_state_ina_voltage(const bench_state_t *b, float *out)
{
    return b != NULL && ina_figure(b, LINK_BN_VOLTAGE_OK, b->voltage, out);
}

bool bench_state_ina_current(const bench_state_t *b, float *out)
{
    return b != NULL && ina_figure(b, LINK_BN_CURRENT_OK, b->current, out);
}

void bench_state_fine_totals(bench_state_t *b, int32_t charge_cmah,
                             uint32_t energy_cwh)
{
    if (b == NULL || (b->flags & (uint16_t)LINK_BN_TOTALS_OK) == 0u) {
        return;
    }
    const float mah = (float)charge_cmah / 100.0f;
    const float wh  = (float)energy_cwh / 100.0f;
    /* BENCH holds no negative charge: a run that gave back more than it
     * took reads 0 there, and the finer figure is the only one. */
    const float coarse_mah = b->charge_mah;
    if (fabsf(mah - coarse_mah) <= BENCH_FINE_MAH_TOL
        || (coarse_mah == 0.0f && mah < 0.0f)
        || (coarse_mah >= BENCH_CHARGE_MAH_MAX
            && mah >= BENCH_CHARGE_MAH_MAX - BENCH_FINE_MAH_TOL)) {
        b->charge_mah = mah;
    }
    /* Nor more than 65535 mAh or 6553.5 Wh: past that BENCH reads its
     * ceiling, and a finer figure at or above it is the only one. */
    const float coarse_wh = b->energy_wh;
    if (fabsf(wh - coarse_wh) <= BENCH_FINE_WH_TOL
        || (coarse_wh >= BENCH_ENERGY_WH_MAX
            && wh >= BENCH_ENERGY_WH_MAX - BENCH_FINE_WH_TOL)) {
        b->energy_wh = wh;
    }
}

void bench_state_track_peaks(bench_state_t *b)
{
    if (b == NULL) {
        return;
    }
    if ((b->flags & (uint16_t)LINK_BN_VOLTAGE_OK) != 0u) {
        if (!b->sag_seeded || b->voltage < b->voltage_min) {
            b->voltage_min = b->voltage;
            b->sag_seeded  = true;
        }
    }
    if ((b->flags & (uint16_t)LINK_BN_CURRENT_OK) != 0u
        && b->current > b->current_max) {
        b->current_max = b->current;
    }
    /* Power is a product and carries no flag of its own; it is left at zero
     * unless both halves arrived, so a zero cannot raise a peak. */
    if (b->power > b->power_max) {
        b->power_max = b->power;
    }
    if (b->rpm > b->rpm_max) {
        b->rpm_max = b->rpm;
    }
}
