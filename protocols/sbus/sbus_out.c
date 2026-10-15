/*
 * SPDX-License-Identifier: MIT
 */

#include "sbus_out.h"

#include <string.h>

/* The raw value of 1500 us. */
#define RAW_CENTRE 992u

void sbus_out_init(sbus_out_t *o, const sbus_driver_t *drv)
{
    if (o == NULL) {
        return;
    }
    memset(o, 0, sizeof(*o));
    if (drv != NULL) {
        o->drv = *drv;
    }
    for (unsigned ch = 0; ch < SBUS_CHANNELS; ++ch) {
        o->frame.channel[ch] = RAW_CENTRE;
    }
    o->period_us = SBUS_PERIOD_US;
    o->mode = SBUS_MODE_SBUS;
    sbus2_timing_default(&o->timing);
}

sbus_out_err_t sbus_out_config(sbus_out_t *o, unsigned mode,
                               uint32_t period_us)
{
    if (o == NULL || mode > SBUS_MODE_SBUS2) {
        return SBUS_OUT_ERR_ARG;
    }
    const uint32_t min = mode == SBUS_MODE_SBUS2 ? SBUS2_PERIOD_MIN_US
                                                 : SBUS_PERIOD_FAST_US;

    if (period_us < min || period_us > SBUS_PERIOD_MAX_US) {
        return SBUS_OUT_ERR_PERIOD;
    }
    o->mode = (uint8_t)mode;
    o->period_us = period_us;
    return SBUS_OUT_OK;
}

bool sbus_out_set_raw(sbus_out_t *o, unsigned ch, uint16_t raw)
{
    if (o == NULL || ch >= SBUS_CHANNELS) {
        return false;
    }
    o->frame.channel[ch] = raw > SBUS_RAW_MAX ? (uint16_t)SBUS_RAW_MAX : raw;
    return true;
}

bool sbus_out_set_us(sbus_out_t *o, unsigned ch, uint16_t us)
{
    return sbus_out_set_raw(o, ch, sbus_from_us(us));
}

void sbus_out_set_flags(sbus_out_t *o, bool ch17, bool ch18, bool frame_lost,
                        bool failsafe)
{
    if (o == NULL) {
        return;
    }
    o->frame.ch17 = ch17;
    o->frame.ch18 = ch18;
    o->frame.frame_lost = frame_lost;
    o->frame.failsafe = failsafe;
}

void sbus_out_start(sbus_out_t *o)
{
    if (o == NULL || o->drv.now_us == NULL) {
        return;
    }
    o->running = true;
    o->group = 0;
    o->due_us = o->drv.now_us(o->drv.ctx);
}

void sbus_out_stop(sbus_out_t *o)
{
    if (o != NULL) {
        o->running = false;
    }
}

void sbus_out_clear_slots(sbus_out_t *o)
{
    if (o == NULL) {
        return;
    }
    sbus2_table_reset(&o->table);
    memset(&o->window, 0, sizeof(o->window));
}

/* The transaction in flight: ended, and for S.BUS2 its window read. */
static void finish(sbus_out_t *o)
{
    if (o->drv.poll == NULL || !o->drv.poll(o->drv.ctx, &o->cap)) {
        return;
    }
    o->busy = false;
    if (sbus2_footer_group(o->busy_footer) >= 0
        && sbus2_window_decode(&o->timing, o->busy_footer, &o->cap,
                               &o->window)) {
        sbus2_table_update(&o->table, &o->window);
    }
}

void sbus_out_step(sbus_out_t *o)
{
    if (o == NULL || o->drv.now_us == NULL || o->drv.start == NULL) {
        return;
    }
    if (o->busy) {
        finish(o);
    }
    if (!o->running || o->busy) {
        return;
    }
    const uint32_t now = o->drv.now_us(o->drv.ctx);

    if ((int32_t)(now - o->due_us) < 0) {
        return;
    }
    const bool sbus2 = o->mode == SBUS_MODE_SBUS2;
    const uint8_t footer = sbus2 ? sbus2_footer(o->group) : (uint8_t)SBUS_FOOTER;
    uint8_t bytes[SBUS_FRAME_BYTES];

    sbus_encode(&o->frame, footer, bytes);
    if (o->drv.start(o->drv.ctx, bytes, sbus2 ? SBUS2_WINDOW_US : 0u)) {
        o->busy = true;
        o->busy_footer = footer;
        ++o->frames;
        if (sbus2) {
            o->group = (uint8_t)((o->group + 1u) % SBUS2_GROUPS);
        }
    } else {
        ++o->refused;
    }
    /* The next frame a period after this one was due, so a late step does
     * not move the ones after it.  A step a whole period late restarts the
     * schedule from now rather than sending the missed frames back to
     * back. */
    o->due_us += o->period_us;
    if ((int32_t)(now - o->due_us) >= 0) {
        ++o->late;
        o->due_us = now + o->period_us;
    }
}
