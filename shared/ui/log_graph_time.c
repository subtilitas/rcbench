/**
 * @file log_graph_time.c
 * @brief LOGS' graph of a time file: each row at its time in the window,
 *        the traces' spans per pixel column, the gaps where rows are
 *        missing and the strips' codes per column.  See log_graph_int.h.
 *
 * The window is the plot's [win0, win0 + win_n), as two fingers set it.
 * Its first row stands on the plot's first column and its last row on the
 * last; a row between at its time.  A window whose times do not rise (one
 * row, or a file whose time column the build replaced by the row number)
 * spaces its rows evenly.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <string.h>

#include "log_graph_int.h"

bool lg_time_on(void)
{
    return lg_on() && lg.time;
}

/**
 * @brief A series' scale over its whole run: its smallest and largest
 *        value padded 6% each way, never across zero; a constant one 1
 *        each way.  As the plain time plot's (lv_compute_spans()).
 * @param k  The series.
 */
static void scale_of(int k)
{
    const float min = s.data.field[k].min;
    const float max = s.data.field[k].max;
    float lo = min;
    float hi = max;
    if (!isfinite(lo) || !isfinite(hi)) {
        lo = 0.0f;
        hi = 1.0f;
    } else if (!(hi > lo)) {
        lo -= 1.0f;
        hi += 1.0f;
    } else {
        const float pad = (hi - lo) * 0.06f;
        lo = (min >= 0.0f && lo - pad < 0.0f) ? 0.0f : lo - pad;
        hi = (max <= 0.0f && hi + pad > 0.0f) ? 0.0f : hi + pad;
    }
    lg.lo[k] = lo;
    lg.hi[k] = hi;
    lv_plot.lo[k] = lo;
    lv_plot.hi[k] = hi;
}

void lg_time_setup(void)
{
    for (int k = 0; k < s.data.n_fields; ++k) {
        scale_of(k);
    }
    lg.has_time = s.data.time_name[0] != '\0';
    const double medians = LG_GAP_MEDIANS * s.data.median_dt_s;
    lg.gap_s = (medians > LG_GAP_MIN_S) ? medians : LG_GAP_MIN_S;
    lg.plot_h = PV_H - lg.mk.n_strips * LG_STRIP_ROW;
}

bool lg_time_gap_before(int i)
{
    if (!lg.has_time || i <= 0 || i >= s.data.count) {
        return false;
    }
    return (double)s.data.time[i] - (double)s.data.time[i - 1] > lg.gap_s;
}

/**
 * @brief Whether the window's rows stand at their time: its last time is
 *        above its first.
 * @return false for a window of one row or of one time.
 */
static bool timed(void)
{
    return lg.t1 > lg.t0;
}

int lg_time_col(int i)
{
    const int last = s.win0 + s.win_n - 1;
    if (i < s.win0 || i > last) {
        return -1;
    }
    float f = 0.0f;
    if (timed()) {
        f = (s.data.time[i] - lg.t0) / (lg.t1 - lg.t0);
    } else if (s.win_n > 1) {
        f = (float)(i - s.win0) / (float)(s.win_n - 1);
    }
    f = (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f;
    return (int)lroundf(f * (float)(PV_W - 1));
}

int lg_time_row_at(int x)
{
    const int last = s.win0 + s.win_n - 1;
    int rel = x - PV_X;
    rel = (rel < 0) ? 0 : (rel > PV_W - 1) ? PV_W - 1 : rel;
    if (!timed()) {
        return (s.win_n > 1)
                   ? s.win0 + (int)lroundf((float)rel * (float)(s.win_n - 1)
                                           / (float)(PV_W - 1))
                   : s.win0;
    }
    const float t = lg.t0 + (lg.t1 - lg.t0) * (float)rel / (float)(PV_W - 1);
    /* The first row at or after t; the times never fall. */
    int a = s.win0;
    int b = last;
    while (a < b) {
        const int m = a + (b - a) / 2;
        if (s.data.time[m] < t) {
            a = m + 1;
        } else {
            b = m;
        }
    }
    if (a > s.win0 && t - s.data.time[a - 1] <= s.data.time[a] - t) {
        return a - 1;
    }
    return a;
}

/**
 * @brief Widen column @p x's span of series @p k to take row @p y.
 * @param k  The series.
 * @param x  The column, from the plot's left.
 * @param y  The pixel row.
 */
static void widen(int k, int x, int y)
{
    int16_t *sp = lv_plot.span[k][x];
    if (sp[0] == PV_EMPTY) {
        sp[0] = (int16_t)y;
        sp[1] = (int16_t)y;
    } else if (y < sp[0]) {
        sp[0] = (int16_t)y;
    } else if (y > sp[1]) {
        sp[1] = (int16_t)y;
    }
}

/**
 * @brief Series @p k's spans: each row's value in its column, the columns
 *        between two rows on the line that joins them, unless a gap or a
 *        cell without a number lies between; then each column reaches the
 *        one before it, but across a gap.
 * @param k  The series.
 */
static void spans_of(int k)
{
    const float *v = s.data.value[k];
    const int last = s.win0 + s.win_n - 1;
    int pc = -1;
    int py = 0;
    for (int x = 0; x < PV_W; ++x) {
        lv_plot.span[k][x][0] = PV_EMPTY;
        lv_plot.span[k][x][1] = PV_EMPTY;
    }
    for (int i = s.win0; i <= last; ++i) {
        if (!isfinite(v[i])) {
            pc = -1;
            continue;
        }
        const int c = lg_time_col(i);
        if (c < 0) {
            continue;
        }
        const int y = lg_y_px(k, v[i]);
        widen(k, c, y);
        if (pc >= 0 && c > pc + 1 && !lg_time_gap_before(i)) {
            for (int x = pc + 1; x < c; ++x) {
                widen(k, x, py + (y - py) * (x - pc) / (c - pc));
            }
        }
        pc = c;
        py = y;
    }
    for (int x = 1; x < PV_W; ++x) {
        int16_t *sp = lv_plot.span[k][x];
        const int16_t *pv = lv_plot.span[k][x - 1];
        if (sp[0] == PV_EMPTY || pv[0] == PV_EMPTY
            || (lg.col[x] & LG_COL_BREAK) != 0u) {
            continue;
        }
        if (sp[0] > pv[1]) {
            sp[0] = pv[1];
        } else if (sp[1] < pv[0]) {
            sp[1] = pv[0];
        }
    }
}

/** @brief The columns' flags: where a row stands, where a gap lies and
 *         where one ends. */
static void gaps(void)
{
    const int last = s.win0 + s.win_n - 1;
    memset(lg.col, 0, sizeof(lg.col));
    int pc = -1;
    for (int i = s.win0; i <= last; ++i) {
        const int c = lg_time_col(i);
        if (c < 0) {
            continue;
        }
        lg.col[c] |= LG_COL_ROW;
        if (pc >= 0 && lg_time_gap_before(i)) {
            for (int x = pc + 1; x < c; ++x) {
                lg.col[x] |= LG_COL_GAP;
            }
            if (c > pc) {
                lg.col[c] |= LG_COL_BREAK;
            }
        }
        pc = c;
    }
}

/**
 * @brief Strip @p k's code per column: the first row's in a column that
 *        holds rows, the one before it held over a column without, none
 *        in a gap.
 * @param k  The strip.
 */
static void strip_cols(int k)
{
    uint8_t *out = lg.strip_col[k];
    const int last = s.win0 + s.win_n - 1;
    memset(out, 0, PV_W);
    int pc = -1;
    for (int i = s.win0; i <= last; ++i) {
        const int c = lg_time_col(i);
        if (c >= 0 && c != pc) {
            out[c] = (uint8_t)log_marks_strip_code(&lg.mk, k, i);
            pc = c;
        }
    }
    uint8_t held = LOG_STRIP_NONE;
    for (int x = 0; x < PV_W; ++x) {
        if ((lg.col[x] & LG_COL_GAP) != 0u) {
            held = LOG_STRIP_NONE;
        } else if ((lg.col[x] & LG_COL_ROW) != 0u) {
            held = out[x];
        } else {
            out[x] = held;
        }
    }
}

void lg_time_spans(void)
{
    const int last = s.win0 + s.win_n - 1;
    lg.t0 = s.data.time[s.win0];
    lg.t1 = s.data.time[last];
    gaps();
    for (int k = 0; k < s.data.n_fields; ++k) {
        lv_plot.lo[k] = lg.lo[k];
        lv_plot.hi[k] = lg.hi[k];
        spans_of(k);
    }
    for (int k = 0; k < lg.mk.n_strips; ++k) {
        strip_cols(k);
    }
}
