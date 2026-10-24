/**
 * @file
 * @brief What the panel's console reports about its own frame times, stacks
 *        and heap: the statistics behind each line and the frame each line
 *        is due on, without the clock, the tasks or the log.
 *
 * Three parts, all pure and all run in the host suite:
 *
 * - The frame window: DRAW, WAIT and TURN, minimum, mean and maximum, over
 *   the frames drawn on one screen since the last console line.  A frame
 *   on another screen restarts the window with that screen, so a line
 *   names one screen and covers fewer than PERF_WINDOW_FRAMES frames when
 *   the screen changed inside it.
 * - The table of lows: for each name (a task, or the internal heap), the
 *   least free stack or heap noted so far.  A note reports whether the name
 *   is new or its figure fell and marks the row due for a console line, so
 *   the console prints a task only when its high-water mark moves.  Two
 *   tasks of one name share a row and keep the lower figure.
 * - The schedule: at most one line a frame.  The console is UART0 at
 *   115,200 baud with no driver installed, so a line that does not fit the
 *   128-byte transmit FIFO (first in, first out) holds the render loop
 *   until the rest is sent, 87us a byte.  One line of at most 128 bytes a
 *   frame finds the FIFO empty: 128 bytes drain in 11.1ms, and a frame at
 *   60.4fps is 16.6ms.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Frames between two rate lines: 5.0s at 60.4fps, 9.9s at
 *         30.2fps. */
#define PERF_WINDOW_FRAMES 300u

/** @brief Rows in the table of lows: tasks, the internal heap, and room.
 *         At most 64, the bits of perf_lows_t::due. */
#define PERF_LOWS_MAX      48u

/** @brief Bytes of a name in the table, the terminator included; FreeRTOS's
 *         configMAX_TASK_NAME_LEN in ESP-IDF. */
#define PERF_LOWS_NAME     16u

/** @brief One figure's minimum, maximum and sum over a window. */
typedef struct {
    uint32_t min_us;        /**< the least added, us; 0 with none        */
    uint32_t max_us;        /**< the most added, us                      */
    uint64_t sum_us;        /**< every figure added, us                  */
} perf_span_t;

/** @brief The frames drawn on one screen since the window started. */
typedef struct {
    int         screen;     /**< the screen the frames were drawn on     */
    uint32_t    frames;     /**< frames added since the window started   */
    perf_span_t draw;       /**< the paint time of each frame            */
    perf_span_t wait;       /**< how long each flip blocked              */
    perf_span_t turn;       /**< the part of each WAIT spent turning     */
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
 * @param turn_us The part of @p wait_us Screen rotation 180° spent copying
 *                the frame turned, us; 0 in NORMAL.
 */
void perf_window_add(perf_window_t *w, int screen, uint32_t draw_us,
                     uint32_t wait_us, uint32_t turn_us);

/**
 * @brief The mean of one figure over @p frames, rounded to the nearest us.
 *
 * @param s      The figure's span.
 * @param frames The frames it was added over.
 * @return The mean, us; 0 with no frame.
 */
uint32_t perf_span_mean_us(const perf_span_t *s, uint32_t frames);

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
    uint64_t   due;                 /**< bit i: row i awaits its line     */
} perf_lows_t;

/**
 * @brief Empty the table.
 *
 * @param t The table.
 */
void perf_lows_init(perf_lows_t *t);

/**
 * @brief Copy a task's name into a row-sized buffer.
 *
 * Reads at most PERF_LOWS_NAME - 1 bytes of @p name and none past its
 * terminator, so a name without one is cut, not run past.
 *
 * @param dst  The buffer, PERF_LOWS_NAME bytes; always terminated.
 * @param name The name; NULL is copied as "".
 */
void perf_name_copy(char dst[PERF_LOWS_NAME], const char *name);

/**
 * @brief Note @p least free bytes for @p name.
 *
 * @param t     The table.
 * @param name  The task's name, or another the caller chooses; cut to
 *              PERF_LOWS_NAME - 1 bytes, of which no more are read
 *              (perf_name_copy()).  NULL is noted as "".
 * @param least The least free bytes now.
 * @return true when the name is new or @p least is below its last figure,
 *         which it then becomes, and the row is due for a line; false when
 *         it held or rose, and for a new name when every row is in use,
 *         which counts in @c dropped.
 */
bool perf_lows_note(perf_lows_t *t, const char *name, uint32_t least);

/**
 * @brief Take the first row due for a line.
 *
 * @param t The table.
 * @return The row's index, no longer due; -1 when none is due.
 */
int perf_lows_take(perf_lows_t *t);

/** @brief The console line a frame carries. */
typedef enum {
    PERF_LINE_RATE,         /**< fps, the last frame, VSYNC, the counts  */
    PERF_LINE_WINDOW,       /**< the window of the screen shown          */
    PERF_LINE_LOW,          /**< one row of the lows, if one is due      */
} perf_line_t;

/**
 * @brief The line due after frame @p frames.
 *
 * The rate line closes a window of PERF_WINDOW_FRAMES frames; the window
 * line follows on the next frame; every other frame may carry one row of
 * the lows.
 *
 * @param frames Frames drawn since the loop started, this one included.
 *               2^32 is not a multiple of 300: the window across the wrap
 *               is 196 frames long, 3.2s at 60.4fps after 2.25 years.
 * @return The line.
 */
perf_line_t perf_line_due(uint32_t frames);

#ifdef __cplusplus
}
#endif
