/**
 * @file
 * @brief SENSE_WIN_A and SENSE_WIN_B, read for the log.
 *
 * See sense_win_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_win_link.h"

#include <string.h>

static const uint8_t k_page[2] = { LINK_PAGE_SENSE_WIN_A,
                                   LINK_PAGE_SENSE_WIN_B };

#define NONE 0xFFu

void sense_win_link_init(sense_win_link_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->pending = NONE;
    }
}

void sense_win_link_lost(sense_win_link_t *s)
{
    if (s == NULL) {
        return;
    }
    const uint8_t want = s->want;
    sense_win_link_init(s);
    s->want = want;
}

void sense_win_link_came_up(sense_win_link_t *s, uint16_t minor)
{
    if (s == NULL) {
        return;
    }
    sense_win_link_lost(s);
    s->up   = true;
    s->page = minor >= LINK_MINOR_SENSE_MAP;
}

void sense_win_link_want(sense_win_link_t *s, uint8_t parts)
{
    if (s == NULL) {
        return;
    }
    const uint8_t want = (uint8_t)(((parts & 0x03u) != 0u ? 0x01u : 0u)
                                   | ((parts & 0x0Cu) != 0u ? 0x02u : 0u));
    for (unsigned p = 0u; p < 2u; ++p) {
        if ((want & (1u << p)) == 0u) {
            s->have[p]      = false;
            s->have_prev[p] = false;
        }
    }
    s->want = want;
}

bool sense_win_link_next(sense_win_link_t *s, uint32_t now_ms,
                         sense_win_link_op_t *op)
{
    if (s == NULL || op == NULL || !s->up || !s->page
        || s->pending != NONE) {
        return false;
    }
    /* The page asked for longest ago first, so two pages share the reads. */
    unsigned best = NONE;
    uint32_t age = 0u;
    for (unsigned p = 0u; p < 2u; ++p) {
        if ((s->want & (1u << p)) == 0u) {
            continue;
        }
        const uint32_t a = (uint32_t)(now_ms - s->read_ms[p]);
        if (s->asked[p] && a < SENSE_WIN_LINK_READ_MS) {
            continue;
        }
        const uint32_t key = s->asked[p] ? a : UINT32_MAX;
        if (best == NONE || key > age) {
            best = p;
            age  = key;
        }
    }
    if (best == NONE) {
        return false;
    }
    s->pending        = (uint8_t)best;
    s->asked[best]    = true;
    s->read_ms[best]  = now_ms;
    op->page = k_page[best];
    op->n    = (uint8_t)LINK_SX_COUNT;
    return true;
}

void sense_win_link_done(sense_win_link_t *s, int result,
                         const uint16_t *regs)
{
    if (s == NULL || s->pending == NONE) {
        return;
    }
    const unsigned p = s->pending;
    s->pending = NONE;
    if (result == SENSE_WIN_LINK_ACK && regs != NULL) {
        /* The read before stays while it holds another window: a row of
         * that window can come after this read. */
        if (s->have[p] && s->regs[p][LINK_SX_WINDOW] != regs[LINK_SX_WINDOW]) {
            memcpy(s->prev[p], s->regs[p], sizeof(s->prev[p]));
            s->have_prev[p] = true;
        }
        memcpy(s->regs[p], regs, sizeof(s->regs[p]));
        s->have[p] = (s->want & (1u << p)) != 0u;
        s->have_prev[p] = s->have_prev[p] && s->have[p];
    } else if (result == (int)LINK_NACK_BAD_PAGE) {
        s->page = false;
        memset(s->have, 0, sizeof(s->have));
        memset(s->have_prev, 0, sizeof(s->have_prev));
    }
}

/**
 * @brief Whether a read @p r kept as @p have holds window @p window.
 *
 * @param have   true when the read was kept.
 * @param r      The registers read.
 * @param window The window number.
 * @return true when the read holds window.
 */
static bool holds(bool have, const uint16_t *r, uint16_t window)
{
    return have && (r[LINK_SX_FLAGS] & LINK_SX_HAVE) != 0u
           && r[LINK_SX_WINDOW] == window;
}

bool sense_win_link_last(const sense_win_link_t *s, unsigned c, uint16_t *mv,
                         int16_t *ma)
{
    if (s == NULL || !s->up || !s->page || c >= LINK_SM_CHANNELS) {
        return false;
    }
    const unsigned p = c / LINK_SX_PER_PAGE;
    const unsigned k = c % LINK_SX_PER_PAGE;
    const uint16_t *r = s->regs[p];
    if (!s->have[p] || (r[LINK_SX_FLAGS] & LINK_SX_HAVE) == 0u) {
        return false;
    }
    const uint16_t both = (uint16_t)(LINK_SX_E_CURRENT | LINK_SX_E_VOLTAGE);
    if ((r[LINK_SX_E(k, LINK_SX_E_FLAGS)] & both) != both) {
        return false;
    }
    if (mv != NULL) {
        *mv = r[LINK_SX_E(k, LINK_SX_E_MEAN_MV)];
    }
    if (ma != NULL) {
        *ma = (int16_t)r[LINK_SX_E(k, LINK_SX_E_MEAN_MA)];
    }
    return true;
}

bool sense_win_link_window(const sense_win_link_t *s, uint16_t window,
                           bench_chw_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->window = window;
    if (s == NULL || !s->up || !s->page) {
        return false;
    }
    for (unsigned p = 0u; p < 2u; ++p) {
        const uint16_t *r = NULL;
        if (holds(s->have[p], s->regs[p], window)) {
            r = s->regs[p];
        } else if (holds(s->have_prev[p], s->prev[p], window)) {
            r = s->prev[p];
        } else {
            continue;
        }
        for (unsigned k = 0u; k < LINK_SX_PER_PAGE; ++k) {
            const unsigned c = p * LINK_SX_PER_PAGE + k;
            const uint16_t f = r[LINK_SX_E(k, LINK_SX_E_FLAGS)];
            const bool cur = (f & LINK_SX_E_CURRENT) != 0u;
            const bool volt = (f & LINK_SX_E_VOLTAGE) != 0u;
            if (!cur && !volt) {
                continue;       /* off, or its part not online */
            }
            const uint16_t bit = (uint16_t)(1u << c);
            out->ok = (uint16_t)(out->ok | bit);
            if (!cur) {
                out->no_current = (uint16_t)(out->no_current | bit);
            }
            if (!volt) {
                out->no_voltage = (uint16_t)(out->no_voltage | bit);
            }
            out->mean_ma[c] = (int16_t)r[LINK_SX_E(k, LINK_SX_E_MEAN_MA)];
            out->max_ma[c]  = (int16_t)r[LINK_SX_E(k, LINK_SX_E_MAX_MA)];
            out->min_mv[c]  = r[LINK_SX_E(k, LINK_SX_E_MIN_MV)];
        }
    }
    return out->ok != 0u;
}

void sense_win_link_ring(bench_chw_t *out, unsigned fast,
                         const bench_state_t *b)
{
    if (out == NULL || b == NULL || !b->servo_new
        || fast >= BENCH_CHW_CHANNELS) {
        return;
    }
    const uint16_t bit = (uint16_t)(1u << fast);
    out->window = b->servo_window;
    out->ok         = (uint16_t)(out->ok & ~bit);
    out->no_current = (uint16_t)(out->no_current & ~bit);
    out->no_voltage = (uint16_t)(out->no_voltage & ~bit);
    out->mean_ma[fast] = 0;
    out->max_ma[fast]  = 0;
    out->min_mv[fast]  = 0u;
    if ((b->servo_ok & 0x01u) == 0u) {
        return;
    }
    out->ok = (uint16_t)(out->ok | bit);
    if ((b->servo_no_current & 0x01u) != 0u) {
        out->no_current = (uint16_t)(out->no_current | bit);
    }
    if ((b->servo_no_voltage & 0x01u) != 0u) {
        out->no_voltage = (uint16_t)(out->no_voltage | bit);
    }
    out->mean_ma[fast] = b->servo_mean_ma[0];
    out->max_ma[fast]  = b->servo_max_ma[0];
    out->min_mv[fast]  = b->servo_min_mv[0];
}
