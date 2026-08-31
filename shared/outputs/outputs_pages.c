/*
 * The link's output pages, expressed as bank operations.  See the header.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_pages.h"

#include <stddef.h>

#include "link_msg.h"
#include "link_pages.h"

/*
 * The wire's driver numbers are a contract, so they are mapped through rather
 * than cast: the day the bank's enum is reordered, this stays correct.
 */
static out_driver_t driver_of(uint16_t wire)
{
    switch (wire) {
    case LINK_DRIVER_PWM:   return OUT_DRIVER_PWM;
    case LINK_DRIVER_PPM:   return OUT_DRIVER_PPM;
    case LINK_DRIVER_DSHOT: return OUT_DRIVER_DSHOT;
    default:                return OUT_DRIVER_NONE;
    }
}

static bool known_driver(uint16_t wire)
{
    return wire <= LINK_DRIVER_DSHOT;
}

/* ------------------------------------------------------------ CHAN_CFG */

void outputs_chan_cfg_defaults(uint16_t *regs)
{
    if (regs == NULL) {
        return;
    }
    for (unsigned c = 0; c < LINK_OUT_CHANNELS; ++c) {
        uint16_t *r = &regs[(size_t)c * LINK_CC_STRIDE];
        r[LINK_CC_ROLE]   = LINK_CC_ROLE_SURFACE;
        r[LINK_CC_SLEW]   = 0u;
        r[LINK_CC_MIN_US] = LINK_CC_DEFAULT_MIN;
        r[LINK_CC_MAX_US] = LINK_CC_DEFAULT_MAX;
    }
}

uint8_t outputs_chan_cfg_write(uint16_t *regs, uint8_t off, uint8_t n,
                               const uint16_t *in)
{
    if (regs == NULL || in == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_CC_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    /* Validated before anything is stored, so a rejected write leaves the page
     * as it was.  Half-applying one would leave a channel described by a role
     * from the new write and a range from the old. */
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t field = (uint8_t)((off + i) % LINK_CC_STRIDE);
        const uint16_t v = in[i];
        if (field == LINK_CC_ROLE && v > LINK_CC_ROLE_SURFACE) {
            return LINK_NACK_BAD_VALUE;
        }
        if ((field == LINK_CC_MIN_US || field == LINK_CC_MAX_US)
            && (v < LINK_CC_FLOOR_US || v > LINK_CC_CEILING_US)) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    for (uint8_t i = 0; i < n; ++i) {
        regs[off + i] = in[i];
    }
    return 0u;
}

void outputs_chan_cfg_apply(outputs_t *o, const uint16_t *regs)
{
    if (o == NULL || regs == NULL) {
        return;
    }
    for (unsigned c = 0; c < LINK_OUT_CHANNELS; ++c) {
        const uint16_t *r = &regs[(size_t)c * LINK_CC_STRIDE];
        const out_role_t role = (r[LINK_CC_ROLE] == LINK_CC_ROLE_THROTTLE)
                                    ? OUT_ROLE_THROTTLE : OUT_ROLE_SURFACE;
        (void)outputs_set_role(o, (uint8_t)c, role);
        (void)outputs_set_slew(o, (uint8_t)c, r[LINK_CC_SLEW]);
        /* The bank straightens an inverted pair and refuses one outside the
         * floor and ceiling; the write above already refused the latter, so
         * this only ever straightens. */
        (void)outputs_set_endpoints(o, (uint8_t)c,
                                    r[LINK_CC_MIN_US], r[LINK_CC_MAX_US]);
    }
}

/* -------------------------------------------------------------- OUTPUTS */

void outputs_slots_defaults(uint16_t *regs)
{
    if (regs == NULL) {
        return;
    }
    for (unsigned i = 0; i < LINK_OS_COUNT; ++i) {
        regs[i] = 0u;   /* every field zero: LINK_DRIVER_NONE and no pin */
    }
}

uint8_t outputs_slots_write(uint16_t *regs, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    if (regs == NULL || in == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_OS_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    /* Only the driver number is checkable without the bank -- a pin conflict
     * or a rate a driver cannot make is a question for outputs_configure at
     * apply, because it needs the other slots to answer.  An unknown driver
     * is refused here so it never reaches the table lookup. */
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t field = (uint8_t)((off + i) % LINK_OS_STRIDE);
        if (field == LINK_OS_DRIVER && !known_driver(in[i])) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    for (uint8_t i = 0; i < n; ++i) {
        regs[off + i] = in[i];
    }
    return 0u;
}

void outputs_slots_apply(outputs_t *o, const uint16_t *regs)
{
    if (o == NULL || regs == NULL) {
        return;
    }
    /*
     * Cleared before rebuilt, and rebuilt from the whole page rather than from
     * the window just written.  outputs_configure checks a slot against the
     * others already set, so a slot half-applied against a half-cleared bank
     * would see conflicts that are not there, or miss ones that are.  Clearing
     * first makes the page the single source of truth every time.
     */
    const out_slot_t none = { .driver = OUT_DRIVER_NONE };
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        (void)outputs_configure(o, (uint8_t)s, &none);
    }
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        const uint16_t *r = &regs[(size_t)s * LINK_OS_STRIDE];
        if (driver_of(r[LINK_OS_DRIVER]) == OUT_DRIVER_NONE) {
            continue;
        }
        const out_slot_t cfg = {
            .driver        = driver_of(r[LINK_OS_DRIVER]),
            .first_channel = LINK_OS_FIRST(r[LINK_OS_RANGE]),
            .channels      = LINK_OS_CHANNELS(r[LINK_OS_RANGE]),
            .pin           = (uint8_t)r[LINK_OS_PIN],
            .rate_hz       = r[LINK_OS_RATE_HZ],
        };
        /* A slot the panel double-books is refused and left cleared; the
         * read-back still shows what was asked, and the bank shows what drives
         * -- the disagreement is the panel's to notice. */
        (void)outputs_configure(o, (uint8_t)s, &cfg);
    }
}

/* ------------------------------------------------------------- CHANNELS */

void outputs_channels_defaults(uint16_t *regs)
{
    if (regs == NULL) {
        return;
    }
    for (unsigned c = 0; c < LINK_CH_COUNT; ++c) {
        regs[c] = 0u;
    }
}

uint8_t outputs_channels_write(uint16_t *regs, uint8_t off, uint8_t n,
                               const uint16_t *in)
{
    if (regs == NULL || in == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_CH_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    /* Clamped rather than refused: a command past the span is a host mid-drag,
     * and an output that stops following is worse than one that stops at its
     * stop.  The store reflects the clamp so a read-back does not claim the
     * out-of-range value was kept. */
    for (uint8_t i = 0; i < n; ++i) {
        regs[off + i] = (in[i] > LINK_CH_SPAN) ? (uint16_t)LINK_CH_SPAN : in[i];
    }
    return 0u;
}

void outputs_channels_apply(outputs_t *o, const uint16_t *regs,
                            uint32_t now_ms)
{
    if (o == NULL || regs == NULL) {
        return;
    }
    for (unsigned c = 0; c < LINK_CH_COUNT; ++c) {
        (void)outputs_set(o, (uint8_t)c, regs[c], now_ms);
    }
}
