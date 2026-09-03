/*
 * Hardware PWM (pulse-width modulation) servo outputs.  See out_pwm.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_pwm.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "outputs.h"

/*
 * One microsecond per count.  The frame period then fits the sixteen-bit
 * counter down to 16 Hz, past the 40 Hz floor the driver table sets, and a
 * microsecond is the resolution every pulse in this firmware is expressed in.
 */
#define TICK_HZ  1000000u

/*
 * What is bound, so the slice-sharing rule can be checked and a release can
 * put the pin back.  One entry per slot the bank can hold; nothing here has
 * more outputs than that.
 */
typedef struct {
    bool     used;
    uint8_t  pin;
    uint16_t rate_hz;
    uint16_t wrap;      /**< the last count of the frame                  */
} binding_t;

static binding_t s_bound[OUT_MAX_SLOTS];

static binding_t *find(uint8_t pin)
{
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (s_bound[i].used && s_bound[i].pin == pin) {
            return &s_bound[i];
        }
    }
    return NULL;
}

static uint16_t wrap_for(uint16_t rate_hz)
{
    const uint32_t period = TICK_HZ / rate_hz;
    return (period == 0u || period > 65536u) ? 0u : (uint16_t)(period - 1u);
}

bool out_pwm_bind(uint8_t pin, uint16_t rate_hz)
{
    if (rate_hz == 0u) {
        return false;
    }
    const uint16_t wrap = wrap_for(rate_hz);
    if (wrap == 0u) {
        return false;
    }

    binding_t *existing = find(pin);
    if (existing != NULL) {
        /* Rebinding the same pin at the same rate is what a reconfiguration
         * that did not move this slot looks like; anything else is a change
         * the caller has to make by releasing first. */
        return existing->rate_hz == rate_hz;
    }

    const uint slice = pwm_gpio_to_slice_num(pin);
    binding_t *free_slot = NULL;
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (!s_bound[i].used) {
            if (free_slot == NULL) {
                free_slot = &s_bound[i];
            }
            continue;
        }
        /* Two channels of one slice count off one wrap register.  Letting
         * the second binding win would move the first output's frame rate
         * without anything saying so. */
        if (pwm_gpio_to_slice_num(s_bound[i].pin) == slice
            && s_bound[i].rate_hz != rate_hz) {
            return false;
        }
    }
    if (free_slot == NULL) {
        return false;
    }

    gpio_set_function(pin, GPIO_FUNC_PWM);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (float)TICK_HZ);
    pwm_config_set_wrap(&c, wrap);
    /* The level is set before the slice runs, so a pin cannot emit one frame
     * of whatever the counter happened to hold. */
    pwm_set_gpio_level(pin, 0u);
    pwm_init(slice, &c, true);
    pwm_set_gpio_level(pin, 0u);

    free_slot->used    = true;
    free_slot->pin     = pin;
    free_slot->rate_hz = rate_hz;
    free_slot->wrap    = wrap;
    return true;
}

void out_pwm_release(uint8_t pin)
{
    binding_t *b = find(pin);
    if (b == NULL) {
        return;
    }
    pwm_set_gpio_level(pin, 0u);

    /* The slice keeps running while its other channel is still bound. */
    const uint slice = pwm_gpio_to_slice_num(pin);
    bool shared = false;
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (s_bound[i].used && &s_bound[i] != b
            && pwm_gpio_to_slice_num(s_bound[i].pin) == slice) {
            shared = true;
        }
    }
    if (!shared) {
        pwm_set_enabled(slice, false);
    }

    /* Back to a plain input: an unbound pin that stayed a PWM output would
     * hold whatever level the slice stopped at. */
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    b->used = false;
}

void out_pwm_write(uint8_t pin, uint16_t pulse_us)
{
    const binding_t *b = find(pin);
    if (b == NULL) {
        return;
    }
    /*
     * A pulse cannot be longer than the frame that carries it.  400 Hz is a
     * 2500 us period and 2500 us is a legal endpoint, and a level above the
     * wrap makes the counter never reach it: the pin sits high for ever,
     * which is not a long pulse but no pulse at all.  Clamped to the wrap,
     * so there is always a tick of low and the output stays a pulse train.
     *
     * The combination is refused nowhere, because the endpoint is on the
     * CHAN_CFG page and the rate is on the OUTPUTS page and either may be
     * written after the other.  This is where both are known.
     */
    pwm_set_gpio_level(pin, (pulse_us > b->wrap) ? b->wrap : pulse_us);
}
