/**
 * @file
 * @brief The bank rendered onto pins: the pin drivers and the clocks behind
 *        shared/outputs/out_render.c.
 *
 * Every rule of the rendering is in out_render.c, where the host suite holds
 * it: the release-before-bind order of slots and ports, the extended
 * telemetry ask sent in place of a stop frame, and the slot whose readings
 * the bench publishes.  This file defines the out_render_hw_*() calls on the
 * RP2350's drivers and keeps the one renderer the IOmcu runs.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_hw.h"

#include "pico/stdlib.h"

#include "dshot.h"
#include "link_pages.h"
#include "out_dshot.h"
#include "out_ppm.h"
#include "out_pwm.h"
#include "out_render.h"
#include "port_park.h"
#include "ppm.h"

/** @brief The IOmcu's one renderer. */
static out_render_t s_render;

/* ------------------------------------------------------------ the pins */

bool out_render_hw_bind(const out_slot_t *s)
{
    switch (s->driver) {
    case OUT_DRIVER_PWM:
        return out_pwm_bind(s->pin, s->rate_hz);
    case OUT_DRIVER_PPM:
        return out_ppm_bind(s->pin, s->channels, s->rate_hz);
    case OUT_DRIVER_DSHOT:
        return out_dshot_bind(s->pin, s->rate_hz, false);
    case OUT_DRIVER_DSHOT_BIDIR:
        return out_dshot_bind(s->pin, s->rate_hz, true);
    case OUT_DRIVER_NONE:
    case OUT_DRIVER_COUNT:
    default:
        return false;
    }
}

void out_render_hw_release(const out_slot_t *s)
{
    switch (s->driver) {
    case OUT_DRIVER_PWM:         out_pwm_release(s->pin);   break;
    case OUT_DRIVER_PPM:         out_ppm_release(s->pin);   break;
    case OUT_DRIVER_DSHOT:
    case OUT_DRIVER_DSHOT_BIDIR: out_dshot_release(s->pin); break;
    case OUT_DRIVER_NONE:
    case OUT_DRIVER_COUNT:
    default:                                                break;
    }
}

bool out_render_hw_park(uint8_t pin)
{
    return port_park(pin);
}

void out_render_hw_unpark(uint8_t pin)
{
    port_unpark(pin);
}

void out_render_hw_pwm(uint8_t pin, uint16_t pulse_us)
{
    out_pwm_write(pin, pulse_us);
}

bool out_render_hw_pwm_stamped(uint8_t pin, uint16_t pulse_us,
                               uint64_t *at_us)
{
    return out_pwm_write_stamped(pin, pulse_us, at_us);
}

void out_render_hw_ppm(uint8_t pin, const uint16_t *us, uint8_t n)
{
    (void)out_ppm_write(pin, us, n);
}

void out_render_hw_ppm_stop(uint8_t pin)
{
    out_ppm_stop(pin);
}

bool out_render_hw_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *t)
{
    return out_dshot_poll(pin, edt, t);
}

void out_render_hw_dshot_stop(uint8_t pin)
{
    out_dshot_stop(pin);
}

bool out_render_hw_dshot_wake(uint8_t pin)
{
    return out_dshot_wake(pin);
}

void out_render_hw_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    out_dshot_send(pin, value, telemetry);
}

uint32_t out_render_hw_us(void)
{
    return time_us_32();
}

uint32_t out_render_hw_ms(void)
{
    return (uint32_t)to_ms_since_boot(get_absolute_time());
}

/* ------------------------------------------------------------ the bank */

void outputs_hw_init(void)
{
    out_render_init(&s_render);
}

bool outputs_hw_bound(uint8_t slot)
{
    return out_render_bound(&s_render, slot);
}

void outputs_hw_apply(const outputs_t *o, const uint16_t *rate_hz)
{
    out_render_apply(&s_render, o, rate_hz, 0xFFu);
}

void outputs_hw_apply_only(const outputs_t *o, const uint16_t *rate_hz,
                           uint8_t may_bind)
{
    out_render_apply(&s_render, o, rate_hz, may_bind);
}

bool outputs_hw_return_slot(const outputs_t *o, const uint16_t *rate_hz,
                            uint8_t slot)
{
    return out_render_return_slot(&s_render, o, rate_hz, slot);
}

void outputs_hw_ports(const uint8_t *pin, const uint16_t *rate_hz,
                      uint8_t parked, uint8_t dshot, uint8_t channel0)
{
    out_render_ports(&s_render, pin, rate_hz, parked, dshot, channel0);
}

uint8_t outputs_hw_ports_bound(void)
{
    return out_render_ports_bound(&s_render);
}

void outputs_hw_ports_drive(uint8_t ports)
{
    out_render_ports_drive(&s_render, ports);
}

void outputs_hw_watch(int ch)
{
    out_render_watch(&s_render, ch);
}

bool outputs_hw_edge(uint64_t *us)
{
    return out_render_edge(&s_render, us);
}

void outputs_hw_service(const outputs_t *o)
{
    out_render_service(&s_render, o);
}

bool outputs_hw_erpm(uint32_t *erpm, uint32_t *age_ms)
{
    return out_render_erpm(&s_render, erpm, age_ms);
}

bool outputs_hw_edt(dshot_telem_kind_t kind, uint16_t *value, uint32_t *age_ms)
{
    return out_render_edt(&s_render, kind, value, age_ms);
}

/*
 * The protocol modules carry their own limits, so that they build without
 * this project.  The bank's limits are the same numbers: a slot the bank can
 * hold is a pin a driver can bind, and a width the bank passes is a width
 * the PPM frame carries.
 */
_Static_assert(OUT_DSHOT_MAX_PINS >= OUT_MAX_SLOTS + LINK_PG_PORTS_MAX,
               "the DShot driver binds fewer pins than the slots and the "
               "programming ports");
_Static_assert(OUT_PPM_MAX_PINS >= OUT_MAX_SLOTS,
               "the PPM driver binds fewer pins than the bank has slots");
_Static_assert(PPM_MAX_CHANNELS == OUT_RENDER_PPM_MAX,
               "the PPM frame and the renderer disagree on its channels");
_Static_assert(PPM_CHANNEL_MIN_US == OUT_FLOOR_US
                   && PPM_CHANNEL_MAX_US == OUT_CEILING_US,
               "the PPM channel range is not the bank's range");
