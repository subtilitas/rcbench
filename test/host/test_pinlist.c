/*
 * The pin name and the pin list formatter: how a pin is named, which pins
 * the list names, where it cuts and what the "+N" counts.
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

/* A pin's name: the pin word, a space and the number, from GPIO 0 to the
 * highest pin a board has (GPIO 47 on the RP2354B) and the highest a mask
 * holds (GPIO 63); a higher number is no pin. */
TEST_CASE(a_pin_is_named_gpio_and_its_number)
{
    char b[UI_PIN_NAME_SIZE];
    CHECK_STR_EQ(UI_PIN_WORD, "GPIO");
    CHECK_EQ(ui_pin_name(b, sizeof(b), 0u), 6u);
    CHECK_STR_EQ(b, "GPIO 0");
    CHECK_EQ(ui_pin_name(b, sizeof(b), 13u), 7u);
    CHECK_STR_EQ(b, "GPIO 13");
    CHECK_EQ(ui_pin_name(b, sizeof(b), 47u), 7u);
    CHECK_STR_EQ(b, "GPIO 47");
    CHECK_EQ(ui_pin_name(b, sizeof(b), UI_PINLIST_PINS - 1u), 7u);
    CHECK_STR_EQ(b, "GPIO 63");
    CHECK_EQ(strlen(b) + 1u, (size_t)UI_PIN_NAME_SIZE);
}

TEST_CASE(a_number_past_the_pins_is_no_pin)
{
    char b[UI_PIN_NAME_SIZE];
    memset(b, 'x', sizeof(b));
    CHECK_EQ(ui_pin_name(b, sizeof(b), UI_PINLIST_PINS), 0u);
    CHECK_STR_EQ(b, "");
    memset(b, 'x', sizeof(b));
    CHECK_EQ(ui_pin_name(b, sizeof(b), 0xFFu), 0u);
    CHECK_STR_EQ(b, "");
}

TEST_CASE(a_pin_name_keeps_to_its_buffer)
{
    char b[4] = "abc";
    CHECK_EQ(ui_pin_name(b, 0u, 2u), 0u);
    CHECK_STR_EQ(b, "abc");
    CHECK_EQ(ui_pin_name(NULL, 8u, 2u), 0u);
    /* Cut as snprintf() cuts, and the length is what is there. */
    CHECK_EQ(ui_pin_name(b, sizeof(b), 13u), 3u);
    CHECK_STR_EQ(b, "GPI");
    char one[1] = { 'x' };
    CHECK_EQ(ui_pin_name(one, sizeof(one), 13u), 0u);
    CHECK_EQ(one[0], '\0');
}

TEST_CASE(one_pin_is_named)
{
    CHECK_STR_EQ(fmt(BIT(2), 20u), "GPIO 2");
    CHECK_STR_EQ(fmt(BIT(13), 20u), "GPIO 13");
    CHECK_STR_EQ(fmt(BIT(0), 6u), "GPIO 0");
    CHECK_STR_EQ(fmt(BIT(0), 5u), "+1");
}

TEST_CASE(pins_are_listed_lowest_first)
{
    CHECK_STR_EQ(fmt(BIT(5) | BIT(2) | BIT(4), 20u), "GPIO 2 GPIO 4 GPIO 5");
}

TEST_CASE(a_list_that_exactly_fits_is_whole)
{
    /* "GPIO 2 GPIO 4 GPIO 5" is 20 characters. */
    CHECK_STR_EQ(fmt(BIT(2) | BIT(4) | BIT(5), 20u), "GPIO 2 GPIO 4 GPIO 5");
}

TEST_CASE(a_list_one_over_drops_pins_for_a_count)
{
    /* 19 cells: "GPIO 2 GPIO 4 +1" is 16, and fits. */
    CHECK_STR_EQ(fmt(BIT(2) | BIT(4) | BIT(5), 19u), "GPIO 2 GPIO 4 +1");
    CHECK_STR_EQ(fmt(BIT(2) | BIT(4) | BIT(5), 16u), "GPIO 2 GPIO 4 +1");
    /* 15 cells: only "GPIO 2 +2" (9) fits with a second pin out. */
    CHECK_STR_EQ(fmt(BIT(2) | BIT(4) | BIT(5), 15u), "GPIO 2 +2");
}

TEST_CASE(the_example_from_the_owner)
{
    const uint64_t m = BIT(2) | BIT(4) | BIT(5) | BIT(6) | BIT(7);
    CHECK_STR_EQ(fmt(m, 23u), "GPIO 2 GPIO 4 GPIO 5 +2");
    CHECK_EQ(strlen(fmt(m, 23u)), 23u);
}

TEST_CASE(a_count_alone_when_no_pin_fits_with_it)
{
    const uint64_t m = BIT(40) | BIT(41);
    CHECK_STR_EQ(fmt(m, 9u), "+2");
    CHECK_STR_EQ(fmt(m, 2u), "+2");
}

TEST_CASE(a_count_wider_than_the_budget_is_cut)
{
    CHECK_STR_EQ(fmt(BIT(1) | BIT(2) | BIT(3), 1u), "+");
    CHECK_STR_EQ(fmt(BIT(1) | BIT(2), 0u), "");
}

TEST_CASE(the_highest_pin_is_gpio_63)
{
    CHECK_STR_EQ(fmt(BIT(63), 20u), "GPIO 63");
    CHECK_STR_EQ(fmt(BIT(0) | BIT(63), 20u), "GPIO 0 GPIO 63");
    CHECK_EQ(ui_pinlist_count(BIT(63)), 1u);
}

TEST_CASE(all_48_pins_show_a_count)
{
    const uint64_t m = ((uint64_t)1u << 48) - 1u;
    CHECK_EQ(ui_pinlist_count(m), 48u);
    /* 24 cells: three pins and " +45" fit, a fourth does not. */
    CHECK_STR_EQ(fmt(m, 24u), "GPIO 0 GPIO 1 GPIO 2 +45");
    CHECK_STR_EQ(fmt(m, 23u), "GPIO 0 GPIO 1 +46");
    CHECK_STR_EQ(fmt(m, 48u), "GPIO 0 GPIO 1 GPIO 2 GPIO 3 GPIO 4 GPIO 5 +42");
    CHECK(strlen(fmt(m, 47u)) <= 47u);
}

TEST_CASE(all_64_pins_show_a_count)
{
    const uint64_t m = ~(uint64_t)0u;
    CHECK_EQ(ui_pinlist_count(m), 64u);
    CHECK_STR_EQ(fmt(m, 24u), "GPIO 0 GPIO 1 GPIO 2 +61");
    for (unsigned cells = 0u; cells < 159u; ++cells) {
        CHECK(strlen(fmt(m, cells)) <= cells);
    }
}

TEST_CASE(the_buffer_bounds_the_text_too)
{
    char b[10];
    const size_t n = ui_pinlist(b, sizeof(b), BIT(1) | BIT(2) | BIT(3), 40u);
    CHECK_EQ(n, strlen(b));
    CHECK(n <= 9u);
    CHECK_STR_EQ(b, "GPIO 1 +2");
}

TEST_CASE(a_pin_and_a_count_of_ten_are_two_digits_at_the_cell_boundary)
{
    /* "GPIO 10" is 7 cells: it fits 7, and a cut at 6 must not show
     * "GPIO 1". */
    CHECK_STR_EQ(fmt(BIT(10), 7u), "GPIO 10");
    CHECK_STR_EQ(fmt(BIT(10), 6u), "+1");
    CHECK_STR_EQ(fmt(BIT(2) | BIT(10), 14u), "GPIO 2 GPIO 10");
    CHECK_STR_EQ(fmt(BIT(2) | BIT(10), 13u), "GPIO 2 +1");
    /* Ten pins left out: " +10" is 4 cells after "GPIO 0". 10 cells fit
     * it. */
    const uint64_t m = ((uint64_t)1u << 11) - 1u;   /* GPIO 0 to GPIO 10 */
    CHECK_STR_EQ(fmt(m, 10u), "GPIO 0 +10");
    CHECK_STR_EQ(fmt(m, 9u), "+11");
    /* 11 pins in all; 2 cells cut "+11" to "+1". */
    CHECK_STR_EQ(fmt(m, 2u), "+1");
    /* GPIO 9 is one digit: 6 cells hold it whole. */
    CHECK_STR_EQ(fmt(BIT(9), 6u), "GPIO 9");
    /* Nine pins left out: " +9" is 3 cells after "GPIO 0 GPIO 1 GPIO 2". */
    CHECK_STR_EQ(fmt(((uint64_t)1u << 12) - 1u, 23u),
                 "GPIO 0 GPIO 1 GPIO 2 +9");
}

TEST_CASE(the_cells_close_to_the_buffer_size_keep_length_and_text_whole)
{
    char b[14];
    memset(b, 'x', sizeof(b));
    /* Cells equal to the size: 13 characters fit.  "GPIO 1 GPIO 22" is
     * 14, so the list gives way to the count and the length equals
     * strlen. */
    size_t n = ui_pinlist(b, sizeof(b), BIT(1) | BIT(22), 14u);
    CHECK_EQ(n, strlen(b));
    CHECK_STR_EQ(b, "GPIO 1 +1");
    memset(b, 'x', sizeof(b));
    n = ui_pinlist(b, sizeof(b), BIT(1) | BIT(2) | BIT(3), 14u);
    CHECK_EQ(n, strlen(b));
    CHECK_STR_EQ(b, "GPIO 1 +2");
    /* Cells one under the size, and far over it: "GPIO 1 GPIO 2" is
     * exactly 13. */
    const unsigned cells[] = {13u, 40u};
    for (size_t i = 0u; i < sizeof(cells) / sizeof(cells[0]); ++i) {
        memset(b, 'x', sizeof(b));
        n = ui_pinlist(b, sizeof(b), BIT(1) | BIT(2), cells[i]);
        CHECK_EQ(n, strlen(b));
        CHECK_STR_EQ(b, "GPIO 1 GPIO 2");
    }
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
    /* A buffer of 7 holds 6 characters: "GPIO 11" does not fit. */
    char d[7];
    memset(d, 'x', sizeof(d));
    const size_t nd = ui_pinlist(d, sizeof(d), BIT(11), 40u);
    CHECK(nd <= 6u);
    CHECK_EQ(nd, strlen(d));
    CHECK_STR_EQ(d, "+1");
}

int main(void)
{
    RUN(no_pins_write_nothing);
    RUN(a_pin_is_named_gpio_and_its_number);
    RUN(a_number_past_the_pins_is_no_pin);
    RUN(a_pin_name_keeps_to_its_buffer);
    RUN(one_pin_is_named);
    RUN(pins_are_listed_lowest_first);
    RUN(a_list_that_exactly_fits_is_whole);
    RUN(a_list_one_over_drops_pins_for_a_count);
    RUN(the_example_from_the_owner);
    RUN(a_count_alone_when_no_pin_fits_with_it);
    RUN(a_count_wider_than_the_budget_is_cut);
    RUN(the_highest_pin_is_gpio_63);
    RUN(all_48_pins_show_a_count);
    RUN(all_64_pins_show_a_count);
    RUN(the_buffer_bounds_the_text_too);
    RUN(a_pin_and_a_count_of_ten_are_two_digits_at_the_cell_boundary);
    RUN(the_cells_close_to_the_buffer_size_keep_length_and_text_whole);
    RUN(a_buffer_smaller_than_the_cells_ends_with_nul_inside_it);
    return test_summary("pinlist");
}
