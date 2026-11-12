/*
 * An error's act as the steps the panel's control task takes, in order
 * (error_act_steps(), shared/safety/error_sites.c): the STOP latch first,
 * then one step per field of error_act_t in the order of the fields, for
 * each of the 32 acts; none for an act that asks nothing.
 * firmware/panel/main/panel_supply.c's error_act() carries the steps out.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include "error_sites.h"

/* Every act: the STOP latch first, then the fields in their order. */
TEST_CASE(an_acts_steps_follow_its_fields)
{
    const error_act_t all = { true, true, true, true, true };
    error_step_t st[ERROR_STEPS_MAX];
    CHECK_EQ(error_act_steps(&all, st), 6u);
    CHECK_EQ(st[0], ERROR_STEP_STOP_LATCH);
    CHECK_EQ(st[1], ERROR_STEP_SUPPLY_OFF);
    CHECK_EQ(st[2], ERROR_STEP_DISARM);
    CHECK_EQ(st[3], ERROR_STEP_SUPPLY_OFF);
    CHECK_EQ(st[4], ERROR_STEP_CUT);
    CHECK_EQ(st[5], ERROR_STEP_PROG_OFF);

    /* The trip's act on an ARMED bench: off first, then DISARM. */
    const error_act_t trip = { true, true, false, false, false };
    CHECK_EQ(error_act_steps(&trip, st), 3u);
    CHECK_EQ(st[0], ERROR_STEP_STOP_LATCH);
    CHECK_EQ(st[1], ERROR_STEP_SUPPLY_OFF);
    CHECK_EQ(st[2], ERROR_STEP_DISARM);
}

/* Each of the 32 acts: one step per field set, plus the STOP latch ahead
 * of them when any is; none for an act that asks nothing, the STOP latch
 * included. */
TEST_CASE(every_act_takes_one_step_per_field_and_the_latch)
{
    static const error_step_t of_bit[5] = {
        ERROR_STEP_SUPPLY_OFF, ERROR_STEP_DISARM, ERROR_STEP_SUPPLY_OFF,
        ERROR_STEP_CUT, ERROR_STEP_PROG_OFF,
    };
    for (unsigned m = 0u; m < 32u; ++m) {
        const error_act_t a = {
            (m & 1u) != 0u, (m & 2u) != 0u, (m & 4u) != 0u,
            (m & 8u) != 0u, (m & 16u) != 0u,
        };
        error_step_t st[ERROR_STEPS_MAX];
        const uint8_t n = error_act_steps(&a, st);
        unsigned want = 0u;
        for (unsigned b = 0u; b < 5u; ++b) {
            want += (m >> b) & 1u;
        }
        if (m == 0u) {
            CHECK_EQ(n, 0u);
            continue;
        }
        CHECK_EQ(n, want + 1u);
        CHECK_EQ(st[0], ERROR_STEP_STOP_LATCH);
        unsigned k = 1u;
        for (unsigned b = 0u; b < 5u; ++b) {
            if ((m >> b) & 1u) {
                CHECK_EQ(st[k], of_bit[b]);
                ++k;
            }
        }
    }
}

TEST_CASE(a_null_act_takes_no_steps)
{
    const error_act_t all = { true, true, true, true, true };
    error_step_t st[ERROR_STEPS_MAX];
    CHECK_EQ(error_act_steps(NULL, st), 0u);
    CHECK_EQ(error_act_steps(&all, NULL), 0u);
}

int main(void)
{
    RUN(an_acts_steps_follow_its_fields);
    RUN(every_act_takes_one_step_per_field_and_the_latch);
    RUN(a_null_act_takes_no_steps);
    return test_summary("error_steps");
}
