/*
 * The tone service core 1 runs for the phase tap (shared/sense/tone_svc.c).
 *
 * The capture is modelled as the PIO program leaves it: raw edges go
 * through the hold-off rule (tone_holdoff_edge(), which test_tone_pio holds
 * the program to) and become words in a ring the service reads.
 *
 * Under test: a beep from words, with its start, length, pitch and carrier
 * in the page's units; the present limiting how far the detector is
 * advanced, so a beep ends only after its silence and no word is late; a
 * 144 kHz carrier reaching the detector as one pulse a burst; a lap of the
 * ring and a dropped word ending the beep under way and showing in the
 * status; the order: not running, a new set-up restarting the detector and
 * keeping the ring reader, a new capture restarting both, a set-up the
 * detector refuses; a silence longer than the counter's wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "tone_svc.h"

#define TICK TONE_SVC_TICK_HZ

static uint32_t   ring[TONE_RING_WORDS];
static uint32_t   wr;
static tone_svc_t svc;
static tone_cmd_t cmd;
static tone_status_t st;
static tone_rec_t recs[TONE_SVC_BEEPS * 4u];
static size_t     nrec;
static tone_holdoff_t hold;

/* The pin's raw edges, in time order, and how many the capture has seen. */
#define RAW_MAX 60000u
static tone_edge_t raw_e[RAW_MAX];
static size_t      raw_n, raw_i;

static void fresh(void)
{
    memset(ring, 0, sizeof(ring));
    wr = 0;
    tone_svc_init(&svc);
    memset(&cmd, 0, sizeof(cmd));
    cmd.gen = 1u;
    cmd.cap_gen = 1u;
    cmd.run = true;
    cmd.f_min_hz = 400u;
    cmd.f_max_hz = 6500u;
    cmd.split_pct = 8u;
    cmd.gap_ms = 3u;
    cmd.min_periods = 3u;
    tone_holdoff_init(&hold, TICK, TONE_SVC_HOLD_NS);
    nrec = 0;
    raw_n = raw_i = 0;
}

static void word(uint64_t t, bool level)
{
    ring[wr % TONE_RING_WORDS] =
        edge_word((uint32_t)(0u - (uint32_t)t) & EDGE_COUNT_MASK, level);
    ++wr;
}

/* A raw edge of the pin. */
static void raw(uint64_t t, bool level)
{
    CHECK(raw_n < RAW_MAX);
    raw_e[raw_n].t = t;
    raw_e[raw_n].level = level;
    ++raw_n;
}

/* The capture up to @p now, as the PIO program leaves it: the edges to
 * now through the hold-off, and a fall whose low has lasted it. */
static void capture(uint64_t now)
{
    tone_edge_t o[2];
    while (raw_i < raw_n && raw_e[raw_i].t <= now) {
        const size_t n = tone_holdoff_edge(&hold, raw_e[raw_i].t,
                                           raw_e[raw_i].level, o);
        for (size_t i = 0; i < n; ++i) {
            word(o[i].t, o[i].level);
        }
        ++raw_i;
    }
    if (tone_holdoff_advance(&hold, now, o) == 1u) {
        word(o[0].t, o[0].level);
    }
}

/* One pass at tick @p now. */
static void pass(uint64_t now)
{
    capture(now);
    tone_rec_t r[TONE_SVC_BEEPS];
    const size_t n = tone_svc_step(&svc, &cmd, ring, wr % TONE_RING_WORDS, now,
                                   false, r, &st);
    for (size_t i = 0; i < n && nrec < sizeof(recs) / sizeof(recs[0]); ++i) {
        recs[nrec++] = r[i];
    }
}

/* Passes every 1 ms from @p from to @p to, in ticks. */
static void run_passes(uint64_t from, uint64_t to)
{
    for (uint64_t t = from; t <= to; t += TICK / 1000u) {
        pass(t);
    }
}

/* @p count bursts of a tone of @p hz, each high for @p on ticks, chopped by
 * a carrier of @p carrier_hz at 50 % (0: not chopped), from @p t0.  The
 * tick after the last edge is returned. */
static uint64_t beep(uint64_t t0, unsigned count, uint32_t hz, uint64_t on,
                     uint32_t carrier_hz)
{
    const uint64_t period = TICK / hz;
    uint64_t end = t0;
    for (unsigned b = 0; b < count; ++b) {
        const uint64_t s = t0 + b * period;
        if (carrier_hz == 0u) {
            raw(s, true);
            raw(s + on, false);
            end = s + on;
        } else {
            const uint64_t c = TICK / carrier_hz;
            for (uint64_t k = 0; k + c <= on; k += c) {
                raw(s + k, true);
                raw(s + k + c / 2u, false);
                end = s + k + c / 2u;
            }
        }
    }
    return end;
}

TEST_CASE(a_beep_is_reported_in_the_pages_units)
{
    fresh();
    /* 40 bursts of 2 kHz, high for a fifth of the period, from 10 ms. */
    const uint64_t t0 = TICK / 100u;
    const uint64_t end = beep(t0, 40u, 2000u, TICK / 2000u / 5u, 0u);
    run_passes(0, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 1u);
    CHECK_EQ(recs[0].start_ms, 10u);
    /* 39 periods of 0.5 ms and the last burst's 0.1 ms: 19.6 ms. */
    CHECK_EQ(recs[0].len_dms, 196u);
    CHECK_EQ(recs[0].freq_dhz, 20000u);
    CHECK_EQ(recs[0].bursts, 40u);
    CHECK_EQ(recs[0].carrier_hhz, 0u);
    CHECK_EQ(recs[0].flags, 0u);
    CHECK(st.running);
    CHECK(!st.overrun);
    CHECK(!st.beep);
    CHECK_EQ(st.gen, 1u);
}

TEST_CASE(a_status_follows_the_beep_while_it_sounds)
{
    fresh();
    const uint64_t t0 = TICK / 100u;
    const uint64_t end = beep(t0, 40u, 2000u, TICK / 2000u / 5u, 0u);
    /* Half way through the beep, with the words so far. */
    run_passes(0, t0 + 10u * TICK / 1000u);
    CHECK(st.running);
    CHECK(st.beep);
    CHECK(st.tone);
    CHECK(st.win_freq_dhz >= 19990u && st.win_freq_dhz <= 20010u);
    CHECK(st.win_periods >= 2u);
    CHECK_EQ(nrec, 0u);
    run_passes(t0 + 10u * TICK / 1000u, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 1u);
    CHECK(!st.beep);
}

TEST_CASE(a_carrier_is_reported_in_100_hz_steps)
{
    fresh();
    const uint64_t t0 = TICK / 100u;
    /* 3 kHz bursts of 0.2 ms chopped at 24 kHz. */
    const uint64_t end = beep(t0, 60u, 3000u, TICK / 5000u, 24000u);
    run_passes(0, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 1u);
    CHECK_EQ(recs[0].freq_dhz, 30000u);
    CHECK_EQ(recs[0].bursts, 60u);
    /* A tick is 26.7 ns; the carrier reads to a tenth of a percent. */
    CHECK(recs[0].carrier_hhz >= 239u && recs[0].carrier_hhz <= 241u);
}

TEST_CASE(a_144_khz_carrier_reaches_the_detector_as_one_pulse_a_burst)
{
    fresh();
    const uint64_t t0 = TICK / 100u;
    const uint64_t end = beep(t0, 60u, 3000u, TICK / 5000u, 144000u);
    run_passes(0, end + 10u * TICK / 1000u);
    /* 60 bursts: a rise and a fall each, the carrier's 28 lows a burst held
     * back by the hold-off. */
    CHECK_EQ(wr, 120u);
    CHECK_EQ(nrec, 1u);
    CHECK_EQ(recs[0].freq_dhz, 30000u);
    CHECK_EQ(recs[0].bursts, 60u);
    CHECK_EQ(recs[0].carrier_hhz, 0u);
}

TEST_CASE(a_lap_of_the_ring_cuts_the_beep_and_shows_in_the_status)
{
    fresh();
    const uint64_t t0 = TICK / 100u;
    const uint64_t end = beep(t0, 20u, 2000u, TICK / 2000u / 5u, 0u);
    run_passes(0, end);
    CHECK(st.beep);
    CHECK(!st.overrun);
    /* The DMA writes a whole ring and more before the next pass. */
    uint64_t t = end + 1000u;
    for (unsigned i = 0; i < TONE_RING_WORDS + 8u; ++i) {
        word(t += 1000u, (i & 1u) == 0u);
    }
    pass(t + 1000u);
    CHECK(st.overrun);
    CHECK(!st.beep);
    CHECK_EQ(nrec, 1u);               /* the beep so far ends at its last edge */
    CHECK(recs[0].bursts >= 19u && recs[0].bursts <= 20u);
}

TEST_CASE(a_dropped_word_ends_the_beep_under_way)
{
    fresh();
    const uint64_t t0 = TICK / 100u;
    const uint64_t end = beep(t0, 20u, 2000u, TICK / 2000u / 5u, 0u);
    run_passes(0, end);
    CHECK(st.beep);
    tone_rec_t r[TONE_SVC_BEEPS];
    const size_t n = tone_svc_step(&svc, &cmd, ring, wr % TONE_RING_WORDS,
                                   end + 100u, true, r, &st);
    CHECK_EQ(n, 1u);
    CHECK(st.overrun);
    CHECK(!st.beep);
    /* Sticky for the capture. */
    pass(end + 200u);
    CHECK(st.overrun);
}

TEST_CASE(a_stopped_order_runs_nothing_and_reports_not_running)
{
    fresh();
    cmd.run = false;
    cmd.gen = 7u;
    pass(1000u);
    CHECK(!st.running);
    CHECK_EQ(st.gen, 7u);
    CHECK(!st.beep);
    /* Words in the ring are not read. */
    const uint64_t end = beep(1000u, 20u, 2000u, TICK / 2000u / 5u, 0u);
    pass(end + TICK / 100u);
    CHECK(!st.running);
    CHECK_EQ(nrec, 0u);
    cmd.run = true;
    cmd.cap_gen = 2u;
    pass(end + TICK / 100u);
    CHECK(st.running);
}

TEST_CASE(a_new_set_up_restarts_the_detector_and_keeps_the_ring_reader)
{
    fresh();
    const uint64_t t0 = TICK / 100u;
    /* Two beeps; the set-up changes between them. */
    uint64_t end = beep(t0, 20u, 2000u, TICK / 2000u / 5u, 0u);
    run_passes(0, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 1u);
    cmd.gen = 2u;
    cmd.f_max_hz = 3000u;
    pass(end + 11u * TICK / 1000u);
    CHECK_EQ(st.gen, 2u);
    CHECK(st.running);
    const uint64_t t1 = end + TICK / 50u;
    end = beep(t1, 20u, 2500u, TICK / 2500u / 5u, 0u);
    run_passes(t1 - TICK / 100u, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 2u);
    CHECK_EQ(recs[1].freq_dhz, 25000u);
    /* Out of the new range, 4 kHz, the same set-up hears nothing. */
    const uint64_t t2 = end + TICK / 50u;
    end = beep(t2, 20u, 4000u, TICK / 4000u / 5u, 0u);
    run_passes(t2 - TICK / 100u, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 2u);
}

TEST_CASE(a_new_capture_starts_the_ring_reader_again)
{
    fresh();
    const uint64_t t0 = TICK / 100u;
    uint64_t end = beep(t0, 20u, 2000u, TICK / 2000u / 5u, 0u);
    run_passes(0, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 1u);
    /* The capture restarts: the counter at 0, the DMA at slot 0. */
    cmd.cap_gen = 2u;
    memset(ring, 0, sizeof(ring));
    wr = 0;
    tone_holdoff_init(&hold, TICK, TONE_SVC_HOLD_NS);
    end = beep(t0, 30u, 1000u, TICK / 1000u / 5u, 0u);
    run_passes(0, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 2u);
    CHECK_EQ(recs[1].freq_dhz, 10000u);
    CHECK_EQ(recs[1].start_ms, 10u);
    CHECK(!st.overrun);
}

TEST_CASE(a_set_up_the_detector_refuses_runs_nothing)
{
    fresh();
    cmd.gap_ms = 1u;                  /* shorter than 400 Hz's period */
    pass(1000u);
    CHECK(!st.running);
    CHECK_EQ(st.gen, 1u);
    cmd.gap_ms = 3u;
    cmd.gen = 2u;
    pass(2000u);
    CHECK(st.running);
}

TEST_CASE(a_silence_longer_than_the_counters_wrap_keeps_the_time)
{
    fresh();
    /* A beep 130 s after the start, with no edge before it. */
    const uint64_t t0 = 130ull * TICK;
    pass(t0 - TICK / 1000u);
    CHECK(st.running);
    const uint64_t end = beep(t0, 20u, 2000u, TICK / 2000u / 5u, 0u);
    run_passes(t0 - TICK / 1000u, end + 10u * TICK / 1000u);
    CHECK_EQ(nrec, 1u);
    CHECK_EQ(recs[0].start_ms, 130000u);
}

TEST_CASE(the_present_in_ticks_follows_the_microsecond_timer)
{
    CHECK_EQ(tone_svc_ticks(0u), 0u);
    CHECK_EQ(tone_svc_ticks(1000000u), 37500000u);
    CHECK_EQ(tone_svc_ticks(8u), 300u);
    CHECK_EQ(tone_svc_ticks(1u), 37u);
    CHECK_EQ(tone_svc_ticks(3600ull * 1000000u * 24u), 3240000000000ull);
}

TEST_CASE(a_beep_is_clipped_to_its_registers)
{
    tone_beep_t b;
    memset(&b, 0, sizeof(b));
    b.start = 5ull * 24 * 3600 * TICK;
    b.end = b.start + 100ull * TICK;       /* 100 s, more than 6553.5 ms */
    b.freq_hz = 7000.0f;                   /* 70000 dHz */
    b.carrier_hz = 8.0e6f;
    b.bursts = 100000u;
    b.flags = 0xFFu;
    tone_rec_t r;
    tone_svc_rec(&b, &r);
    CHECK_EQ(r.start_ms, (uint32_t)(5ull * 24 * 3600 * 1000));
    CHECK_EQ(r.len_dms, 65535u);
    CHECK_EQ(r.freq_dhz, 65535u);
    CHECK_EQ(r.carrier_hhz, 65535u);
    CHECK_EQ(r.bursts, 65535u);
    CHECK_EQ(r.flags, 3u);
    b.freq_hz = -1.0f;
    tone_svc_rec(&b, &r);
    CHECK_EQ(r.freq_dhz, 0u);
}

TEST_CASE(bad_arguments_do_nothing)
{
    fresh();
    tone_svc_init(NULL);
    tone_rec_t r[TONE_SVC_BEEPS];
    CHECK_EQ(tone_svc_step(NULL, &cmd, ring, 0u, 0u, false, r, &st), 0);
    CHECK_EQ(tone_svc_step(&svc, NULL, ring, 0u, 0u, false, r, &st), 0);
    CHECK_EQ(tone_svc_step(&svc, &cmd, ring, 0u, 0u, false, r, NULL), 0);
    /* No ring: the pass still advances time and reports. */
    CHECK_EQ(tone_svc_step(&svc, &cmd, NULL, 0u, 100u, false, r, &st), 0);
    CHECK(st.running);
    /* No place for the beeps: they stay in the detector's queue. */
    CHECK_EQ(tone_svc_step(&svc, &cmd, ring, 0u, 200u, false, NULL, &st), 0);
}

int main(void)
{
    RUN(a_beep_is_reported_in_the_pages_units);
    RUN(a_status_follows_the_beep_while_it_sounds);
    RUN(a_carrier_is_reported_in_100_hz_steps);
    RUN(a_144_khz_carrier_reaches_the_detector_as_one_pulse_a_burst);
    RUN(a_lap_of_the_ring_cuts_the_beep_and_shows_in_the_status);
    RUN(a_dropped_word_ends_the_beep_under_way);
    RUN(a_stopped_order_runs_nothing_and_reports_not_running);
    RUN(a_new_set_up_restarts_the_detector_and_keeps_the_ring_reader);
    RUN(a_new_capture_starts_the_ring_reader_again);
    RUN(a_set_up_the_detector_refuses_runs_nothing);
    RUN(a_silence_longer_than_the_counters_wrap_keeps_the_time);
    RUN(the_present_in_ticks_follows_the_microsecond_timer);
    RUN(a_beep_is_clipped_to_its_registers);
    RUN(bad_arguments_do_nothing);
    return test_summary("tone_svc");
}
