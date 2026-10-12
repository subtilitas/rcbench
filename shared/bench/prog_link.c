/*
 * The panel's side of the programming ports' page.  See prog_link.h.
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
    }
}

void prog_link_came_up(prog_link_t *p, uint16_t minor)
{
    if (p == NULL) {
        return;
    }
    p->page      = minor >= LINK_MINOR_PROG;
    p->known     = false;
    p->read_once = false;
}

void prog_link_lost(prog_link_t *p)
{
    if (p == NULL) {
        return;
    }
    p->page      = false;
    p->known     = false;
    p->read_once = false;
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

bool prog_link_command(const prog_link_t *p, bool release)
{
    if (!prog_link_ready(p)) {
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
    memcpy(p->regs, regs, sizeof(p->regs));
    p->known = true;
}

void prog_link_stale(prog_link_t *p)
{
    if (p != NULL) {
        p->known = false;
    }
}

static bool known(const prog_link_t *p)
{
    return p != NULL && p->page && p->known;
}

uint8_t prog_link_wanted(const prog_link_t *p)
{
    if (!known(p) || p->want_target == 0u
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

/* The pulse bits the page is to hold: the wanted port's while that port is
 * the target the page holds, no other. */
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

    if (!known(p) || w == NULL || p->held) {
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
    if (p->regs[LINK_PG_TARGET] != prog_link_wanted(p)) {
        w->off   = (uint8_t)LINK_PG_TARGET;
        w->value = prog_link_wanted(p);
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
    if (!ack || !p->known || w->off >= LINK_PG_REGS) {
        p->known = false;
        return;
    }
    p->regs[w->off] = w->value;
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

void prog_link_cfg(const uint16_t *cc, uint16_t *out)
{
    if (cc == NULL || out == NULL) {
        return;
    }
    out[0] = cc[LINK_CC_SLEW];
    out[1] = cc[LINK_CC_MIN_US];
    out[2] = cc[LINK_CC_MAX_US];
}
