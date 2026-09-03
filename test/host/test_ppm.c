/*
 * PPM frame layout.
 *
 * The failure this is about is a frame that is well formed and means
 * something other than what was asked.  PPM carries no channel count: a
 * receiver counts marks until it sees a gap, so a sync gap that has shrunk
 * into the range a channel can occupy is read as a ninth channel, and every
 * channel after it shifts.  Nothing about the signal looks wrong.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "ppm.h"

/* What a receiver would make of the runs: mark to mark is one channel. */
static uint32_t total(const uint16_t *runs, size_t n)
{
    uint32_t sum = 0u;
    for (size_t i = 0; i < n; ++i) {
        sum += runs[i];
    }
    return sum;
}

TEST_CASE(a_frame_carries_each_channel_from_one_mark_to_the_next)
{
    static const uint16_t ch[4] = { 1000u, 1500u, 2000u, 1234u };
    uint16_t runs[PPM_MAX_RUNS];
    const ppm_cfg_t cfg = PPM_CFG_DEFAULT();

    const size_t n = ppm_frame(ch, 4u, &cfg, runs, PPM_MAX_RUNS);
    CHECK_EQ(n, 10u);                       /* four channels plus the tail */

    for (unsigned c = 0; c < 4u; ++c) {
        CHECK_EQ(runs[c * 2u], PPM_DEFAULT_MARK_US);
        CHECK_EQ((uint16_t)(runs[c * 2u] + runs[c * 2u + 1u]), ch[c]);
    }
    CHECK_EQ(runs[8], PPM_DEFAULT_MARK_US); /* the terminating mark */
}

TEST_CASE(the_runs_add_up_to_exactly_one_frame)
{
    /* If they did not, the frame rate would drift by whatever was left over
     * and two outputs on the same rate would walk apart. */
    static const uint16_t ch[8] = {
        1000u, 1100u, 1200u, 1300u, 1400u, 1500u, 1600u, 1700u,
    };
    uint16_t runs[PPM_MAX_RUNS];
    ppm_cfg_t cfg = PPM_CFG_DEFAULT();

    for (uint8_t n = 1u; n <= PPM_MAX_CHANNELS; ++n) {
        const size_t got = ppm_frame(ch, n, &cfg, runs, PPM_MAX_RUNS);
        CHECK_EQ(got, (size_t)(n + 1u) * 2u);
        CHECK_EQ(total(runs, got), (uint32_t)cfg.frame_us);
    }

    cfg.frame_us = 20000u;
    const size_t got = ppm_frame(ch, 8u, &cfg, runs, PPM_MAX_RUNS);
    CHECK_EQ(total(runs, got), 20000u);
}

TEST_CASE(a_frame_whose_sync_gap_would_be_too_short_is_refused)
{
    static const uint16_t ch[8] = {
        2000u, 2000u, 2000u, 2000u, 2000u, 2000u, 2000u, 2000u,
    };
    uint16_t runs[PPM_MAX_RUNS];
    ppm_cfg_t cfg = PPM_CFG_DEFAULT();

    /* 16000 us of channels, 300 of mark: 22500 leaves 6200 and 19000 leaves
     * 2700, which is inside the range a channel can occupy. */
    CHECK(ppm_frame(ch, 8u, &cfg, runs, PPM_MAX_RUNS) != 0u);
    cfg.frame_us = 19000u;
    CHECK_EQ(ppm_frame(ch, 8u, &cfg, runs, PPM_MAX_RUNS), 0u);

    /* And nothing was written on the way to refusing. */
    cfg.frame_us = 16000u;
    for (unsigned i = 0; i < PPM_MAX_RUNS; ++i) {
        runs[i] = 0xEEEEu;
    }
    CHECK_EQ(ppm_frame(ch, 8u, &cfg, runs, PPM_MAX_RUNS), 0u);
    for (unsigned i = 0; i < PPM_MAX_RUNS; ++i) {
        CHECK_EQ(runs[i], 0xEEEEu);
    }
}

TEST_CASE(the_shortest_usable_frame_is_reported_before_it_is_needed)
{
    const ppm_cfg_t cfg = PPM_CFG_DEFAULT();
    /* Eight channels at the 2500 us ceiling, the terminating mark and the
     * smallest gap that is still a gap. */
    CHECK_EQ(ppm_min_frame_us(8u, &cfg), 8u * 2500u + 300u + 3000u);
    CHECK_EQ(ppm_min_frame_us(1u, &cfg), 2500u + 300u + 3000u);
    CHECK_EQ(ppm_min_frame_us(0u, &cfg), 0u);
    CHECK_EQ(ppm_min_frame_us(9u, &cfg), 0u);

    /* A frame at exactly that length carries the widest channels there are. */
    uint16_t ch[PPM_MAX_CHANNELS];
    for (unsigned i = 0; i < PPM_MAX_CHANNELS; ++i) {
        ch[i] = OUT_CEILING_US;
    }
    ppm_cfg_t at = cfg;
    at.frame_us = (uint16_t)ppm_min_frame_us(8u, &cfg);
    uint16_t runs[PPM_MAX_RUNS];
    CHECK(ppm_frame(ch, 8u, &at, runs, PPM_MAX_RUNS) != 0u);
}

TEST_CASE(a_channel_outside_the_range_a_servo_takes_is_refused)
{
    uint16_t ch[2] = { 1500u, 1500u };
    uint16_t runs[PPM_MAX_RUNS];
    const ppm_cfg_t cfg = PPM_CFG_DEFAULT();

    CHECK(ppm_frame(ch, 2u, &cfg, runs, PPM_MAX_RUNS) != 0u);
    ch[1] = (uint16_t)(OUT_FLOOR_US - 1u);
    CHECK_EQ(ppm_frame(ch, 2u, &cfg, runs, PPM_MAX_RUNS), 0u);
    ch[1] = (uint16_t)(OUT_CEILING_US + 1u);
    CHECK_EQ(ppm_frame(ch, 2u, &cfg, runs, PPM_MAX_RUNS), 0u);
}

TEST_CASE(a_mark_that_would_not_fit_inside_its_channel_is_refused)
{
    /* A 600 us mark and a 500 us channel is a mark that runs into the next
     * one: the space would have to be negative. */
    const uint16_t ch[1] = { 500u };
    uint16_t runs[PPM_MAX_RUNS];
    ppm_cfg_t cfg = PPM_CFG_DEFAULT();
    cfg.mark_us = 600u;
    CHECK_EQ(ppm_frame(ch, 1u, &cfg, runs, PPM_MAX_RUNS), 0u);
    cfg.mark_us = 500u;
    CHECK_EQ(ppm_frame(ch, 1u, &cfg, runs, PPM_MAX_RUNS), 0u);
    cfg.mark_us = 499u;
    CHECK(ppm_frame(ch, 1u, &cfg, runs, PPM_MAX_RUNS) != 0u);
}

TEST_CASE(the_defaults_fill_in_field_by_field)
{
    const uint16_t ch[2] = { 1500u, 1500u };
    uint16_t runs[PPM_MAX_RUNS];
    ppm_cfg_t cfg = { .mark_us = 0u, .frame_us = 0u, .sync_min_us = 0u };

    CHECK(ppm_frame(ch, 2u, &cfg, runs, PPM_MAX_RUNS) != 0u);
    CHECK_EQ(runs[0], PPM_DEFAULT_MARK_US);
    CHECK_EQ(total(runs, 6u), (uint32_t)PPM_DEFAULT_FRAME_US);

    /* And a null configuration is the default configuration. */
    CHECK(ppm_frame(ch, 2u, NULL, runs, PPM_MAX_RUNS) != 0u);
    CHECK_EQ(total(runs, 6u), (uint32_t)PPM_DEFAULT_FRAME_US);
}

TEST_CASE(a_count_or_a_buffer_that_does_not_fit_is_refused)
{
    const uint16_t ch[8] = { 1500u, 1500u, 1500u, 1500u,
                             1500u, 1500u, 1500u, 1500u };
    uint16_t runs[PPM_MAX_RUNS];
    const ppm_cfg_t cfg = PPM_CFG_DEFAULT();

    CHECK_EQ(ppm_frame(ch, 0u, &cfg, runs, PPM_MAX_RUNS), 0u);
    CHECK_EQ(ppm_frame(ch, 9u, &cfg, runs, PPM_MAX_RUNS), 0u);
    CHECK_EQ(ppm_frame(NULL, 2u, &cfg, runs, PPM_MAX_RUNS), 0u);
    CHECK_EQ(ppm_frame(ch, 2u, &cfg, NULL, PPM_MAX_RUNS), 0u);
    CHECK_EQ(ppm_frame(ch, 4u, &cfg, runs, 9u), 0u);   /* one short of ten */
    CHECK(ppm_frame(ch, 4u, &cfg, runs, 10u) != 0u);
}

int main(void)
{
    RUN(a_frame_carries_each_channel_from_one_mark_to_the_next);
    RUN(the_runs_add_up_to_exactly_one_frame);
    RUN(a_frame_whose_sync_gap_would_be_too_short_is_refused);
    RUN(the_shortest_usable_frame_is_reported_before_it_is_needed);
    RUN(a_channel_outside_the_range_a_servo_takes_is_refused);
    RUN(a_mark_that_would_not_fit_inside_its_channel_is_refused);
    RUN(the_defaults_fill_in_field_by_field);
    RUN(a_count_or_a_buffer_that_does_not_fit_is_refused);
    return test_summary("ppm");
}
