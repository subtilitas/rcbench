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

uint8_t prog_rail_connect_mask(const power_page_t *pg, const int *k_of,
                               uint8_t ports, uint8_t held,
                               const prog_rail_ctx_t *ctx)
{
    if (ports > 8u) {
        ports = 8u;
    }
    const uint8_t all = (uint8_t)((1u << ports) - 1u);
    if (pg == NULL || k_of == NULL || ctx == NULL) {
        return all;
    }
    if ((held & all) != 0u) {
        return all;                     /* one session at a time */
    }
    uint8_t m = 0u;
    for (uint8_t q = 0u; q < ports; ++q) {
        if (k_of[q] != PROG_RAIL_NO_PORT
            && prog_rail_connect(pg, k_of[q], ctx) != PROG_RAIL_OK) {
            m |= (uint8_t)(1u << q);
        }
    }
    return m;
}

bool prog_rail_open_ok(const power_page_t *pg, int k, uint8_t held,
                       const prog_rail_ctx_t *ctx)
{
    return held == 0u && bit_of(k) != 0u
           && prog_rail_connect(pg, k, ctx) == PROG_RAIL_OK;
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
    if (pg->tripped != 0u) {
        return PROG_RAIL_TRIPPED;       /* until the panel's RAIL 1 */
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
    /* A pending ON is asked on and neither reads on nor off, as a PD mini
     * that has not followed yet. */
    out->asked_on = pd->asked_on && (sw || r->pending);
    out->on       = pd->on && sw;
    /* A port switched off is discharged by its switch's QOD at once. */
    out->off      = (pd->off || !sw) && !r->pending;
    out->latched  = pd->latched || r->sticky;
}

/**
 * @brief Whether the PD mini runs at @p mv for port k: read on at that set
 *        point, its output from PROG_RAIL_UNDER_MV under to
 *        PROG_RAIL_OVER_MV over it.
 *
 * @param sp The SUPPLY page.
 * @param mv The voltage, mV.
 * @return   true then.
 */
static bool pd_at(const supply_page_t *sp, uint16_t mv)
{
    supply_port_view_t pd;
    supply_page_port_view(sp, &pd);
    const uint32_t v = sp->regs[LINK_SP_V_MV];
    return pd.online && pd.on && pd.set_mv == mv
           && sp->regs[LINK_SP_SET_MV_RB] == mv
           && v + PROG_RAIL_UNDER_MV >= (uint32_t)mv
           && v <= (uint32_t)mv + PROG_RAIL_OVER_MV;
}

/**
 * @brief A pending ON judged: port k on once the PD mini has read the
 *        voltage long enough and every rule holds; refused at
 *        PROG_RAIL_WAIT_MS.
 *
 * @param r   The state.
 * @param pg  The POWER page.
 * @param sp  The SUPPLY page.
 * @param ctx What the ON is judged against.
 * @param now The clock, ms.
 */
static void pending_step(prog_rail_t *r, power_page_t *pg, supply_page_t *sp,
                         const prog_rail_ctx_t *ctx, uint32_t now)
{
    if (prog_rail_judge(r, pg, ctx) != PROG_RAIL_OK) {
        r->pending = false;     /* a trip, a lost heartbeat, an OFF: gone */
        return;
    }
    const uint16_t n = sp->regs[LINK_SP_SAMPLES];
    const bool fresh = n != r->samp_seen
                       && (uint16_t)(n - r->samp0)
                              >= (r->moved ? PROG_RAIL_SKIP_SAMPLES + 1u
                                           : 1u);
    r->samp_seen = n;
    if (fresh) {
        const bool at = pd_at(sp, r->want_mv);
        if (at && !r->in_band) {
            r->band_t0 = now;   /* the window starts with this sample */
            r->n_in    = 0u;
        }
        r->in_band = at;
        r->last_t  = now;
        if (at) {
            r->n_in = (uint8_t)(r->n_in < 255u ? r->n_in + 1u : 255u);
        }
        if (at && (!r->moved
                   || (r->n_in >= 2u
                       && (uint32_t)(now - r->band_t0)
                              >= PROG_RAIL_SETTLE_MS))) {
            r->pending = false;
            power_port_set(pg, (unsigned)r->port, true);
            return;
        }
    } else if (r->in_band
               && (uint32_t)(now - r->last_t) >= PROG_RAIL_SAMPLE_GAP_MS) {
        r->in_band = false;     /* no sample: the window starts anew */
    }
    if ((uint32_t)(now - r->pend_t0) >= PROG_RAIL_WAIT_MS) {
        r->pending = false;
        r->sticky  = true;      /* refused: the session ends with it */
        if (r->moved) {
            (void)supply_page_port_set(sp, false, 0u, 0u,
                                       ctx->power.beat_alive);
        }
    }
}

/**
 * @brief The current limit for a PD mini that powers a servo now: the
 *        lower of its own and the session's.
 *
 * @param pd The PD mini's view.
 * @param ma The session's limit, mA.
 * @return   The limit to ask, mA.
 */
static uint16_t lower_ma(const supply_port_view_t *pd, uint16_t ma)
{
    return (ma < pd->set_ma) ? ma : pd->set_ma;
}

bool prog_rail_set(prog_rail_t *r, power_page_t *pg, supply_page_t *sp,
                   bool on, uint16_t mv, uint16_t ma,
                   const prog_rail_ctx_t *ctx, uint32_t now)
{
    if (r == NULL || pg == NULL || sp == NULL || ctx == NULL) {
        return false;
    }
    const uint16_t bit = bit_of(r->port);
    if (!on) {
        r->pending = false;
        power_port_set(pg, (unsigned)r->port, false);   /* nothing for none */
        return true;
    }
    if (prog_rail_judge(r, pg, ctx) != PROG_RAIL_OK) {
        return false;
    }
    supply_port_view_t pd;
    supply_page_port_view(sp, &pd);
    const bool beat = ctx->power.beat_alive;
    if ((power_port_on(pg) & bit) != 0u) {
        /* The servo has power already: the PD mini's own rule, never
         * raised under it, and its current limit only lowered. */
        if (pd.asked_on) {
            ma = lower_ma(&pd, ma);
        }
        if (pd.asked_on && pd.set_mv == mv) {
            return ma == pd.set_ma
                   || supply_page_port_set(sp, true, mv, ma, beat);
        }
        if (pg->fitted != 0u) {
            r->dropped |= (uint16_t)(power_port_on(pg) & (uint16_t)~bit);
            power_drop(pg, bit);
            r->alone = true;
        }
        return supply_page_port_set(sp, true, mv, ma, beat);
    }
    const bool same = pd.asked_on && pd.set_mv == mv;
    if (pg->fitted != 0u && (!same || !pg->rail)) {
        /* Another voltage than the rail's, or the rail off: every other
         * port off for the session, before the PD mini moves. */
        r->dropped |= (uint16_t)((power_port_on(pg) | pg->want)
                                 & (uint16_t)~bit);
        power_drop(pg, bit);
        r->alone = r->alone || !same;
    }
    const bool shared = (power_port_on(pg) & (uint16_t)~bit) != 0u;
    if (!same) {
        if (pd.asked_on) {
            (void)supply_page_port_set(sp, false, 0u, 0u, beat);
        }
        if (!supply_page_port_set(sp, true, mv, ma, beat)) {
            return false;
        }
    } else if (shared) {
        /* The others keep the voltage; the limit is lowered for all, never
         * raised under them. */
        const uint16_t low = lower_ma(&pd, ma);
        if (low != pd.set_ma
            && !supply_page_port_set(sp, true, mv, low, beat)) {
            return false;
        }
    } else if (!supply_page_port_set(sp, true, mv, ma, beat)) {
        return false;
    }
    /* Port k waits for the PD mini to read the voltage. */
    r->pending = true;
    r->moved   = !same;
    r->in_band = false;
    r->want_mv = mv;
    r->pend_t0 = now;
    r->samp0   = sp->regs[LINK_SP_SAMPLES];
    r->samp_seen = r->samp0;
    r->n_in    = 0u;
    pending_step(r, pg, sp, ctx, now);
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
    /* A write that starts with RAIL 0 switches the rail off whatever the
     * rest of it is answered (power_write()): that OFF stands for the
     * session from here, and a pending ON ends. */
    if (w_rail && in[0] == 0u) {
        r->pending = false;
        if (r->held) {
            r->sticky = true;
        }
    }
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
            r->sticky  = true;
            r->pending = false;
        }
    }
    if (pg->held_on != 0u && !r->kept) {
        r->kept    = true;
        r->kept_t0 = now;
    }
    return nack;
}

void prog_rail_tell(prog_rail_t *r, power_page_t *pg, supply_page_t *sp,
                    bool held, bool writing, const prog_rail_ctx_t *ctx,
                    uint32_t now)
{
    if (r == NULL || pg == NULL) {
        return;
    }
    if (r->held && !held) {
        r->sticky  = false;     /* the session let go */
        r->alone   = false;
        r->dropped = 0u;
        r->pending = false;
    }
    if (r->pending && sp != NULL && ctx != NULL) {
        pending_step(r, pg, sp, ctx, now);
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
    r->kept    = false;
    r->pending = false;
}
