/*
 * The words the phase tap's PIO program pushes and the ring a DMA channel
 * collects them in (protocols/phase_tap/edge_ring.c).
 *
 * Under test: the word's two fields; the 31-bit count extended to 64 bits
 * against an estimate of the present, across the counter's wrap every
 * 2^31 ticks and across silences longer than one wrap; reading the words
 * written since the last call, in order, across the end of the ring and in
 * batches; and a lap of the DMA over the reader, seen from the slot of the
 * last word taken, with the reader restarted after the newest word.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "edge_ring.h"

#define SIZE 16u
#define WRAP (1ull << 31)

static uint32_t    ring[SIZE];
static edge_ring_t r;
static tone_edge_t out[SIZE * 2u];

/* The counter after @p k ticks of a counter that starts at 0. */
static uint32_t count_at(uint64_t k)
{
    return (uint32_t)(0u - (uint32_t)k) & EDGE_COUNT_MASK;
}

static void put(uint32_t slot, uint64_t tick, bool level)
{
    ring[slot % SIZE] = edge_word(count_at(tick), level);
}

/* A capture's start: the ring cleared, the reader at slot 0. */
static void fresh(void)
{
    memset(ring, 0, sizeof(ring));
    edge_ring_init(&r);
}

TEST_CASE(a_word_carries_a_31_bit_count_and_a_level)
{
    const uint32_t w = edge_word(0x12345678u, true);
    CHECK_EQ(w, 0x2468ACF1u);
    CHECK_EQ(edge_word_count(w), 0x12345678u);
    CHECK(edge_word_level(w));
    CHECK(!edge_word_level(edge_word(0x12345678u, false)));
    /* Bit 31 of the counter does not fit and is dropped. */
    CHECK_EQ(edge_word_count(edge_word(0xFFFFFFFFu, false)), 0x7FFFFFFFu);
    CHECK_EQ(edge_word(0u, true), 1u);
}

TEST_CASE(the_count_runs_down_so_elapsed_ticks_are_its_negative)
{
    CHECK_EQ(edge_tick(count_at(0), 0), 0);
    CHECK_EQ(edge_tick(count_at(1), 1), 1);
    CHECK_EQ(edge_tick(count_at(37500000u), 37500000u), 37500000);
    /* One tick after the start the counter holds 0xFFFFFFFF. */
    CHECK_EQ(edge_tick(0x7FFFFFFFu, 1), 1);
}

TEST_CASE(the_estimate_places_a_count_among_the_ticks_it_repeats_in)
{
    /* A tick shortly before and shortly after the estimate. */
    CHECK_EQ(edge_tick(count_at(1000), 1010), 1000);
    CHECK_EQ(edge_tick(count_at(1000), 990), 1000);
    /* Past the first wrap, and a word just across it. */
    const uint64_t a = WRAP - 5u;
    const uint64_t b = WRAP + 7u;
    CHECK(edge_tick(count_at(a), b + 100u) == a);
    CHECK(edge_tick(count_at(b), b + 100u) == b);
    CHECK(edge_tick(count_at(b), a) == b);
    /* A silence of 3 wraps and a half: the estimate places the word. */
    const uint64_t far = 3u * WRAP + WRAP / 2u + 1234u;
    CHECK(edge_tick(count_at(far - 40u), far) == far - 40u);
    CHECK(edge_tick(count_at(far + 40u), far) == far + 40u);
    /* A quarter wrap either way is still found. */
    CHECK(edge_tick(count_at(far - WRAP / 4u), far) == far - WRAP / 4u);
    CHECK(edge_tick(count_at(far + WRAP / 4u), far) == far + WRAP / 4u);
}

TEST_CASE(a_tick_before_the_start_does_not_exist)
{
    /* A word that would sit before tick 0 is placed at 0. */
    CHECK_EQ(edge_tick(count_at(WRAP - 3u), 2), 0);
}

TEST_CASE(a_ring_read_returns_the_words_written_since_the_last_read)
{
    fresh();
    bool lap = true;
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 0u, 0u, out, 8u, &lap), 0);
    CHECK(!lap);
    put(0, 10, true);
    put(1, 25, false);
    put(2, 90, true);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 3u, 100u, out, 8u, &lap), 3);
    CHECK(!lap);
    CHECK_EQ(out[0].t, 10);
    CHECK(out[0].level);
    CHECK_EQ(out[1].t, 25);
    CHECK(!out[1].level);
    CHECK_EQ(out[2].t, 90);
    CHECK(out[2].level);
    /* Nothing new: nothing returned, and no lap. */
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 3u, 100u, out, 8u, &lap), 0);
    CHECK(!lap);
    put(3, 120, false);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 4u, 130u, out, 8u, &lap), 1);
    CHECK_EQ(out[0].t, 120);
    CHECK_EQ(r.overruns, 0u);
}

TEST_CASE(a_read_follows_the_ring_across_its_end_and_in_batches)
{
    fresh();
    bool lap = false;
    uint64_t t = 100;
    uint32_t wr = 0;
    /* 14 words, then 6 more across the end of the 16-word ring. */
    for (unsigned i = 0; i < 14u; ++i) {
        put(wr++, t += 10u, (i & 1u) == 0u);
    }
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 32u, &lap), 14);
    for (unsigned i = 0; i < 6u; ++i) {
        put(wr++, t += 10u, (i & 1u) != 0u);
    }
    /* Batches of 4: the words stay for the next call. */
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 4u, &lap), 4);
    CHECK(!lap);
    CHECK_EQ(out[0].t, 250);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 4u, &lap), 2);
    CHECK_EQ(out[0].t, 290);
    CHECK_EQ(out[1].t, 300);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 4u, &lap), 0);
    CHECK(!lap);
}

TEST_CASE(a_lap_of_the_dma_shows_in_the_slot_of_the_last_word_taken)
{
    fresh();
    bool lap = false;
    uint64_t t = 0;
    uint32_t wr = 0;
    for (unsigned i = 0; i < 5u; ++i) {
        put(wr++, t += 10u, (i & 1u) == 0u);
    }
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 32u, &lap), 5);
    /* The DMA writes 20 words before the next read: more than the ring. */
    for (unsigned i = 0; i < 20u; ++i) {
        put(wr++, t += 10u, (i & 1u) == 0u);
    }
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 32u, &lap), 0);
    CHECK(lap);
    CHECK_EQ(r.overruns, 1u);
    /* The reader starts after the newest word: nothing more until more is
     * written, and the next word is read in order. */
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 32u, &lap), 0);
    CHECK(!lap);
    put(wr++, t += 10u, true);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 32u, &lap), 1);
    CHECK_EQ(out[0].t, t);
    CHECK_EQ(r.overruns, 1u);
}

TEST_CASE(a_whole_ring_written_between_reads_is_a_lap_too)
{
    fresh();
    bool lap = false;
    uint64_t t = 0;
    uint32_t wr = 0;
    for (unsigned i = 0; i < 3u; ++i) {
        put(wr++, t += 10u, (i & 1u) == 0u);
    }
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 32u, &lap), 3);
    /* Exactly SIZE words: the write position is where it was. */
    for (unsigned i = 0; i < SIZE; ++i) {
        put(wr++, t += 10u, (i & 1u) != 0u);
    }
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, wr % SIZE, t, out, 32u, &lap), 0);
    CHECK(lap);
}

TEST_CASE(a_lap_before_the_first_read_is_an_overrun)
{
    fresh();
    bool lap = false;
    uint64_t t = 0;
    /* The DMA writes SIZE + 5 words before the first read. */
    for (unsigned i = 0; i < SIZE + 5u; ++i) {
        put(i, t += 10u, (i & 1u) == 0u);
    }
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 5u, t, out, 32u, &lap), 0);
    CHECK(lap);
    CHECK_EQ(r.overruns, 1u);
    /* The reader is after the newest word: the next word comes alone. */
    put(5, t += 10u, true);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 6u, t, out, 32u, &lap), 1);
    CHECK(!lap);
    /* Just short of a lap, the ring is read whole and no lap shows. */
    fresh();
    for (unsigned i = 0; i < SIZE - 1u; ++i) {
        put(i, t += 10u, (i & 1u) == 0u);
    }
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, SIZE - 1u, t, out, 32u, &lap),
             (int)SIZE - 1);
    CHECK(!lap);
}

TEST_CASE(a_ring_that_holds_no_words_yet_is_not_a_lap)
{
    fresh();
    bool lap = true;
    memset(ring, 0, sizeof(ring));
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 0u, 0u, out, 8u, &lap), 0);
    CHECK(!lap);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 0u, 0u, out, 8u, &lap), 0);
    CHECK(!lap);
}

TEST_CASE(a_bad_argument_reads_nothing)
{
    bool lap = true;
    fresh();
    edge_ring_init(NULL);
    CHECK_EQ(edge_ring_take(NULL, ring, SIZE, 1u, 0u, out, 8u, &lap), 0);
    CHECK(!lap);
    CHECK_EQ(edge_ring_take(&r, NULL, SIZE, 1u, 0u, out, 8u, &lap), 0);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 1u, 0u, NULL, 8u, &lap), 0);
    CHECK_EQ(edge_ring_take(&r, ring, 0u, 1u, 0u, out, 8u, &lap), 0);
    CHECK_EQ(edge_ring_take(&r, ring, 12u, 1u, 0u, out, 8u, &lap), 0);
    put(0, 5, true);
    CHECK_EQ(edge_ring_take(&r, ring, SIZE, 1u, 5u, out, 8u, NULL), 1);
}

int main(void)
{
    RUN(a_word_carries_a_31_bit_count_and_a_level);
    RUN(the_count_runs_down_so_elapsed_ticks_are_its_negative);
    RUN(the_estimate_places_a_count_among_the_ticks_it_repeats_in);
    RUN(a_tick_before_the_start_does_not_exist);
    RUN(a_ring_read_returns_the_words_written_since_the_last_read);
    RUN(a_read_follows_the_ring_across_its_end_and_in_batches);
    RUN(a_lap_of_the_dma_shows_in_the_slot_of_the_last_word_taken);
    RUN(a_whole_ring_written_between_reads_is_a_lap_too);
    RUN(a_lap_before_the_first_read_is_an_overrun);
    RUN(a_ring_that_holds_no_words_yet_is_not_a_lap);
    RUN(a_bad_argument_reads_nothing);
    return test_summary("edge_ring");
}
