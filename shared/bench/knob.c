/*
 * SPDX-License-Identifier: MIT
 */

#include "knob.h"

#include <stddef.h>

const uint8_t knob_poll_regs[KNOB_POLL_READS] = {
    KNOB_REG_ANGLE_HI,      /* first, as the datasheet asks */
    KNOB_REG_ANGLE_LO,
    KNOB_REG_ANGLE_HI,      /* again: the high byte did not move meanwhile */
};

uint16_t knob_decode(uint8_t hi, uint8_t lo)
{
    return (uint16_t)(((unsigned)hi << 6) | ((unsigned)lo >> 2));
}

uint16_t knob_clockwise(uint16_t angle)
{
    return (uint16_t)((KNOB_COUNTS - (unsigned)angle) & (KNOB_COUNTS - 1u));
}

bool knob_attach(knob_t *k, unsigned addr)
{
    knob_reset(k);
    k->attached = !panel_i2c_addr_reserved(addr);
    k->addr = k->attached ? (uint8_t)addr : 0u;
    return k->attached;
}

int knob_poll_addr(const knob_t *k)
{
    return k->attached ? (int)k->addr : -1;
}

bool knob_poll_probe_first(const knob_t *k)
{
    return k->attached && !k->have_ref;
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

int knob_poll_done(knob_t *k, bool answered,
                   const uint8_t bytes[KNOB_POLL_READS])
{
    if (!k->attached || !answered || bytes == NULL) {
        return knob_feed(k, false, 0);
    }
    if (bytes[0] != bytes[2]) {
        return 0;               /* torn: no reading, the reference stays */
    }
    return knob_feed(k, true, knob_clockwise(knob_decode(bytes[0], bytes[1])));
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
