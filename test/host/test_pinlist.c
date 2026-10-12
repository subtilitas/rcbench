/*
 * The pin list formatter: which pins it names, where it cuts and what the
 * "+N" counts.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ui_pinlist.h"

static const char *fmt(uint64_t mask, unsigned cells)
{
    static char buf[160];
    memset(buf, 'x', sizeof(buf));
    const size_t n = ui_pinlist(buf, sizeof(buf), mask, cells);
    if (n != strlen(buf)) {
        return "LENGTH MISMATCH";
    }
    return buf;
}

#define BIT(n) ((uint64_t)1u << (n))

TEST_CASE(no_pins_write_nothing)
{
    CHECK_STR_EQ(fmt(0u, 20u), "");
    char b[4] = "abc";
    CHECK_EQ(ui_pinlist(b, 0u, BIT(1), 20u), 0u);
    CHECK_EQ(b[0], 'a');
    CHECK_EQ(ui_pinlist(NULL, 8u, BIT(1), 20u), 0u);
    CHECK_EQ(ui_pinlist_count(0u), 0u);
}

TEST_CASE(one_pin_is_named)
{
    CHECK_STR_EQ(fmt(BIT(2), 20u), "GP2");
    CHECK_STR_EQ(fmt(BIT(13), 20u), "GP13");
    CHECK_STR_EQ(fmt(BIT(0), 3u), "GP0");
}

TEST_CASE(pins_are_listed_lowest_first)
{
    CHECK_STR_EQ(fmt(BIT(5) | BIT(2) | BIT(4), 20u), "GP2 GP4 GP5");
}

TEST_CASE(a_list_that_exactly_fits_is_whole)
{
    /* "GP2 GP4 GP5" is 11 characters. */
    CHECK_STR_EQ(fmt(BIT(2) | BIT(4) | BIT(5), 11u), "GP2 GP4 GP5");
}

TEST_CASE(a_list_one_over_drops_pins_for_a_count)
{
    /* 10 cells: "GP2 GP4 +1" is 10, and fits. */
    CHECK_STR_EQ(fmt(BIT(2) | BIT(4) | BIT(5), 10u), "GP2 GP4 +1");
    /* 9 cells: only "GP2 +2" (6) fits with a second pin out. */
    CHECK_STR_EQ(fmt(BIT(2) | BIT(4) | BIT(5), 9u), "GP2 +2");
}

TEST_CASE(the_example_from_the_owner)
{
    const uint64_t m = BIT(2) | BIT(4) | BIT(5) | BIT(6) | BIT(7);
    CHECK_STR_EQ(fmt(m, 14u), "GP2 GP4 GP5 +2");
    CHECK_EQ(strlen(fmt(m, 14u)), 14u);
}

TEST_CASE(a_count_alone_when_no_pin_fits_with_it)
{
    const uint64_t m = BIT(40) | BIT(41);
    CHECK_STR_EQ(fmt(m, 4u), "+2");
    CHECK_STR_EQ(fmt(m, 2u), "+2");
}

TEST_CASE(a_count_wider_than_the_budget_is_cut)
{
    CHECK_STR_EQ(fmt(BIT(1) | BIT(2) | BIT(3), 1u), "+");
    CHECK_STR_EQ(fmt(BIT(1) | BIT(2), 0u), "");
}

TEST_CASE(the_highest_pin_is_gp63)
{
    CHECK_STR_EQ(fmt(BIT(63), 20u), "GP63");
    CHECK_STR_EQ(fmt(BIT(0) | BIT(63), 20u), "GP0 GP63");
    CHECK_EQ(ui_pinlist_count(BIT(63)), 1u);
}

TEST_CASE(all_48_pins_show_a_count)
{
    const uint64_t m = ((uint64_t)1u << 48) - 1u;
    CHECK_EQ(ui_pinlist_count(m), 48u);
    /* 15 cells: three pins and " +45" fit, a fourth does not. */
    CHECK_STR_EQ(fmt(m, 15u), "GP0 GP1 GP2 +45");
    CHECK_STR_EQ(fmt(m, 14u), "GP0 GP1 +46");
    CHECK_STR_EQ(fmt(m, 30u), "GP0 GP1 GP2 GP3 GP4 GP5 +42");
    CHECK(strlen(fmt(m, 47u)) <= 47u);
}

TEST_CASE(all_64_pins_show_a_count)
{
    const uint64_t m = ~(uint64_t)0u;
    CHECK_EQ(ui_pinlist_count(m), 64u);
    CHECK_STR_EQ(fmt(m, 15u), "GP0 GP1 GP2 +61");
    for (unsigned cells = 0u; cells < 120u; ++cells) {
        CHECK(strlen(fmt(m, cells)) <= cells);
    }
}

TEST_CASE(the_buffer_bounds_the_text_too)
{
    char b[8];
    const size_t n = ui_pinlist(b, sizeof(b), BIT(1) | BIT(2) | BIT(3), 40u);
    CHECK_EQ(n, strlen(b));
    CHECK(n <= 7u);
    CHECK_STR_EQ(b, "GP1 +2");
}

TEST_CASE(a_pin_and_a_count_of_ten_are_two_digits_at_the_cell_boundary)
{
    /* "GP10" is 4 cells: it fits 4, and a cut at 3 must not show "GP1". */
    CHECK_STR_EQ(fmt(BIT(10), 4u), "GP10");
    CHECK_STR_EQ(fmt(BIT(10), 3u), "+1");
    CHECK_STR_EQ(fmt(BIT(2) | BIT(10), 8u), "GP2 GP10");
    CHECK_STR_EQ(fmt(BIT(2) | BIT(10), 7u), "GP2 +1");
    /* Ten pins left out: " +10" is 4 cells after "GP0". 7 cells fit it. */
    const uint64_t m = ((uint64_t)1u << 11) - 1u;   /* GP0..GP10 */
    CHECK_STR_EQ(fmt(m, 7u), "GP0 +10");
    CHECK_STR_EQ(fmt(m, 6u), "+11");
    /* 11 pins in all; 2 cells cut "+11" to "+1". */
    CHECK_STR_EQ(fmt(m, 2u), "+1");
}

TEST_CASE(a_buffer_smaller_than_the_cells_ends_with_nul_inside_it)
{
    char b[6];
    memset(b, 'x', sizeof(b));
    const size_t n = ui_pinlist(b, sizeof(b), BIT(1) | BIT(2) | BIT(3), 40u);
    CHECK(n <= 5u);
    CHECK_EQ(n, strlen(b));
    CHECK_STR_EQ(b, "+3");
    char c[5];
    memset(c, 'x', sizeof(c));
    (void)ui_pinlist(c, sizeof(c), BIT(11) | BIT(12), 40u);
    CHECK(memchr(c, '\0', sizeof(c)) != NULL);
    CHECK_STR_EQ(c, "+2");
    /* A buffer of 4 holds 3 characters: "GP11" does not fit. */
    char d[4];
    memset(d, 'x', sizeof(d));
    const size_t nd = ui_pinlist(d, sizeof(d), BIT(11), 40u);
    CHECK(nd <= 3u);
    CHECK_EQ(nd, strlen(d));
    CHECK_STR_EQ(d, "+1");
}

int main(void)
{
    RUN(no_pins_write_nothing);
    RUN(one_pin_is_named);
    RUN(pins_are_listed_lowest_first);
    RUN(a_list_that_exactly_fits_is_whole);
    RUN(a_list_one_over_drops_pins_for_a_count);
    RUN(the_example_from_the_owner);
    RUN(a_count_alone_when_no_pin_fits_with_it);
    RUN(a_count_wider_than_the_budget_is_cut);
    RUN(the_highest_pin_is_gp63);
    RUN(all_48_pins_show_a_count);
    RUN(all_64_pins_show_a_count);
    RUN(the_buffer_bounds_the_text_too);
    RUN(a_pin_and_a_count_of_ten_are_two_digits_at_the_cell_boundary);
    RUN(a_buffer_smaller_than_the_cells_ends_with_nul_inside_it);
    return test_summary("pinlist");
}
