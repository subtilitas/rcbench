/*
 * The outbound half of DShot: sixteen bits and the map from travel onto them.
 * See dshot.h for the model.
 *
 * SPDX-License-Identifier: MIT
 */

#include "dshot.h"

/*
 * The checksum is three nibbles of the twelve-bit payload folded together.
 * It catches every single-bit error and every burst inside one nibble, which
 * is what a bit period misread as its neighbour looks like.  It is not a CRC
 * (cyclic redundancy check) in any useful sense and the protocol does not
 * claim one.
 */
static uint16_t fold(uint16_t payload)
{
    uint16_t c = payload;
    c ^= (uint16_t)(c >> 4);
    c ^= (uint16_t)(c >> 8);
    return (uint16_t)(c & 0x0Fu);
}

uint16_t dshot_frame(uint16_t value, bool telemetry, bool inverted)
{
    if (value > DSHOT_VALUE_MAX) {
        /* Not clamped to the top: a value this end could not encode is a
         * fault in the caller, and the safe reading of a fault is stop. */
        value = 0u;
    }
    const uint16_t payload =
        (uint16_t)((uint16_t)(value << 1) | (telemetry ? 1u : 0u));
    const uint16_t csum = inverted ? (uint16_t)(~fold(payload) & 0x0Fu)
                                   : fold(payload);
    return (uint16_t)((uint16_t)(payload << 4) | csum);
}

uint16_t dshot_throttle(uint16_t command, uint16_t span)
{
    if (command == 0u) {
        return (uint16_t)DSHOT_CMD_MOTOR_STOP;
    }
    if (span <= 1u || command >= span) {
        return (uint16_t)DSHOT_THROTTLE_MAX;
    }
    /*
     * One maps to 48 and the top of the span maps to 2047, so the whole of
     * the commandable range reaches the whole of the throttle range.  The
     * step from stop to 48 is the protocol's discontinuity and is left where
     * it is; spreading it out would make the first percent of travel do
     * nothing.
     *
     * Rounded rather than truncated, and in 32 bits: the numerator reaches
     * 1999 * 65534.
     */
    const uint32_t range = DSHOT_THROTTLE_MAX - DSHOT_THROTTLE_MIN;
    const uint32_t den   = (uint32_t)span - 1u;
    const uint32_t step  =
        ((uint32_t)(command - 1u) * range + den / 2u) / den;
    return (uint16_t)(DSHOT_THROTTLE_MIN + step);
}
