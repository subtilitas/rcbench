/*
 * Touch events that never arrived, and an arm that must not outlive one.
 *
 * Two queues carry touch on the panel: the GT911 driver's, from its task to
 * the control task, and the control task's, from there to the render loop on
 * the other core.  Each drops its oldest entry when the consumer is behind,
 * and a screen that misses an event holds a press that is no longer on the
 * glass: a hold that completes on a timer then finishes on a finger that has
 * gone.
 *
 * Each queue has one producer, and the producer numbers every event it
 * offers, in order.  The consumer sees a loss as a gap in the numbers -- on
 * the first event after it, before that event is dispatched -- and, for a
 * loss at the end of the stream with nothing after it, against the number
 * the producer last published.  Nothing is counted on one core and compared
 * on the other, so no ordering between the two decides whether a loss is
 * seen before a surviving event is handled.
 *
 * The arm watch is the other half.  An arm the render loop posted is retired
 * by any loss from its posting until the render loop has seen the bench
 * armed; after that a loss is answered by the screens, against an armed
 * bench.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_TOUCH_LOSS_H
#define RCBENCH_TOUCH_LOSS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------ numbered event streams */

/**
 * The consumer's side of one numbered stream.
 *
 * The producer numbers its events from 1, adding one per event it offers to
 * the queue whether or not the queue keeps it, and publishes the number of
 * the last one after offering it.  Numbers wrap at 2^32; a gap is measured
 * modulo that, and anything more than 2^31 behind is taken for no gap rather
 * than for four billion.
 */
typedef struct {
    uint32_t next;   /**< the number the next event should carry */
} touch_seq_rx_t;

/** Nothing taken yet: the first event is expected to carry 1. */
void touch_seq_rx_init(touch_seq_rx_t *rx);

/**
 * An event numbered @p seq has been taken off the queue.  Returns how many
 * events before it never arrived; nonzero means the consumer's record of the
 * glass is stale and has to be dropped before this event is handled.
 */
uint32_t touch_seq_take(touch_seq_rx_t *rx, uint32_t seq);

/**
 * The queue has been drained.  @p published is the producer's last published
 * number, read *before* the drain began: every event up to it had been
 * offered by then, so any of them not taken is lost.  Returns how many.
 * Read after the drain instead, it could name an event offered during it and
 * still in the queue.
 */
uint32_t touch_seq_tail(touch_seq_rx_t *rx, uint32_t published);

/* --------------------------------------------------------- the arm watch */

/**
 * Whether an arm stands until the render loop has seen it.
 *
 * Two loss counts are compared, because the two queues are watched by two
 * tasks: @c loss_gen is how many times the render loop has dropped the
 * screens' gestures for a loss, and @c drv_gaps is how many gaps the control
 * task has found in the driver's stream.  The first is stamped on the arm
 * when the render loop posts it; the second is read when the control task
 * takes it, after draining the driver's queue, so a driver loss the render
 * loop has not been told about yet is still caught here.
 */
typedef struct {
    bool     on;
    uint32_t loss_gen;   /**< the render loop's count the arm was posted under */
    uint32_t drv_gaps;   /**< the control task's count when it took the arm   */
    uint32_t arm_gen;    /**< which arm this is, as the snapshot carries it   */
} arm_watch_t;

/**
 * May an arm taken now go ahead?  False when the render loop has dropped
 * gestures since it posted the arm (@p loss_gen_now differs from
 * @p loss_gen_stamp), or when a loss notice for the render loop was queued
 * after the last event it had consumed when it posted (@p lost_notice_seq is
 * later than @p consumed_seq; both 0 means none).
 */
bool arm_watch_take_ok(uint32_t loss_gen_stamp, uint32_t loss_gen_now,
                       uint32_t lost_notice_seq, uint32_t consumed_seq);

/** An arm was applied: watch it from here. */
void arm_watch_begin(arm_watch_t *w, uint32_t loss_gen_stamp,
                     uint32_t drv_gaps_at_take, uint32_t arm_gen);

/**
 * One look, once the snapshot has been published.  Returns true when the
 * arm has to be undone: a loss of either kind since it was taken, before the
 * render loop acknowledged @c arm_gen.  A loss is looked for before the
 * acknowledgement, so one that arrives with it still retires the arm.  The
 * watch ends either way it resolves.
 */
bool arm_watch_lost(arm_watch_t *w, uint32_t loss_gen_now,
                    uint32_t drv_gaps_now, uint32_t acked_arm_gen);

/** The bench disarmed for any reason: nothing is left to watch. */
void arm_watch_end(arm_watch_t *w);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_TOUCH_LOSS_H */
