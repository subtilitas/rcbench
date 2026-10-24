/**
 * @file panel_perf.h
 * @brief The console's measurements of the panel itself: PSRAM
 *        (pseudo-static RAM) speed at start-up, the frame times, and every
 *        task's stack and the internal heap at their lowest.
 *
 * Built with the option RCBENCH_PERF_LOG, ON by default
 * (firmware/panel/CMakeLists.txt).  Built without it,
 *
 *     idf.py -D RCBENCH_PERF_LOG=OFF build
 *
 * every call below is an empty inline function, panel_perf.c is not
 * compiled, and the panel prints none of these lines and runs none of
 * their code.  docs/Performance.md names the lines and the tables they
 * fill.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief The clears the start-up probe times, after one that is not
 *         timed. */
#define PERF_PROBE_CLEARS 4u

#if RCBENCH_PERF_LOG

#include "esp_timer.h"

/**
 * @brief The time a frame's DRAW is measured with.
 * @return Microseconds since start-up, cut to 32 bits; a difference of two
 *         is right across the wrap.
 */
static inline uint32_t perf_now_us(void)
{
    return (uint32_t)esp_timer_get_time();
}

/**
 * @brief Time PERF_PROBE_CLEARS full clears of the back buffer and log the
 *        time of one.
 *
 * Called once, after display_init() and before anything is drawn: the
 * scan-out reads the front buffer while the probe writes the back one, and
 * no screen has painted chrome into it that the clears would leave stale.
 * 5 clears of 1,228,800B each, on the splash's time; how long they take is
 * what the probe measures.
 */
void perf_probe_psram(void);

/**
 * @brief Account one frame and print the console line due after it, at
 *        most one (perf_line_due()).
 *
 * Every PERF_WINDOW_FRAMES frames the rate line and the note of the
 * internal heap's lowest free figure; on the next frame the window line;
 * on the frames between, one line for the stack scan's time, the heap or a
 * stack whose figure fell, while one is due.  Every 6th rate line wants a
 * scan of every task's stack high-water mark, the first at the first rate
 * line; it runs on the first rate line after that with @p quiet set.
 * Called on the render task after each display_flip().
 *
 * @param screen         The screen the frame drew (ui_screen_id_t).
 * @param draw_us        The frame's paint time, us (perf_now_us()).
 * @param flip_timed_out Whether its display_flip() returned
 *                       ESP_ERR_TIMEOUT.
 * @param quiet          Whether the stack scan may run: flash_gate_open()'s
 *                       rule, the bench not armed and no arm waiting, the
 *                       supply's output off and no ON queued.  The scan
 *                       holds the kernel lock, and the control task on the
 *                       other core waits at its next kernel call.
 */
void perf_frame(int screen, uint32_t draw_us, bool flip_timed_out,
                bool quiet);

#else

/**
 * @brief Nothing: built without RCBENCH_PERF_LOG.
 * @return 0.
 */
static inline uint32_t perf_now_us(void)
{
    return 0u;
}

/** @brief Nothing: built without RCBENCH_PERF_LOG. */
static inline void perf_probe_psram(void)
{
}

/**
 * @brief Nothing: built without RCBENCH_PERF_LOG.
 * @param screen         Unused.
 * @param draw_us        Unused.
 * @param flip_timed_out Unused.
 * @param quiet          Unused.
 */
static inline void perf_frame(int screen, uint32_t draw_us,
                              bool flip_timed_out, bool quiet)
{
    (void)screen;
    (void)draw_us;
    (void)flip_timed_out;
    (void)quiet;
}

#endif
