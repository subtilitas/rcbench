/**
 * @file touch.h
 * @brief Touch input for the 7-inch ESP32-P4 display board.
 *
 * A background task polls the GT911 every poll_interval_ms, maps the
 * coordinates into screen space, drops the ones outside the UI's
 * BOARD_UI_H_RES x BOARD_UI_V_RES area and publishes the result two ways:
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

#include "board_pins.h"

#include "touch_map.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How touch_init() sets the touch input up. */
typedef struct {
    touch_rotation_t rotation;      /**< matches how the panel is mounted      */
    bool     mirror_x;              /**< mirror x before rotation             */
    bool     mirror_y;              /**< mirror y before rotation             */
    uint32_t poll_interval_ms;      /**< sampling period                      */
    uint32_t event_queue_len;       /**< 0 disables the event queue           */
    int      task_priority;         /**< FreeRTOS priority of the task        */
    int      task_core;             /**< -1 for no affinity                   */
} touch_config_t;

/** @brief Polled every 10 ms, mirrored as board_pins.h says, 32 events. */
#define TOUCH_CONFIG_DEFAULT()              \
    (touch_config_t) {                      \
        .rotation = TOUCH_ROTATION_0,       \
        .mirror_x = BOARD_TOUCH_MIRROR_X,   \
        .mirror_y = BOARD_TOUCH_MIRROR_Y,   \
        .poll_interval_ms = 10,             \
        .event_queue_len = 32,              \
        .task_priority = 5,                 \
        .task_core = 0,                     \
    }

/**
 * @brief Reset the controller, probe it and start the polling task.
 * @param cfg The set-up; NULL takes TOUCH_CONFIG_DEFAULT().
 * @return ESP_OK, also when already up; ESP_ERR_NO_MEM for the task, lock
 *         or queue; otherwise the board's or the GT911's error.
 */
esp_err_t touch_init(const touch_config_t *cfg);

/**
 * @brief Stop the task and release the controller.
 * @return ESP_OK, also when touch was not up.
 */
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
 *
 * @p seq, when not NULL, receives the event's number.  The driver numbers
 * every event it offers to its queue, from 1 and in order, whether or not the
 * queue kept it, so a number skipped is an event the queue dropped because
 * nobody collected it.  touch_loss.h turns the numbers into losses.
 */
bool touch_wait_event(touch_event_t *out, uint32_t *seq, uint32_t timeout_ms);

/**
 * The number of the last event the driver offered to its queue, stored after
 * the offer.  Read before draining the queue: an event up to it that the
 * drain did not deliver was dropped, which a loss at the end of the stream,
 * with no later event to show the gap, needs.
 */
uint32_t touch_published(void);

/** Drop any queued events (useful after a screen change). */
void touch_flush_events(void);

/**
 * @brief Turn the glass's contacts by 180 degrees with the picture, or
 *        back: a contact mapped to (x, y) maps to (1023 - x, 599 - y).
 *
 * The panel turns its picture by its scan direction (display_set_turned()),
 * so every screen, the router, the band and STOP keep logical pixels.  The
 * touch injector's contacts are logical pixels already and never turn.
 * Taken from the next report on; may be called before touch_init().
 *
 * @param turned True for turned by 180 degrees.
 */
void touch_set_turned(bool turned);

/**
 * @brief Whether the glass's contacts are turned (touch_set_turned()).
 * @return True when turned by 180 degrees.
 */
bool touch_turned(void);

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

#ifdef RCBENCH_TOUCH_INJECTOR
/**
 * Whether a touch injector (testbench/touch-injector) is attached: it
 * answered at start-up and has not been silent for 1000 ms since, or a
 * probe has re-attached it.  Always true when the injector is the only touch
 * device (no GT911 at 0x5D).  Declared in a build with RCBENCH_TOUCH_INJECTOR
 * (the default) only; touch_inject.h has the rest.
 */
bool touch_injector_attached(void);
#endif


#ifdef __cplusplus
}
#endif
