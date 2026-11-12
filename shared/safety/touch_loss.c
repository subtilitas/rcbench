/**
 * @file
 * @brief Touch events that never arrived, and an arm that must not outlive one.
 *
 * See touch_loss.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "touch_loss.h"

#include <stddef.h>

/* More than this behind is taken for a stale or repeated number, not a gap. */
#define HALF_RANGE 0x80000000u

void touch_seq_rx_init(touch_seq_rx_t *rx)
{
    if (rx != NULL) {
        rx->next = 1u;
    }
}

uint32_t touch_seq_take(touch_seq_rx_t *rx, uint32_t seq)
{
    if (rx == NULL) {
        return 0u;
    }
    const uint32_t missed = seq - rx->next;
    if (missed >= HALF_RANGE) {
        /* Behind what was already taken: a single-producer queue cannot
         * deliver that, so nothing is counted and nothing moves back. */
        return 0u;
    }
    rx->next = seq + 1u;
    return missed;
}

uint32_t touch_seq_tail(touch_seq_rx_t *rx, uint32_t published)
{
    if (rx == NULL) {
        return 0u;
    }
    /* Events up to rx->next - 1 were taken; up to published were offered. */
    const uint32_t missed = published - (rx->next - 1u);
    if (missed == 0u || missed >= HALF_RANGE) {
        return 0u;
    }
    rx->next = published + 1u;
    return missed;
}

void touch_seq_tx_init(touch_seq_tx_t *tx)
{
    if (tx != NULL) {
        tx->seq         = 0u;
        tx->lost_notice = 0u;
    }
}

uint32_t touch_seq_tx_next(const touch_seq_tx_t *tx)
{
    return (tx != NULL) ? tx->seq + 1u : 1u;
}

touch_put_op_t touch_put_begin(touch_seq_tx_t *tx, touch_put_t *put,
                               bool lost)
{
    if (tx == NULL || put == NULL) {
        return TOUCH_PUT_DONE;
    }
    put->seq  = ++tx->seq;
    put->step = (uint8_t)TOUCH_PUT_SEND;
    put->sent = false;
    put->kept = false;
    if (lost) {
        tx->lost_notice = put->seq;
    }
    return TOUCH_PUT_SEND;
}

touch_put_op_t touch_put_next(touch_put_t *put, bool ok)
{
    if (put == NULL) {
        return TOUCH_PUT_DONE;
    }
    switch ((touch_put_op_t)put->step) {
    case TOUCH_PUT_SEND:
        if (ok) {
            put->kept = true;
            put->step = (uint8_t)TOUCH_PUT_PUBLISH;
        } else if (!put->sent) {
            /* Full: the oldest entry goes, then one more send. */
            put->sent = true;
            put->step = (uint8_t)TOUCH_PUT_EVICT;
        } else {
            put->step = (uint8_t)TOUCH_PUT_PUBLISH;
        }
        break;
    case TOUCH_PUT_EVICT:
        /* Whether or not anything came off: the consumer may have made
         * the room. */
        put->step = (uint8_t)TOUCH_PUT_SEND;
        break;
    case TOUCH_PUT_PUBLISH:
    default:
        put->step = (uint8_t)TOUCH_PUT_DONE;
        break;
    }
    return (touch_put_op_t)put->step;
}

bool arm_watch_take_ok(uint32_t loss_gen_stamp, uint32_t loss_gen_now,
                       uint32_t lost_notice_seq, uint32_t consumed_seq)
{
    if (loss_gen_stamp != loss_gen_now) {
        return false;
    }
    const uint32_t ahead = lost_notice_seq - consumed_seq;
    return ahead == 0u || ahead >= HALF_RANGE;
}

bool stop_mark_keeps(uint32_t mark, uint32_t cur, bool cur_is_event)
{
    if (mark == 0u) {
        return false;
    }
    const uint32_t ahead = mark - cur;
    if (ahead == 0u) {
        return cur_is_event;
    }
    return ahead < HALF_RANGE;
}

void arm_watch_begin(arm_watch_t *w, uint32_t loss_gen_stamp,
                     uint32_t drv_gaps_at_take, uint32_t arm_gen)
{
    if (w == NULL) {
        return;
    }
    w->on       = true;
    w->loss_gen = loss_gen_stamp;
    w->drv_gaps = drv_gaps_at_take;
    w->arm_gen  = arm_gen;
}

bool arm_watch_lost(arm_watch_t *w, uint32_t loss_gen_now,
                    uint32_t drv_gaps_now, uint32_t acked_arm_gen)
{
    if (w == NULL || !w->on) {
        return false;
    }
    if (loss_gen_now != w->loss_gen || drv_gaps_now != w->drv_gaps) {
        w->on = false;
        return true;
    }
    if (acked_arm_gen == w->arm_gen) {
        w->on = false;
    }
    return false;
}

void arm_watch_end(arm_watch_t *w)
{
    if (w != NULL) {
        w->on = false;
    }
}
