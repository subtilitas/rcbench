/*
 * The SENSE_TRACE build's ring, console and frame time.  See
 * sense_trace_hw.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace_hw.h"

#include "hardware/pwm.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "outputs_hw.h"
#include "sense_trace.h"

_Static_assert(SENSE_TRACE_SLOTS >= OUT_MAX_SLOTS,
               "every slot's pulse is watched");

static sense_trace_t     s_trace;
static sense_trace_rec_t s_ring[SENSE_TRACE_HW_RING];

void sense_trace_hw_init(void)
{
    (void)sense_trace_init(&s_trace, s_ring, SENSE_TRACE_HW_RING);
}

void sense_trace_hw_feed(const sense_svc_t *svc, uint64_t tick_us)
{
    sense_trace_feed(&s_trace, svc->open ? &svc->sched : NULL, tick_us);
}

void sense_trace_hw_outputs(const outputs_t *o)
{
    const bool drive = outputs_driving(o);
    for (uint8_t i = 0; i < OUT_MAX_SLOTS; ++i) {
        const out_slot_t *s = &o->slot[i];
        if (s->driver != OUT_DRIVER_PWM || !outputs_hw_bound(i)) {
            /* No PWM pulse of this slot's: what it rendered before is
             * no measure for what it renders next. */
            sense_trace_unwatch(&s_trace, i);
            continue;
        }
        /* The pulse outputs_hw_service() has just written to this pin,
         * held to the frame as out_pwm_write() holds it. */
        const uint slice = pwm_gpio_to_slice_num(s->pin);
        const uint32_t top = pwm_hw->slice[slice].top;
        const uint16_t pulse = drive
            ? sense_trace_rendered(outputs_pulse_us(o, s->first_channel), top)
            : 0u;
        const uint64_t now = time_us_64();
        if (!sense_trace_pulse(&s_trace, i, s->first_channel, pulse, now)) {
            continue;
        }
        /* The level takes effect at the wrap that ends this frame. */
        const uint32_t count = pwm_get_counter(slice);
        const uint64_t frame = time_us_64() + (uint64_t)(top - count) + 1u;
        sense_trace_trigger(&s_trace, SENSE_TRACE_TRIG_CMD, frame,
                            s->first_channel, pulse);
    }
}

void sense_trace_hw_edge(uint64_t edge_us)
{
    sense_trace_trigger(&s_trace, SENSE_TRACE_TRIG_EDGE, edge_us, 0u, 0u);
}

void sense_trace_hw_pass(void)
{
    const uint64_t now = time_us_64();
    const int c = getchar_timeout_us(0u);
    if (c >= 0) {
        sense_trace_key(&s_trace, c, now);
    }
    /* Room as the transmit buffer has it now: a write of no more than
     * that returns without waiting for the host. */
    char buf[SENSE_TRACE_LINE_MAX];
    const bool connected = stdio_usb_connected();
    size_t room = connected ? (size_t)tud_cdc_write_available() : 0u;
    if (room > sizeof(buf)) {
        room = sizeof(buf);
    }
    const size_t n = sense_trace_pump(&s_trace, now, connected, buf, room);
    if (n > 0u) {
        (void)stdio_put_string(buf, (int)n, false, false);
    }
}
