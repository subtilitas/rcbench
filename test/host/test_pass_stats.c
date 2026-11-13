/*
 * The IOmcu main loop's pass times (pass_stats.h): the longest pass, the
 * passes over 500us, the pass count and its five bins at each edge, the
 * longest run of each section, the clear on a read, and the wrap of the
 * 32-bit microsecond clock.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "pass_stats.h"

TEST_CASE(the_first_pass_only_begins)
{
    pass_stats_t s;
    pass_stats_init(&s);
    pass_stats_pass(&s, 123456u);
    pass_figures_t f;
    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 0);
    CHECK_EQ(f.over, 0);
    CHECK_EQ(f.passes, 0);
    for (unsigned k = 0; k < PASS_STATS_BINS; ++k) {
        CHECK_EQ(f.bins[k], 0);
    }
}

TEST_CASE(each_pass_lands_in_one_bin_by_its_edges)
{
    CHECK_EQ(PASS_STATS_BINS, 5);
    CHECK_EQ(PASS_STATS_EDGE0_US, 250);
    CHECK_EQ(PASS_STATS_EDGE1_US, 500);
    CHECK_EQ(PASS_STATS_EDGE2_US, 1000);
    CHECK_EQ(PASS_STATS_EDGE3_US, 2000);
    /* Each edge and one past it, and a pass of 0us. */
    static const uint32_t len[] = { 0u, 250u, 251u, 500u, 501u, 1000u,
                                    1001u, 2000u, 2001u, 4000000u };
    static const unsigned want[PASS_STATS_BINS] = { 2u, 2u, 2u, 2u, 2u };
    pass_stats_t s;
    pass_stats_init(&s);
    uint32_t t = 7u;
    pass_stats_pass(&s, t);
    for (unsigned i = 0; i < sizeof(len) / sizeof(len[0]); ++i) {
        t += len[i];
        pass_stats_pass(&s, t);
    }
    pass_figures_t f;
    pass_stats_take(&s, &f);
    CHECK_EQ(f.passes, 10);
    for (unsigned k = 0; k < PASS_STATS_BINS; ++k) {
        if (f.bins[k] != want[k]) {
            T_FAIL("bin %u holds %lu, want %u", k, (unsigned long)f.bins[k],
                   want[k]);
        }
    }
    /* Bins 2 to 4 are the passes over. */
    CHECK_EQ(f.over, f.bins[2] + f.bins[3] + f.bins[4]);
}

TEST_CASE(the_longest_pass_is_kept)
{
    pass_stats_t s;
    pass_stats_init(&s);
    pass_stats_pass(&s, 1000u);
    pass_stats_pass(&s, 1300u);   /* 300 */
    pass_stats_pass(&s, 2450u);   /* 1150 */
    pass_stats_pass(&s, 2800u);   /* 350 */
    pass_figures_t f;
    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 1150);
}

TEST_CASE(a_pass_of_500us_is_not_over_and_501us_is)
{
    CHECK_EQ(PASS_STATS_OVER_US, 500);
    pass_stats_t s;
    pass_stats_init(&s);
    pass_stats_pass(&s, 0u);
    pass_stats_pass(&s, 500u);    /* 500: not over */
    pass_stats_pass(&s, 1001u);   /* 501: over */
    pass_stats_pass(&s, 1500u);   /* 499 */
    pass_stats_pass(&s, 2002u);   /* 502: over */
    pass_figures_t f;
    pass_stats_take(&s, &f);
    CHECK_EQ(f.over, 2);
}

TEST_CASE(each_section_keeps_its_longest_run)
{
    pass_stats_t s;
    pass_stats_init(&s);
    pass_stats_section(&s, PASS_SEC_CAN, 100u, 140u);
    pass_stats_section(&s, PASS_SEC_CAN, 200u, 275u);
    pass_stats_section(&s, PASS_SEC_CAN, 300u, 310u);
    pass_stats_section(&s, PASS_SEC_REPORT, 400u, 650u);
    pass_stats_section(&s, PASS_SEC_REPORT, 700u, 720u);
    /* A section out of range writes nothing. */
    pass_stats_section(&s, PASS_SEC_COUNT, 0u, 99999u);
    pass_figures_t f;
    pass_stats_take(&s, &f);
    CHECK_EQ(f.sec_max_us[PASS_SEC_CAN], 75);
    CHECK_EQ(f.sec_max_us[PASS_SEC_REPORT], 250);
    CHECK_EQ(f.max_us, 0);
}

TEST_CASE(a_read_clears_the_figures_and_the_pass_goes_on)
{
    pass_stats_t s;
    pass_stats_init(&s);
    pass_stats_pass(&s, 0u);
    pass_stats_pass(&s, 900u);
    pass_stats_section(&s, PASS_SEC_CAN, 0u, 60u);
    pass_figures_t f;
    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 900);
    CHECK_EQ(f.over, 1);
    CHECK_EQ(f.passes, 1);
    CHECK_EQ(f.bins[2], 1);
    CHECK_EQ(f.sec_max_us[PASS_SEC_CAN], 60);

    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 0);
    CHECK_EQ(f.over, 0);
    CHECK_EQ(f.passes, 0);
    CHECK_EQ(f.bins[3], 0);
    CHECK_EQ(f.sec_max_us[PASS_SEC_CAN], 0);
    CHECK_EQ(f.sec_max_us[PASS_SEC_REPORT], 0);

    /* The pass in progress at the read counts at the next one, whole. */
    pass_stats_pass(&s, 1200u);
    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 300);
    CHECK_EQ(f.over, 0);
    CHECK_EQ(f.passes, 1);
    CHECK_EQ(f.bins[1], 1);
}

TEST_CASE(a_pass_and_a_section_across_the_wrap_of_the_clock)
{
    pass_stats_t s;
    pass_stats_init(&s);
    pass_stats_pass(&s, 0xFFFFFF00u);
    pass_stats_pass(&s, 0x00000200u);   /* 256 + 512 = 768 */
    pass_stats_section(&s, PASS_SEC_REPORT, 0xFFFFFFF0u, 0x00000010u);
    pass_figures_t f;
    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 768);
    CHECK_EQ(f.over, 1);
    CHECK_EQ(f.bins[2], 1);
    CHECK_EQ(f.sec_max_us[PASS_SEC_REPORT], 32);
}

int main(void)
{
    RUN(the_first_pass_only_begins);
    RUN(each_pass_lands_in_one_bin_by_its_edges);
    RUN(the_longest_pass_is_kept);
    RUN(a_pass_of_500us_is_not_over_and_501us_is);
    RUN(each_section_keeps_its_longest_run);
    RUN(a_read_clears_the_figures_and_the_pass_goes_on);
    RUN(a_pass_and_a_section_across_the_wrap_of_the_clock);
    return test_summary("pass_stats");
}
