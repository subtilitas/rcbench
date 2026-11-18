/**
 * @file check_eq_float.c
 * @brief A CHECK_EQ on a float.  The ctest `harness_check_eq_refuses_float`
 *        compiles this file and passes when the compiler stops at the
 *        static assertion of greatest.h; no target builds it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

int main(void)
{
    float v = 21.9f;

    CHECK_EQ(v, 21.5f);
    return test_summary("check_eq_float");
}
