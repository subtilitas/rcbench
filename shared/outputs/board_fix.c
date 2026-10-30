/**
 * @file
 * @brief A board with soldered pins: each role on its own pin.
 *
 * See board_fix.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "board_fix.h"

#include <stddef.h>
#include <string.h>

#include "link_pages.h"
#include "prog_port.h"

int board_fix_out_index(const outbind_roles_t *r, unsigned gpio)
{
    if (r == NULL) {
        return -1;
    }
    for (unsigned i = 0; i < OUTBIND_FIXED_OUTS; ++i) {
        if (r->out[i] == gpio) {
            return (int)i;
        }
    }
    return -1;
}

bool board_fix_slots_ok(const outbind_roles_t *r, const uint16_t *slots)
{
    if (r == NULL) {
        return true;
    }
    if (slots == NULL) {
        return false;
    }
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        const uint16_t *slot = &slots[(size_t)s * LINK_OS_STRIDE];
        if (slot[LINK_OS_DRIVER] == (uint16_t)LINK_DRIVER_NONE) {
            continue;
        }
        if (board_fix_out_index(r, slot[LINK_OS_PIN]) < 0) {
            return false;
        }
    }
    return true;
}

bool board_fix_map_ok(const outbind_roles_t *r, const uint16_t *map)
{
    if (r == NULL || !r->ina.on) {
        return true;
    }
    if (map == NULL) {
        return false;
    }
    if ((map[LINK_SM_ENABLE] & (1u << r->ina.part)) == 0u) {
        return false;
    }
    for (unsigned k = 0; k < OUTBIND_INA_CHANNELS; ++k) {
        if (map[LINK_SM_WATCH(OUTBIND_INA_CHANNELS * r->ina.part + k)]
            != r->ina.watch[k]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief The pin register @p reg of @p page must hold on this board.
 *
 * @param r     The board's roles.
 * @param page  A link_page_t number.
 * @param reg   Register number on that page.
 * @param want  Receives the value the register must hold.
 * @return      true when @p reg is a register the roles fix, false for one
 *              they leave to the page.
 */
static bool fixed_value(const outbind_roles_t *r, uint8_t page, unsigned reg,
                        uint16_t *want)
{
    switch (page) {
    case LINK_PAGE_SUPPLY:
        if (reg == (unsigned)LINK_SP_TX_PIN) { *want = r->pd_tx; return true; }
        if (reg == (unsigned)LINK_SP_RX_PIN) { *want = r->pd_rx; return true; }
        return false;
    case LINK_PAGE_SENSE:
        if (reg == (unsigned)LINK_SN_SDA_PIN) { *want = r->sda; return true; }
        if (reg == (unsigned)LINK_SN_SCL_PIN) { *want = r->scl; return true; }
        return false;
    case LINK_PAGE_TONE:
        if (reg == (unsigned)LINK_TN_PIN) { *want = r->tap; return true; }
        return false;
    case LINK_PAGE_PROG:
        if (reg >= (unsigned)LINK_PG_FUNC0
            && reg < (unsigned)LINK_PG_FUNC0 + LINK_PG_PORTS_MAX) {
            *want = (uint16_t)LINK_PG_FN_NONE;
            return true;
        }
        if (reg >= (unsigned)LINK_PG_PORT0
            && reg < (unsigned)LINK_PG_PORT0
                         + LINK_PG_PORTS_MAX * (unsigned)LINK_PG_STRIDE
            && (reg - (unsigned)LINK_PG_PORT0) % (unsigned)LINK_PG_STRIDE
                   == (unsigned)LINK_PG_PIN) {
            const unsigned port =
                (reg - (unsigned)LINK_PG_PORT0) / (unsigned)LINK_PG_STRIDE;
            *want = r->prog[port];
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool board_fix_write_ok(const outbind_roles_t *r, uint8_t page, uint8_t off,
                        uint8_t n, const uint16_t *in)
{
    if (r == NULL || n == 0u) {
        return true;
    }
    if (in == NULL) {
        return false;
    }
    for (unsigned i = 0; i < n; ++i) {
        uint16_t want = 0u;
        if (fixed_value(r, page, (unsigned)off + i, &want) && in[i] != want) {
            return false;
        }
    }
    return true;
}

void board_fix_store(const outbind_roles_t *r, out_store_t *cfg)
{
    if (r == NULL || cfg == NULL) {
        return;
    }
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        uint16_t *slot = &cfg->slots[(size_t)s * LINK_OS_STRIDE];
        if (slot[LINK_OS_DRIVER] != (uint16_t)LINK_DRIVER_NONE
            && board_fix_out_index(r, slot[LINK_OS_PIN]) < 0) {
            memset(slot, 0, sizeof(uint16_t) * LINK_OS_STRIDE);
        }
    }
    cfg->supply[LINK_SP_TX_PIN] = r->pd_tx;
    cfg->supply[LINK_SP_RX_PIN] = r->pd_rx;
    cfg->sense[LINK_SN_SDA_PIN] = r->sda;
    cfg->sense[LINK_SN_SCL_PIN] = r->scl;
    cfg->tone[LINK_TN_PIN]      = r->tap;
    for (unsigned port = 0; port < OUTBIND_FIXED_PROGS; ++port) {
        cfg->prog[(size_t)port * PROG_PORT_KEPT] = r->prog[port];
    }
    cfg->prog_func = BOARD_FIX_PROG_FUNC_NONE;
    if (r->ina.on) {
        uint16_t *map = cfg->sense_map;
        const unsigned c0 = OUTBIND_INA_CHANNELS * r->ina.part;
        map[LINK_SM_ENABLE] |= (uint16_t)(1u << r->ina.part);
        bool dark = true;
        for (unsigned k = 0; k < OUTBIND_INA_CHANNELS; ++k) {
            map[LINK_SM_WATCH(c0 + k)] = r->ina.watch[k];
            dark = dark && map[LINK_SM_SHUNT(c0 + k)] == 0u;
        }
        for (unsigned k = 0; dark && k < OUTBIND_INA_CHANNELS; ++k) {
            map[LINK_SM_SHUNT(c0 + k)] = (uint16_t)BOARD_FIX_INA_DMOHM;
        }
        cfg->sense_map_set = 1u;
    }
}
