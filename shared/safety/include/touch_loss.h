/**
 * @file
 * @brief Touch events that never arrived, and an arm that must not outlive one.
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
 * @brief The consumer's side of one numbered stream.
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

/**
 * @brief Nothing taken yet: the first event is expected to carry 1.
 *
 * @param rx Consumer state to initialise.
 */
void touch_seq_rx_init(touch_seq_rx_t *rx);

/**
 * @brief An event numbered @p seq has been taken off the queue.
 *
 * Returns how many events before it never arrived; nonzero means the consumer's
 * record of the glass is stale and has to be dropped before this event is
 * handled.
 *
 * @param rx Consumer state.
 * @param seq Number the event carries.
 * @return The number of events that never arrived before this one.
 */
uint32_t touch_seq_take(touch_seq_rx_t *rx, uint32_t seq);

/**
 * @brief The queue has been drained.
 *
 * @p published is the producer's last published number, read *before* the drain
 * began: every event up to it had been offered by then, so any of them not
 * taken is lost.  Returns how many. Read after the drain instead, it could name
 * an event offered during it and still in the queue.
 *
 * @param rx Consumer state.
 * @param published Producer's last published number, read before the drain.
 * @return The number of events offered and not taken.
 */
uint32_t touch_seq_tail(touch_seq_rx_t *rx, uint32_t published);

/* ------------------------------------------------------- the producer */

/**
 * @brief The producer's side of one numbered stream.
 *
 * The panel's control task is the one producer of the render loop's touch
 * queue.  It numbers every item it offers, keeps the number of the last loss
 * notice for the arm's check (arm_watch_take_ok()), and offers each item as
 * the steps touch_put_begin() and touch_put_next() name.  The numbers wrap
 * at 2^32 (touch_seq_rx_t).
 */
typedef struct {
    uint32_t seq;          /**< the number of the last item offered; 0 none */
    uint32_t lost_notice;  /**< the number of the last loss notice; 0 none  */
} touch_seq_tx_t;

/**
 * @brief The queue operation an offer asks for next.
 *
 * The queue holds a bounded number of items and nothing here waits.  A full
 * queue drops its oldest entry and keeps the newest, as the GT911's own event
 * queue does.  Which of the two is lost does not make this safe: the dropped
 * item's number is missing from what the consumer takes, and the consumer
 * drops its gestures before it handles the next item (touch_seq_take()).
 */
typedef enum {
    TOUCH_PUT_SEND = 0,  /**< send the item, without waiting; report whether
                              the queue took it                            */
    TOUCH_PUT_EVICT,     /**< take the oldest entry off, without waiting   */
    TOUCH_PUT_PUBLISH,   /**< publish the item's number (touch_put_t.seq)
                              to the consumer, whatever the queue kept     */
    TOUCH_PUT_DONE,      /**< the offer is over; touch_put_t.kept says
                              whether the queue holds the item            */
} touch_put_op_t;

/** @brief One offer under way: its number, its step and its outcome. */
typedef struct {
    uint32_t seq;    /**< the item's number                              */
    uint8_t  step;   /**< the touch_put_op_t asked last                  */
    bool     sent;   /**< a send has been asked before the current one   */
    bool     kept;   /**< the queue holds the item                       */
} touch_put_t;

/**
 * @brief Nothing offered yet: the first item takes the number 1.
 *
 * @param tx Producer state to initialise.
 */
void touch_seq_tx_init(touch_seq_tx_t *tx);

/**
 * @brief The number the next offer takes, known before the offer: the STOP
 *        mark of a press is set before the press is offered.
 *
 * @param tx Producer state.
 * @return The next number; 1 for a NULL @p tx.
 */
uint32_t touch_seq_tx_next(const touch_seq_tx_t *tx);

/**
 * @brief Start an offer: number the item and ask for the first send.
 *
 * Every offer takes a number, whether or not the queue keeps the item.  A
 * loss notice's number is kept as touch_seq_tx_t.lost_notice.
 *
 * @param tx   Producer state; receives the number.
 * @param put  The offer; receives the number and its first step.
 * @param lost The item is a loss notice, not an event.
 * @return TOUCH_PUT_SEND; TOUCH_PUT_DONE, with nothing numbered, for a NULL
 *         @p tx or @p put.
 */
touch_put_op_t touch_put_begin(touch_seq_tx_t *tx, touch_put_t *put,
                               bool lost);

/**
 * @brief The operation after the one asked last, from its outcome.
 *
 * A send the queue refused asks for the oldest entry off and then for one
 * more send, whether or not the eviction took anything: the consumer can
 * empty the queue between the refused send and the eviction, and then there
 * is room with nothing evicted.  A second refused send ends with the item
 * not kept.  The number is published after the sends, kept or not.
 *
 * @param put The offer.
 * @param ok  The outcome of the operation asked last: the queue took the
 *            item (TOUCH_PUT_SEND) or an entry came off (TOUCH_PUT_EVICT);
 *            ignored after TOUCH_PUT_PUBLISH.
 * @return The next operation; TOUCH_PUT_DONE for a NULL @p put.
 */
touch_put_op_t touch_put_next(touch_put_t *put, bool ok);

/* ------------------------------------------------------------ the STOP mark */

/**
 * @brief Whether the mark of a STOP the control task applied stays across a
 *        loss the render loop has found.
 *
 * The control task applies a STOP at its press and marks the press with the
 * number it gives it in the render loop's queue, so the router's latch of
 * the same press does not stop the bench a second time.  A loss cancels the
 * router's gestures.  A press the router has yet to take still latches after
 * that, and needs the mark: one queued after the item that shows the loss,
 * or the event whose arrival showed it, which is dispatched after the
 * cancel.  A press numbered before that item was taken already or is among
 * the lost, and the cancel decides it.  The numbers wrap.
 *
 * @param mark The marked press's number; 0 for none.
 * @param cur The number of the item that showed the loss: the notice or the
 *        event after a gap, or the last published number for a loss at the
 *        end of a drain.
 * @param cur_is_event Whether that item is an event the router takes next.
 * @return True to keep the mark.
 */
bool stop_mark_keeps(uint32_t mark, uint32_t cur, bool cur_is_event);

/* --------------------------------------------------------- the arm watch */

/**
 * @brief Whether an arm stands until the render loop has seen it.
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
    /** @brief An arm stands and has not yet been seen by the render loop. */
    bool     on;
    uint32_t loss_gen;   /**< the render loop's count the arm was posted under */
    uint32_t drv_gaps;   /**< the control task's count when it took the arm   */
    uint32_t arm_gen;    /**< which arm this is, as the snapshot carries it   */
} arm_watch_t;

/**
 * @brief May an arm taken now go ahead?
 *
 * False when the render loop has dropped gestures since it posted the arm (@p
 * loss_gen_now differs from @p loss_gen_stamp), or when a loss notice for the
 * render loop was queued after the last event it had consumed when it posted
 * (@p lost_notice_seq is later than @p consumed_seq; both 0 means none).
 *
 * @param loss_gen_stamp Render loop loss count stamped on the arm.
 * @param loss_gen_now Render loop loss count now.
 * @param lost_notice_seq Sequence of the last loss notice queued; 0 for none.
 * @param consumed_seq Sequence of the last event the render loop consumed; 0
 *        for none.
 * @return true when the arm may go ahead.
 */
bool arm_watch_take_ok(uint32_t loss_gen_stamp, uint32_t loss_gen_now,
                       uint32_t lost_notice_seq, uint32_t consumed_seq);

/**
 * @brief An arm was applied: watch it from here.
 *
 * @param w Watch state.
 * @param loss_gen_stamp Render loop loss count at posting.
 * @param drv_gaps_at_take Control task gap count when the arm was taken.
 * @param arm_gen Number of this arm.
 */
void arm_watch_begin(arm_watch_t *w, uint32_t loss_gen_stamp,
                     uint32_t drv_gaps_at_take, uint32_t arm_gen);

/**
 * @brief One look, once the snapshot has been published.
 *
 * Returns true when the arm has to be undone: a loss of either kind since it
 * was taken, before the render loop acknowledged @c arm_gen.  A loss is looked
 * for before the acknowledgement, so one that arrives with it still retires the
 * arm.  The watch ends either way it resolves.
 *
 * @param w Watch state.
 * @param loss_gen_now Render loop loss count now.
 * @param drv_gaps_now Control task gap count now.
 * @param acked_arm_gen Arm number the render loop acknowledged.
 * @return true when the arm has to be undone.
 */
bool arm_watch_lost(arm_watch_t *w, uint32_t loss_gen_now,
                    uint32_t drv_gaps_now, uint32_t acked_arm_gen);

/**
 * @brief The bench disarmed for any reason: nothing is left to watch.
 *
 * @param w Watch state.
 */
void arm_watch_end(arm_watch_t *w);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_TOUCH_LOSS_H */
