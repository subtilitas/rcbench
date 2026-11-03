/**
 * @file
 * @brief The automatic servo test's brown-out walk: the voltages it asks
 *        for, from the start down by STEP to LOWEST VOLTAGE, and the start,
 *        floor and step a configuration and a supply give.  The walk itself
 *        runs in servo_test.c; servo_test.h describes it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_test.h"

#include <math.h>
#include <stddef.h>

_Static_assert(SERVO_TEST_STEP_SLOTS <= 255u, "a step's index is a uint8_t");
/* The finest walk the settings allow fits: from the start to the lowest
 * floor in the smallest step, both ends included. */
_Static_assert(SERVO_TEST_BROWNOUT_MAX
                   == (unsigned)((SERVO_TEST_BROWNOUT_START_V
                                  - SERVO_TEST_BROWNOUT_FLOOR_MIN_V)
                                 / SERVO_TEST_BROWNOUT_STEP_MIN_V + 0.5f) + 1u,
               "SERVO_TEST_BROWNOUT_MAX holds the finest walk");

/**
 * @brief @p v to 0.01V, as the report prints a voltage.
 *
 * @param v Voltage, V.
 * @return the voltage rounded to 0.01V.
 */
static float centivolts(float v)
{
    return (float)lroundf(v * 100.0f) / 100.0f;
}

float servo_test_brownout_v(float start_v, float floor_v, float step_v,
                            unsigned k)
{
    if (!(step_v > 0.0f) || start_v < floor_v - 0.001f) {
        return -1.0f;
    }
    if (k == 0u) {
        return start_v;
    }
    const float v = centivolts(start_v - (float)k * step_v);
    if (v >= floor_v - 0.001f) {
        return v;
    }
    /* Past the floor: the floor itself once, after a voltage above it. */
    const float prev = centivolts(start_v - (float)(k - 1u) * step_v);
    return (prev > floor_v + 0.001f) ? floor_v : -1.0f;
}

unsigned servo_test_brownout_count(float start_v, float floor_v, float step_v)
{
    unsigned n = 0u;
    while (n <= SERVO_TEST_BROWNOUT_MAX
           && servo_test_brownout_v(start_v, floor_v, step_v, n) >= 0.0f) {
        ++n;
    }
    return n;
}

/**
 * @brief @p v held to @p lo to @p hi, and @p def for 0 or less.
 *
 * @param v   Value.
 * @param lo  Lowest value taken.
 * @param hi  Highest value taken.
 * @param def Value for 0 or less, and for NaN.
 * @return the value held to its range.
 */
static float bo_setting(float v, float lo, float hi, float def)
{
    if (!(v > 0.0f)) {
        return def;
    }
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void servo_test_brownout_walk(float floor_set, float step_set, float v_min,
                              float v_max, float *start_v, float *floor_v,
                              float *step_v)
{
    const float start = (v_max < SERVO_TEST_BROWNOUT_START_V)
                            ? v_max : SERVO_TEST_BROWNOUT_START_V;
    float lowest = bo_setting(floor_set, SERVO_TEST_BROWNOUT_FLOOR_MIN_V,
                              SERVO_TEST_BROWNOUT_FLOOR_MAX_V,
                              SERVO_TEST_BROWNOUT_FLOOR_V);
    /* LOWEST VOLTAGE at or above the start: the start alone. */
    if (lowest > start) {
        lowest = start;
    }
    if (start_v != NULL) {
        *start_v = start;
    }
    if (floor_v != NULL) {
        *floor_v = (v_min > lowest) ? v_min : lowest;
    }
    if (step_v != NULL) {
        *step_v = bo_setting(step_set, SERVO_TEST_BROWNOUT_STEP_MIN_V,
                             SERVO_TEST_BROWNOUT_STEP_MAX_V,
                             SERVO_TEST_BROWNOUT_STEP_V);
    }
}
