/*
 * Touch input for the Waveshare ESP32-S3-Touch-LCD-7.
 *
 * A background task samples the GT911, maps the coordinates into screen
 * space and publishes the result two ways:
 *
 *   - touch_snapshot(): the current contacts, never blocks.  A redraw loop
 *     polls it every frame.
 *   - touch_wait_event(): DOWN, MOVE and UP events off a queue, for code
 *     that wants edges rather than levels.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"

#include "touch_map.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    touch_rotation_t rotation;      /**< matches how the panel is mounted      */
    bool     mirror_x;
    bool     mirror_y;
    bool     use_interrupt;         /**< react to the GT911 INT line          */
    uint32_t poll_interval_ms;      /**< sampling period, and the INT timeout */
    uint32_t event_queue_len;       /**< 0 disables the event queue           */
    int      task_priority;
    int      task_core;             /**< -1 for no affinity                   */
} touch_config_t;

#define TOUCH_CONFIG_DEFAULT()          \
    (touch_config_t) {                  \
        .rotation = TOUCH_ROTATION_0,   \
        .mirror_x = false,              \
        .mirror_y = false,              \
        .use_interrupt = true,          \
        .poll_interval_ms = 10,         \
        .event_queue_len = 32,          \
        .task_priority = 5,             \
        .task_core = 0,                 \
    }

/** Reset the controller, probe it and start the sampling task. */
esp_err_t touch_init(const touch_config_t *cfg);

/** Stop the task and release the controller. */
esp_err_t touch_deinit(void);

/**
 * Copy the current contact set.  Returns the number of active contacts,
 * which may exceed @p max (only @p max of them are written).
 */
int touch_snapshot(touch_point_t *out, int max);

/** True when at least one finger is down; fills @p out if given. */
bool touch_pressed(touch_point_t *out);

/**
 * Pop the next input event.  @p timeout_ms of 0 polls, UINT32_MAX blocks.
 * Returns false when nothing arrived in time.
 */
bool touch_wait_event(touch_event_t *out, uint32_t timeout_ms);

/** Drop any queued events (useful after a screen change). */
void touch_flush_events(void);

/** I2C address the controller answered on, or 0 before init. */
uint16_t touch_i2c_address(void);

/**
 * Milliseconds since the controller last answered a read, or UINT32_MAX when
 * touch never came up.
 *
 * "Answered" means the I2C (Inter-Integrated Circuit) transaction succeeded,
 * not that a finger was down.  An untouched panel is healthy and reports an
 * age near zero, so this separates "nobody is touching it" from "the
 * controller stopped answering", which the event stream cannot.
 *
 * An application that can act on input, and only on input, treats a large
 * age as a fault rather than as quiet.
 */
uint32_t touch_age_ms(void);

#ifdef __cplusplus
}
#endif
