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
        if (r->out[i] != OUTBIND_NO_GPIO && r->out[i] == gpio) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief The INA3221 parts the board solders: 1 when the roles say 0.
 *
 * @param r  The board's roles, its INA3221s on.
 * @return   1 to OUTBIND_INA_PARTS.
 */
static unsigned ina_parts(const outbind_roles_t *r)
{
    const unsigned n = (r->ina.parts == 0u) ? 1u : (unsigned)r->ina.parts;
    return (n > OUTBIND_INA_PARTS) ? OUTBIND_INA_PARTS : n;
}

bool board_fix_slots_ok(const outbind_roles_t *r, const uint16_t *slots)
{
    if (r == NULL) {
        return true;
    }
    if (slots == NULL) {
        return false;
    }
    for (unsigned s = 0; s < LINK_OUT_SLOTS_ALL; ++s) {
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
    const unsigned parts = ina_parts(r);
    for (unsigned a = 0; a < parts; ++a) {
        if ((map[LINK_SM_ENABLE] & (1u << (r->ina.part + a))) == 0u) {
            return false;
        }
    }
    const unsigned c0 = OUTBIND_INA_CHANNELS * r->ina.part;
    for (unsigned k = 0; k < OUTBIND_INA_CHANNELS * parts; ++k) {
        if (map[LINK_SM_WATCH(c0 + k)] != r->ina.watch[k]) {
            return false;
        }
        if (r->ina.shunt_dmohm != 0u
            && map[LINK_SM_SHUNT(c0 + k)] != r->ina.shunt_dmohm) {
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
        if (r->prog_on_outs) {
            return false;   /* any servo output: prog_pin_ok() judges it */
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

/**
 * @brief Whether a register of the PROG page, as written, is one the board
 *        takes: a port's PIN on a board whose ports take any servo output,
 *        and every port's FUNC.
 *
 * @param r    The board's roles.
 * @param reg  Register number on the PROG page.
 * @param v    The value written.
 * @return     false for a PIN that is no servo output and not
 *             LINK_PG_PIN_NONE, and for a FUNC the board does not take;
 *             true otherwise.
 */
static bool prog_reg_ok(const outbind_roles_t *r, unsigned reg, uint16_t v)
{
    const unsigned ports_end = (unsigned)LINK_PG_PORT0
                               + LINK_PG_PORTS_MAX * (unsigned)LINK_PG_STRIDE;
    if (r->prog_on_outs && reg >= (unsigned)LINK_PG_PORT0 && reg < ports_end
        && (reg - (unsigned)LINK_PG_PORT0) % (unsigned)LINK_PG_STRIDE
               == (unsigned)LINK_PG_PIN) {
        return v == (uint16_t)LINK_PG_PIN_NONE
               || board_fix_out_index(r, v) >= 0;
    }
    /* FUNC: NONE, or SERVO PWM for the tester's pulse and the limit search
     * (protocol 4.36); a DShot rate only where the board takes one. */
    if (reg >= (unsigned)LINK_PG_FUNC0
        && reg < (unsigned)LINK_PG_FUNC0 + LINK_PG_PORTS_MAX) {
        return v == (uint16_t)LINK_PG_FN_NONE
               || v == (uint16_t)LINK_PG_FN_SERVO_PWM
               || (r->prog_dshot && LINK_PG_FN_IS_DSHOT(v));
    }
    return true;
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
    /* No phase tap fitted: the TONE page takes no write. */
    if (page == LINK_PAGE_TONE && r->tap == OUTBIND_NO_GPIO) {
        return false;
    }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned reg = (unsigned)off + i;
        uint16_t want = 0u;
        if (fixed_value(r, page, reg, &want) && in[i] != want) {
            return false;
        }
        if (page == LINK_PAGE_PROG && !prog_reg_ok(r, reg, in[i])) {
            return false;
        }
        if (page == LINK_PAGE_SUPPLY && reg == (unsigned)LINK_SP_SET_MV
            && r->supply_max_mv != 0u && in[i] > r->supply_max_mv) {
            return false;
        }
        if (page == LINK_PAGE_SENSE && reg == (unsigned)LINK_SN_ENABLE
            && r->no_encoder && (in[i] & LINK_SN_EN_AS5600) != 0u) {
            return false;
        }
    }
    return true;
}

void board_fix_crit(const outbind_roles_t *r, const uint16_t *limit_ma,
                    uint16_t *crit_ma)
{
    if (crit_ma == NULL) {
        return;
    }
    memset(crit_ma, 0, sizeof(uint16_t) * LINK_SM_CHANNELS);
    if (r == NULL || !r->ina.on || r->ina.crit_ma == 0u) {
        return;
    }
    const unsigned c0 = OUTBIND_INA_CHANNELS * r->ina.part;
    for (unsigned k = 0; k < OUTBIND_INA_CHANNELS * ina_parts(r)
                         && c0 + k < LINK_SM_CHANNELS; ++k) {
        const uint16_t lim = (limit_ma != NULL) ? limit_ma[c0 + k] : 0u;
        crit_ma[c0 + k] = (lim != 0u && lim < r->ina.crit_ma)
                              ? lim : r->ina.crit_ma;
    }
}

void board_fix_store(const outbind_roles_t *r, out_store_t *cfg)
{
    if (r == NULL || cfg == NULL) {
        return;
    }
    for (unsigned s = 0; s < LINK_OUT_SLOTS_ALL; ++s) {
        uint16_t *slot = (s < LINK_OUT_SLOTS)
                             ? &cfg->slots[(size_t)s * LINK_OS_STRIDE]
                             : &cfg->slots_hi[(size_t)(s - LINK_OUT_SLOTS)
                                              * LINK_OS_STRIDE];
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
    if (r->tap == OUTBIND_NO_GPIO) {
        cfg->tone[LINK_TN_ENABLE] = 0u;     /* no tap fitted */
    }
    if (r->no_encoder) {
        cfg->sense[LINK_SN_ENABLE] &= (uint16_t)~LINK_SN_EN_AS5600;
    }
    for (unsigned port = 0; port < OUTBIND_FIXED_PROGS; ++port) {
        cfg->prog[(size_t)port * PROG_PORT_KEPT] =
            r->prog_on_outs ? (uint16_t)LINK_PG_PIN_NONE : r->prog[port];
    }
    cfg->prog_func = BOARD_FIX_PROG_FUNC_NONE;
    if (r->ina.on) {
        uint16_t *map = cfg->sense_map;
        const unsigned c0 = OUTBIND_INA_CHANNELS * r->ina.part;
        const unsigned parts = ina_parts(r);
        bool dark = true;
        for (unsigned a = 0; a < parts; ++a) {
            map[LINK_SM_ENABLE] |= (uint16_t)(1u << (r->ina.part + a));
        }
        for (unsigned k = 0; k < OUTBIND_INA_CHANNELS * parts; ++k) {
            map[LINK_SM_WATCH(c0 + k)] = r->ina.watch[k];
            dark = dark && map[LINK_SM_SHUNT(c0 + k)] == 0u;
            if (r->ina.shunt_dmohm != 0u) {
                map[LINK_SM_SHUNT(c0 + k)] = r->ina.shunt_dmohm;
            }
        }
        for (unsigned k = 0; dark && r->ina.shunt_dmohm == 0u
                             && k < OUTBIND_INA_CHANNELS * parts; ++k) {
            map[LINK_SM_SHUNT(c0 + k)] = (uint16_t)BOARD_FIX_INA_DMOHM;
        }
        /* A record without a limit on any of the board's channels starts
         * each at the board's own. */
        bool unset = r->ina.limit_ma != 0u;
        for (unsigned k = 0; unset && k < OUTBIND_INA_CHANNELS * parts; ++k) {
            unset = cfg->sense_limit[c0 + k] == 0u;
        }
        for (unsigned k = 0; unset && k < OUTBIND_INA_CHANNELS * parts; ++k) {
            cfg->sense_limit[c0 + k] = r->ina.limit_ma;
        }
        cfg->sense_map_set = 1u;
    }
}
