/*
 * A multi-series plot on one time base, with a scale per series.
 *
 * Voltage, current, power and rpm (revolutions per minute) differ by three
 * orders of magnitude and cannot share a Y axis.  Tick labels for four scales
 * would take a third of the plot width, so the scales are shown two ways: the
 * legend prints each series' full scale beside its name, and the focused
 * series prints its full scale and zero inside the plot in its own colour.
 *
 * State lives in the caller's struct rather than in file statics, so the
 * motor bench, the servo bench and the analyser each own a plot.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_PLOT_MAX_SERIES 6
/** One sample per pixel column at the widest the panel can show, plus slack. */
#define UI_PLOT_HISTORY    800

typedef struct {
    const char *name;
    const char *unit;
    gfx_color_t color;
    int         decimals;
    /** Lower bound of the autorange: a 0 to 0.4 A scale on an idle bench
     *  draws noise at full height. */
    float       floor;
} ui_plot_series_t;

typedef struct {
    ui_plot_series_t series[UI_PLOT_MAX_SERIES];
    int   count;

    float ring[UI_PLOT_MAX_SERIES][UI_PLOT_HISTORY];
    int   head;      /**< where the next sample goes                       */
    int   filled;    /**< how many are valid, capped at UI_PLOT_HISTORY    */

    float scale[UI_PLOT_MAX_SERIES];
    int   shrink_hold[UI_PLOT_MAX_SERIES];
    bool  hidden[UI_PLOT_MAX_SERIES];
    int   focus;     /**< index, or -1                                     */

    float span_s;    /**< the width of the time base, for the axis label   */

    /**
     * Samples pushed since init.
     *
     * A screen keeps the count it last drew into each framebuffer and skips
     * the plot when it has not moved.  The panel refreshes at 39 Hz and
     * samples arrive at 20 Hz, so about half of all frames would otherwise
     * repaint an identical 762 x 212 px region into the PSRAM (pseudo-static
     * random-access memory) the LCD (liquid-crystal display) scans out of.
     */
    uint32_t pushes;
} ui_plot_t;

/**
 * Round @p v up to 1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8 or 10 times a power of
 * ten.
 *
 * The finer steps keep a 6S pack (25.2 V) on a 0 to 30 V scale rather than
 * the 0 to 50 V a 1/2/5 ladder gives.
 */
float ui_plot_nice_ceil(float v);

void ui_plot_init(ui_plot_t *p, const ui_plot_series_t *series, int count,
                  float span_s);

/** Push one sample per series, newest last.  @p values must have `count`. */
void ui_plot_push(ui_plot_t *p, const float *values);

/** Newest is @p back = 0.  Returns 0 for samples that have not arrived. */
float ui_plot_sample(const ui_plot_t *p, int series, int back);

/**
 * Recompute the scales.
 *
 * A scale grows immediately and shrinks only after 60 consecutive samples
 * (3 s at 20 Hz) below it, so a brief spike does not rescale the plot twice.
 */
void ui_plot_update_scales(ui_plot_t *p, int visible_samples);

/** Tap a legend chip: focus it, or hide it if it was already focused. */
void ui_plot_touch_series(ui_plot_t *p, int series);

/** Where @p value lands inside a plot @p h tall.  Clamped to the box. */
int ui_plot_map_y(const ui_plot_t *p, int series, float value, int y0, int h);

/** The plot body: grid, traces, the focused series' scale. */
void ui_plot_render(const ui_plot_t *p, gfx_canvas_t *c, gfx_rect_t r);

/** A row of swatch + name + full scale, one entry per series. */
void ui_plot_render_legend(const ui_plot_t *p, gfx_canvas_t *c, gfx_rect_t r);

#ifdef __cplusplus
}
#endif
