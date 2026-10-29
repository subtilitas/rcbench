/**
 * @file
 * @brief The PROG_SUPPLY page.  See prog_supply.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "prog_supply.h"

#include <stddef.h>
#include <string.h>

void prog_supply_init(prog_supply_t *p, uint8_t pin, uint16_t mv)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->pin = pin;
    p->mv  = (pin != LINK_PS_NO_PIN) ? mv : 0u;
}

bool prog_supply_fitted(const prog_supply_t *p)
{
    return p != NULL && p->pin != LINK_PS_NO_PIN;
}

uint8_t prog_supply_write(prog_supply_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, bool beat_alive)
{
    if (p == NULL || in == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_PS_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_PS_FLAGS) {
        return LINK_NACK_READ_ONLY;
    }
    const uint16_t v = in[0];
    if (v > 1u) {
        return LINK_NACK_BAD_VALUE;
    }
    if (v == 0u) {
        /* Always taken.  Under a port's hold it stands until the port lets
         * go, and waits for a write that runs. */
        if (p->held) {
            p->sticky = true;
            if (p->writing && p->output && !p->off_held) {
                p->off_held = true;
                p->held_t0  = p->now_ms;
            }
        }
        p->output = false;
        return 0u;
    }
    if (!prog_supply_fitted(p) || p->held) {
        return LINK_NACK_BAD_VALUE;
    }
    if (!beat_alive) {
        return LINK_NACK_NOT_ARMED;
    }
    p->output = true;
    return 0u;
}

void prog_supply_read(const prog_supply_t *p, uint8_t off, uint8_t n,
                      uint16_t *out)
{
    if (out == NULL) {
        return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        const unsigned r = (unsigned)off + i;
        uint16_t v = 0u;
        if (p != NULL) {
            switch (r) {
            case LINK_PS_OUTPUT:
                /* 0 from the panel's OFF on, held for a write or not. */
                v = p->output ? 1u : 0u;
                break;
            case LINK_PS_FLAGS:
                v = (uint16_t)((prog_supply_fitted(p) ? LINK_PS_FITTED : 0u)
                               | (p->drive ? LINK_PS_ON : 0u)
                               | (p->held ? LINK_PS_PORT : 0u)
                               | (p->off_held ? LINK_PS_HELD : 0u));
                break;
            case LINK_PS_PIN:
                v = p->pin;
                break;
            case LINK_PS_MV:
                v = p->mv;
                break;
            default:
                break;
            }
        }
        out[i] = v;
    }
}

bool prog_supply_port_set(prog_supply_t *p, bool on, bool beat_alive)
{
    if (p == NULL) {
        return false;
    }
    if (!on) {
        p->output   = false;
        p->off_held = false;
        return true;
    }
    if (!prog_supply_fitted(p) || p->sticky || !beat_alive) {
        return false;
    }
    p->output = true;
    return true;
}

void prog_supply_port_view(const prog_supply_t *p, supply_port_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!prog_supply_fitted(p)) {
        return;
    }
    out->online   = true;
    out->asked_on = p->output || p->off_held;
    out->on       = out->asked_on && p->drive;
    out->off      = !p->drive;
    out->set_mv   = p->mv;
}

void prog_supply_hold(prog_supply_t *p, bool held)
{
    if (p != NULL && p->held != held) {
        p->held = held;
        if (!held) {
            p->sticky = false;
        }
    }
}

void prog_supply_port_writes(prog_supply_t *p, bool writing)
{
    if (p != NULL) {
        p->writing = writing;
    }
}

void prog_supply_port_frame(prog_supply_t *p, bool frame_out,
                            uint32_t now_ms)
{
    if (p != NULL) {
        p->frame_out = frame_out;
        p->now_ms    = now_ms;
    }
}

bool prog_supply_step(prog_supply_t *p, bool beat_alive)
{
    if (p == NULL) {
        return false;
    }
    if (!beat_alive) {
        p->output = false;
    }
    /* The panel's OFF held for a port's write: until the write ends, the
     * port lets go or the bound passes.  No hold without a heartbeat. */
    if (p->off_held
        && (!beat_alive || !p->writing || !p->held
            || (uint32_t)(p->now_ms - p->held_t0)
                   >= SUPPLY_PAGE_WRITE_WAIT_MS)) {
        p->off_held = false;
    }
    const bool asked = p->output || p->off_held;
    if (asked) {
        p->was_on   = true;
        p->off_wait = false;
    } else if (p->was_on) {
        p->was_on = false;
        if (p->frame_out) {
            p->off_wait = true;
            p->off_t0   = p->now_ms;
        }
    }
    if (p->off_wait
        && (!p->frame_out
            || (uint32_t)(p->now_ms - p->off_t0) >= SUPPLY_PAGE_OFF_WAIT_MS)) {
        p->off_wait = false;
    }
    p->drive = prog_supply_fitted(p) && (asked || p->off_wait);
    return p->drive;
}
