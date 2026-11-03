/*
 * The automatic servo test's brown-out walk under its two settings, LOWEST
 * VOLTAGE (the floor) and STEP, against the servo and supply models.
 *
 * Under test: the voltages the walk asks for at the settings' limits --
 * floor 2.0 and 4.8V, step 0.1 and 1.0V, the finest walk of 31 voltages --
 * a floor off the step's grid from the start, a floor at or above the
 * start, a cap under 5.0V, a supply whose lowest set point lies above the
 * floor, a configuration of 0 (the defaults) and values outside the
 * ranges; and the report's line that names the walk.
 *
 * The rig is servo_test_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_test_rig.h"

/* ------------------------------------------------------------------ helpers */

/**
 * @brief A run of the brown-out walk alone, with every voltage moving.
 *
 * @param floor_v LOWEST VOLTAGE, V; 0 for the default.
 * @param step_v  STEP, V; 0 for the default.
 */
static void walk_alone(float floor_v, float step_v)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout   = true;
    c.bo_floor_v = floor_v;
    c.bo_step_v  = step_v;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(600000u);
    CHECK(!servo_test_running(&g.t));
}

/**
 * @brief Whether the run's brown-out steps asked for @p v, in order.
 *
 * @param v The voltages, V.
 * @param n How many.
 * @return true when the walk holds exactly those, each to 0.001V.
 */
static bool walked(const float *v, unsigned n)
{
    if (g.t.step_count != n) {
        return false;
    }
    for (unsigned k = 0; k < n; ++k) {
        if (!g.t.steps[k].brownout || !g.t.steps[k].done
            || fabsf(g.t.steps[k].set_v - v[k]) > 0.001f) {
            return false;
        }
    }
    return true;
}

/** @brief A supply that sets down to 2.0V: the PD mini's PPS (programmable
 *         power supply) floor of 3.3V would raise every floor under it. */
static void low_supply(void)
{
    g.v_min = 2.0f;
    g.sup.caps.v_min = 2.0f;
}

/* ------------------------------------------------------------------ tests */

/* The defaults, 3.0V in 0.2V steps, are the walk of before: 11 voltages
 * from 5.0V, and the report names them. */
TEST_CASE(the_defaults_walk_from_5_v_to_3_v_in_0_2_v_steps)
{
    rig_fresh();
    low_supply();
    walk_alone(3.0f, 0.2f);
    static const float k_v[] = { 5.0f, 4.8f, 4.6f, 4.4f, 4.2f, 4.0f,
                                 3.8f, 3.6f, 3.4f, 3.2f, 3.0f };
    CHECK(walked(k_v, 11u));
    CHECK(strstr(g.report, "Brown-out:      from 5.00V down in 0.20V steps "
                           "to 3.00V") != NULL);
    CHECK_EQ(servo_test_brownout_count(5.0f, 3.0f, 0.2f), 11u);

    /* 0 for both is the same walk. */
    rig_fresh();
    low_supply();
    walk_alone(0.0f, 0.0f);
    CHECK(walked(k_v, 11u));
}

/* The finest walk, 2.0V in 0.1V steps, is 31 voltages, each the start
 * less k steps, and the run's slots hold all of them. */
TEST_CASE(the_finest_walk_is_31_voltages_down_to_2_v)
{
    rig_fresh();
    low_supply();
    walk_alone(2.0f, 0.1f);
    CHECK_EQ(g.t.step_count, 31u);
    CHECK_EQ(g.t.step_count, SERVO_TEST_BROWNOUT_MAX);
    for (unsigned k = 0; k < 31u; ++k) {
        CHECK_NEAR(g.t.steps[k].set_v, 5.0f - 0.1f * (float)k, 0.0005f);
        CHECK(g.t.steps[k].done);
    }
    CHECK_NEAR(g.t.steps[30].set_v, 2.0f, 0.0005f);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.report, "from 5.00V down in 0.10V steps to 2.00V")
          != NULL);
    CHECK(strstr(g.report, "every step down to 2.00V") != NULL);
    CHECK_EQ(servo_test_brownout_count(5.0f, 2.0f, 0.1f), 31u);
    CHECK_EQ(g.t.rows_lost, 0u);
}

/* The coarsest: the floor 4.8V in 1.0V steps.  The step past 5.0V lies
 * under the floor, so the floor itself is the last voltage, 0.2V down. */
TEST_CASE(a_last_step_shorter_than_step_lands_on_the_floor)
{
    rig_fresh();
    walk_alone(4.8f, 1.0f);
    static const float k_coarse[] = { 5.0f, 4.8f };
    CHECK(walked(k_coarse, 2u));
    CHECK(strstr(g.report, "from 5.00V down in 1.00V steps to 4.80V")
          != NULL);

    /* A floor off the grid from the start: 3.1V in 0.2V steps ends 3.2,
     * then 3.1. */
    rig_fresh();
    low_supply();
    walk_alone(3.1f, 0.2f);
    CHECK_EQ(g.t.step_count, 11u);
    CHECK_NEAR(g.t.steps[9].set_v, 3.2f, 0.0005f);
    CHECK_NEAR(g.t.steps[10].set_v, 3.1f, 0.0005f);

    /* 2.0V in 0.7V steps: 5.0, 4.3, 3.6, 2.9, 2.2, then 2.0. */
    rig_fresh();
    low_supply();
    walk_alone(2.0f, 0.7f);
    static const float k_odd[] = { 5.0f, 4.3f, 3.6f, 2.9f, 2.2f, 2.0f };
    CHECK(walked(k_odd, 6u));
    CHECK_EQ(servo_test_brownout_count(5.0f, 2.0f, 0.7f), 6u);
}

/* A floor at or above the start leaves one voltage, the start: under a cap
 * of 4.5V, LOWEST VOLTAGE 4.8V walks 4.5V alone; at a cap of 4.8V it walks
 * 4.8V alone. */
TEST_CASE(a_floor_at_or_above_the_start_walks_the_start_alone)
{
    rig_fresh();
    g.v_max = 4.5f;
    walk_alone(4.8f, 0.2f);
    static const float k_cap[] = { 4.5f };
    CHECK(walked(k_cap, 1u));
    CHECK(g.set_v_max <= 4.5f + 0.001f);
    CHECK(strstr(g.report, "from 4.50V down in 0.20V steps to 4.50V")
          != NULL);

    rig_fresh();
    g.v_max = 4.8f;
    walk_alone(4.8f, 0.1f);
    static const float k_equal[] = { 4.8f };
    CHECK(walked(k_equal, 1u));

    float from = 0.0f, floor_v = 0.0f, step = 0.0f;
    servo_test_brownout_walk(4.8f, 0.0f, 3.3f, 4.5f, &from, &floor_v, &step);
    CHECK_NEAR(from, 4.5f, 1e-6f);
    CHECK_NEAR(floor_v, 4.5f, 1e-6f);
    CHECK_EQ(servo_test_brownout_count(from, floor_v, step), 1u);
}

/* A cap under 5.0V starts the walk at the cap and steps from there; a
 * supply whose lowest set point lies above LOWEST VOLTAGE ends it there. */
TEST_CASE(the_cap_and_the_supply_bound_the_walk)
{
    rig_fresh();
    g.v_max = 4.0f;
    low_supply();
    walk_alone(3.0f, 0.2f);
    static const float k_cap[] = { 4.0f, 3.8f, 3.6f, 3.4f, 3.2f, 3.0f };
    CHECK(walked(k_cap, 6u));
    CHECK(g.set_v_max <= 4.0f + 0.001f);

    /* The model's lowest set point, 3.3V, over LOWEST VOLTAGE 2.0V. */
    rig_fresh();
    walk_alone(2.0f, 0.5f);
    static const float k_sup[] = { 5.0f, 4.5f, 4.0f, 3.5f, 3.3f };
    CHECK(walked(k_sup, 5u));
    CHECK(strstr(g.report, "from 5.00V down in 0.50V steps to 3.30V")
          != NULL);

    /* A cap under the supply's lowest set point: no walk, as before. */
    rig_fresh();
    g.v_max = 3.0f;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(10000u);
    CHECK_EQ(g.t.step_count, 0u);
}

/* The walk stops at the first voltage without movement whatever the step:
 * a servo that stops under 3.05V, walked in 0.1V steps, moves at 3.1V and
 * not at 3.0V. */
TEST_CASE(a_fine_walk_stops_where_the_servo_stops)
{
    rig_fresh();
    low_supply();
    g.brownout_v = 3.05f;
    walk_alone(2.0f, 0.1f);
    float moved = 0.0f;
    bool stopped = false;
    CHECK(servo_test_brownout(&g.t, &moved, &stopped));
    CHECK(stopped);
    CHECK_NEAR(moved, 3.1f, 0.0005f);
    CHECK_EQ(g.t.step_count, 21u);
    CHECK(strstr(g.report, "Movement seen at 3.10V; no movement seen at "
                           "3.00V.") != NULL);
}

/* A configuration outside the settings' ranges is held to them, so the
 * walk never needs more than SERVO_TEST_BROWNOUT_MAX slots; NaN and 0 are
 * the defaults.  The voltage function refuses a step of 0 and a start
 * under the floor. */
TEST_CASE(values_outside_the_ranges_are_held_to_them)
{
    float from = 0.0f, floor_v = 0.0f, step = 0.0f;
    servo_test_brownout_walk(1.0f, 0.01f, 0.0f, 21.0f, &from, &floor_v,
                             &step);
    CHECK_NEAR(from, 5.0f, 1e-6f);
    CHECK_NEAR(floor_v, 2.0f, 1e-6f);
    CHECK_NEAR(step, 0.1f, 1e-6f);
    CHECK_EQ(servo_test_brownout_count(from, floor_v, step), 31u);

    servo_test_brownout_walk(9.0f, 5.0f, 0.0f, 21.0f, &from, &floor_v,
                             &step);
    CHECK_NEAR(floor_v, 4.8f, 1e-6f);
    CHECK_NEAR(step, 1.0f, 1e-6f);

    servo_test_brownout_walk(NAN, -1.0f, 0.0f, 21.0f, &from, &floor_v,
                             &step);
    CHECK_NEAR(floor_v, SERVO_TEST_BROWNOUT_FLOOR_V, 1e-6f);
    CHECK_NEAR(step, SERVO_TEST_BROWNOUT_STEP_V, 1e-6f);
    servo_test_brownout_walk(0.0f, 0.0f, 0.0f, 21.0f, NULL, NULL, NULL);

    CHECK(servo_test_brownout_v(5.0f, 3.0f, 0.0f, 0u) < 0.0f);
    CHECK(servo_test_brownout_v(2.5f, 3.0f, 0.2f, 0u) < 0.0f);
    CHECK(servo_test_brownout_v(5.0f, 3.0f, 0.2f, 11u) < 0.0f);
    CHECK_NEAR(servo_test_brownout_v(5.0f, 3.0f, 0.2f, 10u), 3.0f, 1e-6f);
    CHECK_EQ(servo_test_brownout_count(5.0f, 3.0f, 0.0f), 0u);
}

int main(void)
{
    RUN(the_defaults_walk_from_5_v_to_3_v_in_0_2_v_steps);
    RUN(the_finest_walk_is_31_voltages_down_to_2_v);
    RUN(a_last_step_shorter_than_step_lands_on_the_floor);
    RUN(a_floor_at_or_above_the_start_walks_the_start_alone);
    RUN(the_cap_and_the_supply_bound_the_walk);
    RUN(a_fine_walk_stops_where_the_servo_stops);
    RUN(values_outside_the_ranges_are_held_to_them);
    return test_summary("servo_brownout");
}
