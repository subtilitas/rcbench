/*
 * SPDX-License-Identifier: MIT
 */

#include "knob.h"

#include <stddef.h>

bool knob_decode(uint8_t status, const uint8_t raw[KNOB_WORD_LEN],
                 const uint8_t magnitude[KNOB_WORD_LEN], knob_reading_t *out)
{
    knob_reading_t r;
    r.magnet     = (status & KNOB_STATUS_MD) != 0u;
    r.too_weak   = (status & KNOB_STATUS_ML) != 0u;
    r.too_strong = (status & KNOB_STATUS_MH) != 0u;
    r.raw = (uint16_t)((((unsigned)raw[0] & 0x0Fu) << 8) | raw[1]);
    r.magnitude = (uint16_t)((((unsigned)magnitude[0] & 0x0Fu) << 8) | magnitude[1]);
    if (out != NULL) {
        *out = r;
    }
    return r.magnet && !r.too_weak && !r.too_strong && r.magnitude != 0u;
}

void knob_reset(knob_t *k)
{
    k->have_ref = false;
    k->ref = 0;
}

int knob_feed(knob_t *k, bool usable, uint16_t raw)
{
    if (!usable) {
        knob_reset(k);
        return 0;
    }
    raw &= (uint16_t)(KNOB_COUNTS - 1);
    if (!k->have_ref) {
        k->have_ref = true;
        k->ref = raw;
        return 0;
    }
    int d = (int)raw - (int)k->ref;
    if (d >= KNOB_COUNTS / 2) {
        d -= KNOB_COUNTS;
    } else if (d < -KNOB_COUNTS / 2) {
        d += KNOB_COUNTS;
    }
    k->ref = raw;
    if (d > KNOB_MAX_STEP || d < -KNOB_MAX_STEP) {
        return 0;
    }
    return d;
}

float knob_span_fraction(int steps, int scale_deg)
{
    if (scale_deg < KNOB_SCALE_DEG_MIN) {
        scale_deg = KNOB_SCALE_DEG_MIN;
    }
    if (scale_deg > KNOB_SCALE_DEG_MAX) {
        scale_deg = KNOB_SCALE_DEG_MAX;
    }
    return (float)steps * 360.0f / (float)KNOB_COUNTS / (float)scale_deg;
}
