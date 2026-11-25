/**
 * @file log_graph_xy.c
 * @brief LOGS' graph of an XY file: the scales over the rows drawn and the
 *        markers, each row's place on screen, and the row under a finger.
 *        See log_graph_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdlib.h>

#include "log_graph_int.h"

/**
 * @brief Widen [*lo, *hi] to take @p v.
 * @param lo   The foot.
 * @param hi   The top.
 * @param any  Whether the range holds a value yet; set.
 * @param v    The value; one that is not finite is left out.
 */
static void take(float *lo, float *hi, bool *any, float v)
{
    if (!isfinite(v)) {
        return;
    }
    if (!*any) {
        *lo = *hi = v;
        *any = true;
    } else if (v < *lo) {
        *lo = v;
    } else if (v > *hi) {
        *hi = v;
    }
}

/**
 * @brief Make a range a scale: a value alone or none gets a width of 2
 *        round it; a range of a series @p pad 6% of its width each way,
 *        never across zero.
 * @param lo   The foot.
 * @param hi   The top.
 * @param any  Whether the range holds a value.
 * @param pad  Pad it: the series' scales are padded, the x scale is not,
 *             so its first and last values stand on the plot's first and
 *             last columns.
 */
static void settle(float *lo, float *hi, bool any, bool pad)
{
    if (!any) {
        *lo = 0.0f;
        *hi = 1.0f;
    } else if (!(*hi > *lo)) {
        *lo -= 1.0f;
        *hi += 1.0f;
    } else if (pad) {
        const float min = *lo;
        const float max = *hi;
        const float p = (max - min) * 0.06f;
        *lo = (min >= 0.0f && min - p < 0.0f) ? 0.0f : min - p;
        *hi = (max <= 0.0f && max + p > 0.0f) ? 0.0f : max + p;
    }
}

int lg_series_of(int column)
{
    for (int k = 1; k < s.data.n_fields; ++k) {
        if (s.data.field[k].column == column) {
            return k;
        }
    }
    return -1;
}

int lg_x_px(float x)
{
    const float span = lg.xhi - lg.xlo;
    float f = (span > 0.0f && isfinite(x)) ? (x - lg.xlo) / span : 0.0f;
    f = (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f;
    return PV_X + (int)lroundf(f * (float)(PV_W - 1));
}

int lg_y_px(int k, float v)
{
    const float span = lg.hi[k] - lg.lo[k];
    float f = (span > 0.0f && isfinite(v)) ? (v - lg.lo[k]) / span : 0.0f;
    f = (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f;
    return PV_Y + PV_H - 1 - (int)lroundf(f * (float)(PV_H - 1));
}

void lg_scale(void)
{
    const log_markset_t *mk = &lg.mk;
    bool any_x = false;
    for (int i = 0; i < mk->rows; ++i) {
        if (lg_drawn(i)) {
            take(&lg.xlo, &lg.xhi, &any_x, s.data.value[0][i]);
        }
    }
    for (int m = 0; m < mk->n_marks; ++m) {
        if (!mk->mark[m].level) {
            take(&lg.xlo, &lg.xhi, &any_x, mk->mark[m].at);
        }
    }
    settle(&lg.xlo, &lg.xhi, any_x, false);
    for (int k = 1; k < s.data.n_fields; ++k) {
        bool any = false;
        for (int i = 0; i < mk->rows; ++i) {
            if (lg_drawn(i)) {
                take(&lg.lo[k], &lg.hi[k], &any, s.data.value[k][i]);
            }
        }
        for (int m = 0; m < mk->n_marks; ++m) {
            if (mk->mark[m].level
                && lg_series_of(mk->mark[m].column) == k) {
                take(&lg.lo[k], &lg.hi[k], &any, mk->mark[m].at);
            }
        }
        settle(&lg.lo[k], &lg.hi[k], any, true);
    }
    for (int i = 0; i < mk->rows; ++i) {
        lg.px[i] = (int16_t)lg_x_px(s.data.value[0][i]);
        for (int k = 1; k < s.data.n_fields; ++k) {
            const float v = s.data.value[k][i];
            lg.py[k][i] = (int16_t)(isfinite(v) ? lg_y_px(k, v) : PV_EMPTY);
        }
    }
}

/**
 * @brief The first series shown with a number in row @p i.
 * @param i  The row.
 * @return the series, -1 for none.
 */
static int first_shown(int i)
{
    for (int k = 1; k < s.data.n_fields; ++k) {
        if (!lg.hidden[k] && lg.py[k][i] != PV_EMPTY) {
            return k;
        }
    }
    return -1;
}

int lg_row_at(int x, int y)
{
    int best = -1;
    int best_dx = 0;
    int best_dy = 0;
    for (int i = 0; i < lg.mk.rows; ++i) {
        if (!lg_drawn(i)) {
            continue;
        }
        const int dx = abs(lg.px[i] - x);
        const int k = first_shown(i);
        const int dy = (k > 0) ? abs(lg.py[k][i] - y) : 0;
        if (best < 0 || dx < best_dx || (dx == best_dx && dy < best_dy)) {
            best = i;
            best_dx = dx;
            best_dy = dy;
        }
    }
    return best;
}
