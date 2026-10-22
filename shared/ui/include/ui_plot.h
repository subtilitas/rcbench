/**
 * @file
 * @brief A multi-series plot on one time base, with a scale per series.
 *
 * Voltage, current, power and rpm (revolutions per minute) differ by three
 * orders of magnitude and cannot share a Y axis.  Tick labels for four scales
 * would take a third of the plot width, so the scales are shown two ways: the
 * legend prints each series' full scale beside its name, and the focused
 * series prints its full scale and zero inside the plot in its own colour.
 *
 * State lives in the caller's struct rather than in file statics, so a screen
 * that wants a plot owns one.  The motor, supply and servo screens do.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "ui_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The most series one plot draws. */
#define UI_PLOT_MAX_SERIES 6
/**
 * @brief The samples a series keeps: one per pixel column of a plot as
 *        wide as the panel, UI_PANEL_W, 1,024.  At 20 samples a second
 *        that is 51.2 s; a plot of the ring's 6 series takes 24,576 B for
 *        it.
 */
#define UI_PLOT_HISTORY    UI_PANEL_W

typedef struct {
    const char *name;
    const char *unit;
    gfx_color_t color;
    int         decimals;
    /** Lower bound of the autorange: a 0 to 0.4 A scale on an idle bench
     *  draws noise at full height. */
    float       floor;
    /**
     * 0 for a series with a scale of its own.  1 + the index of another
     * series for one drawn on that series' scale: a set point beside its
     * reading.  Such a follower is drawn dashed, takes no legend entry, is
     * hidden with its leader, and its samples count in the leader's
     * autorange, so it is never drawn off the top of the plot.
     */
    uint8_t     follows;
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
     * Whether the trace advances.
     *
     * A stopped plot holds what it has: ui_plot_push() counts the call and
     * drops the sample, and ui_plot_update_scales() leaves the scales where
     * they are.  Stopping is what makes the plot a record of something that
     * has ended rather than a window that slides over it.
     */
    bool running;

    /**
     * Calls to ui_plot_push(), whether or not the plot took the sample.
     *
     * A screen keeps the count it last drew into each framebuffer and skips
     * the readouts beside the plot when it has not moved.  The samples a
     * stopped plot declines are still new numbers for those, so this counts
     * the call: what the plot would draw is `revision`.
     */
    uint32_t pushes;

    /**
     * @brief Bumped when what ui_plot_render() would draw changes: an
     *        accepted push, a clear, or a change of running state.
     *
     * A screen keys the plot on this and the readouts on `pushes`.  The panel
     * refreshes at 60 Hz and samples arrive at 20 Hz, so about two frames
     * in three would otherwise repaint an identical plot region into the
     * PSRAM (pseudo-static random-access memory) the LCD (liquid-crystal
     * display) scans out of -- and a stopped plot would repaint every one of
     * them.
     */
    uint32_t revision;
} ui_plot_t;

/**
 * @brief Round @p v up to 1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8 or 10 times a
 *        power of ten.
 *
 * The finer steps keep a 6S pack (25.2 V) on a 0 to 30 V scale rather than
 * the 0 to 50 V a 1/2/5 ladder gives.
 * @param v The value.
 * @return The step at or above it.
 */
float ui_plot_nice_ceil(float v);

/**
 * @brief An empty, running plot of @p series.
 * @param p      The plot.
 * @param series The series, copied.
 * @param count  How many, at most UI_PLOT_MAX_SERIES.
 * @param span_s The width of the time base, for the axis label.
 */
void ui_plot_init(ui_plot_t *p, const ui_plot_series_t *series, int count,
                  float span_s);

/**
 * @brief Push one sample per series, newest last.
 *
 * A non-finite value is a gap: not drawn and not in the autorange.  A
 * stopped plot counts the call and drops the sample.
 * @param p      The plot.
 * @param values One value per series: `count` of them.
 */
void ui_plot_push(ui_plot_t *p, const float *values);

/**
 * @brief Start or stop the trace.  Stopping holds it; starting does not
 *        clear it.
 *
 * Two calls rather than one setter that also empties the ring: this widget
 * has no idea what a run is, and a start that silently wiped the samples
 * would be a trap for the next caller that wanted to resume one.
 * @param p       The plot.
 * @param running Whether the trace advances.
 */
void ui_plot_set_running(ui_plot_t *p, bool running);

/**
 * @brief Drop every sample and return each scale to its floor.  Keeps the
 *        series, the time base, the focus and what is hidden.
 * @param p The plot.
 */
void ui_plot_clear(ui_plot_t *p);

/**
 * @brief A sample of a series.
 * @param p      The plot.
 * @param series The series.
 * @param back   How far back: the newest is 0.
 * @return The sample; 0 for one that has not arrived or out of range, and
 *         not-a-number for a gap pushed as a non-finite value.
 */
float ui_plot_sample(const ui_plot_t *p, int series, int back);

/**
 * @brief Recompute the scales.
 *
 * A scale grows immediately and shrinks only after 60 consecutive samples
 * (3 s at 20 Hz) below it, so a brief spike does not rescale the plot twice.
 * @param p               The plot.
 * @param visible_samples The newest samples the plot shows.
 */
void ui_plot_update_scales(ui_plot_t *p, int visible_samples);

/**
 * @brief Tap a legend chip: focus it, or hide it if it was already focused.
 * @param p      The plot.
 * @param series The series of the chip.
 */
void ui_plot_touch_series(ui_plot_t *p, int series);

/**
 * @brief Where @p value lands inside a plot @p h tall.
 * @param p      The plot.
 * @param series The series whose scale it is on.
 * @param value  The value.
 * @param y0     The plot's top.
 * @param h      The plot's height.
 * @return The row, clamped to the box.
 */
int ui_plot_map_y(const ui_plot_t *p, int series, float value, int y0, int h);

/**
 * @brief The plot body: grid, traces, the focused series' scale.  One
 *        sample a pixel column, in the first UI_PLOT_HISTORY columns from
 *        the left edge, the newest in the last of them.
 * @param p The plot.
 * @param c Canvas.
 * @param r The plot's box.
 */
void ui_plot_render(const ui_plot_t *p, gfx_canvas_t *c, gfx_rect_t r);

/**
 * @brief A row of swatch + name + full scale, one entry per series.
 * @param p The plot.
 * @param c Canvas.
 * @param r The legend's row.
 */
void ui_plot_render_legend(const ui_plot_t *p, gfx_canvas_t *c, gfx_rect_t r);

#ifdef __cplusplus
}
#endif
