/*
 * The output bank.  See outputs.h for the model.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs.h"

#include <stddef.h>
#include <string.h>

static const out_driver_def_t k_drivers[OUT_DRIVER_COUNT] = {
    [OUT_DRIVER_NONE]  = { "none",  0, 0, 0, false, false,   0,     0 },
    /*
     * PWM (pulse-width modulation): the 400 Hz ceiling is the frame rate a
     * narrow-band digital servo takes; below 40 Hz a servo audibly steps.
     */
    [OUT_DRIVER_PWM]   = { "PWM",   1, 1, 1, false, true,   40,   400 },
    /*
     * PPM (pulse-position modulation) is the reason the channel count is a
     * range.  Eight channels in a 22.5 ms frame is the convention; more fits
     * only by shortening frames.
     */
    [OUT_DRIVER_PPM]   = { "PPM",   1, 8, 1, false, true,   20,    50 },
    /*
     * DShot's rate is its bit rate in kbit/s rather than a frame rate, which
     * is why the rate range here is not a frame rate range.
     */
    [OUT_DRIVER_DSHOT] = { "DShot", 1, 1, 1, false, false, 150,  1200 },
    /*
     * Bidirectional DShot listens on the pin it drives, which is why pin
     * count and read-back are separate questions: one wire, both directions.
     */
    [OUT_DRIVER_DSHOT_BIDIR] =
                         { "DShot bidir", 1, 1, 1, true, false, 150, 1200 },
};

const out_driver_def_t *out_driver(out_driver_t d)
{
    if ((unsigned)d >= (unsigned)OUT_DRIVER_COUNT) {
        return NULL;
    }
    return &k_drivers[d];
}

static uint16_t rest_for(out_role_t role)
{
    return (role == OUT_ROLE_THROTTLE) ? 0u : (uint16_t)(OUT_SPAN / 2u);
}

void outputs_init(outputs_t *o, uint32_t now_ms)
{
    if (o == NULL) {
        return;
    }
    memset(o, 0, sizeof(*o));
    for (unsigned i = 0; i < OUT_MAX_CHANNELS; ++i) {
        out_channel_t *c = &o->channel[i];
        /*
         * Surface rather than throttle by default.  Getting the role wrong in
         * that direction gives an output that centres when it should stop;
         * the other way gives one that stops when it should centre, and a
         * surface slamming to an endpoint is the worse of the two.
         */
        c->role   = OUT_ROLE_SURFACE;
        c->rest   = rest_for(c->role);
        c->actual = c->rest;
        c->command = c->rest;
        c->min_us = 1000u;
        c->max_us = 2000u;
        c->last_command_ms = now_ms;
    }
    o->timeout_ms   = OUT_DEFAULT_TIMEOUT_MS;
    o->last_step_ms = now_ms;
}

void outputs_reserve_pins(outputs_t *o, uint64_t mask)
{
    if (o == NULL) {
        return;
    }
    o->reserved = mask;
}

bool outputs_pin_available(const outputs_t *o, uint8_t pin)
{
    if (o == NULL || pin > OUT_MAX_PIN) {
        return false;
    }
    return (o->reserved & ((uint64_t)1u << pin)) == 0u;
}

/* Does [a0,a0+an) meet [b0,b0+bn)? */
static bool overlaps(unsigned a0, unsigned an, unsigned b0, unsigned bn)
{
    return a0 < b0 + bn && b0 < a0 + an;
}

bool outputs_configure(outputs_t *o, uint8_t slot, const out_slot_t *cfg)
{
    if (o == NULL || cfg == NULL || slot >= OUT_MAX_SLOTS) {
        return false;
    }
    const out_driver_def_t *d = out_driver(cfg->driver);
    if (d == NULL) {
        return false;
    }
    if (cfg->driver == OUT_DRIVER_NONE) {
        o->slot[slot] = (out_slot_t){ 0 };
        return true;
    }
    if (cfg->channels < d->channels_min || cfg->channels > d->channels_max) {
        return false;
    }
    if ((unsigned)cfg->first_channel + cfg->channels > OUT_MAX_CHANNELS) {
        return false;
    }
    if (cfg->rate_hz < d->rate_min_hz || cfg->rate_hz > d->rate_max_hz) {
        return false;
    }
    /* A pin that does not exist or already carries something else.  Refused
     * here rather than at the driver, so the read-back of the OUTPUTS page
     * and the bank disagree in the one place the panel already watches. */
    if (!outputs_pin_available(o, cfg->pin)) {
        return false;
    }

    /*
     * The two checks only a shared table can make: two drivers on one pin,
     * and two slots rendering the same channel to different wires.  Both are
     * silent until something moves that should not.
     */
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (i == slot || o->slot[i].driver == OUT_DRIVER_NONE) {
            continue;
        }
        if (o->slot[i].pin == cfg->pin) {
            return false;
        }
        if (overlaps(cfg->first_channel, cfg->channels,
                     o->slot[i].first_channel, o->slot[i].channels)) {
            return false;
        }
    }

    o->slot[slot] = *cfg;
    return true;
}

bool outputs_set_endpoints(outputs_t *o, uint8_t ch, uint16_t min_us,
                           uint16_t max_us)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return false;
    }
    if (min_us < OUT_FLOOR_US || min_us > OUT_CEILING_US
        || max_us < OUT_FLOOR_US || max_us > OUT_CEILING_US) {
        return false;
    }
    if (min_us > max_us) {
        const uint16_t t = min_us;
        min_us = max_us;
        max_us = t;
    }
    o->channel[ch].min_us = min_us;
    o->channel[ch].max_us = max_us;
    return true;
}

bool outputs_set_role(outputs_t *o, uint8_t ch, out_role_t role)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return false;
    }
    if (role != OUT_ROLE_THROTTLE && role != OUT_ROLE_SURFACE) {
        return false;
    }
    out_channel_t *c = &o->channel[ch];
    if (c->role == role) {
        return true;   /* setting the role it has is not a role change */
    }
    c->role = role;
    c->rest = rest_for(role);
    /*
     * A role change moves where rest is, and a channel sitting at the old
     * rest is idle, not commanded.  A channel left at a throttle's zero after
     * becoming a surface shows a surface hard over.
     */
    if (!o->armed) {
        c->actual  = c->rest;
        c->command = c->rest;
    }
    return true;
}

bool outputs_set_slew(outputs_t *o, uint8_t ch, uint16_t per_s)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return false;
    }
    o->channel[ch].slew_per_s = per_s;
    return true;
}

bool outputs_set(outputs_t *o, uint8_t ch, uint16_t command, uint32_t now_ms)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return false;
    }
    /* Clamped, not refused: see the header. */
    if (command > OUT_SPAN) {
        command = OUT_SPAN;
    }
    o->channel[ch].command         = command;
    o->channel[ch].last_command_ms = now_ms;
    return true;
}

bool outputs_keepalive(outputs_t *o, uint8_t ch, uint32_t now_ms)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return false;
    }
    o->channel[ch].last_command_ms = now_ms;
    return true;
}

bool outputs_armed(const outputs_t *o)
{
    return o != NULL && o->armed;
}

void outputs_arm(outputs_t *o, bool armed, uint32_t now_ms)
{
    if (o == NULL || o->armed == armed) {
        return;   /* re-asserting a state is not an event; see the header */
    }
    if (!armed) {
        outputs_all_off(o);
    }
    o->armed = armed;
    /* Arming is activity, so nothing is stale the instant it happens.  A
     * bank armed after a quiet minute must not go to rest on its first step,
     * before anybody has commanded it. */
    for (unsigned i = 0; i < OUT_MAX_CHANNELS; ++i) {
        o->channel[i].last_command_ms = now_ms;
    }
}

bool outputs_overdue(const outputs_t *o, uint8_t ch, uint32_t now_ms)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return true;
    }
    /* Wrap-safe: the difference is compared, never the order.  The deadline
     * is inclusive: a timeout of 500 ms is overdue at 500 ms and not at
     * 499 ms. */
    return (uint32_t)(now_ms - o->channel[ch].last_command_ms)
           >= o->timeout_ms;
}

void outputs_all_off(outputs_t *o)
{
    if (o == NULL) {
        return;
    }
    for (unsigned i = 0; i < OUT_MAX_CHANNELS; ++i) {
        o->channel[i].actual = o->channel[i].rest;
    }
}

void outputs_step(outputs_t *o, uint32_t now_ms)
{
    if (o == NULL) {
        return;
    }
    const uint32_t dt_ms = (uint32_t)(now_ms - o->last_step_ms);
    o->last_step_ms = now_ms;

    if (!o->armed) {
        outputs_all_off(o);
        return;
    }

    for (unsigned i = 0; i < OUT_MAX_CHANNELS; ++i) {
        out_channel_t *c = &o->channel[i];
        /* A channel nobody is commanding goes to rest on its own account,
         * and takes none of the others with it. */
        if (outputs_overdue(o, (uint8_t)i, now_ms)) {
            c->actual = c->rest;
            continue;
        }
        /* Immediate means immediate.  Testing the elapsed time first would
         * make an unslewed channel wait 1 ms before it followed, which is an
         * unstated slew limit. */
        if (c->slew_per_s == 0u) {
            c->actual = c->command;
            continue;
        }
        if (dt_ms == 0u) {
            continue;
        }
        /*
         * Rounded up, so a slew slow enough that a step lands under one unit
         * still moves.  Truncating there gives an output that never arrives.
         */
        const uint32_t step = ((uint32_t)c->slew_per_s * dt_ms + 999u) / 1000u;

        if (c->command > c->actual) {
            const uint32_t next = (uint32_t)c->actual + step;
            c->actual = (next > c->command) ? c->command : (uint16_t)next;
        } else if (c->command < c->actual) {
            /* A throttle coming down is not ramped: reducing throttle is the
             * safe direction.  A surface has no safe direction, so it is
             * ramped both ways. */
            if (c->role == OUT_ROLE_THROTTLE) {
                c->actual = c->command;
            } else {
                const uint32_t back = (uint32_t)c->actual - step;
                c->actual = (c->actual < step || back < c->command)
                            ? c->command : (uint16_t)back;
            }
        }
    }
}

uint16_t outputs_actual(const outputs_t *o, uint8_t ch)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return 0u;
    }
    return o->channel[ch].actual;
}

uint16_t outputs_pulse_us(const outputs_t *o, uint8_t ch)
{
    if (o == NULL || ch >= OUT_MAX_CHANNELS) {
        return 0u;
    }
    const out_channel_t *c = &o->channel[ch];
    const uint32_t span = (uint32_t)(c->max_us - c->min_us);
    return (uint16_t)(c->min_us
                      + (span * (uint32_t)c->actual + OUT_SPAN / 2u) / OUT_SPAN);
}

bool outputs_driving(const outputs_t *o)
{
    return o != NULL && o->armed;
}
