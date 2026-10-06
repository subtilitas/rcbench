/*
 * The SUPPLY link page.  See supply_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_page.h"

#include <stddef.h>
#include <string.h>

#include "link_msg.h"

uint32_t supply_page_baud(uint16_t setting)
{
    static const uint32_t k_baud[SUPPLY_BAUD_COUNT] = {
        9600u, 19200u, 38400u, 57600u, 115200u, 230400u, 460800u,
    };
    return (setting < SUPPLY_BAUD_COUNT) ? k_baud[setting] : 0u;
}

void supply_page_init(supply_page_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
        p->regs[LINK_SP_BAUD] = 1u;   /* 19200, the module's as shipped */
    }
}

bool supply_page_enabled(const supply_page_t *p)
{
    return p != NULL && p->regs[LINK_SP_ENABLE] != 0u;
}

uint8_t supply_page_tx(const supply_page_t *p)
{
    return (p != NULL) ? (uint8_t)p->regs[LINK_SP_TX_PIN] : 0u;
}

uint8_t supply_page_rx(const supply_page_t *p)
{
    return (p != NULL) ? (uint8_t)p->regs[LINK_SP_RX_PIN] : 0u;
}

uint64_t supply_page_pins(const supply_page_t *p)
{
    if (!supply_page_enabled(p)) {
        return 0u;
    }
    return ((uint64_t)1u << p->regs[LINK_SP_TX_PIN])
           | ((uint64_t)1u << p->regs[LINK_SP_RX_PIN]);
}

/* Whether @p pin may carry the supply: in the bank, not reserved unless it
 * is one this page already holds, and no output's. */
static bool pin_free(const supply_page_t *p, const outputs_t *o, uint16_t pin)
{
    if (pin > OUT_MAX_PIN) {
        return false;
    }
    const bool ours = (supply_page_pins(p) & ((uint64_t)1u << pin)) != 0u;
    if (!ours && !outputs_pin_available(o, (uint8_t)pin)) {
        return false;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (o->slot[i].driver != OUT_DRIVER_NONE && o->slot[i].pin == pin) {
            return false;
        }
    }
    return true;
}

uint8_t supply_page_write(supply_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, const outputs_t *o,
                          bool beat_alive)
{
    if (p == NULL || in == NULL || o == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SP_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SP_FLAGS) {
        return LINK_NACK_READ_ONLY;
    }
    uint16_t next[LINK_SP_COUNT];
    memcpy(next, p->regs, sizeof(next));
    for (uint8_t i = 0; i < n; ++i) {
        next[off + i] = in[i];
    }
    const bool wiring = off < (uint8_t)LINK_SP_OUTPUT;
    const bool command = (unsigned)off + (unsigned)n > (unsigned)LINK_SP_OUTPUT;

    if (wiring) {
        bool changed = false;
        for (unsigned r = LINK_SP_ENABLE; r <= LINK_SP_BAUD; ++r) {
            changed = changed || next[r] != p->regs[r];
        }
        /* Not under an output that is or may be on: the module keeps it on
         * whatever happens to the pins, and nothing would be left to switch
         * it off. */
        if (changed && (p->regs[LINK_SP_OUTPUT] != 0u
                        || (p->regs[LINK_SP_FLAGS] & LINK_SP_LIVE) != 0u)) {
            return LINK_NACK_BAD_VALUE;
        }
        if (next[LINK_SP_ENABLE] > 1u
            || next[LINK_SP_BAUD] >= SUPPLY_BAUD_COUNT) {
            return LINK_NACK_BAD_VALUE;
        }
        if (next[LINK_SP_ENABLE] != 0u
            && (next[LINK_SP_TX_PIN] == next[LINK_SP_RX_PIN]
                || !pin_free(p, o, next[LINK_SP_TX_PIN])
                || !pin_free(p, o, next[LINK_SP_RX_PIN]))) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    if (command) {
        if (next[LINK_SP_OUTPUT] > 1u
            || next[LINK_SP_SET_MV] > PDMINI_V_MAX_MV
            || next[LINK_SP_SET_MA] > PDMINI_I_MAX_MA) {
            return LINK_NACK_BAD_VALUE;
        }
        if (next[LINK_SP_OUTPUT] != 0u) {
            /* An ON written comes with its set points, in one frame:
             * never at whatever the page held before. */
            const bool writes_on = off <= (uint8_t)LINK_SP_OUTPUT;
            if (writes_on
                && (unsigned)off + (unsigned)n <= (unsigned)LINK_SP_SET_MA) {
                return LINK_NACK_BAD_VALUE;
            }
            if (next[LINK_SP_ENABLE] == 0u) {
                return LINK_NACK_BAD_VALUE;
            }
            /* No supply comes on that a dead panel could not switch off. */
            if (!beat_alive) {
                return LINK_NACK_NOT_ARMED;
            }
        }
    }
    /* Set points written: up to the module's least, as the driver will take
     * them, so the page reads back what the module is asked.  An OFF alone
     * names none, and leaves the page asking for none. */
    const bool sets = (unsigned)off + (unsigned)n > (unsigned)LINK_SP_SET_MV
                      && off <= (uint8_t)LINK_SP_SET_MA;
    if (sets) {
        if (next[LINK_SP_SET_MV] < PDMINI_V_MIN_MV) {
            next[LINK_SP_SET_MV] = (uint16_t)PDMINI_V_MIN_MV;
        }
        if (next[LINK_SP_SET_MA] < PDMINI_I_MIN_MA) {
            next[LINK_SP_SET_MA] = (uint16_t)PDMINI_I_MIN_MA;
        }
    }
    memcpy(p->regs, next, sizeof(next));
    if (sets) {
        p->commanded = true;
    }
    return 0u;
}

uint8_t supply_page_slots_check(const supply_page_t *p, const uint16_t *slots)
{
    if (slots == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint64_t held = supply_page_pins(p);
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        const uint16_t *r = &slots[(size_t)s * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] != 0u && r[LINK_OS_PIN] <= OUT_MAX_PIN
            && (held & ((uint64_t)1u << r[LINK_OS_PIN])) != 0u) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    return 0u;
}

void supply_page_read(const supply_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SP_COUNT) {
        return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = p->regs[off + i];
    }
}

void supply_page_step(supply_page_t *p, bool beat_alive, pdmini_t *drv)
{
    if (p == NULL) {
        return;
    }
    if (!beat_alive) {
        p->regs[LINK_SP_OUTPUT] = 0u;
    }
    if (drv == NULL || !supply_page_enabled(p)) {
        p->regs[LINK_SP_FLAGS] = 0u;
        return;
    }
    if (p->commanded) {
        pdmini_want(drv, p->regs[LINK_SP_OUTPUT] != 0u,
                    p->regs[LINK_SP_SET_MV], p->regs[LINK_SP_SET_MA]);
    } else {
        /* Wired with no command yet: off, and the module's set points left
         * as they are rather than written at the page's zeros. */
        pdmini_want_off(drv);
    }
    const pdmini_status_t *st = pdmini_status(drv);
    uint16_t flags = 0u;
    if (st->online) { flags |= LINK_SP_ONLINE; }
    if (st->output) { flags |= LINK_SP_ON; }
    if (st->stuck)  { flags |= LINK_SP_STUCK; }
    if (st->set_stuck) { flags |= LINK_SP_SET_STUCK; }
    if (pdmini_may_be_on(drv)) { flags |= LINK_SP_LIVE; }
    flags |= (uint16_t)((st->mode & 3u) << 2);
    p->regs[LINK_SP_FLAGS]     = flags;
    p->regs[LINK_SP_V_MV]      = st->v_mv;
    p->regs[LINK_SP_I_MA]      = st->i_ma;
    p->regs[LINK_SP_SET_MV_RB] = st->set_mv;
    p->regs[LINK_SP_SET_MA_RB] = st->set_ma;
    p->regs[LINK_SP_IN_STATE]  = st->in_state;
    p->regs[LINK_SP_VIN_MV]    = st->vin_mv;
    p->regs[LINK_SP_SAMPLES]   = (uint16_t)st->samples;
    p->regs[LINK_SP_ERRORS]    = (uint16_t)st->errors;
}
