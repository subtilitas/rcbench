/**
 * @file
 * @brief The panel's end of the PROG_SUPPLY page.  See prog_supply_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "prog_supply_link.h"

#include <stddef.h>
#include <string.h>

void prog_supply_link_init(prog_supply_link_t *l, bool page)
{
    if (l == NULL) {
        return;
    }
    memset(l, 0, sizeof(*l));
    l->page      = page;
    l->pin       = LINK_PS_NO_PIN;
    l->on_result = PROG_SUPPLY_LINK_NONE;
}

void prog_supply_link_cut(prog_supply_link_t *l)
{
    if (l != NULL && l->page) {
        l->owed    = true;
        l->on_owed = false;      /* an ON not yet written is dropped */
    }
}

bool prog_supply_link_ask_on(prog_supply_link_t *l)
{
    if (l == NULL || !l->page || !l->answered || !l->fitted || l->port
        || l->owed) {
        return false;
    }
    l->on_owed   = true;
    l->on_result = PROG_SUPPLY_LINK_NONE;
    return true;
}

void prog_supply_link_ask_off(prog_supply_link_t *l)
{
    prog_supply_link_cut(l);
}

int prog_supply_link_take_on_result(prog_supply_link_t *l)
{
    if (l == NULL) {
        return PROG_SUPPLY_LINK_NONE;
    }
    const int r = l->on_result;
    l->on_result = PROG_SUPPLY_LINK_NONE;
    return r;
}

bool prog_supply_link_next(const prog_supply_link_t *l, uint8_t *off,
                           uint16_t *v)
{
    if (l == NULL || !l->page) {
        return false;
    }
    /* The OFF first; then the operator's ON; then PANEL, once a read says
     * it is 0 on a board with the switch. */
    const bool declare = l->fitted && l->answered && !l->declared;
    if (!l->owed && !l->on_owed && !declare) {
        return false;
    }
    const bool output = l->owed || l->on_owed;
    if (off != NULL) {
        *off = (uint8_t)(output ? LINK_PS_OUTPUT : LINK_PS_PANEL);
    }
    if (v != NULL) {
        *v = (uint16_t)(l->owed ? 0u : 1u);
    }
    return true;
}

void prog_supply_link_written(prog_supply_link_t *l, int result)
{
    if (l == NULL) {
        return;
    }
    if (!l->owed && l->on_owed) {
        /* The ON, written once whatever became of it. */
        l->on_owed   = false;
        l->on_result = result;
        if (result == PROG_SUPPLY_LINK_ACK) {
            l->on = true;
        }
        return;
    }
    if (result == PROG_SUPPLY_LINK_NO_ANSWER) {
        return;
    }
    if (l->owed) {
        l->owed = false;
    } else {
        /* Answered: done until a read says 0 again. */
        l->declared = true;
    }
}

bool prog_supply_link_read_due(const prog_supply_link_t *l, uint32_t now_ms)
{
    if (l == NULL || !l->page) {
        return false;
    }
    return !l->read_once
           || (uint32_t)(now_ms - l->read_ms) >= PROG_SUPPLY_LINK_READ_MS;
}

void prog_supply_link_read(prog_supply_link_t *l, const uint16_t *regs,
                           uint32_t now_ms)
{
    if (l == NULL) {
        return;
    }
    l->read_once = true;
    l->read_ms   = now_ms;
    l->answered  = regs != NULL;
    if (regs == NULL) {
        return;               /* fitted and pin stay as last read */
    }
    const uint16_t f = regs[LINK_PS_FLAGS];
    l->fitted = (f & LINK_PS_FITTED) != 0u;
    l->on     = l->fitted && (f & LINK_PS_ON) != 0u;
    l->port   = l->fitted && (f & LINK_PS_PORT) != 0u;
    l->pin    = (uint8_t)regs[LINK_PS_PIN];
    l->declared = regs[LINK_PS_PANEL] != 0u;
}

prog_supply_state_t prog_supply_link_state(const prog_supply_link_t *l)
{
    if (l == NULL || !l->page || !l->fitted) {
        return PROG_SUPPLY_NONE;
    }
    if (!l->answered) {
        return PROG_SUPPLY_UNKNOWN;
    }
    return l->on ? PROG_SUPPLY_ON : PROG_SUPPLY_OFF;
}

bool prog_supply_link_port(const prog_supply_link_t *l)
{
    return l != NULL && l->page && l->answered && l->port;
}

bool prog_supply_link_owns(const prog_supply_link_t *l, bool board_has)
{
    return l != NULL && l->page && (l->fitted || board_has);
}

bool prog_supply_link_live(const prog_supply_link_t *l, bool board_has,
                           bool port_taken)
{
    if (!prog_supply_link_owns(l, board_has)) {
        return false;
    }
    return !l->answered || l->on || port_taken;
}
