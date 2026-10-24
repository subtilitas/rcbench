/**
 * @file
 * @brief The render loop's watchdog: when a render loop that stopped making
 *        progress stops the bench.
 *
 * The control task reads touch, tests STOP's rectangle and beats the
 * heartbeat on its own core, so a render loop that stops leaves STOP
 * working and the outputs at their last command.  DISARM, the throttle
 * slider, the servo dial and OUTPUT OFF are the render loop's, and they stop
 * working with no sign but a picture that no longer changes.
 *
 * The render loop counts its passes in a counter the control task reads.
 * While the bench is ARMED, or while the supply's output is on, a counter
 * that has not moved for FRAME_WATCH_MS is a stop, once per stall.  The
 * clock starts at the counter's last move, not at the arm: a loop that froze
 * before an arm or an ON that it had already posted stops the bench as soon
 * as the bench is ARMED or the output on.  A SAFE bench with the supply off
 * is never stopped by it: a SAFE frame may take seconds, the panel's own
 * update among them, and there is nothing to stop.
 *
 * What the stop is, and what is told, is the caller's: the panel latches it
 * as a STOP of the bench's own (arming_stop()), which drops the heartbeat and
 * switches the supply off, and an alert names the cause.  Nothing here can
 * arm the bench or switch the output on, and a loop that recovers after the
 * stop finds the latch as STOP leaves it.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FRAME_WATCH_H
#define RCBENCH_FRAME_WATCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How long the render loop's counter may stand still on an ARMED bench
 *        or under a live supply output before the bench is stopped: 1000ms.
 *
 * The longest pass the loop can make on a screen that drives an output is
 * bounded by its timeouts: the 50ms PPA (Pixel-Processing Accelerator)
 * budget of Screen rotation 180° (GFX_TURN_BUDGET_US), the fallback copy of
 * the frame after it, and the 200ms wait for a frame-done event in the flip
 * (FLIP_WAIT_MS, display.c), about 0.3s with the paint.  The paint's own
 * worst case on the ESP32-P4 is not measured.  A card read on the render
 * loop moves the counter as a pass does, one read at a time (panel_card.c).
 * 1000ms is three times the bounded part, and twice ARMING_TOUCH_DEAD_MS.
 */
#define FRAME_WATCH_MS 1000u

/** @brief The watch's state; frame_watch_init() sets it up. */
typedef struct {
    uint32_t seen;      /**< the counter at the last look              */
    uint32_t moved_ms;  /**< when it last moved, or when the watch began */
    bool     fired;     /**< stopped the bench since it last moved     */
} frame_watch_t;

/**
 * @brief Start watching from @p count at @p now_ms.
 *
 * @param w      Watch state.
 * @param count  The render loop's counter now.
 * @param now_ms Current time, in milliseconds.
 */
void frame_watch_init(frame_watch_t *w, uint32_t count, uint32_t now_ms);

/**
 * @brief One look at the counter: whether the bench is to be stopped now.
 *
 * A counter that differs from the last look has moved, whatever the
 * difference, so a counter that wraps through 0 moves.  The time is taken
 * modulo 2^32ms.  True once per stall, on the first look at or past
 * FRAME_WATCH_MS from the last move with @p armed or @p supply_on; the
 * next stall begins at the next move.
 *
 * @param w         Watch state.
 * @param count     The render loop's counter now.
 * @param armed     The bench is ARMED.
 * @param supply_on The supply's output is on.
 * @param now_ms    Current time, in milliseconds.
 * @return true to stop the bench.
 */
bool frame_watch_step(frame_watch_t *w, uint32_t count, bool armed,
                      bool supply_on, uint32_t now_ms);

/**
 * @brief How long the counter has stood still, at @p now_ms.
 *
 * @param w      Watch state.
 * @param now_ms Current time, in milliseconds.
 * @return Milliseconds since the counter last moved, modulo 2^32.
 */
uint32_t frame_watch_still_ms(const frame_watch_t *w, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FRAME_WATCH_H */
