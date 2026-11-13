/*
 * The IOmcu main loop's pass times (pass_stats.h): the longest pass, the
 * passes over 500us, the longest run of each section, the clear on a read,
 * and the wrap of the 32-bit microsecond clock.
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
    CHECK_EQ(f.sec_max_us[PASS_SEC_CAN], 60);

    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 0);
    CHECK_EQ(f.over, 0);
    CHECK_EQ(f.sec_max_us[PASS_SEC_CAN], 0);
    CHECK_EQ(f.sec_max_us[PASS_SEC_REPORT], 0);

    /* The pass in progress at the read counts at the next one, whole. */
    pass_stats_pass(&s, 1200u);
    pass_stats_take(&s, &f);
    CHECK_EQ(f.max_us, 300);
    CHECK_EQ(f.over, 0);
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
    CHECK_EQ(f.sec_max_us[PASS_SEC_REPORT], 32);
}

int main(void)
{
    RUN(the_first_pass_only_begins);
    RUN(the_longest_pass_is_kept);
    RUN(a_pass_of_500us_is_not_over_and_501us_is);
    RUN(each_section_keeps_its_longest_run);
    RUN(a_read_clears_the_figures_and_the_pass_goes_on);
    RUN(a_pass_and_a_section_across_the_wrap_of_the_clock);
    return test_summary("pass_stats");
}
