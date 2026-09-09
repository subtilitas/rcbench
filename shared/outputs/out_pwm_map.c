/*
 * The RP2350's GPIO-to-PWM fold. See out_pwm_map.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_pwm_map.h"

uint8_t out_pwm_slice_of(uint8_t pin)
{
    if (pin >= OUT_PWM_GPIOS) {
        return OUT_PWM_NONE;
    }
    /* Eight slices serve GP0 to GP31 and the remaining four serve GP32 to
     * GP47, so the two ranges wrap at different widths. */
    return (pin < 32u) ? (uint8_t)((pin >> 1) & 7u)
                       : (uint8_t)(8u + ((pin >> 1) & 3u));
}

uint8_t out_pwm_channel_of(uint8_t pin)
{
    if (pin >= OUT_PWM_GPIOS) {
        return OUT_PWM_NONE;
    }
    return (uint8_t)(pin & 1u);
}

bool out_pwm_same_slice(uint8_t a, uint8_t b)
{
    const uint8_t slice = out_pwm_slice_of(a);
    return slice != OUT_PWM_NONE && slice == out_pwm_slice_of(b);
}

bool out_pwm_same_compare(uint8_t a, uint8_t b)
{
    return out_pwm_same_slice(a, b)
           && out_pwm_channel_of(a) == out_pwm_channel_of(b);
}
