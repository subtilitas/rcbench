/*
 * The servo page's rules.
 *
 * Two refusals and one clamp, and the difference between them is the whole
 * design.
 *
 * A limit outside what a servo can physically take is *refused*: it is a
 * configuration mistake, and accepting it quietly would leave the range
 * looking set when it was not.  A pulse outside the limits is *clamped*
 * instead, because a pulse arrives many times a second from a host that may
 * be mid-drag -- refusing one gives a servo that stops following, where
 * clamping gives a servo that stops at its stop, which is what a stop is for.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>

#include "link_servo.h"

#include "link_msg.h"
#include "link_pages.h"

void link_servo_defaults(uint16_t *regs)
{
    if (regs == NULL) {
        return;
    }
    regs[LINK_SV_ENABLE]   = 0u;
    regs[LINK_SV_MIN_US]   = LINK_SV_DEFAULT_MIN;
    regs[LINK_SV_MAX_US]   = LINK_SV_DEFAULT_MAX;
    regs[LINK_SV_PULSE_US] = (uint16_t)((LINK_SV_DEFAULT_MIN
                                         + LINK_SV_DEFAULT_MAX) / 2u);
    regs[LINK_SV_SLEW_US]  = 0u;
}

uint8_t link_servo_write(uint16_t *regs, uint8_t off, uint8_t n,
                         const uint16_t *in, bool may_drive)
{
    if (regs == NULL || in == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SV_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }

    /*
     * Validated before anything is stored, so a rejected write leaves the
     * page as it was.  Half-applying one would put a servo somewhere nobody
     * asked for and report a refusal at the same time.
     */
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if ((reg == LINK_SV_MIN_US || reg == LINK_SV_MAX_US)
            && (in[i] < LINK_SV_FLOOR_US || in[i] > LINK_SV_CEILING_US)) {
            return LINK_NACK_BAD_VALUE;
        }
        if (reg == LINK_SV_ENABLE && in[i] > 1u) {
            return LINK_NACK_BAD_VALUE;
        }
    }

    /* Driving an output is arming something, and the holder of the wire
     * decides that, not whoever asked. */
    for (uint8_t i = 0; i < n; ++i) {
        if ((uint8_t)(off + i) == LINK_SV_ENABLE && in[i] != 0u && !may_drive) {
            return LINK_NACK_NOT_ARMED;
        }
    }

    for (uint8_t i = 0; i < n; ++i) {
        regs[off + i] = in[i];
    }

    /* An inverted pair is a mistake rather than a range, and it would make
     * the clamp below unsatisfiable. */
    if (regs[LINK_SV_MIN_US] > regs[LINK_SV_MAX_US]) {
        const uint16_t t = regs[LINK_SV_MIN_US];
        regs[LINK_SV_MIN_US] = regs[LINK_SV_MAX_US];
        regs[LINK_SV_MAX_US] = t;
    }
    if (regs[LINK_SV_PULSE_US] < regs[LINK_SV_MIN_US]) {
        regs[LINK_SV_PULSE_US] = regs[LINK_SV_MIN_US];
    }
    if (regs[LINK_SV_PULSE_US] > regs[LINK_SV_MAX_US]) {
        regs[LINK_SV_PULSE_US] = regs[LINK_SV_MAX_US];
    }
    return 0u;
}
