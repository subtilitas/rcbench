/*
 * The capture's ring reader (protocols/passthru/pass_ring.c).
 *
 * The test writes the ring as the two DMA channels do, a state and then
 * its stamp at the head, and drains it as the main loop does.
 *
 * Under test: events read in order, across the ring's end, in drains of
 * every size; nothing read twice; a ring lapped while the reader was away
 * found by the slot before the reader, the decoder restarted at the newest
 * state, and the next drain normal again; a ring exactly full taken as
 * lapped; a lap before the first drain found against the cleared last
 * slot; one event short of a lap not a lap; a reader stalled for a 42ms
 * flash window at 195,000 events/s keeping up and at 227,680 events/s (the
 * capture's worst mix) lapped; the lost signals judged at the drain's
 * clock; a lap that happens while a drain reads (the DMA's write stood in
 * for between the read and the second check) leaving the decoder's figures
 * as they were before the drain; a state dropped on a full FIFO, seen in
 * the drain that read the rise or in the next, pairing no pulse across
 * the gap and the next whole pulse decoding.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "pass_decode.h"
#include "pass_ring.h"

#define N PASS_RING_EVENTS

static uint32_t    states[N];
static uint32_t    stamps[N];
static uint32_t    head;      /* events written, not wrapped */
static pass_ring_t r;
static pass_dec_t  d;

static void reset(void)
{
    memset(states, 0, sizeof(states));
    memset(stamps, 0, sizeof(stamps));
    head = 0u;
    pass_ring_init(&r);
    pass_dec_init(&d, 0u);
}

/* One event as the DMA writes it. */
static void put(uint32_t state, uint32_t stamp)
{
    states[head & (N - 1u)] = state;
    stamps[head & (N - 1u)] = stamp;
    ++head;
}

/* A drain told CAPTURE dropped a state since the last one. */
static pass_drain_t drain_dropped(uint32_t now)
{
    return pass_ring_drain(&r, states, stamps, head, now, true, &d);
}

static pass_drain_t drain(uint32_t now)
{
    return pass_ring_drain(&r, states, stamps, head, now, false, &d);
}

/* Pulses of 1500us on input 0 every 20ms: 2 events each. */
static uint32_t pulses(uint32_t t0, unsigned n)
{
    uint32_t t = t0;
    for (unsigned i = 0; i < n; ++i) {
        put(1u << PASS_IN_BIT(0), t);
        put(0u, t + 1500u);
        t += 20000u;
    }
    return t;
}

TEST_CASE(events_are_read_in_order_across_the_rings_end)
{
    reset();
    put(0u, 10u);                       /* the capture's first state */
    pass_drain_t x = drain(20u);
    CHECK_EQ(x.events, 1u);
    CHECK(!x.lapped);
    /* Fill to just short of the end, drain, then cross it. */
    uint32_t t = 100u;
    for (unsigned round = 0; round < 7u; ++round) {
        const unsigned n = (round % 2u == 0u) ? 2000u : 333u;
        t = pulses(t, n / 2u);
        x = drain(t);
        CHECK(!x.lapped);
        CHECK_EQ(x.events, (n / 2u) * 2u);
        /* Nothing read twice: a drain with nothing new reads nothing. */
        x = drain(t);
        CHECK_EQ(x.events, 0u);
        CHECK(!x.lapped);
    }
    CHECK(head > N);
    const pass_pin_t *p = &d.pin[PASS_IN_BIT(0)];
    CHECK_EQ(p->width_us, 1500u);
    CHECK_EQ(p->period_us, 20000u);
    CHECK_EQ(p->missed, 0u);
    CHECK_EQ(p->frames, (head - 1u) / 2u);
}

TEST_CASE(one_event_short_of_a_lap_is_no_lap)
{
    reset();
    put(0u, 10u);
    (void)drain(10u);
    /* N - 1 events: the slot before the reader is not reached. */
    for (unsigned i = 0; i < N - 1u; ++i) {
        put((i & 1u) ? 0u : (1u << PASS_IN_BIT(0)), 100u + i * 1000u);
    }
    const pass_drain_t x = drain(100u + N * 1000u);
    CHECK(!x.lapped);
    CHECK_EQ(x.events, N - 1u);
}

TEST_CASE(a_ring_exactly_full_or_lapped_is_found_and_the_reader_starts_again)
{
    static const unsigned more[] = { N, N + 1u, N + 5u, 3u * N + 17u };
    for (unsigned i = 0; i < sizeof(more) / sizeof(more[0]); ++i) {
        reset();
        put(0u, 10u);
        (void)pulses(100u, 3u);
        (void)drain(60100u);
        const uint32_t frames = d.pin[PASS_IN_BIT(0)].frames;
        CHECK_EQ(frames, 3u);
        /* The reader away while more[i] events arrive, the last one a
         * rise on input 1. */
        for (unsigned e = 0; e + 1u < more[i]; ++e) {
            put((e & 1u) ? 0u : (1u << PASS_IN_BIT(0)), 70000u + e * 10u);
        }
        put(1u << PASS_IN_BIT(1), 70000u + more[i] * 10u);
        const uint32_t now = 70000u + more[i] * 10u + 5u;
        pass_drain_t x = drain(now);
        CHECK(x.lapped);
        CHECK_EQ(x.events, 0u);
        /* Started again after the newest event: its levels, no pulse. */
        CHECK_EQ(d.level, 1u << PASS_IN_BIT(1));
        CHECK(!d.pin[PASS_IN_BIT(1)].rise_seen);
        CHECK_EQ(d.pin[PASS_IN_BIT(0)].frames, frames);
        /* The next drain is normal. */
        put(0u, now + 100u);
        put(1u << PASS_IN_BIT(2), now + 200u);
        x = drain(now + 300u);
        CHECK(!x.lapped);
        CHECK_EQ(x.events, 2u);
    }
}

TEST_CASE(a_lap_before_the_first_drain_is_found_against_the_cleared_slot)
{
    reset();
    for (unsigned e = 0; e < N + 2u; ++e) {
        put((e & 1u) ? 0u : 1u, 100u + e);
    }
    const pass_drain_t x = drain(100u + N + 2u);
    CHECK(x.lapped);
    /* N events with the last slot never written: no lap. */
    reset();
    for (unsigned e = 0; e < N - 1u; ++e) {
        put((e & 1u) ? 0u : 1u, 100u + e);
    }
    CHECK(!drain(100u + N).lapped);
}

/* A reader stalled for 42ms with events arriving at @p rate a second,
 * evenly: lapped or not. */
static bool stalled_42ms_laps(uint32_t rate)
{
    reset();
    put(0u, 1u);
    (void)drain(1u);
    const uint64_t n = (uint64_t)rate * 42u / 1000u;
    for (uint64_t e = 0; e < n; ++e) {
        put((e & 1u) ? 0u : (1u << PASS_IN_BIT(0)),
            (uint32_t)(10u + e * 1000000u / rate));
    }
    return drain(42010u).lapped;
}

TEST_CASE(a_42ms_flash_window_laps_the_ring_only_above_195000_events_a_second)
{
    /* 8192 events in 42ms is 195,048 a second. */
    CHECK(!stalled_42ms_laps(20160u));     /* 9 inputs + 9 servos at 560Hz */
    CHECK(!stalled_42ms_laps(195000u));
    CHECK(stalled_42ms_laps(195100u));
    CHECK(stalled_42ms_laps(227680u));     /* the worst mix, tools/pio_budget.py */
}

TEST_CASE(the_lost_signals_are_judged_at_the_drains_clock)
{
    reset();
    put(0u, 0u);
    const uint32_t t = pulses(1000u, 3u);
    (void)drain(t);
    CHECK(!d.pin[PASS_IN_BIT(0)].lost);
    /* The last rise at t - 20000: lost 100ms after it. */
    (void)drain(t - 20000u + 99999u);
    CHECK(!d.pin[PASS_IN_BIT(0)].lost);
    (void)drain(t - 20000u + 100000u);
    CHECK(d.pin[PASS_IN_BIT(0)].lost);
}

/* The DMA's write during the read: the slot before the reader's first
 * overwritten, as a lap leaves it. */
static void lap_meanwhile(void *ctx)
{
    (void)ctx;
    states[(r.rd - 1u) & (N - 1u)] ^= 1u;
    stamps[(r.rd - 1u) & (N - 1u)] += N;
}

TEST_CASE(a_lap_during_the_read_leaves_the_figures_as_before_the_drain)
{
    reset();
    put(0u, 1u);
    (void)drain(1u);
    const uint32_t t = pulses(1000u, 3u);
    CHECK(!drain(t).lapped);
    const pass_dec_t was = d;
    CHECK_EQ(was.pin[PASS_IN_BIT(0)].frames, 3u);
    CHECK_EQ(was.pin[PASS_IN_BIT(0)].width_us, 1500u);
    /* A 100us glitch on input 0, a pulse of 1000us on input 1, read while
     * the DMA laps the reader. */
    put(1u << PASS_IN_BIT(0), t);
    put(0u, t + 100u);
    put(1u << PASS_IN_BIT(1), t + 200u);
    put(0u, t + 1200u);
    r.read_done = lap_meanwhile;
    const pass_drain_t x = drain(t + 2000u);
    r.read_done = NULL;
    CHECK(x.lapped);
    CHECK_EQ(x.events, 0u);
    for (unsigned b = 0; b < PASS_PINS; ++b) {
        CHECK_EQ(d.pin[b].frames, was.pin[b].frames);
        CHECK_EQ(d.pin[b].glitches, was.pin[b].glitches);
        CHECK_EQ(d.pin[b].missed, was.pin[b].missed);
        CHECK_EQ(d.pin[b].width_us, was.pin[b].width_us);
        CHECK_EQ(d.pin[b].period_us, was.pin[b].period_us);
    }
    /* The restart: the newest state taken, no high in progress. */
    CHECK_EQ(d.level, 0u);
    CHECK(!d.pin[PASS_IN_BIT(0)].rise_seen);
    /* The same events without the lap: decoded. */
    reset();
    put(0u, 1u);
    (void)drain(1u);
    (void)drain(pulses(1000u, 3u));
    put(1u << PASS_IN_BIT(0), t);
    put(0u, t + 100u);
    const pass_drain_t y = drain(t + 2000u);
    CHECK(!y.lapped);
    CHECK_EQ(y.events, 2u);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].glitches, 1u);
}

TEST_CASE(a_state_dropped_on_a_full_fifo_pairs_no_pulse_across_the_gap)
{
    const uint32_t in0 = 1u << PASS_IN_BIT(0);
    /* Seen in the drain after the one that read the rise. */
    reset();
    put(0u, 1u);
    (void)drain(1u);
    uint32_t t = pulses(1000u, 3u);
    CHECK(!drain(t).lapped);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].width_us, 1500u);
    const uint32_t glitches = d.pin[PASS_IN_BIT(0)].glitches;
    put(in0, t);
    (void)drain(t + 10u);
    CHECK(d.pin[PASS_IN_BIT(0)].rise_seen);
    /* The fall at t + 1500 dropped; the next rise is no change, the
     * next fall 21500us after the rise read. */
    put(in0, t + 20000u);
    put(0u, t + 21500u);
    const pass_drain_t x = drain_dropped(t + 22000u);
    CHECK(!x.lapped);
    CHECK_EQ(x.events, 0u);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].glitches, glitches);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].width_us, 1500u);
    CHECK(!d.pin[PASS_IN_BIT(0)].rise_seen);
    CHECK_EQ(d.level, 0u);
    /* The next whole pulse decodes. */
    put(in0, t + 40000u);
    put(0u, t + 41200u);
    (void)drain(t + 42000u);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].width_us, 1200u);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].glitches, glitches);

    /* Seen in the drain that read the rise: it is undone, the decoder
     * restarts high with no rise of its own. */
    reset();
    put(0u, 1u);
    (void)drain(1u);
    t = pulses(1000u, 3u);
    (void)drain(t);
    put(in0, t);
    CHECK_EQ(drain_dropped(t + 10u).events, 0u);
    CHECK_EQ(d.level, in0);
    CHECK(!d.pin[PASS_IN_BIT(0)].rise_seen);
    put(in0, t + 20000u);
    put(0u, t + 21500u);
    (void)drain(t + 22000u);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].glitches, glitches);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].width_us, 1500u);
    put(in0, t + 40000u);
    put(0u, t + 41300u);
    (void)drain(t + 42000u);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].width_us, 1300u);
    CHECK_EQ(d.pin[PASS_IN_BIT(0)].glitches, glitches);
}

int main(void)
{
    RUN(events_are_read_in_order_across_the_rings_end);
    RUN(one_event_short_of_a_lap_is_no_lap);
    RUN(a_ring_exactly_full_or_lapped_is_found_and_the_reader_starts_again);
    RUN(a_lap_before_the_first_drain_is_found_against_the_cleared_slot);
    RUN(a_42ms_flash_window_laps_the_ring_only_above_195000_events_a_second);
    RUN(the_lost_signals_are_judged_at_the_drains_clock);
    RUN(a_lap_during_the_read_leaves_the_figures_as_before_the_drain);
    RUN(a_state_dropped_on_a_full_fifo_pairs_no_pulse_across_the_gap);
    return test_summary("pass_ring");
}
