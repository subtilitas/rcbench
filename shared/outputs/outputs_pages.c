/*
 * The link's pages, expressed as outputs.  See outputs_pages.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_pages.h"

#include <stddef.h>

#include "link_pages.h"

/* What a servo slot runs at until the outputs page can say otherwise. */
#define SERVO_RATE_HZ  50u

void outputs_apply_servo_page(outputs_t *o, const uint16_t *page,
                              uint8_t ch, uint8_t slot, uint8_t pin,
                              uint32_t now_ms)
{
    if (o == NULL || page == NULL) {
        return;
    }
    const uint16_t min_us = page[LINK_SV_MIN_US];
    const uint16_t max_us = page[LINK_SV_MAX_US];

    (void)outputs_set_role(o, ch, OUT_ROLE_SURFACE);
    (void)outputs_set_endpoints(o, ch, min_us, max_us);

    /*
     * A degenerate range is possible on the wire even though the page's own
     * rules straighten inverted pairs -- min and max may be written equal.
     * Everything below divides by the span, so it is answered once here.
     */
    const uint16_t span_us = (max_us > min_us) ? (uint16_t)(max_us - min_us)
                                               : 0u;

    /*
     * The page's slew is microseconds per second, which is this servo's own
     * travel; the bank's is a proportion of it.  Converting means the same
     * number is the same speed whatever the endpoints are, which is what a
     * slew limit is supposed to promise.
     */
    (void)outputs_set_slew(o, ch,
        (span_us == 0u)
            ? 0u
            : (uint16_t)(((uint32_t)page[LINK_SV_SLEW_US] * OUT_SPAN)
                         / span_us));

    /* Below the range is a floor rather than a wrap: the page clamps its own
     * pulse, but nothing here may depend on that having happened first. */
    const uint16_t pulse = page[LINK_SV_PULSE_US];
    uint16_t cmd = (uint16_t)(OUT_SPAN / 2u);
    if (span_us != 0u) {
        cmd = (pulse <= min_us)
              ? 0u
              : (uint16_t)((((uint32_t)pulse - min_us) * OUT_SPAN
                            + span_us / 2u) / span_us);
        if (cmd > OUT_SPAN) {
            cmd = OUT_SPAN;
        }
    }
    (void)outputs_set(o, ch, cmd, now_ms);

    out_slot_t cfg = { .driver = OUT_DRIVER_NONE };
    if (page[LINK_SV_ENABLE] != 0u) {
        cfg = (out_slot_t){ .driver = OUT_DRIVER_PWM, .first_channel = ch,
                            .channels = 1, .pin = pin,
                            .rate_hz = SERVO_RATE_HZ };
    }
    (void)outputs_configure(o, slot, &cfg);
}

void outputs_apply_control_page(outputs_t *o, const uint16_t *page,
                                uint8_t ch, uint32_t now_ms)
{
    if (o == NULL || page == NULL) {
        return;
    }
    (void)outputs_set_role(o, ch, OUT_ROLE_THROTTLE);
    (void)outputs_set(o, ch,
                      (uint16_t)(((uint32_t)page[LINK_CT_THROTTLE] * OUT_SPAN)
                                 / LINK_THROTTLE_MAX),
                      now_ms);
}
