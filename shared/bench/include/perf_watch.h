/**
 * @file
 * @brief What the panel's console reports about its own frame times, stacks
 *        and heap: the statistics behind each line, without the clock, the
 *        tasks or the log.
 *
 * Two parts, both pure and both run in the host suite:
 *
 * - The frame window: DRAW and WAIT, minimum, mean and maximum, over the
 *   frames drawn on one screen since the last console line.  A frame on
 *   another screen restarts the window with that screen, so a line names
 *   one screen and covers fewer than PERF_WINDOW_FRAMES frames when the
 *   screen changed inside it.
 * - The table of lows: for each name (a task, or the internal heap), the
 *   least free stack or heap noted so far.  A note reports whether the name
 *   is new or its figure fell, so the console prints a task only when its
 *   high-water mark moves.  Two tasks of one name share a row and keep the
 *   lower figure.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Frames between two console lines: 5 s at the panel's 60.38 Hz. */
#define PERF_WINDOW_FRAMES 300u

/** @brief Rows in the table of lows: tasks, the internal heap, and room. */
#define PERF_LOWS_MAX      48u

/** @brief Bytes of a name in the table, the terminator included; FreeRTOS's
 *         configMAX_TASK_NAME_LEN in ESP-IDF. */
#define PERF_LOWS_NAME     16u

/** @brief The frames drawn on one screen since the window started. */
typedef struct {
    int      screen;        /**< the screen the frames were drawn on     */
    uint32_t frames;        /**< frames added since the window started   */
    uint32_t draw_min_us;   /**< shortest DRAW, us; 0 with no frame      */
    uint32_t draw_max_us;   /**< longest DRAW, us                        */
    uint32_t wait_min_us;   /**< shortest WAIT, us; 0 with no frame      */
    uint32_t wait_max_us;   /**< longest WAIT, us                        */
    uint64_t draw_sum_us;   /**< every DRAW added, us                    */
    uint64_t wait_sum_us;   /**< every WAIT added, us                    */
} perf_window_t;

/**
 * @brief Start an empty window on @p screen.
 *
 * @param w      The window.
 * @param screen The screen its frames are drawn on.
 */
void perf_window_start(perf_window_t *w, int screen);

/**
 * @brief Add one frame.
 *
 * @param w       The window.
 * @param screen  The screen the frame was drawn on; another than the
 *                window's starts the window again on it, with this frame.
 * @param draw_us The frame's paint time, us.
 * @param wait_us How long the flip after it blocked, us.
 */
void perf_window_add(perf_window_t *w, int screen, uint32_t draw_us,
                     uint32_t wait_us);

/**
 * @brief The mean DRAW of the window, rounded to the nearest us.
 *
 * @param w The window.
 * @return The mean, us; 0 with no frame.
 */
uint32_t perf_window_draw_mean_us(const perf_window_t *w);

/**
 * @brief The mean WAIT of the window, rounded to the nearest us.
 *
 * @param w The window.
 * @return The mean, us; 0 with no frame.
 */
uint32_t perf_window_wait_mean_us(const perf_window_t *w);

/** @brief One name's least free bytes. */
typedef struct {
    char     name[PERF_LOWS_NAME];  /**< the name, cut to 15 bytes        */
    uint32_t least;                 /**< the least free bytes noted, B    */
} perf_low_t;

/** @brief The least free figure of every name noted. */
typedef struct {
    perf_low_t rows[PERF_LOWS_MAX]; /**< the names noted, in that order   */
    uint32_t   count;               /**< rows in use                      */
    uint32_t   dropped;             /**< notes of a new name with no row  */
} perf_lows_t;

/**
 * @brief Empty the table.
 *
 * @param t The table.
 */
void perf_lows_init(perf_lows_t *t);

/**
 * @brief Note @p least free bytes for @p name.
 *
 * @param t     The table.
 * @param name  The task's name, or another the caller chooses; cut to
 *              PERF_LOWS_NAME - 1 bytes.  NULL is noted as "".
 * @param least The least free bytes now.
 * @return true when the name is new or @p least is below its last figure,
 *         which it then becomes; false when it held or rose, and for a new
 *         name when every row is in use, which counts in @c dropped.
 */
bool perf_lows_note(perf_lows_t *t, const char *name, uint32_t least);

#ifdef __cplusplus
}
#endif
