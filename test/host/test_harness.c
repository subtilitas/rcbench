/**
 * @file test_harness.c
 * @brief The asserts of greatest.h: CHECK_FEQ tells floats apart that differ
 *        by less than 1, CHECK_EQ compares integers and pointers.  That
 *        CHECK_EQ does not compile on a floating operand is the ctest
 *        `harness_check_eq_refuses_float` (check_eq_float.c).
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

TEST_CASE(check_feq_fails_on_a_difference_below_one)
{
    float v = 21.9f;
    int   failed;

    printf("      the 4 lines below are expected failures\n");
    CHECK_FEQ(v, 21.5f);
    failed = take_failure();
    CHECK_EQ(failed, 1);
    CHECK_FEQ(90.9f, 90.0f);
    failed = take_failure();
    CHECK_EQ(failed, 1);
    CHECK_FEQ(-0.1f, 0.0f);
    failed = take_failure();
    CHECK_EQ(failed, 1);
    CHECK_FEQ(0.5, 0);
    failed = take_failure();
    CHECK_EQ(failed, 1);
}

TEST_CASE(check_feq_passes_on_equal_floats)
{
    float  v = 89.9f;
    double d = 33.3;

    CHECK_FEQ(v, 89.9f);
    CHECK_FEQ(d, 33.3);
    CHECK_FEQ(-0.0f, 0.0f);
    CHECK_FEQ(INFINITY, INFINITY);
    CHECK_FEQ(2.0f, 2);
    CHECK_EQ(take_failure(), 0);
}

TEST_CASE(check_feq_fails_on_nan)
{
    int failed;

    printf("      the line below is an expected failure\n");
    CHECK_FEQ(NAN, NAN);
    failed = take_failure();
    CHECK_EQ(failed, 1);
}

TEST_CASE(check_eq_compares_integers_and_pointers)
{
    int         failed;
    const char *p = "x";

    CHECK_EQ(3, 3L);
    CHECK_EQ(p, p);
    CHECK_EQ(-1, -1);
    CHECK_EQ(take_failure(), 0);
    printf("      the line below is an expected failure\n");
    CHECK_EQ(3, 4);
    failed = take_failure();
    CHECK_EQ(failed, 1);
}

int main(void)
{
    RUN(check_feq_fails_on_a_difference_below_one);
    RUN(check_feq_passes_on_equal_floats);
    RUN(check_feq_fails_on_nan);
    RUN(check_eq_compares_integers_and_pointers);
    return test_summary("harness");
}
