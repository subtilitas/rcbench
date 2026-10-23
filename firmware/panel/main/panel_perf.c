/**
 * @file panel_perf.c
 * @brief The PSRAM probe at start-up and the lows of every stack and of the
 *        internal heap (panel_perf.h).
 *
 * The lines, as docs/Performance.md quotes them:
 *
 *     PSRAM probe: 4 clears of 1228800 B in <n> us, <n> us each
 *     stack <task>: <n> B never used
 *     internal RAM: <n> B free at the lowest, <n> B free now, largest block <n> B
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_perf.h"

#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "gfx.h"
#include "perf_watch.h"

/** @brief The log tag of the panel's own lines. */
static const char *TAG = "rcbench";

/** @brief Every task's least free stack, as last logged. */
static perf_lows_t   s_stacks;
/** @brief The internal heap's lowest free figure, as last logged, B. */
static uint32_t      s_heap_low = UINT32_MAX;
/** @brief The task list uxTaskGetSystemState() fills, in PSRAM. */
static TaskStatus_t *s_tasks;
/** @brief Room in s_tasks, in tasks. */
static UBaseType_t   s_task_room;

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

void perf_report_lows(void)
{
    if (s_stacks.count == 0u) {
        perf_lows_init(&s_stacks);
    }
    const UBaseType_t room = task_room();
    const UBaseType_t got =
        (room != 0u) ? uxTaskGetSystemState(s_tasks, room, NULL) : 0u;
    for (UBaseType_t i = 0; i < got; ++i) {
        const uint32_t least = (uint32_t)s_tasks[i].usStackHighWaterMark;
        if (perf_lows_note(&s_stacks, s_tasks[i].pcTaskName, least)) {
            ESP_LOGI(TAG, "stack %s: %u B never used",
                     s_tasks[i].pcTaskName, (unsigned)least);
        }
    }
    if (s_stacks.dropped != 0u) {
        ESP_LOGW(TAG, "stack: %u notes of tasks with no row",
                 (unsigned)s_stacks.dropped);
        s_stacks.dropped = 0u;
    }
    const uint32_t low =
        (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    if (low < s_heap_low) {
        s_heap_low = low;
        ESP_LOGI(TAG, "internal RAM: %u B free at the lowest, %u B free now, "
                 "largest block %u B", (unsigned)low,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL));
    }
}
