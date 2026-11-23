/*
 * The pass-through's pulse decoder (protocols/passthru/pass_decode.c).
 *
 * Under test, on synthetic edges with microsecond stamps: pulses of 400us
 * and 2500us taken at their exact width, highs of 399us and 2601us counted
 * as glitches and not taken, 2600us taken; frame periods at 40Hz and
 * 560Hz; the 32-bit stamp's wrap inside a pulse and inside a period; all 9
 * inputs and all 9 outputs moving on one state; the first state giving
 * levels and no pulse; a signal lost at 100ms when 3 periods are shorter
 * and at 3 periods when they are longer, one microsecond either side, with
 * width and period cleared and the gap that ends the loss counting no
 * missed frame; missed-frame counts at 1.5 periods, 1.50005, 2.5 and 3
 * periods, and a new slower rate learnt from two long gaps in a row; a
 * bidirectional DShot pin's edges skipped without a glitch or a figure,
 * and a pin leaving the skip mask starting clean; a restart forgetting the
 * high in progress and the gap; a reference later than the clock counting
 * no time.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "pass_decode.h"

/** @brief The state bit of input k and output k. */
#define IN(k)  (1u << PASS_IN_BIT(k))
#define OUT(k) (1u << PASS_OUT_BIT(k))

static pass_dec_t d;
static uint32_t   lvl;

/* Set or clear @p bits at @p t and hand the state to the decoder. */
static void hi(uint32_t bits, uint32_t t)
{
    lvl |= bits;
    pass_dec_event(&d, lvl, t);
}

static void lo(uint32_t bits, uint32_t t)
{
    lvl &= ~bits;
    pass_dec_event(&d, lvl, t);
}

/* A decoder that has seen the all-low state at @p t0. */
static void start(uint32_t t0)
{
    pass_dec_init(&d, t0);
    lvl = 0u;
    pass_dec_event(&d, 0u, t0);
}

/* @p n pulses of @p w us on @p bits, a rise every @p period us from @p t0.
 * Returns the time of the last rise. */
static uint32_t train(uint32_t bits, uint32_t t0, uint32_t period,
                      uint32_t w, unsigned n)
{
    uint32_t t = t0;
    for (unsigned i = 0; i < n; ++i) {
        t = t0 + i * period;
        hi(bits, t);
        lo(bits, t + w);
    }
    return t;
}

static const pass_pin_t *in_pin(unsigned k)
{
    return &d.pin[PASS_IN_BIT(k)];
}

TEST_CASE(pulses_of_400_and_2500us_are_taken_at_their_width)
{
    start(0u);
    hi(IN(0), 1000u);
    lo(IN(0), 1400u);
    CHECK_EQ(in_pin(0)->width_us, 400u);
    CHECK_EQ(in_pin(0)->frames, 1u);
    hi(IN(0), 21000u);
    lo(IN(0), 23500u);
    CHECK_EQ(in_pin(0)->width_us, 2500u);
    CHECK_EQ(in_pin(0)->frames, 2u);
    CHECK_EQ(in_pin(0)->glitches, 0u);
    CHECK_EQ(in_pin(0)->period_us, 20000u);
}

TEST_CASE(highs_of_399_and_2601us_are_glitches_and_400_and_2600_pulses)
{
    static const struct { uint32_t w; bool taken; } c[] = {
        { 399u, false }, { 400u, true }, { 2600u, true }, { 2601u, false },
        { 1u, false }, { 30000u, false },
    };
    for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        start(0u);
        train(IN(3), 1000u, 20000u, 1500u, 2u);
        const uint32_t frames = in_pin(3)->frames;
        hi(IN(3), 50000u);
        lo(IN(3), 50000u + c[i].w);
        if (c[i].taken) {
            CHECK_EQ(in_pin(3)->width_us, c[i].w);
            CHECK_EQ(in_pin(3)->frames, frames + 1u);
            CHECK_EQ(in_pin(3)->glitches, 0u);
        } else {
            /* Counted, not taken: the last pulse stays, no frame. */
            CHECK_EQ(in_pin(3)->width_us, 1500u);
            CHECK_EQ(in_pin(3)->frames, frames);
            CHECK_EQ(in_pin(3)->glitches, 1u);
        }
    }
}

TEST_CASE(a_glitch_rise_is_no_frame_for_the_period)
{
    start(0u);
    train(IN(1), 1000u, 20000u, 1500u, 3u);
    /* A 50us spike between two frames. */
    hi(IN(1), 51000u);
    lo(IN(1), 51050u);
    train(IN(1), 61000u, 20000u, 1500u, 2u);
    CHECK_EQ(in_pin(1)->period_us, 20000u);
    CHECK_EQ(in_pin(1)->missed, 0u);
    CHECK_EQ(in_pin(1)->glitches, 1u);
}

TEST_CASE(frame_periods_at_40_and_560hz)
{
    start(0u);
    train(IN(2), 100u, 25000u, 1500u, 40u);
    CHECK_EQ(in_pin(2)->period_us, 25000u);
    CHECK_EQ(in_pin(2)->frames, 40u);
    CHECK_EQ(in_pin(2)->missed, 0u);
    /* 560Hz: 1786us and 1785us periods as a 1us clock gives them. */
    start(0u);
    uint32_t t = 100u;
    for (unsigned i = 0; i < 560u; ++i) {
        hi(IN(4), t);
        lo(IN(4), t + 1000u);
        t += (i % 7u == 0u) ? 1785u : 1786u;
    }
    CHECK(in_pin(4)->period_us == 1785u || in_pin(4)->period_us == 1786u);
    CHECK_EQ(in_pin(4)->frames, 560u);
    CHECK_EQ(in_pin(4)->missed, 0u);
    pass_dec_now(&d, t);
    CHECK(!in_pin(4)->lost);
}

TEST_CASE(the_stamps_wrap_inside_a_pulse_and_a_period)
{
    const uint32_t t0 = 0xFFFFFFFFu - 30000u;
    start(t0);
    hi(IN(5), t0 + 100u);
    lo(IN(5), t0 + 1600u);
    /* The next rise is past the wrap, and its fall too. */
    hi(IN(5), t0 + 20100u);
    lo(IN(5), t0 + 21600u);
    CHECK_EQ(in_pin(5)->width_us, 1500u);
    CHECK_EQ(in_pin(5)->period_us, 20000u);
    /* One whose high spans the wrap. */
    const uint32_t r = 0xFFFFFFFFu - 700u;
    start(r - 20000u);
    hi(IN(6), r - 20000u);
    lo(IN(6), r - 18500u);
    hi(IN(6), r);
    lo(IN(6), r + 1500u);
    CHECK_EQ(in_pin(6)->width_us, 1500u);
    CHECK_EQ(in_pin(6)->period_us, 20000u);
    pass_dec_now(&d, r + 99999u);
    CHECK(!in_pin(6)->lost);
    pass_dec_now(&d, r + 100000u);
    CHECK(in_pin(6)->lost);
}

TEST_CASE(nine_inputs_and_nine_outputs_on_one_edge)
{
    start(0u);
    uint32_t all = 0u;
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        all |= IN(k) | OUT(k);
    }
    for (unsigned f = 0; f < 3u; ++f) {
        const uint32_t t = 1000u + f * 20000u;
        hi(all, t);
        /* Each falls on its own: 1000us + 100us a channel, outputs 3us
         * later, as through MIRROR. */
        for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
            lo(IN(k), t + 1000u + 100u * k);
            lo(OUT(k), t + 1003u + 100u * k);
        }
    }
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        CHECK_EQ(in_pin(k)->width_us, 1000u + 100u * k);
        CHECK_EQ(d.pin[PASS_OUT_BIT(k)].width_us, 1003u + 100u * k);
        CHECK_EQ(in_pin(k)->period_us, 20000u);
        CHECK_EQ(in_pin(k)->frames, 3u);
    }
    /* And all 18 falling on one state as well. */
    hi(all, 80000u);
    lo(all, 81200u);
    for (unsigned b = 0; b < PASS_PINS; ++b) {
        CHECK_EQ(d.pin[b].width_us, 1200u);
    }
}

TEST_CASE(the_first_state_gives_levels_and_no_pulse)
{
    pass_dec_init(&d, 0u);
    lvl = IN(0) | OUT(0);
    pass_dec_event(&d, lvl, 500u);
    lo(IN(0) | OUT(0), 1500u);
    CHECK_EQ(in_pin(0)->frames, 0u);
    CHECK_EQ(in_pin(0)->glitches, 0u);
    CHECK_EQ(in_pin(0)->width_us, 0u);
    /* Bits above the 18 pins are no pins. */
    pass_dec_event(&d, 0xFFFC0000u, 2000u);
    CHECK_EQ(d.level, 0u);
}

TEST_CASE(lost_at_100ms_while_3_periods_are_shorter)
{
    /* 50Hz and 40Hz: 3 periods are 60ms and 75ms, under 100ms. */
    static const uint32_t period[] = { 20000u, 25000u };
    for (unsigned i = 0; i < 2u; ++i) {
        start(0u);
        const uint32_t last = train(IN(7), 1000u, period[i], 1500u, 5u);
        CHECK_EQ(pass_dec_lost_us(in_pin(7)), 100000u);
        pass_dec_now(&d, last + 99999u);
        CHECK(!in_pin(7)->lost);
        pass_dec_now(&d, last + 100000u);
        CHECK(in_pin(7)->lost);
        CHECK_EQ(in_pin(7)->width_us, 0u);
        CHECK_EQ(in_pin(7)->period_us, 0u);
        /* The gap that ends the loss counts no missed frame. */
        train(IN(7), last + 400000u, period[i], 1500u, 3u);
        CHECK(!in_pin(7)->lost);
        CHECK_EQ(in_pin(7)->missed, 0u);
        CHECK_EQ(in_pin(7)->period_us, period[i]);
    }
}

TEST_CASE(lost_at_3_periods_while_they_are_longer_than_100ms)
{
    /* 20Hz: 3 periods are 150ms. */
    start(0u);
    const uint32_t last = train(IN(8), 1000u, 50000u, 1500u, 4u);
    CHECK_EQ(pass_dec_lost_us(in_pin(8)), 150000u);
    pass_dec_now(&d, last + 149999u);
    CHECK(!in_pin(8)->lost);
    pass_dec_now(&d, last + 150000u);
    CHECK(in_pin(8)->lost);
}

TEST_CASE(a_pin_that_never_pulses_is_lost_100ms_after_the_start)
{
    start(5000u);
    pass_dec_now(&d, 104999u);
    CHECK(!in_pin(0)->lost);
    pass_dec_now(&d, 105000u);
    CHECK(in_pin(0)->lost);
    /* A line stuck high: no fall, no pulse, lost all the same. */
    start(0u);
    hi(IN(1), 10u);
    pass_dec_now(&d, 100000u);
    CHECK(in_pin(1)->lost);
}

TEST_CASE(a_gap_past_the_lost_time_between_two_passes_is_a_loss)
{
    start(0u);
    const uint32_t last = train(IN(0), 1000u, 20000u, 1500u, 3u);
    /* No pass_dec_now() in between: the next pulse 150ms later. */
    train(IN(0), last + 150000u, 20000u, 1500u, 2u);
    CHECK_EQ(in_pin(0)->missed, 0u);
    CHECK_EQ(in_pin(0)->period_us, 20000u);
}

TEST_CASE(missed_frames_count_round_of_the_gap_less_one)
{
    static const struct { uint32_t gap; uint32_t missed; } c[] = {
        { 30000u, 0u },   /* 1.5 periods: not over, taken as the period */
        { 30001u, 1u },   /* just over 1.5 */
        { 40000u, 1u },   /* 2 */
        { 49999u, 1u },   /* under 2.5 */
        { 50000u, 2u },   /* 2.5 rounds up */
        { 60000u, 2u },   /* 3 */
        { 99000u, 4u },   /* 4.95, under the 100ms lost time */
    };
    for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        start(0u);
        const uint32_t last = train(IN(2), 1000u, 20000u, 1500u, 3u);
        hi(IN(2), last + c[i].gap);
        lo(IN(2), last + c[i].gap + 1500u);
        if (in_pin(2)->missed != c[i].missed) {
            T_FAIL("gap %u: missed %u, want %u", (unsigned)c[i].gap,
                   (unsigned)in_pin(2)->missed, (unsigned)c[i].missed);
        }
    }
}

TEST_CASE(two_long_gaps_in_a_row_are_a_new_rate)
{
    start(0u);
    uint32_t t = train(IN(3), 1000u, 20000u, 1500u, 3u);
    /* The receiver drops to 25Hz: 40ms gaps from now on. */
    t = train(IN(3), t + 40000u, 40000u, 1500u, 6u);
    CHECK_EQ(in_pin(3)->period_us, 40000u);
    /* Only the first long gap counted. */
    CHECK_EQ(in_pin(3)->missed, 1u);
    /* Two long gaps that differ by more than 1/8 are two misses. */
    start(0u);
    t = train(IN(4), 1000u, 20000u, 1500u, 3u);
    hi(IN(4), t + 40000u);
    lo(IN(4), t + 41500u);
    hi(IN(4), t + 40000u + 60000u);
    lo(IN(4), t + 101500u);
    CHECK_EQ(in_pin(4)->period_us, 20000u);
    CHECK_EQ(in_pin(4)->missed, 3u);
}

/* A bidirectional DShot600 output: 16 bits of 1.67us every 1ms, each high
 * 0.6us to 1.25us (here 1us or 2us at a 1us clock), and the ESC's reply. */
static void dshot_frames(uint32_t bit, uint32_t t0, unsigned frames)
{
    for (unsigned f = 0; f < frames; ++f) {
        uint32_t t = t0 + f * 1000u;
        for (unsigned b = 0; b < 16u; ++b) {
            hi(bit, t);
            lo(bit, t + 1u + (b & 1u));
            t += 2u;
        }
        /* The reply 30us later: 21 edges of 1 to 3us. */
        t += 30u;
        for (unsigned e = 0; e < 21u; ++e) {
            if ((e & 1u) == 0u) {
                hi(bit, t);
            } else {
                lo(bit, t);
            }
            t += 1u + e % 3u;
        }
        lo(bit, t);
    }
}

TEST_CASE(a_bidirectional_dshot_pins_edges_are_skipped)
{
    start(0u);
    pass_dec_skip(&d, OUT(4), 0u);
    /* The DShot line beside a servo input that keeps going. */
    for (unsigned f = 0; f < 5u; ++f) {
        dshot_frames(OUT(4), 1000u + f * 20000u, 18u);
        hi(IN(4), 1100u + f * 20000u);
        lo(IN(4), 2600u + f * 20000u);
    }
    const pass_pin_t *o = &d.pin[PASS_OUT_BIT(4)];
    CHECK_EQ(o->glitches, 0u);
    CHECK_EQ(o->frames, 0u);
    CHECK_EQ(o->width_us, 0u);
    CHECK_EQ(in_pin(4)->width_us, 1500u);
    CHECK_EQ(in_pin(4)->frames, 5u);
    CHECK_EQ(in_pin(4)->glitches, 0u);
    /* Skipped pins are never lost. */
    pass_dec_now(&d, 900000u);
    CHECK(!o->lost);
    /* Without the skip, the same line is glitches. */
    start(0u);
    dshot_frames(OUT(4), 1000u, 2u);
    CHECK(d.pin[PASS_OUT_BIT(4)].glitches > 0u);
    CHECK_EQ(d.pin[PASS_OUT_BIT(4)].frames, 0u);
}

TEST_CASE(a_pin_entering_or_leaving_the_skip_mask_starts_clean)
{
    start(0u);
    train(OUT(1), 1000u, 20000u, 1500u, 3u);
    CHECK_EQ(d.pin[PASS_OUT_BIT(1)].width_us, 1500u);
    pass_dec_skip(&d, OUT(1), 70000u);
    CHECK_EQ(d.pin[PASS_OUT_BIT(1)].width_us, 0u);
    CHECK_EQ(d.pin[PASS_OUT_BIT(1)].frames, 0u);
    pass_dec_skip(&d, 0u, 80000u);
    pass_dec_now(&d, 179999u);
    CHECK(!d.pin[PASS_OUT_BIT(1)].lost);
    pass_dec_now(&d, 180000u);
    CHECK(d.pin[PASS_OUT_BIT(1)].lost);
}

TEST_CASE(a_restart_forgets_the_high_and_the_gap_and_keeps_the_counts)
{
    start(0u);
    uint32_t t = train(IN(0), 1000u, 20000u, 1500u, 3u);
    hi(IN(0), t + 20000u);
    pass_dec_restart(&d, lvl, t + 20500u);
    /* The fall of the high from before the restart is no pulse. */
    lo(IN(0), t + 21500u);
    CHECK_EQ(in_pin(0)->frames, 3u);
    CHECK_EQ(in_pin(0)->width_us, 1500u);
    /* The next pulse 60ms on counts no missed frame. */
    train(IN(0), t + 80000u, 20000u, 1500u, 2u);
    CHECK_EQ(in_pin(0)->missed, 0u);
    CHECK_EQ(in_pin(0)->frames, 5u);
    /* The lost time runs from the restart. */
    start(0u);
    pass_dec_restart(&d, 0u, 90000u);
    pass_dec_now(&d, 189999u);
    CHECK(!in_pin(5)->lost);
}

TEST_CASE(a_reference_later_than_the_clock_counts_no_time)
{
    start(0u);
    train(IN(0), 1000u, 20000u, 1500u, 2u);
    /* The clock read before the last stamp. */
    pass_dec_now(&d, 20000u);
    CHECK(!in_pin(0)->lost);
}

int main(void)
{
    RUN(pulses_of_400_and_2500us_are_taken_at_their_width);
    RUN(highs_of_399_and_2601us_are_glitches_and_400_and_2600_pulses);
    RUN(a_glitch_rise_is_no_frame_for_the_period);
    RUN(frame_periods_at_40_and_560hz);
    RUN(the_stamps_wrap_inside_a_pulse_and_a_period);
    RUN(nine_inputs_and_nine_outputs_on_one_edge);
    RUN(the_first_state_gives_levels_and_no_pulse);
    RUN(lost_at_100ms_while_3_periods_are_shorter);
    RUN(lost_at_3_periods_while_they_are_longer_than_100ms);
    RUN(a_pin_that_never_pulses_is_lost_100ms_after_the_start);
    RUN(a_gap_past_the_lost_time_between_two_passes_is_a_loss);
    RUN(missed_frames_count_round_of_the_gap_less_one);
    RUN(two_long_gaps_in_a_row_are_a_new_rate);
    RUN(a_bidirectional_dshot_pins_edges_are_skipped);
    RUN(a_pin_entering_or_leaving_the_skip_mask_starts_clean);
    RUN(a_restart_forgets_the_high_and_the_gap_and_keeps_the_counts);
    RUN(a_reference_later_than_the_clock_counts_no_time);
    return test_summary("pass_decode");
}
