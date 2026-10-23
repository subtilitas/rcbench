/**
 * @file panel_perf.h
 * @brief The console's measurements of the panel itself: PSRAM
 *        (pseudo-static RAM) speed at start-up, and every task's stack and
 *        the internal heap at their lowest.
 *
 * docs/Performance.md names the lines and the tables they fill.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/** @brief The clears the start-up probe times, after one that is not
 *         timed. */
#define PERF_PROBE_CLEARS 4u

/**
 * @brief Time PERF_PROBE_CLEARS full clears of the back buffer and log the
 *        time of one.
 *
 * Called once, after display_init() and before anything is drawn: the
 * scan-out reads the front buffer while the probe writes the back one, and
 * no screen has painted chrome into it that the clears would leave stale.
 * 5 clears of 1,228,800 B each, on the splash's time; how long they take
 * is what the probe measures.
 */
void perf_probe_psram(void);

/**
 * @brief Log every task whose stack's high-water mark fell since the last
 *        call, and the internal heap when its lowest free figure fell.
 *
 * The first call logs every task.  Called on the render task after the
 * frame line, every PERF_WINDOW_FRAMES frames.  Reading the marks scans
 * each task's unused stack with the scheduler suspended.
 */
void perf_report_lows(void);
