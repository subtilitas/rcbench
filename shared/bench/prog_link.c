/**
 * @file
 * @brief The panel's side of the programming ports' page.
 *
 * See prog_link.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "prog_link.h"

#include <stddef.h>
#include <string.h>

void prog_link_init(prog_link_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
        p->seen = PROG_LINK_SEEN_NONE;
    }
}

void prog_link_came_up(prog_link_t *p, uint16_t minor)
{
    if (p == NULL) {
        return;
    }
    p->up          = true;
    p->page        = minor >= LINK_MINOR_PROG;
    p->target_rate = minor >= LINK_MINOR_SERVO_TARGET_RATE;
    p->func        = minor >= LINK_MINOR_PROG_FUNC;
    p->dshot       = minor >= LINK_MINOR_PROG_DSHOT;
    p->hitec       = minor >= LINK_MINOR_HITEC;
    p->hitec_held  = 0u;
    /* An ask is for the IOmcu it was made to: one still waiting is not
     * carried to the one that answers now. */
    if (p->ask_state == (uint8_t)PROG_LINK_ASK_WAITING) {
        p->ask_state = (uint8_t)PROG_LINK_ASK_LOST;
    }
    p->known       = false;
    p->read_once   = false;
    p->seen        = PROG_LINK_SEEN_NONE;
    p->rate        = (uint8_t)PROG_LINK_RATE_KEPT;
}

void prog_link_lost(prog_link_t *p)
{
    if (p == NULL) {
        return;
    }
    p->up          = false;
    p->page        = false;
    p->target_rate = false;
    p->func        = false;
    p->dshot       = false;
    p->hitec       = false;
    p->hitec_held  = 0u;
    if (p->ask_state == (uint8_t)PROG_LINK_ASK_WAITING) {
        p->ask_state = (uint8_t)PROG_LINK_ASK_LOST;
    }
    p->known       = false;
    p->read_once   = false;
    p->seen        = PROG_LINK_SEEN_NONE;
    p->rate        = (uint8_t)PROG_LINK_RATE_KEPT;
}

bool prog_link_hitec_due(const prog_link_t *p)
{
    return p != NULL && p->up && p->hitec;
}

void prog_link_hitec_read(prog_link_t *p, const uint16_t *state)
{
    if (p == NULL || !p->up || !p->hitec || state == NULL) {
        return;
    }
    const uint8_t port = LINK_HT_PORT_OF(*state);
    p->hitec_held = (LINK_HT_STATE_OF(*state) == (uint8_t)LINK_HITEC_FREE
                     || port >= LINK_PG_PORTS_MAX)
                        ? 0u : (uint8_t)(1u << port);
}

uint8_t prog_link_hitec_held(const prog_link_t *p)
{
    return (p != NULL && p->up && p->hitec) ? p->hitec_held : 0u;
}

/**
 * @brief The TARGET now known to be @p t: a change from the one known before
 *        returned the SERVO rate to 0.
 *
 * @param p The link state.
 * @param t The TARGET read or written.
 */
static void target_seen(prog_link_t *p, uint16_t t)
{
    if (!p->target_rate) {
        return;
    }
    if (p->seen != PROG_LINK_SEEN_NONE && t != p->seen
        && p->rate < (uint8_t)PROG_LINK_RATE_RESET) {
        p->rate = (uint8_t)PROG_LINK_RATE_RESET;
    }
    p->seen = (t < PROG_LINK_SEEN_NONE) ? (uint8_t)t : PROG_LINK_SEEN_NONE;
}

prog_link_rate_t prog_link_rate(prog_link_t *p)
{
    if (p == NULL) {
        return PROG_LINK_RATE_KEPT;
    }
    const prog_link_rate_t r = (prog_link_rate_t)p->rate;
    p->rate = (uint8_t)PROG_LINK_RATE_KEPT;
    return r;
}

void prog_link_want(prog_link_t *p, uint8_t target, bool pwm)
{
    if (p == NULL || p->held) {
        return;
    }
    p->want_target = target > LINK_PG_PORTS_MAX ? 0u : target;
    p->want_pwm    = pwm;
}

void prog_link_hold(prog_link_t *p, bool held)
{
    if (p != NULL) {
        p->held = held;
    }
}

bool prog_link_held(const prog_link_t *p)
{
    return p != NULL && p->held;
}

bool prog_link_no_drive(const prog_link_t *p)
{
    return p != NULL && p->page && p->known && p->want_target != 0u
           && prog_link_wanted(p) == 0u;
}

bool prog_link_command(const prog_link_t *p, bool release)
{
    if (!prog_link_ready(p)) {
        return false;
    }
    if (!release && prog_link_no_drive(p)) {
        return false;
    }
    if (prog_link_held(p) && prog_link_target(p) != 0u) {
        return false;
    }
    return release || prog_link_settled(p);
}

bool prog_link_read_due(const prog_link_t *p, uint32_t now_ms)
{
    if (p == NULL || !p->page) {
        return false;
    }
    return !p->known || !p->read_once
           || (uint32_t)(now_ms - p->read_ms) >= PROG_LINK_POLL_MS;
}

void prog_link_read(prog_link_t *p, const uint16_t *regs, uint32_t now_ms)
{
    if (p == NULL || !p->page) {
        return;
    }
    p->read_ms   = now_ms;
    p->read_once = true;
    if (regs == NULL) {
        p->known = false;
        return;
    }
    memcpy(p->regs, regs, (size_t)prog_link_regs(p) * sizeof(uint16_t));
    p->reread = false;
    if (!p->func) {
        /* An older IOmcu runs every port's pin as SERVO PWM. */
        for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
            p->regs[LINK_PG_FUNC(port)] = (uint16_t)LINK_PG_FN_SERVO_PWM;
        }
    }
    p->known = true;
    target_seen(p, regs[LINK_PG_TARGET]);
}

void prog_link_stale(prog_link_t *p)
{
    if (p != NULL) {
        p->known = false;
    }
}

/**
 * @brief Whether the page is known: the IOmcu has it and it is read.
 *
 * @param p The link state.
 * @return true when the page is known.
 */
static bool known(const prog_link_t *p)
{
    return p != NULL && p->page && p->known;
}

/**
 * @brief Whether an ask waits on port @p port.
 *
 * @param p    The link state.
 * @param port The port.
 * @return true when an ask waits on the port.
 */
static bool asking(const prog_link_t *p, unsigned port)
{
    return p->ask_state == (uint8_t)PROG_LINK_ASK_WAITING
           && p->ask_port == port;
}

uint8_t prog_link_regs(const prog_link_t *p)
{
    return (p != NULL && p->func) ? (uint8_t)LINK_PG_REGS
                                  : (uint8_t)LINK_PG_REGS_V424;
}

bool prog_link_has_func(const prog_link_t *p)
{
    return p != NULL && p->page && p->func;
}

bool prog_link_has_dshot(const prog_link_t *p)
{
    return prog_link_has_func(p) && p->dshot;
}

bool prog_link_ask(prog_link_t *p, uint8_t port, prog_link_ask_kind_t kind,
                   uint16_t value)
{
    if (p == NULL || !p->page || port >= LINK_PG_PORTS_MAX
        || (p->known && port >= p->regs[LINK_PG_COUNT])) {
        return false;
    }
    if (kind == PROG_LINK_ASK_FUNC
        && (!p->func
            || value >= (p->dshot ? (uint16_t)LINK_PG_FN_COUNT
                                  : (uint16_t)LINK_PG_FN_COUNT_V426))) {
        return false;
    }
    if (kind != PROG_LINK_ASK_PIN && kind != PROG_LINK_ASK_FUNC) {
        return false;
    }
    p->ask_state = (uint8_t)PROG_LINK_ASK_WAITING;
    p->ask_port  = port;
    p->ask_kind  = (uint8_t)kind;
    p->ask_value = value;
    return true;
}

prog_link_ask_state_t prog_link_ask_state(const prog_link_t *p,
                                          uint8_t *port, uint8_t *kind,
                                          uint16_t *value)
{
    if (p == NULL) {
        return PROG_LINK_ASK_IDLE;
    }
    if (port != NULL) {
        *port = p->ask_port;
    }
    if (kind != NULL) {
        *kind = p->ask_kind;
    }
    if (value != NULL) {
        *value = p->ask_value;
    }
    return (prog_link_ask_state_t)p->ask_state;
}

/**
 * @brief The register the ask waiting writes.
 *
 * @param p The link state.
 * @return The register number.
 */
static uint8_t ask_reg(const prog_link_t *p)
{
    return (p->ask_kind == (uint8_t)PROG_LINK_ASK_FUNC)
               ? LINK_PG_FUNC(p->ask_port)
               : LINK_PG_REG(p->ask_port, LINK_PG_PIN);
}

uint8_t prog_link_wanted(const prog_link_t *p)
{
    if (!known(p) || p->want_target == 0u
        || asking(p, p->want_target - 1u)
        || p->want_target > LINK_PG_PORTS_MAX
        || (uint16_t)(p->want_target - 1u) >= p->regs[LINK_PG_COUNT]
        || (p->regs[LINK_PG_FLAGS]
            & LINK_PG_F_BOUND(p->want_target - 1u)) == 0u) {
        return 0u;
    }
    return p->want_target;
}

bool prog_link_ready(const prog_link_t *p)
{
    return p == NULL || !p->page || p->known;
}

uint8_t prog_link_target(const prog_link_t *p)
{
    if (!known(p) || p->regs[LINK_PG_TARGET] > LINK_PG_PORTS_MAX) {
        return 0u;
    }
    return (uint8_t)p->regs[LINK_PG_TARGET];
}

bool prog_link_pending(const prog_link_t *p)
{
    return known(p) && !p->held
           && p->regs[LINK_PG_TARGET] != prog_link_wanted(p);
}

bool prog_link_settled(const prog_link_t *p)
{
    if (p == NULL || !p->page) {
        return true;
    }
    return p->known && p->regs[LINK_PG_TARGET] == prog_link_wanted(p);
}

bool prog_link_pwm(const prog_link_t *p)
{
    const uint8_t t = prog_link_target(p);

    return t != 0u
           && (p->regs[LINK_PG_PWM] & (uint16_t)(1u << (t - 1u))) != 0u;
}

/**
 * @brief The pulse bits the page is to hold: the wanted port's while that port
 *        is the target the page holds, no other.
 *
 * @param p The link state.
 * @return The pulse bits: 1 << (target - 1) for the wanted port, else 0.
 */
static uint16_t pwm_wanted(const prog_link_t *p)
{
    const uint8_t t = prog_link_wanted(p);

    if (t == 0u || !p->want_pwm || p->regs[LINK_PG_TARGET] != t) {
        return 0u;
    }
    return (uint16_t)(1u << (t - 1u));
}

bool prog_link_next(const prog_link_t *p, bool quiet, prog_link_write_t *w)
{
    uint16_t pwm;

    if (!known(p) || w == NULL || p->held || p->reread) {
        return false;
    }
    pwm = pwm_wanted(p);
    /* Off first: a bit that is not wanted, whatever the bench does. */
    if ((p->regs[LINK_PG_PWM] & (uint16_t)~pwm) != 0u) {
        w->off   = (uint8_t)LINK_PG_PWM;
        w->value = (uint16_t)(p->regs[LINK_PG_PWM] & pwm);
        return true;
    }
    if (!quiet) {
        return false;
    }
    /* A port an ask waits on is let go first: its pulse is off above, and
     * no port is the target while its pin or function changes. */
    const bool ask = p->ask_state == (uint8_t)PROG_LINK_ASK_WAITING;
    if (p->regs[LINK_PG_TARGET] != prog_link_wanted(p)) {
        /* prog_link_wanted() is 0 for a port an ask waits on. */
        w->off   = (uint8_t)LINK_PG_TARGET;
        w->value = prog_link_wanted(p);
        return true;
    }
    if (ask && (p->regs[LINK_PG_PWM] & (1u << p->ask_port)) == 0u) {
        w->off   = ask_reg(p);
        w->value = p->ask_value;
        return true;
    }
    if (p->regs[LINK_PG_PWM] != pwm) {
        w->off   = (uint8_t)LINK_PG_PWM;
        w->value = pwm;
        return true;
    }
    return false;
}

uint8_t prog_link_pulse_on(const prog_link_t *p, const prog_link_write_t *w)
{
    if (!known(p) || w == NULL || w->off != (uint8_t)LINK_PG_PWM) {
        return 0u;
    }
    const uint16_t on = (uint16_t)(w->value & ~p->regs[LINK_PG_PWM]);
    for (uint8_t port = 0u; port < LINK_PG_PORTS_MAX; ++port) {
        if ((on & (1u << port)) != 0u) {
            return (uint8_t)(port + 1u);
        }
    }
    return 0u;
}

void prog_link_written(prog_link_t *p, const prog_link_write_t *w, bool ack)
{
    if (p == NULL || w == NULL || !p->page) {
        return;
    }
    if (w->off == (uint8_t)LINK_PG_TARGET && p->target_rate) {
        if (ack) {
            target_seen(p, w->value);
        } else {
            /* Changed or not, the rate it leaves is not known. */
            p->rate = (uint8_t)PROG_LINK_RATE_UNKNOWN;
            p->seen = PROG_LINK_SEEN_NONE;
        }
    }
    const bool was_ask = p->ask_state == (uint8_t)PROG_LINK_ASK_WAITING
                         && w->off == ask_reg(p) && w->value == p->ask_value;
    if (was_ask) {
        p->ask_state = ack ? (uint8_t)PROG_LINK_ASK_DONE
                           : (uint8_t)PROG_LINK_ASK_REFUSED;
    }
    if (!ack || !p->known || w->off >= prog_link_regs(p)) {
        p->known = false;
        return;
    }
    p->regs[w->off] = w->value;
    if (was_ask) {
        /* FLAGS moves with a PIN or FUNC: the page is read again now, and
         * nothing more is written on what it said before. */
        p->read_once = false;
        p->reread    = true;
    }
}

uint8_t prog_link_count(const prog_link_t *p)
{
    if (p == NULL || !p->up) {
        return PROG_LINK_COUNT_UNKNOWN;
    }
    if (!p->page) {
        return 0u;
    }
    if (!p->known) {
        return PROG_LINK_COUNT_UNKNOWN;
    }
    const uint16_t n = p->regs[LINK_PG_COUNT];
    return (n > LINK_PG_PORTS_MAX) ? (uint8_t)LINK_PG_PORTS_MAX : (uint8_t)n;
}

uint8_t prog_link_pin(const prog_link_t *p, uint8_t port)
{
    uint16_t pin;

    if (!known(p) || port >= LINK_PG_PORTS_MAX
        || port >= p->regs[LINK_PG_COUNT]
        || (p->regs[LINK_PG_FLAGS] & LINK_PG_F_REFUSED(port)) != 0u) {
        return PROG_LINK_NO_PIN;
    }
    pin = p->regs[LINK_PG_REG(port, LINK_PG_PIN)];
    return pin >= PROG_LINK_NO_PIN ? PROG_LINK_NO_PIN : (uint8_t)pin;
}

bool prog_link_port(const prog_link_t *p, uint8_t port, prog_link_port_t *out)
{
    if (!known(p) || out == NULL || port >= LINK_PG_PORTS_MAX
        || port >= p->regs[LINK_PG_COUNT]) {
        return false;
    }
    const uint16_t pin = p->regs[LINK_PG_REG(port, LINK_PG_PIN)];
    const uint16_t fn  = p->regs[LINK_PG_FUNC(port)];
    out->pin     = (pin >= PROG_LINK_NO_PIN) ? PROG_LINK_NO_PIN : (uint8_t)pin;
    const uint16_t fns = p->dshot ? (uint16_t)LINK_PG_FN_COUNT
                                  : (uint16_t)LINK_PG_FN_COUNT_V426;
    out->func    = (fn < fns) ? (uint8_t)fn : (uint8_t)LINK_PG_FN_NONE;
    out->refused = (p->regs[LINK_PG_FLAGS] & LINK_PG_F_REFUSED(port)) != 0u;
    return true;
}

prog_link_drive_t prog_link_drive(const prog_link_t *p)
{
    prog_link_drive_t d = { 0u, PROG_LINK_NO_PIN, false };

    if (p == NULL) {
        return d;
    }
    if (!p->up) {
        d.target = PROG_LINK_TARGET_NO_LINK;
    } else if (!prog_link_ready(p)) {
        /* The gate prog_link_command() takes first. */
        d.target = PROG_LINK_TARGET_UNKNOWN;
    } else {
        d.target = prog_link_target(p);
        if (d.target == 0u && prog_link_no_drive(p)) {
            /* The port the screen wants, which cannot be driven: named,
             * with no pin, and nothing goes to the standard outputs. */
            d.target = p->want_target;
            d.pwm    = p->want_pwm;
        } else if (d.target != 0u) {
            d.pin = prog_link_pin(p, (uint8_t)(d.target - 1u));
            d.pwm = prog_link_pwm(p);
        }
    }
    return d;
}

void prog_link_cfg(const uint16_t *cc, uint16_t *out)
{
    if (cc == NULL || out == NULL) {
        return;
    }
    out[0] = cc[LINK_CC_SLEW];
    out[1] = cc[LINK_CC_MIN_US];
    out[2] = cc[LINK_CC_MAX_US];
}
