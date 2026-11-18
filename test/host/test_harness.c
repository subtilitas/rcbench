/**
 * @file test_harness.c
 * @brief The asserts of greatest.h: CHECK_FEQ tells floats apart that differ
 *        by less than 1, CHECK_EQ compares integers and pointers.  That
 *        CHECK_EQ does not compile on a floating operand is the ctest
 *        `harness_check_eq_refuses_float` (check_eq_float.c.in).
 *
 * Cases:
 * - check_feq_fails_on_a_difference_below_one
 * - check_feq_passes_on_equal_floats
 * - check_feq_fails_on_nan
 * - check_eq_compares_integers_and_pointers
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>

#include "greatest.h"

/**
 * @brief Read and clear the failure mark of the running case.
 * @return 1 when an assert failed since the last call, else 0.
 */
static int take_failure(void)
{
    int failed = t_case_failed;

    t_case_failed = 0;
    return failed;
}

/* Each case takes the outcome of every assert under test into a variable
 * first and asserts on the variables last: an assert in between would be
 * cleared by the next take_failure() and never fail the case. */

TEST_CASE(check_feq_fails_on_a_difference_below_one)
{
    float v = 21.9f;
    int   f1, f2, f3, f4;

    printf("      the 4 lines below are expected failures\n");
    CHECK_FEQ(v, 21.5f);
    f1 = take_failure();
    CHECK_FEQ(90.9f, 90.0f);
    f2 = take_failure();
    CHECK_FEQ(-0.1f, 0.0f);
    f3 = take_failure();
    CHECK_FEQ(0.5, 0);
    f4 = take_failure();

    CHECK_EQ(f1, 1);
    CHECK_EQ(f2, 1);
    CHECK_EQ(f3, 1);
    CHECK_EQ(f4, 1);
}

TEST_CASE(check_feq_passes_on_equal_floats)
{
    float  v = 89.9f;
    double d = 33.3;
    int    f1, f2, f3, f4, f5;

    CHECK_FEQ(v, 89.9f);
    f1 = take_failure();
    CHECK_FEQ(d, 33.3);
    f2 = take_failure();
    CHECK_FEQ(-0.0f, 0.0f);
    f3 = take_failure();
    CHECK_FEQ(INFINITY, INFINITY);
    f4 = take_failure();
    CHECK_FEQ(2.0f, 2);
    f5 = take_failure();

    CHECK_EQ(f1, 0);
    CHECK_EQ(f2, 0);
    CHECK_EQ(f3, 0);
    CHECK_EQ(f4, 0);
    CHECK_EQ(f5, 0);
}

TEST_CASE(check_feq_fails_on_nan)
{
    int f1, f2;

    printf("      the 2 lines below are expected failures\n");
    CHECK_FEQ(NAN, NAN);
    f1 = take_failure();
    CHECK_FEQ(NAN, 0.0f);
    f2 = take_failure();

    CHECK_EQ(f1, 1);
    CHECK_EQ(f2, 1);
}

TEST_CASE(check_eq_compares_integers_and_pointers)
{
    const char *p = "x";
    int         f1, f2, f3, f4;

    CHECK_EQ(3, 3L);
    f1 = take_failure();
    CHECK_EQ(p, p);
    f2 = take_failure();
    CHECK_EQ(-1, -1);
    f3 = take_failure();
    printf("      the line below is an expected failure\n");
    CHECK_EQ(3, 4);
    f4 = take_failure();

    /* CHECK_EQ is the assert under test here, so the verdict is CHECK's. */
    CHECK(f1 == 0);
    CHECK(f2 == 0);
    CHECK(f3 == 0);
    CHECK(f4 == 1);
}

int main(void)
{
    RUN(check_feq_fails_on_a_difference_below_one);
    RUN(check_feq_passes_on_equal_floats);
    RUN(check_feq_fails_on_nan);
    RUN(check_eq_compares_integers_and_pointers);
    return test_summary("harness");
}
