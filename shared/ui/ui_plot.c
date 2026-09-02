/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_plot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

/* Samples a scale stays above its target before it shrinks: 3 s at 20 Hz.
 * A shorter hold makes a single spike rescale the plot twice. */
#define SHRINK_HOLD 60

/* Headroom above the peak, so the trace does not ride the top edge. */
#define HEADROOM 1.12f

float ui_plot_nice_ceil(float v)
{
    if (!(v > 0.0f)) {
        return 1.0f;
    }
    static const float steps[] = {
        1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f
    };
    /* cppcheck-suppress invalidFunctionArg
     * v is positive here: the guard above returns for anything that is not,
     * NaN (not a number) included, which is why it is written !(v > 0) and
     * not (v <= 0). */
    const float mag = powf(10.0f, floorf(log10f(v)));
    const float n = v / mag;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        if (n <= steps[i] + 1e-4f) {
            return steps[i] * mag;
        }
    }
    return 10.0f * mag;
}

void ui_plot_init(ui_plot_t *p, const ui_plot_series_t *series, int count,
                  float span_s)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    if (count > UI_PLOT_MAX_SERIES) {
        count = UI_PLOT_MAX_SERIES;
    }
    p->count  = count;
    p->span_s = span_s;
    p->focus  = -1;
    for (int i = 0; i < count; ++i) {
        p->series[i]      = series[i];
        p->scale[i]       = series[i].floor > 0.0f ? series[i].floor : 1.0f;
        p->shrink_hold[i] = SHRINK_HOLD;
    }
}

void ui_plot_push(ui_plot_t *p, const float *values)
{
    if (p == NULL || values == NULL) {
        return;
    }
    for (int k = 0; k < p->count; ++k) {
        /* A non-finite reading is stored as zero, so it cannot poison the
         * scale; the trace dips to zero at that sample. */
        const float v = values[k];
        p->ring[k][p->head] = isfinite(v) ? v : 0.0f;
    }
    ++p->pushes;
    p->head = (p->head + 1) % UI_PLOT_HISTORY;
    if (p->filled < UI_PLOT_HISTORY) {
        ++p->filled;
    }
}

float ui_plot_sample(const ui_plot_t *p, int series, int back)
{
    if (p == NULL || series < 0 || series >= p->count
        || back < 0 || back >= p->filled) {
        return 0.0f;
    }
    int idx = p->head - 1 - back;
    while (idx < 0) {
        idx += UI_PLOT_HISTORY;
    }
    return p->ring[series][idx];
}

void ui_plot_update_scales(ui_plot_t *p, int visible_samples)
{
    if (p == NULL) {
        return;
    }
    int n = (p->filled < visible_samples) ? p->filled : visible_samples;
    if (n < 0) {
        n = 0;
    }
    for (int k = 0; k < p->count; ++k) {
        float mx = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float v = ui_plot_sample(p, k, i);
            if (v > mx) {
                mx = v;
            }
        }
        float target = ui_plot_nice_ceil(mx * HEADROOM);
        if (target < p->series[k].floor) {
            target = p->series[k].floor;
        }

        if (target > p->scale[k]) {
            p->scale[k] = target;                 /* grow at once */
            p->shrink_hold[k] = SHRINK_HOLD;
        } else if (target < p->scale[k]) {
            if (--p->shrink_hold[k] <= 0) {
                p->scale[k] = target;             /* shrink after the hold */
                p->shrink_hold[k] = SHRINK_HOLD;
            }
        } else {
            p->shrink_hold[k] = SHRINK_HOLD;
        }
    }
}

void ui_plot_touch_series(ui_plot_t *p, int series)
{
    if (p == NULL || series < 0 || series >= p->count) {
        return;
    }
    if (p->hidden[series]) {
        p->hidden[series] = false;   /* bring it back first */
        p->focus = series;
        return;
    }
    if (p->focus == series) {
        p->hidden[series] = true;    /* focused already: the next tap hides */
        p->focus = -1;
        return;
    }
    p->focus = series;
}

int ui_plot_map_y(const ui_plot_t *p, int series, float value, int y0, int h)
{
    if (p == NULL || series < 0 || series >= p->count || h <= 1) {
        return y0;
    }
    const float scale = p->scale[series] > 0.0f ? p->scale[series] : 1.0f;
    float frac = value / scale;
    if (frac < 0.0f) { frac = 0.0f; }
    if (frac > 1.0f) { frac = 1.0f; }
    return y0 + (int)((1.0f - frac) * (float)(h - 1) + 0.5f);
}

/* ----------------------------------------------------------------- drawing */

#define RAIL_W     6
#define RAIL_PITCH 10
#define TICKS      8

/*
 * The channel legend: a swatch, a name, and the full scale of that channel.
 * Each series has its own scale, so the legend is where the range of each
 * trace is stated.
 */
void ui_plot_render_legend(const ui_plot_t *p, gfx_canvas_t *c, gfx_rect_t r)
{
    if (p == NULL || c == NULL || p->count <= 0) {
        return;
    }
    const int slot = r.w / p->count;
    for (int k = 0; k < p->count; ++k) {
        const bool off = p->hidden[k];
        const gfx_color_t col = off ? ui_theme_color(UI_C_TEXT_FAINT)
                                    : p->series[k].color;
        const int x = r.x + k * slot;
        const int cy = r.y + r.h / 2;

        gfx_fill_round_rect(c, x, cy - 1, 12, 3, 1, col);

        int tx = x + 18;
        tx += gfx_text(c, tx, cy - 8, p->series[k].name, UI_FONT_LABEL,
                       off ? ui_theme_color(UI_C_TEXT_FAINT)
                           : ui_theme_color(UI_C_TEXT_DIM), 1);

        /* The full scale, which the trace's height is drawn against. */
        char top[24];
        ui_fmt(top, sizeof(top), p->scale[k], p->series[k].decimals);
        char full[32];
        snprintf(full, sizeof(full), " %s %s", top, p->series[k].unit);
        gfx_text(c, tx, cy - 8, full, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}

void ui_plot_render(const ui_plot_t *p, gfx_canvas_t *c, gfx_rect_t r)
{
    if (p == NULL || c == NULL || r.w <= 2 || r.h <= 2) {
        return;
    }

    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL_SUNK));

    /* Horizontal grid lines only.  A full-height vertical line costs about
     * 480 cache-line fills on this panel (17 lines measure 8,160) and the
     * same pixels drawn horizontally cost none, so the time axis is labelled
     * rather than ruled. */
    for (int t = 1; t < TICKS; ++t) {
        const int y = r.y + (r.h - 1) * t / TICKS;
        gfx_fill_rect(c, r.x, y, r.w, 1, ui_theme_color(UI_C_GRID));
    }

    const int cols = (r.w < UI_PLOT_HISTORY) ? r.w : UI_PLOT_HISTORY;

    for (int k = 0; k < p->count; ++k) {
        if (p->hidden[k]) {
            continue;
        }
        const bool focused = (p->focus == k);
        const gfx_color_t col = p->series[k].color;

        int prev_y = -1;
        for (int i = 0; i < cols; ++i) {
            const int back = cols - 1 - i;
            if (back >= p->filled) {
                prev_y = -1;
                continue;
            }
            const int x = r.x + i;
            const int y = ui_plot_map_y(p, k, ui_plot_sample(p, k, back),
                                        r.y, r.h);
            if (prev_y >= 0) {
                /* Join to the previous column so a fast edge is a line
                 * rather than two unrelated dots. */
                const int lo = (prev_y < y) ? prev_y : y;
                const int hi = (prev_y < y) ? y : prev_y;
                gfx_fill_rect(c, x, lo, 1, hi - lo + 1, col);
            } else {
                gfx_fill_rect(c, x, y, 1, 1, col);
            }
            if (focused) {
                gfx_fill_rect(c, x, y + 1, 1, 1, col);   /* drawn thicker */
            }
            prev_y = y;
        }
    }

    if (p->focus >= 0 && p->focus < p->count && !p->hidden[p->focus]) {
        char top[24];
        snprintf(top, sizeof(top), "%.*f %s",
                 p->series[p->focus].decimals,
                 (double)p->scale[p->focus], p->series[p->focus].unit);
        gfx_text(c, r.x + 4, r.y + 3, top, &gfx_font_8x16,
                 p->series[p->focus].color, 1);
        /* Above the axis row at r.h - 19, so the zero label and the time
         * base do not overlap. */
        gfx_text(c, r.x + 4, r.y + r.h - 38, "0", &gfx_font_8x16,
                 p->series[p->focus].color, 1);
    }

    char axis[24];
    snprintf(axis, sizeof(axis), "-%.0fs", (double)p->span_s);
    gfx_text(c, r.x + 4, r.y + r.h - 19, axis, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 60),
                                 (int16_t)(r.y + r.h - 19), 56, 16 },
                "NOW", &gfx_font_8x16, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}
