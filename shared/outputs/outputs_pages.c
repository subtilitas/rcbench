/*
 * The link's output pages, expressed as bank operations.  See
 * outputs_pages.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_pages.h"

#include <stddef.h>

#include "link_msg.h"
#include "link_pages.h"
#include "out_pwm_map.h"

/*
 * The wire's driver numbers are a contract, so they are mapped rather than
 * cast: a reordered bank enum leaves this correct.
 */
static out_driver_t driver_of(uint16_t wire)
{
    switch (wire) {
    case LINK_DRIVER_PWM:   return OUT_DRIVER_PWM;
    case LINK_DRIVER_PPM:   return OUT_DRIVER_PPM;
    case LINK_DRIVER_DSHOT: return OUT_DRIVER_DSHOT;
    case LINK_DRIVER_DSHOT_BIDIR: return OUT_DRIVER_DSHOT_BIDIR;
    default:                return OUT_DRIVER_NONE;
    }
}

uint16_t link_driver_of(out_driver_t d)
{
    switch (d) {
    case OUT_DRIVER_PWM:         return (uint16_t)LINK_DRIVER_PWM;
    case OUT_DRIVER_PPM:         return (uint16_t)LINK_DRIVER_PPM;
    case OUT_DRIVER_DSHOT:       return (uint16_t)LINK_DRIVER_DSHOT;
    case OUT_DRIVER_DSHOT_BIDIR: return (uint16_t)LINK_DRIVER_DSHOT_BIDIR;
    case OUT_DRIVER_NONE:
    case OUT_DRIVER_COUNT:
    default:                     return (uint16_t)LINK_DRIVER_NONE;
    }
}

static bool known_driver(uint16_t wire)
{
    return wire <= LINK_DRIVER_DSHOT_BIDIR;
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

uint8_t outputs_chan_cfg_armed_check(const uint16_t *regs,
                                     const uint16_t *next, bool armed)
{
    if (!armed || regs == NULL || next == NULL) {
        return 0u;
    }
    for (unsigned c = 0; c < LINK_OUT_CHANNELS; ++c) {
        const uint16_t *was = &regs[(size_t)c * LINK_CC_STRIDE];
        const uint16_t *now = &next[(size_t)c * LINK_CC_STRIDE];
        if (was[LINK_CC_ROLE] != now[LINK_CC_ROLE]) {
            return LINK_NACK_BAD_VALUE;
        }
        if (was[LINK_CC_ROLE] != LINK_CC_ROLE_THROTTLE) {
            continue;
        }
        for (unsigned f = 0; f < LINK_CC_STRIDE; ++f) {
            if (was[f] != now[f]) {
                return LINK_NACK_BAD_VALUE;
            }
        }
    }
    return 0u;
}

uint8_t outputs_slots_armed_check(const uint16_t *regs, const uint16_t *next,
                                  bool armed)
{
    if (!armed || regs == NULL || next == NULL) {
        return 0u;
    }
    return outputs_slots_changed(regs, next) != 0u ? LINK_NACK_BAD_VALUE : 0u;
}

bool outputs_chan_cfg_set_throttle_range(uint16_t *regs, uint16_t min_us,
                                         uint16_t max_us)
{
    if (regs == NULL || min_us < LINK_CC_FLOOR_US
        || max_us > LINK_CC_CEILING_US || min_us >= max_us) {
        return false;
    }
    for (unsigned c = 0; c < LINK_OUT_CHANNELS; ++c) {
        uint16_t *r = &regs[(size_t)c * LINK_CC_STRIDE];
        if (r[LINK_CC_ROLE] == LINK_CC_ROLE_THROTTLE) {
            r[LINK_CC_MIN_US] = min_us;
            r[LINK_CC_MAX_US] = max_us;
        }
    }
    return true;
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

/* ---------------------------------------------------------------- SERVO */

/* Whether the SERVO page's rate, when not 0, is the one slot @p i runs at. */
static bool servo_rate_reaches(const outputs_t *o, unsigned i)
{
    const out_slot_t *sl = &o->slot[i];
    return sl->driver == OUT_DRIVER_PWM
           && sl->first_channel < OUT_MAX_CHANNELS
           && o->channel[sl->first_channel].role == OUT_ROLE_SURFACE;
}

void outputs_slot_rates(const outputs_t *o, uint16_t servo_hz, uint16_t *rate)
{
    if (o == NULL || rate == NULL) {
        return;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        rate[i] = (servo_hz != 0u && servo_rate_reaches(o, i))
                      ? servo_hz
                      : o->slot[i].rate_hz;
    }
}

uint8_t outputs_servo_rate_check(const outputs_t *o, uint16_t hz)
{
    if (o == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    const out_driver_def_t *pwm = out_driver(OUT_DRIVER_PWM);
    if (hz != 0u && (hz < pwm->rate_min_hz || hz > pwm->rate_max_hz)) {
        return LINK_NACK_BAD_VALUE;
    }
    /*
     * A slice counts against one wrap register, so its two channels run at
     * one rate.  Moving a surface off the rate of the output beside it on
     * its slice would leave the silicon refusing whichever binds second, and
     * the page reading back a rate no pin runs at.  Only pairs the rate
     * moves are asked about: two slots the OUTPUTS page itself put on one
     * slice at two rates are that page's to answer for, and 0 always goes
     * back to them.
     */
    uint16_t rate[OUT_MAX_SLOTS];
    outputs_slot_rates(o, hz, rate);
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        for (unsigned j = i + 1u; j < OUT_MAX_SLOTS; ++j) {
            const out_slot_t *a = &o->slot[i];
            const out_slot_t *b = &o->slot[j];
            if (a->driver != OUT_DRIVER_PWM || b->driver != OUT_DRIVER_PWM
                || !out_pwm_same_slice(a->pin, b->pin)
                || rate[i] == rate[j]) {
                continue;
            }
            if (rate[i] != a->rate_hz || rate[j] != b->rate_hz) {
                return LINK_NACK_BAD_VALUE;
            }
        }
    }
    return 0u;
}

/* The bank a page write would leave, judged on a copy: the bank itself is
 * what drives, and a refused write must leave it as it was.  Static rather
 * than on the stack, which on the coprocessor is the link task's. */
static outputs_t s_trial;

uint8_t outputs_chan_cfg_rate_check(const outputs_t *o,
                                    const uint16_t *chan_cfg,
                                    uint16_t servo_hz)
{
    if (o == NULL || chan_cfg == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    if (servo_hz == 0u) {
        return 0u;
    }
    s_trial = *o;
    outputs_chan_cfg_apply(&s_trial, chan_cfg);
    return outputs_servo_rate_check(&s_trial, servo_hz);
}

uint8_t outputs_slots_rate_check(const outputs_t *o, const uint16_t *slots,
                                 uint16_t servo_hz)
{
    if (o == NULL || slots == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    if (servo_hz == 0u) {
        return 0u;
    }
    s_trial = *o;
    outputs_slots_apply(&s_trial, slots);
    return outputs_servo_rate_check(&s_trial, servo_hz);
}

uint8_t outputs_slots_changed(const uint16_t *prev, const uint16_t *next)
{
    uint8_t mask = 0u;
    if (prev == NULL || next == NULL) {
        return 0xFFu;
    }
    for (unsigned i = 0; i < LINK_OS_COUNT; ++i) {
        if (prev[i] != next[i]) {
            mask |= (uint8_t)(1u << (i / LINK_OS_STRIDE));
        }
    }
    return mask;
}

uint8_t outputs_bind_check(const outputs_t *o, uint8_t watch,
                           uint8_t bound_after)
{
    if (o == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        const uint8_t bit = (uint8_t)(1u << i);
        if (o->slot[i].driver != OUT_DRIVER_NONE
            && (watch & bit) != 0u && (bound_after & bit) == 0u) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    return 0u;
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
    /* Only the driver number is checkable without the bank.  A pin conflict
     * or a rate a driver cannot make is a question for outputs_configure at
     * apply, because it needs the other slots to answer.  An unknown driver
     * is refused here so it never reaches the table lookup. */
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t field = (uint8_t)((off + i) % LINK_OS_STRIDE);
        if (field == LINK_OS_DRIVER && !known_driver(in[i])) {
            return LINK_NACK_BAD_VALUE;
        }
        /* The register is sixteen bits wide and a pin number is eight, so a
         * pin above the top would be stored, truncated at apply, and drive a
         * different pin than the one the page reads back. */
        if (field == LINK_OS_PIN && in[i] > OUT_MAX_PIN) {
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
     * the window written.  outputs_configure checks a slot against the
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
        /* A slot the panel double-books is refused and left cleared.  The
         * read-back shows what was asked, and the bank -- which is what
         * drives -- is not on the wire, so a cleared slot reads back from the
         * page like any other. */
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

void outputs_channels_from_bank(const outputs_t *o, uint16_t *regs)
{
    if (o == NULL || regs == NULL) {
        return;
    }
    for (unsigned c = 0; c < LINK_CH_COUNT; ++c) {
        regs[c] = outputs_command(o, (uint8_t)c);
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

void outputs_channels_apply_n(outputs_t *o, const uint16_t *regs,
                              uint8_t first, uint8_t count, uint32_t now_ms)
{
    if (o == NULL || regs == NULL || (unsigned)first >= LINK_CH_COUNT) {
        return;
    }
    unsigned last = (unsigned)first + count;
    if (last > LINK_CH_COUNT) {
        last = LINK_CH_COUNT;
    }
    for (unsigned c = first; c < last; ++c) {
        (void)outputs_set(o, (uint8_t)c, regs[c], now_ms);
    }
}

void outputs_channels_apply(outputs_t *o, const uint16_t *regs,
                            uint32_t now_ms)
{
    outputs_channels_apply_n(o, regs, 0u, (uint8_t)LINK_CH_COUNT, now_ms);
}

uint8_t outputs_role_channels(const uint16_t *slots, const uint16_t *chan_cfg,
                              out_role_t role)
{
    /* One bit per channel.  The mask is as wide as the page. */
    _Static_assert(LINK_OUT_CHANNELS <= 8u,
                   "a channel mask holds LINK_OUT_CHANNELS bits");
    if (slots == NULL || chan_cfg == NULL) {
        return 0u;
    }
    const uint16_t want = (role == OUT_ROLE_THROTTLE)
                              ? (uint16_t)LINK_CC_ROLE_THROTTLE
                              : (uint16_t)LINK_CC_ROLE_SURFACE;
    uint8_t mask = 0u;
    for (uint8_t sl = 0; sl < LINK_OUT_SLOTS; ++sl) {
        const uint16_t *r = &slots[(size_t)sl * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] == (uint16_t)LINK_DRIVER_NONE) {
            continue;
        }
        const uint8_t first = LINK_OS_FIRST(r[LINK_OS_RANGE]);
        const uint8_t n     = LINK_OS_CHANNELS(r[LINK_OS_RANGE]);
        for (uint8_t c = first; c < first + n && c < LINK_OUT_CHANNELS; ++c) {
            /*
             * Each channel answers for itself.  A slot whose channels carry
             * different roles is not a page this end writes, but it is a page
             * this end can be handed, and reading it by its first channel is
             * how a throttle ends up in a surface's mask.
             */
            const uint16_t got =
                chan_cfg[(size_t)c * LINK_CC_STRIDE + LINK_CC_ROLE];
            if (got == want) {
                mask |= (uint8_t)(1u << c);
            }
        }
    }
    return mask;
}
