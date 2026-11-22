/**
 * @file
 * @brief The POWER page of the remote servo node's IOmcu.
 *
 * See power_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "power_page.h"

#include <stddef.h>
#include <string.h>

#include "board_fix.h"
#include "link_msg.h"
#include "sense_sched.h"

/** @brief The critical alert flags of a Mask/Enable value, CH1 first. */
static const uint16_t k_cf[INA3221_CHANNELS] = {
    INA3221_ME_CF1, INA3221_ME_CF2, INA3221_ME_CF3,
};

/**
 * @brief Note the edges of PORT_ON and of the quiet state against the
 *        newest window: a window counts for a port only when it closed
 *        wholly after the port came on, and for the BEC check only when it
 *        closed wholly after the rail and the ports went off.
 *
 * @param p The state.
 */
static void track(power_page_t *p)
{
    const uint16_t on = power_port_on(p);
    const uint16_t rose = (uint16_t)(on & (uint16_t)~p->on_prev);
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) {
        if ((rose & (1u << k)) != 0u) {
            p->on_win[k] = p->last_win;
            p->low_n[k]  = 0u;
            p->settled  &= (uint16_t)~(1u << k);
        }
    }
    p->on_prev = on;
    const bool quiet = !p->rail && on == 0u;
    if (quiet && !p->quiet) {
        p->quiet_win = p->last_win;
        p->quiet_any = false;
    }
    p->quiet = quiet;
}

/**
 * @brief Whether window @p number closed wholly after the window @p mark
 *        that was the newest when something changed.
 *
 * The window after @p mark was being filled at the change, so the one after
 * that is the first that holds no reading from before it.  Asked only until
 * it first holds: the caller latches it (settled, quiet_any), so the
 * 16-bit numbers' half range bounds the wait, not the time a port stays
 * on.
 *
 * @param number The window's number.
 * @param mark   The newest window number at the change.
 * @return       true for @p number 2 or more past @p mark, modulo 65536.
 */
static bool after(uint16_t number, uint16_t mark)
{
    const uint16_t d = (uint16_t)(number - mark);
    return d >= 2u && d < 0x8000u;
}

void power_init(power_page_t *p, bool switches, bool chip_ok)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->fitted    = switches ? (uint16_t)LINK_PW_PORT_ALL : 0u;
    p->chip_ok   = chip_ok;
    p->quiet     = true;
    p->quiet_any = true;
}

uint16_t power_port_on(const power_page_t *p)
{
    if (p == NULL || !p->rail) {
        return 0u;
    }
    return (p->fitted != 0u) ? (uint16_t)(p->want & p->fitted)
                             : (uint16_t)LINK_PW_PORT_ALL;
}

bool power_rail(const power_page_t *p)
{
    return p != NULL && p->rail;
}

/**
 * @brief The value a write leaves in register @p reg.
 *
 * @param p    The state.
 * @param off  First register written.
 * @param n    Registers written.
 * @param in   The values.
 * @param reg  LINK_PW_RAIL or LINK_PW_PORT_WANT.
 * @return     The written value, or the register as it stands.
 */
static uint16_t next_of(const power_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, unsigned reg)
{
    if (reg >= (unsigned)off && reg < (unsigned)off + n) {
        return in[reg - off];
    }
    return (reg == (unsigned)LINK_PW_RAIL) ? (p->rail ? 1u : 0u) : p->want;
}

uint8_t power_write(power_page_t *p, uint8_t off, uint8_t n,
                    const uint16_t *in, const power_ctx_t *ctx)
{
    if (p == NULL || in == NULL || ctx == NULL || n == 0u
        || (unsigned)off + n > (unsigned)LINK_PW_COUNT) {
        return (uint8_t)LINK_NACK_BAD_RANGE;
    }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned reg = (unsigned)off + i;
        if (reg != (unsigned)LINK_PW_RAIL
            && reg != (unsigned)LINK_PW_PORT_WANT) {
            return (uint8_t)LINK_NACK_READ_ONLY;
        }
    }
    const uint16_t rail = next_of(p, off, n, in, LINK_PW_RAIL);
    const uint16_t want = next_of(p, off, n, in, LINK_PW_PORT_WANT);
    const bool rail_written = (unsigned)off == (unsigned)LINK_PW_RAIL;
    const bool rail_on = rail_written && rail == 1u;
    const uint16_t added = (uint16_t)(want & (uint16_t)~p->want);
    const bool new_bit = added != 0u;
    /* Only a write that powers a port is judged against the throttles: a
     * new bit, or RAIL 1 with the bits kept.  An OFF is always taken. */
    if (rail > 1u || (want & (uint16_t)~LINK_PW_PORT_ALL) != 0u
        || (added & ctx->throttle) != 0u
        || (rail_on && (want & ctx->throttle) != 0u)) {
        return (uint8_t)LINK_NACK_BAD_VALUE;
    }
    if ((rail_on || new_bit) && (!ctx->beat_alive || !p->chip_ok)) {
        return (uint8_t)LINK_NACK_NOT_ARMED;
    }
    if (rail_on && !ctx->limits_held) {
        return (uint8_t)LINK_NACK_BUSY;
    }
    if (rail_on) {
        p->tripped = 0u;
    }
    p->rail = rail == 1u;
    p->want = want;
    track(p);
    return 0u;
}

void power_read(const power_page_t *p, uint8_t off, uint8_t n,
                uint16_t *out)
{
    if (out == NULL) {
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned reg = (unsigned)off + i;
        uint16_t v = 0u;
        if (p != NULL) {
            switch (reg) {
            case LINK_PW_RAIL:      v = p->rail ? 1u : 0u;    break;
            case LINK_PW_FITTED:    v = p->fitted;            break;
            case LINK_PW_PORT_ON:   v = power_port_on(p);     break;
            case LINK_PW_PORT_WANT: v = p->want;              break;
            case LINK_PW_TRIPPED:   v = p->tripped;           break;
            case LINK_PW_BEC:       v = p->bec;               break;
            default:                                          break;
            }
        }
        out[i] = v;
    }
}

void power_off(power_page_t *p)
{
    if (p == NULL) {
        return;
    }
    p->rail = false;
    p->want = 0u;
    track(p);
}

void power_trip(power_page_t *p, uint16_t bits)
{
    if (p == NULL) {
        return;
    }
    p->tripped |= bits;
    power_off(p);
}

uint16_t power_step(power_page_t *p, const power_in_t *in)
{
    if (p == NULL || in == NULL) {
        return 0u;
    }
    uint16_t trip = in->crit_n_low ? (uint16_t)LINK_PW_TRIP_CRIT_N : 0u;
    for (unsigned a = 0; a < POWER_PARTS; ++a) {
        if ((in->me_ok & (1u << a)) == 0u) {
            continue;
        }
        const bool fresh = p->have_me[a] && in->me_n[a] != p->me_n[a];
        if (fresh) {
            for (unsigned ch = 0; ch < INA3221_CHANNELS; ++ch) {
                if ((in->me[a] & k_cf[ch]) != 0u) {
                    trip |= LINK_PW_TRIP_CF(a * INA3221_CHANNELS + ch);
                }
            }
        }
        p->have_me[a] = true;
        p->me_n[a]    = in->me_n[a];
    }
    if (p->have_over) {
        for (unsigned k = 0; k < LINK_PW_PORTS; ++k) {
            if (in->over_n[k] != p->over_n[k]) {
                trip |= (uint16_t)LINK_PW_TRIP_LIMIT;
            }
        }
    }
    memcpy(p->over_n, in->over_n, sizeof(p->over_n));
    p->have_over = true;
    if (trip != 0u) {
        power_trip(p, trip);
    }
    return trip;
}

uint16_t power_window(power_page_t *p, uint32_t series, uint16_t number,
                      const int32_t *mv,
                      uint16_t have, uint16_t rail_mv)
{
    if (p == NULL || mv == NULL) {
        return 0u;
    }
    const bool restart = p->have_win && series != p->series;
    if (p->have_win && !restart && number == p->last_win) {
        return 0u;
    }
    const uint16_t on = power_port_on(p);
    if (restart) {
        /* The sensor schedule started again and numbers its windows anew:
         * every window of the new series was filled after whatever was on
         * or off already, so those checks count from its first window. */
        p->settled = on;
        memset(p->low_n, 0, sizeof(p->low_n));
        if (p->quiet) {
            p->quiet_any = true;
        }
    }
    p->have_win = true;
    p->series   = series;
    p->last_win = number;
    bool hot = false;
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) {
        const uint16_t bit = (uint16_t)(1u << k);
        if ((have & bit) == 0u) {
            continue;
        }
        if ((on & bit) != 0u && (p->settled & bit) == 0u
            && after(number, p->on_win[k])) {
            p->settled |= bit;          /* latched until the port goes off */
        }
        if ((on & bit) != 0u && rail_mv != 0u && (p->settled & bit) != 0u) {
            if (mv[k] * 2 < (int32_t)rail_mv) {
                if (++p->low_n[k] >= POWER_THERMAL_N) {
                    hot = true;
                }
            } else {
                p->low_n[k] = 0u;
            }
        }
        if (p->quiet && !p->quiet_any && after(number, p->quiet_win)) {
            p->quiet_any = true;        /* latched until the rail goes on */
        }
        if (p->quiet && p->quiet_any) {
            if (mv[k] >= (int32_t)LINK_PW_BEC_MV) {
                p->bec |= bit;
            } else {
                p->bec &= (uint16_t)~bit;
            }
        }
    }
    if (hot) {
        power_trip(p, (uint16_t)LINK_PW_TRIP_THERMAL);
        return (uint16_t)LINK_PW_TRIP_THERMAL;
    }
    return 0u;
}

uint16_t power_throttle(const outputs_t *o, const outbind_roles_t *r)
{
    if (o == NULL || r == NULL) {
        return 0u;
    }
    uint16_t m = 0u;
    for (unsigned s = 0; s < OUT_MAX_SLOTS; ++s) {
        const out_slot_t *sl = &o->slot[s];
        if (sl->driver == OUT_DRIVER_NONE) {
            continue;
        }
        const int k = board_fix_out_index(r, sl->pin);
        if (k < 0 || k >= (int)LINK_PW_PORTS) {
            continue;
        }
        for (unsigned c = sl->first_channel;
             c < (unsigned)sl->first_channel + sl->channels
             && c < OUT_MAX_CHANNELS; ++c) {
            if (o->channel[c].role == OUT_ROLE_THROTTLE) {
                m |= (uint16_t)(1u << (unsigned)k);
            }
        }
    }
    return m;
}

uint16_t power_faults(const power_page_t *p)
{
    if (p == NULL) {
        return 0u;
    }
    uint16_t f = 0u;
    if ((p->tripped & (uint16_t)~LINK_PW_TRIP_THERMAL) != 0u) {
        f |= (uint16_t)LINK_FAULT_OVERCURRENT;
    }
    if ((p->tripped & LINK_PW_TRIP_THERMAL) != 0u) {
        f |= (uint16_t)LINK_FAULT_OVERTEMP;
    }
    return f;
}
