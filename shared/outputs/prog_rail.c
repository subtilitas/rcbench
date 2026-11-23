/**
 * @file
 * @brief Programming on the remote servo node's common rail.
 *
 * See prog_rail.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "prog_rail.h"

#include <stddef.h>
#include <string.h>

#include "link_msg.h"

/**
 * @brief The bit of a port, 0 for none.
 *
 * @param k The port.
 * @return  1 << k for 0 to 8; 0 otherwise.
 */
static uint16_t bit_of(int k)
{
    return (k >= 0 && k < (int)LINK_PW_PORTS) ? (uint16_t)(1u << (unsigned)k)
                                              : 0u;
}

void prog_rail_init(prog_rail_t *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->port = (int8_t)PROG_RAIL_NO_PORT;
}

void prog_rail_port(prog_rail_t *r, int k)
{
    if (r == NULL) {
        return;
    }
    r->port = (bit_of(k) != 0u) ? (int8_t)k : (int8_t)PROG_RAIL_NO_PORT;
}

prog_rail_refusal_t prog_rail_connect(const power_page_t *pg, int k,
                                      const prog_rail_ctx_t *ctx)
{
    if (pg == NULL || ctx == NULL) {
        return PROG_RAIL_PORT;
    }
    if (pg->bec != 0u) {
        return PROG_RAIL_BEC;
    }
    /* Without the switches the session's cycle is the master switch: every
     * other channel must be OFF. */
    if (pg->fitted == 0u
        && (ctx->active & (uint16_t)~bit_of(k) & LINK_PW_PORT_ALL) != 0u) {
        return PROG_RAIL_OTHERS;
    }
    return PROG_RAIL_OK;
}

prog_rail_refusal_t prog_rail_judge(const prog_rail_t *r,
                                    const power_page_t *pg,
                                    const prog_rail_ctx_t *ctx)
{
    if (r == NULL || pg == NULL || ctx == NULL || bit_of(r->port) == 0u) {
        return PROG_RAIL_PORT;
    }
    if (r->sticky) {
        return PROG_RAIL_OFF_STANDS;
    }
    if (!ctx->power.beat_alive || !pg->chip_ok) {
        return PROG_RAIL_UNSAFE;
    }
    if (!ctx->power.limits_held) {
        return PROG_RAIL_LIMITS;
    }
    if ((ctx->power.throttle & bit_of(r->port)) != 0u) {
        return PROG_RAIL_THROTTLE;
    }
    return prog_rail_connect(pg, r->port, ctx);
}

void prog_rail_view(const prog_rail_t *r, const power_page_t *pg,
                    const supply_port_view_t *pd, supply_port_view_t *out)
{
    if (r == NULL || pg == NULL || pd == NULL || out == NULL) {
        return;
    }
    const bool sw = (power_port_on(pg) & bit_of(r->port)) != 0u;
    *out = *pd;
    out->asked_on = pd->asked_on && sw;
    out->on       = pd->on && sw;
    /* A port switched off is discharged by its switch's QOD at once. */
    out->off      = pd->off || !sw;
    out->latched  = pd->latched || r->sticky;
}

/**
 * @brief The PD mini for the session's ON: set off and on again at @p mv
 *        while port k is off, set as asked while it is on, left alone when
 *        it runs at @p mv for other ports as well.
 *
 * @param pg   The POWER page.
 * @param sp   The SUPPLY page.
 * @param bit  The session's port bit.
 * @param mv   The voltage, mV.
 * @param ma   The current limit, mA.
 * @param beat The heartbeat is live.
 * @return     true when the PD mini takes it.
 */
static bool pd_on(const power_page_t *pg, supply_page_t *sp, uint16_t bit,
                  uint16_t mv, uint16_t ma, bool beat)
{
    supply_port_view_t pd;
    supply_page_port_view(sp, &pd);
    const uint16_t on = power_port_on(pg);
    const bool same = pd.asked_on && pd.set_mv == mv;
    const bool shared = (on & (uint16_t)~bit) != 0u && pg->fitted != 0u;
    if (same && shared) {
        return true;            /* the other ports keep their current limit */
    }
    if (!same && pd.asked_on && (on & bit) == 0u) {
        (void)supply_page_port_set(sp, false, 0u, 0u, beat);
    }
    return supply_page_port_set(sp, true, mv, ma, beat);
}

bool prog_rail_set(prog_rail_t *r, power_page_t *pg, supply_page_t *sp,
                   bool on, uint16_t mv, uint16_t ma,
                   const prog_rail_ctx_t *ctx)
{
    if (r == NULL || pg == NULL || sp == NULL || ctx == NULL) {
        return false;
    }
    const uint16_t bit = bit_of(r->port);
    if (!on) {
        power_port_set(pg, (unsigned)r->port, false);   /* nothing for none */
        return true;
    }
    if (prog_rail_judge(r, pg, ctx) != PROG_RAIL_OK) {
        return false;
    }
    supply_port_view_t pd;
    supply_page_port_view(sp, &pd);
    if (pg->fitted != 0u && !(pd.asked_on && pd.set_mv == mv)) {
        /* Another voltage than the rail's: every other port off for the
         * session, before the PD mini moves. */
        r->dropped |= (uint16_t)(power_port_on(pg) & (uint16_t)~bit);
        power_drop(pg, bit);
        r->alone = true;
    }
    if (pg->fitted != 0u && !pg->rail) {
        r->dropped |= (uint16_t)(pg->want & (uint16_t)~bit);
    }
    if (!pd_on(pg, sp, bit, mv, ma, ctx->power.beat_alive)) {
        return false;
    }
    power_port_set(pg, (unsigned)r->port, true);
    return true;
}

uint8_t prog_rail_write(prog_rail_t *r, power_page_t *pg, uint8_t off,
                        uint8_t n, const uint16_t *in,
                        const prog_rail_ctx_t *ctx, uint32_t now)
{
    if (r == NULL || pg == NULL || in == NULL || ctx == NULL) {
        return (uint8_t)LINK_NACK_BAD_RANGE;
    }
    const uint16_t bit = r->held ? bit_of(r->port) : 0u;
    const bool w_rail = n > 0u && off == (uint8_t)LINK_PW_RAIL;
    const bool w_want = (unsigned)off <= (unsigned)LINK_PW_PORT_WANT
                        && (unsigned)off + n > (unsigned)LINK_PW_PORT_WANT;
    const bool rail = w_rail ? in[0] == 1u : pg->rail;
    const uint16_t want = w_want ? in[LINK_PW_PORT_WANT - off] : pg->want;
    if (bit != 0u) {
        const uint16_t now_on = power_port_on(pg);
        const bool k_after = rail && (pg->fitted == 0u || (want & bit) != 0u);
        const bool newly = k_after && (now_on & bit) == 0u;
        const bool more = r->alone
                          && (want & (uint16_t)~pg->want & (uint16_t)~bit)
                                 != 0u;
        if (newly || more) {
            if (w_rail && in[0] == 0u) {
                const uint16_t zero = 0u;
                (void)power_write(pg, (uint8_t)LINK_PW_RAIL, 1u, &zero,
                                  &ctx->power);
            }
            return (uint8_t)LINK_NACK_BUSY;
        }
    }
    const bool had_k = (pg->want & bit) != 0u || (pg->held_on & bit) != 0u;
    const uint8_t nack = power_write(pg, off, n, in, &ctx->power);
    if (bit != 0u && nack == 0u) {
        const bool cut_rail = w_rail && in[0] == 0u;
        const bool cut_k = pg->fitted != 0u && w_want && had_k
                           && (want & bit) == 0u;
        if (cut_rail || cut_k) {
            r->sticky = true;
        }
    }
    if (pg->held_on != 0u && !r->kept) {
        r->kept    = true;
        r->kept_t0 = now;
    }
    return nack;
}

void prog_rail_tell(prog_rail_t *r, power_page_t *pg, bool held,
                    bool writing, uint32_t now)
{
    if (r == NULL || pg == NULL) {
        return;
    }
    if (r->held && !held) {
        r->sticky  = false;     /* the session let go */
        r->alone   = false;
        r->dropped = 0u;
    }
    r->held    = held;
    r->writing = writing;
    uint16_t keep = (held && writing) ? bit_of(r->port) : 0u;
    if (pg->held_on == 0u) {
        r->kept = false;
    } else if (!r->kept) {
        r->kept    = true;
        r->kept_t0 = now;
    }
    if (r->kept && (uint32_t)(now - r->kept_t0) >= SUPPLY_PAGE_WRITE_WAIT_MS) {
        keep = 0u;
    }
    power_keep(pg, keep);
    if (pg->held_on == 0u) {
        r->kept = false;
    }
}

void prog_rail_off(prog_rail_t *r)
{
    if (r == NULL) {
        return;
    }
    if (r->held) {
        r->sticky = true;
    }
    r->kept = false;
}
