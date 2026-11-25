/**
 * @file log_graph_time.c
 * @brief LOGS' graph of a time file: each row at its time in the window,
 *        the traces' spans per pixel column, the gaps where rows are
 *        missing and the strips' codes per column.  See log_graph_int.h.
 *
 * The window is the plot's [win0, win0 + win_n).  Its first row stands on
 * the plot's first column and its last row on the last; a row between at
 * its time.  Two fingers on a file with a time column set a range of time
 * instead (lg_time_window()): the window then holds the rows that bracket
 * it, the range fills the plot, and the lines run to its edges from the
 * rows beyond them.  A window whose times do not rise (one
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
    /* In whole milliseconds, as the bench writes its times: a median of
     * 0.1s the build keeps as 0.1000000015 is 100ms. */
    const long median_ms = lround(s.data.median_dt_s * 1000.0);
    const long medians_ms = lround(LG_GAP_MEDIANS * (double)median_ms);
    const long min_ms = lround(LG_GAP_MIN_S * 1000.0);
    lg.gap_ms = (medians_ms > min_ms) ? medians_ms : min_ms;
    lg.plot_h = PV_H - lg.mk.n_strips * LG_STRIP_ROW;
}

bool lg_time_gap_before(int i)
{
    if (!lg.has_time || i <= 0 || i >= s.data.count) {
        return false;
    }
    /* Whole milliseconds: the build keeps the times as floats, so 0.7s to
     * 1.2s reads 0.50000006s, which is the threshold and no gap. */
    const double dt = (double)s.data.time[i] - (double)s.data.time[i - 1];
    return lround(dt * 1000.0) > lg.gap_ms;
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

float lg_time_colf(int i)
{
    if (timed()) {
        return (s.data.time[i] - lg.t0) / (lg.t1 - lg.t0)
               * (float)(PV_W - 1);
    }
    return (s.win_n > 1) ? (float)(i - s.win0) * (float)(PV_W - 1)
                               / (float)(s.win_n - 1)
                         : 0.0f;
}

int lg_time_col(int i)
{
    const int last = s.win0 + s.win_n - 1;
    if (i < s.win0 || i > last) {
        return -1;
    }
    const long c = lroundf(lg_time_colf(i));
    return (c < 0 || c > PV_W - 1) ? -1 : (int)c;
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

bool lg_time_gestures(void)
{
    return lg_time_on() && lg.has_time && s.data.count > 1
           && s.data.time[s.data.count - 1] > s.data.time[0];
}

float lg_time_at(int x)
{
    int rel = x - PV_X;
    rel = (rel < 0) ? 0 : (rel > PV_W - 1) ? PV_W - 1 : rel;
    return lg.t0 + (lg.t1 - lg.t0) * (float)rel / (float)(PV_W - 1);
}

/**
 * @brief The first row whose time is at or after @p t.
 * @param t  The time.
 * @return the row; s.data.count - 1 past the run's end.
 */
static int row_from(float t)
{
    int a = 0;
    int b = s.data.count - 1;
    while (a < b) {
        const int m = a + (b - a) / 2;
        if (s.data.time[m] < t) {
            a = m + 1;
        } else {
            b = m;
        }
    }
    return a;
}

void lg_time_window(float t_lo, float t_hi)
{
    const int n = s.data.count;
    const float first = s.data.time[0];
    const float end = s.data.time[n - 1];
    float span = t_hi - t_lo;
    span = (span > end - first) ? end - first : span;
    t_lo = (t_lo < first) ? first : (t_lo > end - span) ? end - span : t_lo;
    t_hi = t_lo + span;
    /* The rows that bracket the range, so the lines reach its edges. */
    int a = row_from(t_lo);
    if (a > 0 && s.data.time[a] > t_lo) {
        --a;
    }
    int b = row_from(t_hi);
    const int min_n = (n < PV_MIN_WIN) ? n : PV_MIN_WIN;
    lg.req = true;
    while (b - a + 1 < min_n) {
        /* Narrower than PV_MIN_WIN rows: the rows' own times. */
        lg.req = false;
        if (b < n - 1) {
            ++b;
        }
        if (b - a + 1 < min_n && a > 0) {
            --a;
        }
    }
    s.win0 = a;
    s.win_n = b - a + 1;
    lg.req_lo = t_lo;
    lg.req_hi = t_hi;
    lg.req_w0 = s.win0;
    lg.req_n = s.win_n;
    lv_compute_spans();
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
    bool have = false;
    float pcf = 0.0f;
    float py = 0.0f;
    for (int x = 0; x < PV_W; ++x) {
        lv_plot.span[k][x][0] = PV_EMPTY;
        lv_plot.span[k][x][1] = PV_EMPTY;
    }
    for (int i = s.win0; i <= last; ++i) {
        if (!isfinite(v[i])) {
            have = false;
            continue;
        }
        const float cf = lg_time_colf(i);
        const float y = (float)lg_y_px(k, v[i]);
        const int c = lg_time_col(i);
        if (c >= 0) {
            widen(k, c, (int)y);
        }
        if (have && cf > pcf && !lg_time_gap_before(i)) {
            /* The line from the row before, over the columns on the plot. */
            const int x0 = (pcf < 0.0f) ? 0 : (int)ceilf(pcf);
            const int x1 = (cf > (float)(PV_W - 1)) ? PV_W - 1
                                                    : (int)floorf(cf);
            for (int x = x0; x <= x1; ++x) {
                widen(k, x, (int)lroundf(py + (y - py) * ((float)x - pcf)
                                                  / (cf - pcf)));
            }
        }
        have = true;
        pcf = cf;
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
    float pcf = 0.0f;
    for (int i = s.win0; i <= last; ++i) {
        const float cf = lg_time_colf(i);
        const int c = lg_time_col(i);
        if (c >= 0) {
            lg.col[c] |= LG_COL_ROW;
        }
        if (i > s.win0 && lg_time_gap_before(i)) {
            /* The columns strictly between the two rows, on the plot. */
            const int a = (int)lroundf(pcf);
            const int b = (int)lroundf(cf);
            for (int x = (a + 1 < 0) ? 0 : a + 1; x < b && x < PV_W; ++x) {
                lg.col[x] |= LG_COL_GAP;
            }
            if (c >= 0 && b > a) {
                lg.col[c] |= LG_COL_BREAK;
            }
        }
        pcf = cf;
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
    uint8_t held = LOG_STRIP_NONE;
    for (int i = s.win0; i <= last; ++i) {
        const int c = lg_time_col(i);
        if (c >= 0 && c != pc) {
            out[c] = (uint8_t)log_marks_strip_code(&lg.mk, k, i);
            pc = c;
        } else if (c < 0 && lg_time_colf(i) < 0.0f) {
            /* A row left of the plot: its code holds into the first
             * columns. */
            held = (uint8_t)log_marks_strip_code(&lg.mk, k, i);
        }
    }
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
    if (lg.req && lg.req_w0 == s.win0 && lg.req_n == s.win_n) {
        /* The range two fingers asked for. */
        lg.t0 = lg.req_lo;
        lg.t1 = lg.req_hi;
    } else {
        lg.req = false;
        lg.t0 = s.data.time[s.win0];
        lg.t1 = s.data.time[last];
    }
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
