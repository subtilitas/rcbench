/*
 * The bank rendered onto pins.  See outputs_hw.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_hw.h"

#include <string.h>

#include "pico/stdlib.h"

#include "dshot.h"
#include "out_dshot.h"
#include "out_ppm.h"
#include "out_pwm.h"
#include "ppm.h"

/*
 * How often an ESC (electronic speed controller) is told what to do.
 *
 * The OUTPUTS page's rate field is a bit rate for DShot, not a frame rate, so
 * this is the one output timing the wire does not carry.  A kilohertz is
 * twenty times the rate the panel polls at and far inside every ESC's own
 * signal timeout, and for bidirectional DShot it is also the telemetry rate:
 * a thousand speed readings a second against a bench that samples fifty.
 */
#define DSHOT_UPDATE_HZ   1000u
#define DSHOT_PERIOD_US   (1000000u / DSHOT_UPDATE_HZ)

typedef struct {
    bool     bound;
    uint32_t next_us;      /* DShot only: when the next frame is due */
} slot_state_t;

static out_slot_t   s_shadow[OUT_MAX_SLOTS];
static slot_state_t s_state[OUT_MAX_SLOTS];

/* The last reading any bidirectional ESC gave, with the clock it arrived on.
 * One motor is under test at a time, so one reading is the whole of it. */
static bool     s_have_erpm;
static uint32_t s_erpm;
static uint32_t s_erpm_ms;

void outputs_hw_init(void)
{
    memset(s_shadow, 0, sizeof(s_shadow));
    memset(s_state, 0, sizeof(s_state));
    s_have_erpm = false;
    s_erpm      = 0u;
    s_erpm_ms   = 0u;
}

bool outputs_hw_bound(uint8_t slot)
{
    return slot < OUT_MAX_SLOTS && s_state[slot].bound;
}

/* -------------------------------------------------------------- binding */

static bool same(const out_slot_t *a, const out_slot_t *b)
{
    return a->driver == b->driver
           && a->first_channel == b->first_channel
           && a->channels == b->channels
           && a->pin == b->pin
           && a->rate_hz == b->rate_hz;
}

static void unbind(const out_slot_t *s)
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

static bool bind(const out_slot_t *s)
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

void outputs_hw_apply(const outputs_t *o)
{
    if (o == NULL) {
        return;
    }
    /*
     * Every slot that moved is released before any is bound.  Two slots
     * exchanging pins is a legal reconfiguration, and binding the first one
     * while the second still held its new pin would find the backend of a
     * slot that is about to go and take it for its own.
     */
    bool moved[OUT_MAX_SLOTS];
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        moved[i] = !(same(&s_shadow[i], &o->slot[i]) && s_state[i].bound);
        if (moved[i] && s_state[i].bound) {
            unbind(&s_shadow[i]);
            s_state[i].bound = false;
        }
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (!moved[i]) {
            continue;
        }
        s_shadow[i] = o->slot[i];
        if (s_shadow[i].driver == OUT_DRIVER_NONE) {
            continue;
        }
        /*
         * A slot the silicon cannot serve is left unbound.  The OUTPUTS page
         * still reads back what was asked for, so the disagreement between
         * the page and what is driving is visible from the panel, which is
         * the same way a slot the bank refused already behaves.
         */
        s_state[i].bound   = bind(&s_shadow[i]);
        s_state[i].next_us = time_us_32();
    }
}

/* ------------------------------------------------------------ rendering */

static void service_ppm(const outputs_t *o, const out_slot_t *s, bool drive)
{
    if (!drive) {
        out_ppm_stop(s->pin);
        return;
    }
    uint16_t us[PPM_MAX_CHANNELS];
    const uint8_t n = (s->channels > PPM_MAX_CHANNELS)
                          ? (uint8_t)PPM_MAX_CHANNELS : s->channels;
    for (uint8_t c = 0; c < n; ++c) {
        us[c] = outputs_pulse_us(o, (uint8_t)(s->first_channel + c));
    }
    (void)out_ppm_write(s->pin, us, n);
}

static void service_dshot(const outputs_t *o, const out_slot_t *s,
                          slot_state_t *st, bool drive)
{
    /*
     * The reply to the previous frame is read before the next one is sent.
     * The receiver holds it in its queue until it is taken, and taking it
     * after sending would read it against a frame it did not answer.
     */
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR) {
        dshot_telem_t t;
        if (out_dshot_poll(s->pin, &t) && t.kind == DSHOT_TELEM_ERPM) {
            s_erpm      = t.erpm;
            s_erpm_ms   = to_ms_since_boot(get_absolute_time());
            s_have_erpm = true;
        }
    }

    if (!drive) {
        out_dshot_stop(s->pin);
        /*
         * Kept current while nothing is being sent.  The comparison below is
         * wrap-safe over half of the microsecond clock's 71 minutes, so a
         * due time left behind by a bench that sat disarmed for more than 36
         * of them would read as "not due yet" and stay that way for another
         * 36 after it was armed again.
         */
        st->next_us = time_us_32();
        return;
    }
    const uint32_t now = time_us_32();
    if ((uint32_t)(now - st->next_us) >= 0x80000000u) {
        return;                              /* not due yet, wrap-safe */
    }
    st->next_us = now + DSHOT_PERIOD_US;

    const uint16_t command = outputs_actual(o, s->first_channel);
    out_dshot_send(s->pin, dshot_throttle(command, OUT_SPAN), false);
}

void outputs_hw_service(const outputs_t *o)
{
    if (o == NULL) {
        return;
    }
    /*
     * One question for the whole bank: is it armed and being commanded.
     * Asking it per slot would let one output keep driving on a stale answer
     * while another had already stopped.
     */
    const bool drive = outputs_driving(o);

    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (!s_state[i].bound) {
            continue;
        }
        const out_slot_t *s = &s_shadow[i];
        switch (s->driver) {
        case OUT_DRIVER_PWM:
            out_pwm_write(s->pin,
                          drive ? outputs_pulse_us(o, s->first_channel) : 0u);
            break;
        case OUT_DRIVER_PPM:
            service_ppm(o, s, drive);
            break;
        case OUT_DRIVER_DSHOT:
        case OUT_DRIVER_DSHOT_BIDIR:
            service_dshot(o, s, &s_state[i], drive);
            break;
        case OUT_DRIVER_NONE:
        case OUT_DRIVER_COUNT:
        default:
            break;
        }
    }
}

bool outputs_hw_erpm(uint32_t *erpm, uint32_t *age_ms)
{
    if (!s_have_erpm || erpm == NULL || age_ms == NULL) {
        return false;
    }
    *erpm   = s_erpm;
    *age_ms = (uint32_t)(to_ms_since_boot(get_absolute_time()) - s_erpm_ms);
    return true;
}
