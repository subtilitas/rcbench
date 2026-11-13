/**
 * @file
 * @brief The power node's 20ms windows and the extremes since the RUN
 * marker: samples in, RCNODE_T_PWR_WIN and RCNODE_T_PWR_RUN out.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rcnode.h"

#include <string.h>

void rcnode_win_init(rcnode_win_t *w)
{
    if (w != NULL) {
        memset(w, 0, sizeof *w);
    }
}

/**
 * @brief Start window @p idx with nothing in it.
 *
 * @param w The reducer.
 * @param idx The window's number.
 */
static void win_start(rcnode_win_t *w, uint64_t idx)
{
    w->idx   = idx;
    w->open  = true;
    w->flags = 0u;
    w->n     = 0u;
    w->ni    = 0u;
    w->i_sum = 0;
    w->i_max = RCNODE_I_NONE;
    w->v_sum = 0u;
    w->v_min = RCNODE_V_NONE;
}

/**
 * @brief The mean of a signed sum, rounded half away from 0.
 *
 * @param sum The sum.
 * @param n The count, 1 or more.
 * @return The mean.
 */
static int32_t mean_i(int64_t sum, uint8_t n)
{
    const int64_t half = (int64_t)(n / 2u);
    const int64_t q = (sum >= 0) ? (sum + half) / n : (sum - half) / n;
    return (int32_t)q;
}

/**
 * @brief Write the window being filled into @p out.
 *
 * @param w The reducer.
 * @param keep_flags RCNODE_WF_ONLINE and RCNODE_WF_RUN as they stand.
 * @param out Receives the window.
 */
static void win_close(const rcnode_win_t *w, uint8_t keep_flags,
                      rcnode_pwr_win_t *out)
{
    memset(out, 0, sizeof *out);
    out->hdr.type   = RCNODE_T_PWR_WIN;
    out->hdr.t_node = (uint32_t)(((w->idx + 1u) * RCNODE_WIN_US) & 0xFFFFFFFFu);
    out->flags = (uint8_t)(w->flags
                           | (keep_flags & (RCNODE_WF_ONLINE | RCNODE_WF_RUN)));
    out->n     = w->n;
    out->i_max = RCNODE_I_NONE;
    out->v_min = RCNODE_V_NONE;
    if (w->ni > 0u) {
        out->i_mean = mean_i(w->i_sum, w->ni);
        out->i_max  = w->i_max;
    }
    if (w->n > 0u) {
        out->v_mean = (uint16_t)((w->v_sum + w->n / 2u) / w->n);
        out->v_min  = w->v_min;
        if ((uint32_t)out->v_mean * RCNODE_V_STEP_UV >= (uint32_t)RCNODE_PACK_UV) {
            out->flags |= RCNODE_WF_PACK;
        }
    }
}

bool rcnode_win_add(rcnode_win_t *w, uint64_t t_us, const rcnode_sample_t *s,
                    uint8_t keep_flags, rcnode_pwr_win_t *out)
{
    if (w == NULL || s == NULL || out == NULL) {
        return false;
    }
    const uint64_t idx = t_us / RCNODE_WIN_US;
    bool closed = false;
    if (w->open && idx != w->idx) {
        win_close(w, keep_flags, out);
        closed = true;
    }
    if (!w->open || closed) {
        win_start(w, idx);
    }
    if (!s->ok) {
        w->flags |= RCNODE_WF_READ_FAIL;
        return closed;
    }
    if (w->n == UINT8_MAX) {
        return closed;           /* a window this full is not reached at 1130µs */
    }
    ++w->n;
    const uint16_t v = rcnode_v_field(s->v_uv);
    w->v_sum += v;
    if (v < w->v_min) {
        w->v_min = v;
    }
    if (s->clip > 0) {
        w->flags |= RCNODE_WF_CLIP_HIGH;
    } else if (s->clip < 0) {
        w->flags |= RCNODE_WF_CLIP_LOW;
    } else {
        ++w->ni;
        w->i_sum += s->i_ua;
        if (s->i_ua > w->i_max) {
            w->i_max = s->i_ua;
        }
    }
    return closed;
}

void rcnode_run_mark(rcnode_run_t *r, uint8_t tag)
{
    if (r == NULL) {
        return;
    }
    r->tag     = tag;
    r->flags   = 0u;
    r->i_max   = RCNODE_I_NONE;
    r->v_min   = RCNODE_V_NONE;
    r->windows = 0u;
}

void rcnode_run_take(rcnode_run_t *r, const rcnode_pwr_win_t *w)
{
    if (r == NULL || w == NULL) {
        return;
    }
    if (r->windows < UINT32_MAX) {
        ++r->windows;
    }
    if ((w->flags & (RCNODE_WF_CLIP_HIGH | RCNODE_WF_CLIP_LOW)) != 0u) {
        r->flags |= RCNODE_RF_CLIP;
    }
    if ((w->flags & RCNODE_WF_READ_FAIL) != 0u) {
        r->flags |= RCNODE_RF_READ_FAIL;
    }
    if (w->n == 0u) {
        r->flags |= RCNODE_RF_EMPTY;
        return;
    }
    if (w->i_max != RCNODE_I_NONE && w->i_max > r->i_max) {
        r->i_max = w->i_max;
    }
    if (w->v_min < r->v_min) {
        r->v_min = w->v_min;
    }
}
