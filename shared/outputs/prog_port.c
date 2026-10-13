/*
 * The programming ports and the PROG link page.  See prog_port.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "prog_port.h"

#include <stddef.h>
#include <string.h>

#include "link_msg.h"
#include "out_pwm_map.h"
#include "outputs_pages.h"

#define REG(port, r) LINK_PG_REG(port, r)

static uint64_t bit64(uint16_t pin)
{
    return (uint64_t)1u << pin;
}

/* Whether port @p port holds the pin its register names. */
static bool holds(const prog_port_t *p, unsigned port)
{
    return port < p->count
           && p->regs[REG(port, LINK_PG_PIN)] != LINK_PG_PIN_NONE
           && (p->blocked & (1u << port)) == 0u;
}

/* Two port pins that cannot both be on the generator. */
static bool ports_clash(uint16_t a, uint16_t b)
{
    return a == b || out_pwm_same_compare((uint8_t)a, (uint8_t)b);
}

/*
 * Whether @p pin may be a port's beside the bank: a pin of the PWM
 * generator, not reserved unless it is one the ports hold now (@p ours),
 * not in @p taken, no slot's, and beside no PWM slot's in the silicon.
 */
static bool pin_ok(const outputs_t *o, uint16_t pin, uint64_t ours,
                   uint64_t taken)
{
    if (pin > OUT_MAX_PIN || out_pwm_slice_of((uint8_t)pin) == OUT_PWM_NONE) {
        return false;
    }
    if ((taken & bit64(pin)) != 0u) {
        return false;
    }
    if ((ours & bit64(pin)) == 0u && !outputs_pin_available(o, (uint8_t)pin)) {
        return false;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        const out_slot_t *s = &o->slot[i];
        if (s->driver == OUT_DRIVER_NONE) {
            continue;
        }
        if (s->pin == pin) {
            return false;
        }
        if (s->driver == OUT_DRIVER_PWM
            && out_pwm_same_slice(s->pin, (uint8_t)pin)) {
            return false;
        }
    }
    return true;
}

static bool us_ok(uint16_t us)
{
    return us >= LINK_CC_FLOOR_US && us <= LINK_CC_CEILING_US;
}

/* The channel of @p port as the page describes it. */
static void apply_cfg(const prog_port_t *p, outputs_t *o, unsigned port)
{
    const uint8_t ch = prog_port_channel((uint8_t)port);

    (void)outputs_set_role(o, ch, OUT_ROLE_SURFACE);
    (void)outputs_set_slew(o, ch, p->regs[REG(port, LINK_PG_SLEW)]);
    (void)outputs_set_endpoints(o, ch, p->regs[REG(port, LINK_PG_MIN_US)],
                                p->regs[REG(port, LINK_PG_MAX_US)]);
}

void prog_port_init(prog_port_t *p, uint8_t count)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->count = (count > LINK_PG_PORTS_MAX) ? (uint8_t)LINK_PG_PORTS_MAX
                                           : count;
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        p->regs[REG(port, LINK_PG_PIN)]    = LINK_PG_PIN_NONE;
        p->regs[REG(port, LINK_PG_CMD)]    = (uint16_t)(OUT_SPAN / 2u);
        p->regs[REG(port, LINK_PG_MIN_US)] = LINK_CC_DEFAULT_MIN;
        p->regs[REG(port, LINK_PG_MAX_US)] = LINK_CC_DEFAULT_MAX;
    }
}

void prog_port_defaults(uint16_t *kept, uint16_t pin0)
{
    if (kept == NULL) {
        return;
    }
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        uint16_t *k = &kept[(size_t)port * PROG_PORT_KEPT];
        k[0] = (port == 0u) ? pin0 : (uint16_t)LINK_PG_PIN_NONE;
        k[1] = 0u;
        k[2] = LINK_CC_DEFAULT_MIN;
        k[3] = LINK_CC_DEFAULT_MAX;
    }
}

void prog_port_restore(prog_port_t *p, const uint16_t *kept, outputs_t *o,
                       uint64_t taken)
{
    if (p == NULL || kept == NULL || o == NULL) {
        return;
    }
    p->bound   = 0u;
    p->blocked = 0u;
    p->regs[LINK_PG_PWM]    = 0u;
    p->regs[LINK_PG_TARGET] = 0u;
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        const uint16_t *k = &kept[(size_t)port * PROG_PORT_KEPT];
        uint16_t pin = k[0];

        if (port >= p->count
            || (pin != LINK_PG_PIN_NONE
                && (pin > OUT_MAX_PIN
                    || out_pwm_slice_of((uint8_t)pin) == OUT_PWM_NONE))) {
            pin = LINK_PG_PIN_NONE;
        }
        p->regs[REG(port, LINK_PG_PIN)]  = pin;
        p->regs[REG(port, LINK_PG_CMD)]  = (uint16_t)(OUT_SPAN / 2u);
        /* A kept slew above the maximum starts as the maximum. */
        p->regs[REG(port, LINK_PG_SLEW)] =
            (k[1] > LINK_CC_SLEW_MAX) ? (uint16_t)LINK_CC_SLEW_MAX : k[1];
        if (us_ok(k[2]) && us_ok(k[3])) {
            p->regs[REG(port, LINK_PG_MIN_US)] = k[2];
            p->regs[REG(port, LINK_PG_MAX_US)] = k[3];
        } else {
            p->regs[REG(port, LINK_PG_MIN_US)] = LINK_CC_DEFAULT_MIN;
            p->regs[REG(port, LINK_PG_MAX_US)] = LINK_CC_DEFAULT_MAX;
        }
        if (pin != LINK_PG_PIN_NONE) {
            bool free = pin_ok(o, pin, 0u, taken);

            for (unsigned q = 0; q < port; ++q) {
                if (holds(p, q)
                    && ports_clash(p->regs[REG(q, LINK_PG_PIN)], pin)) {
                    free = false;
                }
            }
            if (!free) {
                p->blocked |= (uint8_t)(1u << port);
            }
        }
        apply_cfg(p, o, port);
    }
}

void prog_port_kept(const prog_port_t *p, uint16_t *kept)
{
    if (p == NULL || kept == NULL) {
        return;
    }
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        uint16_t *k = &kept[(size_t)port * PROG_PORT_KEPT];
        k[0] = p->regs[REG(port, LINK_PG_PIN)];
        k[1] = p->regs[REG(port, LINK_PG_SLEW)];
        k[2] = p->regs[REG(port, LINK_PG_MIN_US)];
        k[3] = p->regs[REG(port, LINK_PG_MAX_US)];
    }
}

/* The rate port @p port runs at with @p target as TARGET and the SERVO
 * rate @p hz: @p hz for the target port, PROG_PORT_RATE_HZ otherwise. */
static uint16_t port_hz(uint16_t target, uint16_t hz, unsigned port)
{
    return (hz != 0u && target == port + 1u) ? hz : (uint16_t)PROG_PORT_RATE_HZ;
}

/* Whether the window @p off, @p n carries register @p r. */
static bool carries(uint8_t off, uint8_t n, unsigned r)
{
    return r >= off && r < (unsigned)off + n;
}

uint8_t prog_port_write(prog_port_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, outputs_t *o, uint64_t taken,
                        bool locked, uint32_t now_ms)
{
    if (p == NULL || in == NULL || o == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_PG_REGS) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if (off < (uint8_t)LINK_PG_PWM) {
        return LINK_NACK_READ_ONLY;
    }
    uint16_t next[LINK_PG_REGS];
    memcpy(next, p->regs, sizeof(next));
    for (uint8_t i = 0; i < n; ++i) {
        next[off + i] = in[i];
    }
    /* No register of a port the build does not have. */
    if ((unsigned)off + (unsigned)n > REG(p->count, 0)) {
        return LINK_NACK_BAD_VALUE;
    }

    uint8_t moved = 0u;   /* ports whose PIN this frame changes */
    for (unsigned port = 0; port < p->count; ++port) {
        const unsigned r = REG(port, LINK_PG_PIN);
        if (carries(off, n, r)
            && (next[r] != p->regs[r]
                || (p->blocked & (1u << port)) != 0u)) {
            moved |= (uint8_t)(1u << port);
        }
    }
    const uint64_t ours = prog_port_pins(p);
    for (unsigned port = 0; port < p->count; ++port) {
        const uint16_t pin = next[REG(port, LINK_PG_PIN)];

        if (carries(off, n, REG(port, LINK_PG_CMD))
            && next[REG(port, LINK_PG_CMD)] > OUT_SPAN) {
            next[REG(port, LINK_PG_CMD)] = (uint16_t)OUT_SPAN;
        }
        if (carries(off, n, REG(port, LINK_PG_SLEW))
            && next[REG(port, LINK_PG_SLEW)] > LINK_CC_SLEW_MAX) {
            return LINK_NACK_BAD_VALUE;
        }
        if ((carries(off, n, REG(port, LINK_PG_MIN_US))
             && !us_ok(next[REG(port, LINK_PG_MIN_US)]))
            || (carries(off, n, REG(port, LINK_PG_MAX_US))
                && !us_ok(next[REG(port, LINK_PG_MAX_US)]))) {
            return LINK_NACK_BAD_VALUE;
        }
        if ((moved & (1u << port)) == 0u) {
            continue;
        }
        /* Not under a bank that drives, nor under the port's own pulse or
         * the tester's sweep, nor under the wire driver. */
        if (locked || outputs_armed(o)
            || ((p->regs[LINK_PG_PWM] | next[LINK_PG_PWM])
                & (1u << port)) != 0u
            || p->regs[LINK_PG_TARGET] == port + 1u
            || next[LINK_PG_TARGET] == port + 1u) {
            return LINK_NACK_BAD_VALUE;
        }
        if (pin == LINK_PG_PIN_NONE) {
            continue;
        }
        if (!pin_ok(o, pin, ours, taken)) {
            return LINK_NACK_BAD_VALUE;
        }
        for (unsigned q = 0; q < p->count; ++q) {
            const uint16_t other = next[REG(q, LINK_PG_PIN)];
            const bool there = (moved & (1u << q)) != 0u || holds(p, q);

            if (q != port && there && other != LINK_PG_PIN_NONE
                && ports_clash(other, pin)) {
                return LINK_NACK_BAD_VALUE;
            }
        }
    }
    /* The pulse and the tester only on a pin the generator has. */
    const uint8_t usable = (uint8_t)(p->bound & ~moved);
    if ((next[LINK_PG_PWM] & (uint16_t)~usable) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /*
     * The SERVO rate is the target's, and a change of target returns it to
     * 0 (servo_page_target()): each port is judged at the rate the write
     * leaves it at.
     */
    const uint16_t target = p->regs[LINK_PG_TARGET];
    const bool retarget = next[LINK_PG_TARGET] != target;
    const uint16_t hz = retarget ? 0u : p->hz;
    /* A slice counts against one wrap register: no pin moves onto the
     * slice of a port that runs at a rate of its own. */
    if (hz != 0u && hz != PROG_PORT_RATE_HZ && target != 0u
        && target <= p->count) {
        const uint16_t tpin = next[REG(target - 1u, LINK_PG_PIN)];
        for (unsigned port = 0; port < p->count; ++port) {
            const uint16_t pin = next[REG(port, LINK_PG_PIN)];
            if ((moved & (1u << port)) != 0u && port + 1u != target
                && pin != LINK_PG_PIN_NONE && tpin != LINK_PG_PIN_NONE
                && out_pwm_same_slice((uint8_t)pin, (uint8_t)tpin)) {
                return LINK_NACK_BAD_VALUE;
            }
        }
    }
    /* No pulse longer than the frame that carries it: the pin would sit
     * high for the whole frame. */
    for (unsigned port = 0; port < p->count; ++port) {
        const uint16_t longest =
            out_pwm_pulse_max(port_hz(next[LINK_PG_TARGET], hz, port));
        if ((next[LINK_PG_PWM] & (1u << port)) != 0u
            && (next[REG(port, LINK_PG_MIN_US)] > longest
                || next[REG(port, LINK_PG_MAX_US)] > longest)) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    /* A target taken from the standard servo ports while they run at the
     * SERVO rate sends every slot back to its own rate: none may be left
     * with a pulse longer than that frame. */
    if (retarget && target == 0u && p->hz != 0u
        && outputs_pulse_check(o, 0u, 0u, 0u) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    if (next[LINK_PG_TARGET] != 0u
        && (next[LINK_PG_TARGET] > LINK_PG_PORTS_MAX
            || (usable & (1u << (next[LINK_PG_TARGET] - 1u))) == 0u)) {
        return LINK_NACK_BAD_VALUE;
    }

    memcpy(p->regs, next, sizeof(next));
    /* As servo_page_target() returns the SERVO page's rate to 0: the ports
     * are bound at the rates the write leaves before the SERVO page follows
     * the TARGET, and a revert (prog_port_revert()) brings the rate back. */
    p->hz = hz;
    p->blocked &= (uint8_t)~moved;
    p->bound   &= (uint8_t)~moved;   /* until the caller has bound it */
    for (unsigned port = 0; port < p->count; ++port) {
        const uint8_t ch = prog_port_channel((uint8_t)port);

        if (carries(off, n, REG(port, LINK_PG_SLEW))
            || carries(off, n, REG(port, LINK_PG_MIN_US))
            || carries(off, n, REG(port, LINK_PG_MAX_US))) {
            apply_cfg(p, o, port);
        }
        if (carries(off, n, REG(port, LINK_PG_CMD))) {
            (void)outputs_set(o, ch, p->regs[REG(port, LINK_PG_CMD)], now_ms);
        }
    }
    return 0u;
}

void prog_port_read(const prog_port_t *p, uint8_t off, uint8_t n,
                    uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_PG_REGS) {
        return;
    }
    uint16_t flags = p->bound;
    for (unsigned port = 0; port < p->count; ++port) {
        if (p->regs[REG(port, LINK_PG_PIN)] != LINK_PG_PIN_NONE
            && (p->bound & (1u << port)) == 0u) {
            flags |= LINK_PG_F_REFUSED(port);
        }
    }
    for (uint8_t i = 0; i < n; ++i) {
        const unsigned r = (unsigned)off + i;
        if (r == (unsigned)LINK_PG_COUNT) {
            out[i] = p->count;
        } else if (r == (unsigned)LINK_PG_FLAGS) {
            out[i] = flags;
        } else {
            out[i] = p->regs[r];
        }
    }
}

void prog_port_revert(prog_port_t *p, const prog_port_t *was, outputs_t *o)
{
    if (p == NULL || was == NULL || o == NULL) {
        return;
    }
    *p = *was;
    for (unsigned port = 0; port < p->count; ++port) {
        apply_cfg(p, o, port);
    }
}

void prog_port_bound(prog_port_t *p, uint8_t mask)
{
    if (p == NULL) {
        return;
    }
    uint8_t held = 0u;
    for (unsigned port = 0; port < p->count; ++port) {
        if (holds(p, port)) {
            held |= (uint8_t)(1u << port);
        }
    }
    p->bound = (uint8_t)(mask & held);
    p->regs[LINK_PG_PWM] &= p->bound;
    const uint16_t t = p->regs[LINK_PG_TARGET];
    if (t != 0u && (p->bound & (1u << (t - 1u))) == 0u) {
        p->regs[LINK_PG_TARGET] = 0u;
    }
}

uint8_t prog_port_count(const prog_port_t *p)
{
    return (p != NULL) ? p->count : 0u;
}

uint8_t prog_port_pin(const prog_port_t *p, uint8_t port)
{
    if (p == NULL || !holds(p, port)) {
        return PROG_PORT_NO_PIN;
    }
    return (uint8_t)p->regs[REG(port, LINK_PG_PIN)];
}

uint64_t prog_port_pins(const prog_port_t *p)
{
    uint64_t pins = 0u;

    if (p == NULL) {
        return 0u;
    }
    for (unsigned port = 0; port < p->count; ++port) {
        if (holds(p, port)) {
            pins |= bit64(p->regs[REG(port, LINK_PG_PIN)]);
        }
    }
    return pins;
}

uint8_t prog_port_pwm(const prog_port_t *p)
{
    return (p != NULL) ? (uint8_t)p->regs[LINK_PG_PWM] : 0u;
}

uint16_t prog_port_pwm_channels(const prog_port_t *p)
{
    uint16_t ch = 0u;

    if (p == NULL) {
        return 0u;
    }
    for (unsigned port = 0; port < p->count; ++port) {
        if ((p->regs[LINK_PG_PWM] & (1u << port)) != 0u) {
            ch |= (uint16_t)(1u << prog_port_channel((uint8_t)port));
        }
    }
    return ch;
}

void prog_port_rate(prog_port_t *p, uint16_t hz)
{
    if (p != NULL) {
        p->hz = hz;
    }
}

uint16_t prog_port_port_hz(const prog_port_t *p, uint8_t port)
{
    if (p == NULL) {
        return PROG_PORT_RATE_HZ;
    }
    return port_hz(p->regs[LINK_PG_TARGET], p->hz, port);
}

uint16_t prog_port_sliced_channels(const prog_port_t *p)
{
    uint16_t ch = 0u;

    if (p == NULL) {
        return 0u;
    }
    for (unsigned a = 0; a < p->count; ++a) {
        for (unsigned b = 0; b < p->count; ++b) {
            if (a != b && holds(p, a) && holds(p, b)
                && out_pwm_same_slice(
                    (uint8_t)p->regs[REG(a, LINK_PG_PIN)],
                    (uint8_t)p->regs[REG(b, LINK_PG_PIN)])) {
                ch |= (uint16_t)(1u << prog_port_channel((uint8_t)a));
            }
        }
    }
    return ch;
}

uint8_t prog_port_target(const prog_port_t *p)
{
    return (p != NULL) ? (uint8_t)p->regs[LINK_PG_TARGET] : 0u;
}

uint8_t prog_port_channel(uint8_t port)
{
    return (uint8_t)(PROG_PORT_CHANNEL0 + port);
}

/* Whether a bound slot's channels reach a port's bank channel. */
static bool slot_on_port_channel(const uint16_t *r)
{
    const unsigned first = LINK_OS_FIRST(r[LINK_OS_RANGE]);
    const unsigned end   = first + LINK_OS_CHANNELS(r[LINK_OS_RANGE]);

    return r[LINK_OS_DRIVER] != 0u && end > first
           && end > PROG_PORT_CHANNEL0
           && first < PROG_PORT_CHANNEL0 + LINK_PG_PORTS_MAX;
}

unsigned prog_port_slots_free(uint16_t *slots)
{
    unsigned n = 0;

    if (slots == NULL) {
        return 0u;
    }
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        uint16_t *r = &slots[(size_t)s * LINK_OS_STRIDE];

        if (slot_on_port_channel(r)) {
            r[LINK_OS_DRIVER] = 0u;
            ++n;
        }
    }
    return n;
}

uint8_t prog_port_slots_check(const prog_port_t *p, const uint16_t *slots)
{
    if (slots == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    if (p == NULL) {
        return 0u;
    }
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        const uint16_t *r = &slots[(size_t)s * LINK_OS_STRIDE];

        /* The ports' bank channels belong to the ports alone. */
        if (slot_on_port_channel(r)) {
            return LINK_NACK_BAD_VALUE;
        }
        if (r[LINK_OS_DRIVER] == 0u || r[LINK_OS_PIN] > OUT_MAX_PIN) {
            continue;
        }
        for (unsigned port = 0; port < p->count; ++port) {
            const uint16_t pin = p->regs[REG(port, LINK_PG_PIN)];

            if (!holds(p, port)) {
                continue;
            }
            if (r[LINK_OS_PIN] == pin
                || (r[LINK_OS_DRIVER] == (uint16_t)OUT_DRIVER_PWM
                    && out_pwm_same_slice((uint8_t)r[LINK_OS_PIN],
                                          (uint8_t)pin))) {
                return LINK_NACK_BAD_VALUE;
            }
        }
    }
    return 0u;
}

_Static_assert(LINK_PG_PORT0 + LINK_PG_PORTS_MAX * LINK_PG_STRIDE
                   == LINK_PG_REGS,
               "the PROG page's ports do not fill its registers");
_Static_assert(PROG_PORT_CHANNEL0 + LINK_PG_PORTS_MAX <= OUT_MAX_CHANNELS,
               "the bank has no channels for the programming ports");
_Static_assert(PROG_PORT_CHANNEL0 > LINK_OUT_CHANNELS,
               "a programming port's channel is a CHANNELS register");
