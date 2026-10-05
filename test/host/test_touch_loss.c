/*
 * Host unit tests for touch_loss: numbered touch streams and the arm watch.
 *
 * The stream half is held against a model of the panel's queues: a bounded
 * FIFO with one producer that evicts the oldest entry when it is full, and a
 * consumer that drains it, with the steps of each interleaved in every order
 * a seeded generator picks.  The producer's offer is four steps -- try, evict,
 * retry, publish -- because on the panel those are separate operations on two
 * cores and the consumer can run between any two of them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "greatest.h"

#include "touch_loss.h"

/* ---------------------------------------------------------------- basics */

TEST_CASE(an_unbroken_stream_reports_nothing)
{
    touch_seq_rx_t rx;
    touch_seq_rx_init(&rx);
    for (uint32_t s = 1u; s <= 100u; ++s) {
        CHECK_EQ(touch_seq_take(&rx, s), 0u);
    }
    CHECK_EQ(touch_seq_tail(&rx, 100u), 0u);
}

TEST_CASE(a_gap_is_reported_on_the_event_after_it)
{
    touch_seq_rx_t rx;
    touch_seq_rx_init(&rx);
    CHECK_EQ(touch_seq_take(&rx, 1u), 0u);
    CHECK_EQ(touch_seq_take(&rx, 4u), 2u);       /* 2 and 3 never came */
    CHECK_EQ(touch_seq_take(&rx, 5u), 0u);
}

TEST_CASE(a_loss_at_the_end_is_reported_against_the_published_number)
{
    touch_seq_rx_t rx;
    touch_seq_rx_init(&rx);
    CHECK_EQ(touch_seq_take(&rx, 1u), 0u);
    CHECK_EQ(touch_seq_tail(&rx, 3u), 2u);       /* 2 and 3, nothing after */
    /* And only once. */
    CHECK_EQ(touch_seq_tail(&rx, 3u), 0u);
    CHECK_EQ(touch_seq_take(&rx, 4u), 0u);
}

TEST_CASE(nothing_published_yet_is_not_a_loss)
{
    touch_seq_rx_t rx;
    touch_seq_rx_init(&rx);
    CHECK_EQ(touch_seq_tail(&rx, 0u), 0u);
}

TEST_CASE(a_number_behind_the_stream_moves_nothing_back)
{
    touch_seq_rx_t rx;
    touch_seq_rx_init(&rx);
    CHECK_EQ(touch_seq_take(&rx, 10u), 9u);
    CHECK_EQ(touch_seq_take(&rx, 7u), 0u);
    CHECK_EQ(touch_seq_take(&rx, 11u), 0u);
    CHECK_EQ(touch_seq_tail(&rx, 5u), 0u);       /* published read long ago */
}

TEST_CASE(the_numbers_wrap)
{
    touch_seq_rx_t rx = { .next = 0xFFFFFFFEu };
    CHECK_EQ(touch_seq_take(&rx, 0xFFFFFFFEu), 0u);
    CHECK_EQ(touch_seq_take(&rx, 0xFFFFFFFFu), 0u);
    CHECK_EQ(touch_seq_take(&rx, 0u), 0u);
    CHECK_EQ(touch_seq_take(&rx, 2u), 1u);
    CHECK_EQ(touch_seq_tail(&rx, 4u), 2u);
}

TEST_CASE(a_null_receiver_is_refused)
{
    touch_seq_rx_init(NULL);
    CHECK_EQ(touch_seq_take(NULL, 5u), 0u);
    CHECK_EQ(touch_seq_tail(NULL, 5u), 0u);
}

/* ------------------------------------------- the queue, interleaved */

#define QCAP 4u
#define EVENTS 400u

typedef struct {
    uint32_t buf[QCAP];
    unsigned head, count;
} fifo_t;

static bool fifo_push(fifo_t *q, uint32_t v)
{
    if (q->count == QCAP) {
        return false;
    }
    q->buf[(q->head + q->count) % QCAP] = v;
    ++q->count;
    return true;
}

static bool fifo_pop(fifo_t *q, uint32_t *v)
{
    if (q->count == 0u) {
        return false;
    }
    *v = q->buf[q->head];
    q->head = (q->head + 1u) % QCAP;
    --q->count;
    return true;
}

static uint32_t s_rand;
static unsigned rnd(unsigned n)
{
    s_rand = s_rand * 1664525u + 1013904223u;
    return (s_rand >> 8) % n;
}

/* Producer steps for one offer. */
typedef enum { P_TRY, P_EVICT, P_RETRY, P_PUBLISH, P_DONE } pstep_t;
/* Consumer steps for one drain. */
typedef enum { C_POP, C_TAIL, C_IDLE } cstep_t;

/* Returns how many events the run evicted. */
static unsigned run_interleaving(uint32_t seed, unsigned consumer_bias,
                                 unsigned *false_alarm)
{
    fifo_t q;
    memset(&q, 0, sizeof(q));
    bool evicted[EVENTS + 2];
    memset(evicted, 0, sizeof(evicted));
    uint32_t published = 0u;

    touch_seq_rx_t rx;
    touch_seq_rx_init(&rx);
    unsigned reported = 0u;
    unsigned evicted_n = 0u;

    s_rand = seed;
    uint32_t seq = 0u;
    pstep_t ps = P_DONE;
    cstep_t cs = C_IDLE;
    uint32_t drain_published = 0u;

    while (seq < EVENTS || ps != P_DONE || q.count > 0u || cs != C_IDLE) {
        const bool producer_can = (seq < EVENTS || ps != P_DONE);
        const bool run_consumer = !producer_can || rnd(100u) < consumer_bias;
        if (!run_consumer) {
            switch (ps) {
            case P_DONE:
                ++seq;
                ps = P_TRY;
                break;
            case P_TRY:
                ps = fifo_push(&q, seq) ? P_PUBLISH : P_EVICT;
                break;
            case P_EVICT: {
                uint32_t old;
                if (fifo_pop(&q, &old)) {
                    evicted[old] = true;
                    ++evicted_n;
                }
                ps = P_RETRY;
                break;
            }
            case P_RETRY:
                if (!fifo_push(&q, seq)) {
                    evicted[seq] = true;     /* the new one is the loss */
                    ++evicted_n;
                }
                ps = P_PUBLISH;
                break;
            case P_PUBLISH:
                published = seq;
                ps = P_DONE;
                break;
            }
            continue;
        }
        switch (cs) {
        case C_IDLE:
            drain_published = published;
            cs = C_POP;
            break;
        case C_POP: {
            uint32_t v;
            if (fifo_pop(&q, &v)) {
                reported += touch_seq_take(&rx, v);
                /* Every event evicted ahead of this one is reported by the
                 * time this one would be dispatched. */
                unsigned before = 0u;
                for (uint32_t k = 1u; k < v; ++k) {
                    before += evicted[k] ? 1u : 0u;
                }
                if (reported < before) {
                    T_FAIL("seed %u: event %u dispatched with %u of %u "
                           "earlier losses reported", seed, v, reported,
                           before);
                    return evicted_n;
                }
            } else {
                /* Found empty; the tail is a step of its own, because the
                 * producer can offer and publish between the two. */
                cs = C_TAIL;
            }
            break;
        }
        case C_TAIL:
            reported += touch_seq_tail(&rx, drain_published);
            cs = C_IDLE;
            break;
        }
    }
    /* A last drain after the producer has published its final number. */
    uint32_t v;
    const uint32_t last = published;
    while (fifo_pop(&q, &v)) {
        reported += touch_seq_take(&rx, v);
    }
    reported += touch_seq_tail(&rx, last);

    if (reported != evicted_n) {
        T_FAIL("seed %u: %u evicted, %u reported", seed, evicted_n, reported);
    }
    if (evicted_n == 0u && reported != 0u) {
        ++*false_alarm;
    }
    return evicted_n;
}

TEST_CASE(every_eviction_is_reported_once_and_before_what_follows_it)
{
    unsigned evicted = 0u;
    unsigned false_alarm = 0u;
    /* A slow consumer evicts often; a fast one rarely or never. */
    static const unsigned bias[] = { 10u, 30u, 50u, 70u, 95u };
    for (unsigned b = 0; b < sizeof(bias) / sizeof(bias[0]); ++b) {
        for (uint32_t seed = 1u; seed <= 200u; ++seed) {
            evicted += run_interleaving(seed * 7919u + b, bias[b],
                                        &false_alarm);
        }
    }
    CHECK(evicted > 0u);           /* the model did exercise eviction */
    CHECK_EQ(false_alarm, 0u);
}

TEST_CASE(a_consumer_that_keeps_up_reports_nothing)
{
    /* A consumer that runs almost every step rarely lets the queue fill.
     * Runs with no eviction at all must report nothing; enough of them have
     * to happen for that to have been tried. */
    unsigned clean = 0u;
    unsigned false_alarm = 0u;
    for (uint32_t seed = 1u; seed <= 100u; ++seed) {
        if (run_interleaving(seed, 98u, &false_alarm) == 0u) {
            ++clean;
        }
    }
    CHECK(clean >= 10u);
    CHECK_EQ(false_alarm, 0u);
}

/* ------------------------------------------------------- the arm watch */

TEST_CASE(an_arm_is_taken_only_without_a_loss_since_it_was_posted)
{
    CHECK(arm_watch_take_ok(3u, 3u, 0u, 0u));
    CHECK(!arm_watch_take_ok(3u, 4u, 0u, 0u));     /* render cancelled since */
    /* A loss notice queued after what the render loop had consumed. */
    CHECK(!arm_watch_take_ok(3u, 3u, 41u, 40u));
    CHECK(arm_watch_take_ok(3u, 3u, 40u, 40u));     /* it had seen it */
    CHECK(arm_watch_take_ok(3u, 3u, 12u, 40u));
    /* Across the wrap. */
    CHECK(!arm_watch_take_ok(3u, 3u, 2u, 0xFFFFFFFFu));
    CHECK(arm_watch_take_ok(3u, 3u, 0xFFFFFFFFu, 2u));
}

TEST_CASE(a_loss_before_the_render_loop_sees_the_arm_undoes_it)
{
    arm_watch_t w;
    arm_watch_begin(&w, 5u, 9u, 2u);
    CHECK(!arm_watch_lost(&w, 5u, 9u, 1u));         /* acknowledged: old arm */
    CHECK(arm_watch_lost(&w, 6u, 9u, 1u));          /* render dropped gestures */
    CHECK(!w.on);
    CHECK(!arm_watch_lost(&w, 7u, 9u, 1u));         /* once */

    arm_watch_begin(&w, 5u, 9u, 2u);
    CHECK(arm_watch_lost(&w, 5u, 10u, 1u));         /* a driver gap */
}

TEST_CASE(the_acknowledgement_ends_the_watch)
{
    arm_watch_t w;
    arm_watch_begin(&w, 5u, 9u, 2u);
    CHECK(!arm_watch_lost(&w, 5u, 9u, 2u));
    CHECK(!w.on);
    /* A loss after the render loop has seen the arm is the screens' to
     * answer, against an armed bench. */
    CHECK(!arm_watch_lost(&w, 6u, 10u, 2u));
}

TEST_CASE(a_loss_arriving_with_the_acknowledgement_still_undoes_the_arm)
{
    arm_watch_t w;
    arm_watch_begin(&w, 5u, 9u, 2u);
    CHECK(arm_watch_lost(&w, 6u, 9u, 2u));
}

TEST_CASE(a_disarm_ends_the_watch)
{
    arm_watch_t w;
    arm_watch_begin(&w, 5u, 9u, 2u);
    arm_watch_end(&w);
    CHECK(!arm_watch_lost(&w, 6u, 10u, 0u));
    arm_watch_end(NULL);
    arm_watch_begin(NULL, 0u, 0u, 0u);
    CHECK(!arm_watch_lost(NULL, 0u, 0u, 0u));
}

int main(void)
{
    RUN(an_unbroken_stream_reports_nothing);
    RUN(a_gap_is_reported_on_the_event_after_it);
    RUN(a_loss_at_the_end_is_reported_against_the_published_number);
    RUN(nothing_published_yet_is_not_a_loss);
    RUN(a_number_behind_the_stream_moves_nothing_back);
    RUN(the_numbers_wrap);
    RUN(a_null_receiver_is_refused);
    RUN(every_eviction_is_reported_once_and_before_what_follows_it);
    RUN(a_consumer_that_keeps_up_reports_nothing);
    RUN(an_arm_is_taken_only_without_a_loss_since_it_was_posted);
    RUN(a_loss_before_the_render_loop_sees_the_arm_undoes_it);
    RUN(the_acknowledgement_ends_the_watch);
    RUN(a_loss_arriving_with_the_acknowledgement_still_undoes_the_arm);
    RUN(a_disarm_ends_the_watch);
    return test_summary("touch_loss");
}
