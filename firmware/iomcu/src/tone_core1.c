/*
 * Core 1's half of the phase tap.  See tone_core1.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tone_core1.h"

#include <string.h>

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "tone_cap.h"

typedef struct {
    tone_rec_t rec;
    uint16_t   gen, cap_gen;
} q_entry_t;

/* --- shared between the cores ----------------------------------------- */

static spin_lock_t     *s_lock;
static tone_cmd_t       s_order;           /* core 0 writes, core 1 copies */
static uint64_t         s_order_t0_us;
static uint32_t         s_order_seq;       /* core 0 counts the orders     */
static tone_status_t    s_status;          /* core 1 writes, core 0 copies */
static volatile uint32_t s_status_seq;
static volatile uint32_t s_ack_seq;        /* core 1: the order its last
                                              pass ran under, set when the
                                              pass is over                 */
static volatile bool     s_live;           /* core 1 has run a pass        */

static q_entry_t        s_q[TONE_Q_LEN];
static volatile uint32_t s_q_head;         /* core 1 advances              */
static volatile uint32_t s_q_tail;         /* core 0 advances              */
static volatile uint32_t s_dropped;        /* core 1 counts                */

/* --- core 1's own -------------------------------------------------------- */

/* Static: the service holds the detector, 1,100 bytes or so, and core 1's
 * stack is small. */
static tone_svc_t    s_svc;
static tone_cmd_t    s_cmd;
static uint64_t      s_cmd_t0_us;
static uint32_t      s_cmd_seq;
static tone_status_t s_snap;
static tone_rec_t    s_rec[TONE_SVC_BEEPS];
static bool          s_started;

/* --- core 0's own -------------------------------------------------------- */

static uint32_t s_taken_seq;
static uint32_t s_dropped_seen;

void tone_core1_init(void)
{
    s_lock = spin_lock_instance((uint)spin_lock_claim_unused(true));
    tone_svc_init(&s_svc);
    memset(&s_order, 0, sizeof(s_order));
    s_started = true;
}

void tone_core1_order(const tone_cmd_t *cmd, uint64_t t0_us)
{
    if (!s_started) {
        return;
    }
    const uint32_t irq = spin_lock_blocking(s_lock);
    s_order = *cmd;
    s_order_t0_us = t0_us;
    ++s_order_seq;
    spin_unlock(s_lock, irq);
}

void tone_core1_quiesce(void)
{
    if (!s_started || !s_live) {
        return;
    }
    const uint32_t irq = spin_lock_blocking(s_lock);
    const uint32_t seq = s_order_seq;
    spin_unlock(s_lock, irq);
    const uint64_t until = time_us_64() + TONE_CORE1_WAIT_US;
    while ((int32_t)(s_ack_seq - seq) < 0 && time_us_64() < until) {
        tight_loop_contents();
    }
}

static void push(const tone_rec_t *r, uint16_t gen, uint16_t cap_gen)
{
    const uint32_t head = s_q_head;
    if (head - s_q_tail >= TONE_Q_LEN) {
        s_dropped = s_dropped + 1u;
        return;
    }
    q_entry_t *e = &s_q[head % TONE_Q_LEN];
    e->rec = *r;
    e->gen = gen;
    e->cap_gen = cap_gen;
    __dmb();
    s_q_head = head + 1u;
}

/* One pass under the order taken. */
static void pass(void)
{
    /* No order to run and none run before: nothing to do or to say. */
    if (!s_cmd.run && !s_snap.running && s_snap.gen == s_cmd.gen
        && s_snap.cap_gen == s_cmd.cap_gen) {
        return;
    }

    uint64_t now = 0u;
    uint32_t wr = 0u;
    bool stalled = false;
    if (s_cmd.run) {
        const uint64_t t = time_us_64();
        now = tone_svc_ticks(t > s_cmd_t0_us ? t - s_cmd_t0_us : 0u);
        /* The flag before the write pointer: every word the DMA moves after
         * this boundary was pushed after the flag was cleared, or sat in
         * the FIFO then and is discarded with the next pass. */
        stalled = tone_cap_stalled();
        wr = tone_cap_wr();
    }
    const size_t n = tone_svc_step(&s_svc, &s_cmd,
                                   s_cmd.run ? tone_cap_ring() : NULL, wr, now,
                                   stalled, s_rec, &s_snap);
    for (size_t i = 0; i < n; ++i) {
        push(&s_rec[i], s_snap.gen, s_snap.cap_gen);
    }
    const uint32_t irq = spin_lock_blocking(s_lock);
    s_status = s_snap;
    s_status_seq = s_status_seq + 1u;
    spin_unlock(s_lock, irq);
}

void tone_core1_step(void)
{
    if (!s_started) {
        return;
    }
    uint32_t irq = spin_lock_blocking(s_lock);
    memcpy(&s_cmd, &s_order, sizeof(s_cmd));
    s_cmd_t0_us = s_order_t0_us;
    s_cmd_seq = s_order_seq;
    spin_unlock(s_lock, irq);

    pass();
    /* The capture is not touched past here under an order that said it
     * ran: core 0 may tear it down once it sees this. */
    __dmb();
    s_ack_seq = s_cmd_seq;
    s_live = true;
}

void tone_core1_sync(tone_page_t *page)
{
    if (!s_started) {
        return;
    }
    /* The status first, then the queue: a beep is published under the
     * status that is as new as it, or newer. */
    tone_status_t st;
    bool have = false;
    if (s_status_seq != s_taken_seq) {
        const uint32_t irq = spin_lock_blocking(s_lock);
        st = s_status;
        s_taken_seq = s_status_seq;
        spin_unlock(s_lock, irq);
        have = true;
    }
    uint32_t tail = s_q_tail;
    while (tail != s_q_head) {
        __dmb();
        const q_entry_t e = s_q[tail % TONE_Q_LEN];
        tone_page_beeps(page, e.gen, e.cap_gen, &e.rec, 1u);
        ++tail;
        __dmb();
        s_q_tail = tail;
    }
    if (have) {
        tone_page_publish(page, &st, NULL, 0u);
    }
    const uint32_t dropped = s_dropped;
    if (dropped != s_dropped_seen) {
        tone_page_dropped(page, dropped - s_dropped_seen);
        s_dropped_seen = dropped;
    }
}
