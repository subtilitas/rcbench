/**
 * @file panel_perf.c
 * @brief The PSRAM probe at start-up, the frame times, and the lows of
 *        every stack and of the internal heap (panel_perf.h).
 *
 * Compiled with RCBENCH_PERF_LOG only.  The lines, as docs/Performance.md
 * quotes them:
 *
 *     PSRAM probe: 4 clears of 1228800B in <n> us, <n> us each
 *     <f> fps  DRAW <n> us  WAIT <n> us  TURN <n> us  VSYNC <f> Hz  FLIP TIMEOUTS <n>  TOUCHLOST <n>/<n>
 *     frames <n> on <title> #<id>: DRAW <min>/<mean>/<max> WAIT <...> TURN <...> us
 *     stack scan: <n> tasks in <n> us
 *     stack <task>: <n> B never used
 *     internal RAM: <n> B free at the lowest, <n> B free now, largest block <n> B
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_perf.h"

#include <stdatomic.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "gfx.h"
#include "panel.h"
#include "perf_watch.h"
#include "ui_screen.h"

/** @brief The log tag of the panel's own lines. */
static const char *TAG = "rcbench";

/** @brief Rate lines between two stack scans: 30s at 60.4fps. */
#define PERF_SCAN_WINDOWS 6u

/** @brief Every task's least free stack, as last noted. */
static perf_lows_t   s_stacks;
/** @brief The internal heap's lowest free figure, as last noted, B. */
static uint32_t      s_heap_low = UINT32_MAX;
/** @brief Whether the heap line is due. */
static bool          s_heap_due;
/** @brief The longest stack scan so far, us; its line is due when it grows. */
static uint32_t      s_scan_us_max;
/** @brief Tasks the longest scan read; 0 while its line is not due. */
static UBaseType_t   s_scan_due_tasks;
/** @brief Whether a stack scan waits for a frame it may run on. */
static bool          s_scan_wanted = true;
/** @brief Rate lines since the last stack scan. */
static uint32_t      s_since_scan;
/** @brief The task list uxTaskGetSystemState() fills, in PSRAM. */
static TaskStatus_t *s_tasks;
/** @brief Room in s_tasks, in tasks. */
static UBaseType_t   s_task_room;

/** @brief Whether perf_frame() has seen its first frame. */
static bool          s_started;
/** @brief Frames since the loop started. */
static uint32_t      s_frames;
/** @brief The flips since start-up that timed out. */
static uint32_t      s_flip_timeouts;
/** @brief VSYNC count and time at the last rate line. */
static uint32_t      s_last_vs;
/** @brief The time of s_last_vs, us. */
static uint32_t      s_last_vs_us;
/** @brief The frames since the last rate line, on one screen. */
static perf_window_t s_window;
/** @brief The window the last rate line closed, for the window line; no
 *         frames when none waits. */
static perf_window_t s_closed;

/** @brief Time the clears of the back buffer and log one's time
 *         (panel_perf.h). */
void perf_probe_psram(void)
{
    gfx_canvas_t *c = display_canvas();
    if (c == NULL) {
        return;
    }
    const uint32_t bytes =
        (uint32_t)c->stride * (uint32_t)c->height * sizeof(gfx_color_t);
    /* One clear untimed: the cache then holds the buffer's last lines, as
     * it does between the timed ones. */
    gfx_clear(c, 0);
    const int64_t t0 = esp_timer_get_time();
    for (uint32_t i = 0; i < PERF_PROBE_CLEARS; ++i) {
        gfx_clear(c, (gfx_color_t)(0x1234u + i));
    }
    const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    /* What the clears leave the splash's first frame clears away. */
    ESP_LOGI(TAG, "PSRAM probe: %u clears of %u B in %u us, %u us each",
             (unsigned)PERF_PROBE_CLEARS, (unsigned)bytes, (unsigned)us,
             (unsigned)((us + PERF_PROBE_CLEARS / 2u) / PERF_PROBE_CLEARS));
}

/**
 * @brief Room in s_tasks for every task now and 8 more, from PSRAM.
 *
 * @return The room, in tasks; 0 when the heap has none.
 */
static UBaseType_t task_room(void)
{
    const UBaseType_t want = uxTaskGetNumberOfTasks() + 8u;
    if (s_tasks != NULL && s_task_room >= want) {
        return s_task_room;
    }
    heap_caps_free(s_tasks);
    s_tasks = heap_caps_malloc(want * sizeof(*s_tasks), MALLOC_CAP_SPIRAM);
    s_task_room = (s_tasks != NULL) ? want : 0u;
    return s_task_room;
}

/**
 * @brief Read every task's stack high-water mark into the table of lows.
 *
 * uxTaskGetSystemState() reads all of them in one critical section: this
 * core's interrupts are masked and the kernel lock is held while it scans
 * each task's unused stack, so a kernel call on the other core waits for
 * it.  The time is logged; the scan runs on a quiet frame only.
 */
static void scan_stacks(void)
{
    if (s_stacks.count == 0u) {
        perf_lows_init(&s_stacks);
    }
    const UBaseType_t room = task_room();
    if (room == 0u) {
        return;
    }
    const int64_t t0 = esp_timer_get_time();
    const UBaseType_t got = uxTaskGetSystemState(s_tasks, room, NULL);
    const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    if (us > s_scan_us_max) {
        s_scan_us_max = us;
        s_scan_due_tasks = (got != 0u) ? got : 1u;
    }
    for (UBaseType_t i = 0; i < got; ++i) {
        (void)perf_lows_note(&s_stacks, s_tasks[i].pcTaskName,
                             (uint32_t)s_tasks[i].usStackHighWaterMark);
    }
}

/** @brief Note the internal heap's lowest free figure; its line is due when
 *         it fell. */
static void note_heap(void)
{
    const uint32_t low =
        (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    if (low < s_heap_low) {
        s_heap_low = low;
        s_heap_due = true;
    }
}

/**
 * @brief Print the rate line: fps, the last frame's DRAW, WAIT and TURN,
 *        the panel's VSYNC rate since the last one, and the counts.
 *
 * @param draw_us The last frame's paint time, us.
 */
static void rate_line(uint32_t draw_us)
{
    const uint32_t vs = display_vsync_count();
    const uint32_t vs_us = perf_now_us();
    const float vsync_hz =
        (vs_us != s_last_vs_us)
            ? (float)(vs - s_last_vs) * 1e6f / (float)(vs_us - s_last_vs_us)
            : 0.0f;
    s_last_vs = vs;
    s_last_vs_us = vs_us;
    ESP_LOGI(TAG,
             "%.1f fps  DRAW %u us  WAIT %u us  TURN %u us  "
             "VSYNC %.2f Hz  FLIP TIMEOUTS %u  TOUCHLOST %u/%u",
             (double)display_fps(), (unsigned)draw_us,
             (unsigned)display_last_wait_us(),
             (unsigned)display_last_turn_us(), (double)vsync_hz,
             (unsigned)s_flip_timeouts, s_tq_lost,
             atomic_load(&s_drv_gaps));
}

/** @brief Print the window the last rate line closed, if one waits. */
static void window_line(void)
{
    const perf_window_t *w = &s_closed;
    if (w->frames == 0u) {
        return;
    }
    const char *title = ui_router_title((ui_screen_id_t)w->screen);
    ESP_LOGI(TAG,
             "frames %u on %s #%d: DRAW %u/%u/%u WAIT %u/%u/%u "
             "TURN %u/%u/%u us",
             (unsigned)w->frames, (title != NULL) ? title : "-", w->screen,
             (unsigned)w->draw.min_us,
             (unsigned)perf_span_mean_us(&w->draw, w->frames),
             (unsigned)w->draw.max_us, (unsigned)w->wait.min_us,
             (unsigned)perf_span_mean_us(&w->wait, w->frames),
             (unsigned)w->wait.max_us, (unsigned)w->turn.min_us,
             (unsigned)perf_span_mean_us(&w->turn, w->frames),
             (unsigned)w->turn.max_us);
    s_closed.frames = 0u;
}

/** @brief Print one due line of the lows: the scan's time, then the heap,
 *         then the first stack due; nothing when none is due. */
static void low_line(void)
{
    if (s_scan_due_tasks != 0u) {
        ESP_LOGI(TAG, "stack scan: %u tasks in %u us",
                 (unsigned)s_scan_due_tasks, (unsigned)s_scan_us_max);
        s_scan_due_tasks = 0u;
        return;
    }
    if (s_heap_due) {
        s_heap_due = false;
        ESP_LOGI(TAG, "internal RAM: %u B free at the lowest, %u B free now, "
                 "largest block %u B", (unsigned)s_heap_low,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL));
        return;
    }
    const int row = perf_lows_take(&s_stacks);
    if (row >= 0) {
        ESP_LOGI(TAG, "stack %s: %u B never used", s_stacks.rows[row].name,
                 (unsigned)s_stacks.rows[row].least);
        return;
    }
    if (s_stacks.dropped != 0u) {
        ESP_LOGW(TAG, "stack: %u notes of tasks with no row",
                 (unsigned)s_stacks.dropped);
        s_stacks.dropped = 0u;
    }
}

/**
 * @brief Account one frame and print the line due after it (panel_perf.h).
 *
 * @param screen         The screen the frame drew.
 * @param draw_us        The frame's paint time, us.
 * @param flip_timed_out Whether its flip timed out.
 * @param quiet          Whether the stack scan may run.
 */
void perf_frame(int screen, uint32_t draw_us, bool flip_timed_out,
                bool quiet)
{
    if (!s_started) {
        s_started = true;
        s_last_vs = display_vsync_count();
        s_last_vs_us = perf_now_us();
        perf_window_start(&s_window, screen);
    }
    if (flip_timed_out) {
        ++s_flip_timeouts;
    }
    perf_window_add(&s_window, screen, draw_us, display_last_wait_us(),
                    display_last_turn_us());
    switch (perf_line_due(++s_frames)) {
    case PERF_LINE_RATE:
        rate_line(draw_us);
        s_closed = s_window;
        perf_window_start(&s_window, screen);
        note_heap();
        if (++s_since_scan >= PERF_SCAN_WINDOWS) {
            s_scan_wanted = true;
        }
        if (s_scan_wanted && quiet) {
            scan_stacks();
            s_scan_wanted = false;
            s_since_scan = 0u;
        }
        break;
    case PERF_LINE_WINDOW:
        window_line();
        break;
    case PERF_LINE_LOW:
    default:
        low_line();
        break;
    }
}
