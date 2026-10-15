/*
 * SPDX-License-Identifier: MIT
 */

#include "feetech_reg.h"

#include <string.h>

static const uint32_t k_baud[FEETECH_BAUD_COUNT] = {
    1000000u, 500000u, 250000u, 128000u, 115200u, 76800u, 57600u, 38400u,
};

/* The tables by role; size 0 is a register the series does not have. */
static const feetech_reg_t k_scs[FEETECH_R_COUNT] = {
    [FEETECH_R_ID]        = { FEETECH_REG_ID,        1, 0, 253,  true  },
    [FEETECH_R_BAUD]      = { FEETECH_REG_BAUD,      1, 0, 7,    true  },
    [FEETECH_R_MIN_ANGLE] = { FEETECH_REG_MIN_ANGLE, 2, 0, 1023, true  },
    [FEETECH_R_MAX_ANGLE] = { FEETECH_REG_MAX_ANGLE, 2, 0, 1023, true  },
    [FEETECH_R_MODE]      = { 0, 0, 0, 0, false },
    [FEETECH_R_LOCK]      = { FEETECH_REG_LOCK_SCS,  1, 0, 1,    true  },
    [FEETECH_R_TORQUE]    = { FEETECH_REG_TORQUE,    1, 0, 1,    false },
    [FEETECH_R_ACC]       = { 0, 0, 0, 0, false },
    [FEETECH_R_GOAL]      = { FEETECH_REG_GOAL,      2, 0, 1023, false },
    [FEETECH_R_SPEED]     = { FEETECH_REG_SPEED,     2, 0, 1000, false },
};

static const feetech_reg_t k_sts[FEETECH_R_COUNT] = {
    [FEETECH_R_ID]        = { FEETECH_REG_ID,        1, 0, 253,  true  },
    [FEETECH_R_BAUD]      = { FEETECH_REG_BAUD,      1, 0, 7,    true  },
    [FEETECH_R_MIN_ANGLE] = { FEETECH_REG_MIN_ANGLE, 2, 0, 4095, true  },
    [FEETECH_R_MAX_ANGLE] = { FEETECH_REG_MAX_ANGLE, 2, 0, 4095, true  },
    [FEETECH_R_MODE]      = { FEETECH_REG_MODE,      1, 0, 3,    true  },
    [FEETECH_R_LOCK]      = { FEETECH_REG_LOCK_STS,  1, 0, 1,    true  },
    [FEETECH_R_TORQUE]    = { FEETECH_REG_TORQUE,    1, 0, 1,    false },
    [FEETECH_R_ACC]       = { FEETECH_REG_ACC,       1, 0, 255,  false },
    [FEETECH_R_GOAL]      = { FEETECH_REG_GOAL,      2, 0, 4095, false },
    [FEETECH_R_SPEED]     = { FEETECH_REG_SPEED,     2, 0, 3400, false },
};

bool feetech_reg(feetech_series_t series, feetech_role_t role,
                 feetech_reg_t *out)
{
    const feetech_reg_t *t;

    if ((unsigned)role >= (unsigned)FEETECH_R_COUNT) {
        return false;
    }
    if (series == FEETECH_SCS) {
        t = &k_scs[role];
    } else if (series == FEETECH_STS) {
        t = &k_sts[role];
    } else {
        return false;
    }
    if (t->size == 0u) {
        return false;
    }
    if (out != NULL) {
        *out = *t;
    }
    return true;
}

uint32_t feetech_baud(unsigned index)
{
    return index < FEETECH_BAUD_COUNT ? k_baud[index] : 0u;
}

uint8_t feetech_lock_addr(feetech_series_t series)
{
    return series == FEETECH_SCS ? FEETECH_REG_LOCK_SCS
         : series == FEETECH_STS ? FEETECH_REG_LOCK_STS : 0u;
}

uint8_t feetech_live_bytes(feetech_series_t series)
{
    return series == FEETECH_SCS ? FEETECH_LIVE_SCS
         : series == FEETECH_STS ? FEETECH_LIVE_STS : 0u;
}

uint16_t feetech_position_max(feetech_series_t series)
{
    return series == FEETECH_SCS ? 1023u : 4095u;
}

void feetech_put16(feetech_series_t series, uint8_t *at, uint16_t v)
{
    const uint8_t lo = (uint8_t)(v & 0xFFu);
    const uint8_t hi = (uint8_t)(v >> 8);

    if (series == FEETECH_SCS) {
        at[0] = hi;
        at[1] = lo;
    } else {
        at[0] = lo;
        at[1] = hi;
    }
}

uint16_t feetech_get16(feetech_series_t series, const uint8_t *at)
{
    if (series == FEETECH_SCS) {
        return (uint16_t)(((unsigned)at[0] << 8) | at[1]);
    }
    return (uint16_t)(((unsigned)at[1] << 8) | at[0]);
}

uint16_t feetech_sign_mag(int32_t v)
{
    if (v < 0) {
        const int32_t m = v < -32767 ? 32767 : -v;

        return (uint16_t)(0x8000u | (uint32_t)m);
    }
    return (uint16_t)(v > 32767 ? 32767 : v);
}

int32_t feetech_from_sign_mag(uint16_t raw)
{
    const int32_t m = (int32_t)(raw & 0x7FFFu);

    return (raw & 0x8000u) != 0u ? -m : m;
}

bool feetech_live(feetech_series_t series, const uint8_t *bytes, unsigned n,
                  feetech_live_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (bytes == NULL || n == 0u || n != feetech_live_bytes(series)) {
        return false;
    }
    /* Offsets from address 56. */
    out->position = feetech_get16(series, &bytes[0]);
    out->speed = (int16_t)feetech_from_sign_mag(
        feetech_get16(series, &bytes[2]));
    out->load = (uint16_t)(feetech_get16(series, &bytes[4]) & 0x3FFu);
    out->voltage_dv = bytes[6];
    out->temp_c = bytes[7];
    out->status = bytes[FEETECH_REG_STATUS - FEETECH_REG_PRESENT];
    out->moving = bytes[10];
    if (series == FEETECH_STS) {
        out->has_current = true;
        out->current_raw = feetech_get16(
            series, &bytes[FEETECH_REG_CURRENT - FEETECH_REG_PRESENT]);
    }
    return true;
}

uint32_t feetech_current_ma(uint16_t raw)
{
    return ((uint32_t)raw * 65u + 5u) / 10u;
}
