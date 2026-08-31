/*
 * The parameter cache: one value arrives per telemetry frame, and the table is
 * either whole or it is not the ESC's settings.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "openyge.h"

/* Feed a whole table of `count` parameters, values 100 + index. */
static void fill(openyge_params_t *p, uint16_t count)
{
    openyge_params_observe(p, 0, count);
    for (uint16_t i = 1; i < count; ++i) {
        openyge_params_observe(p, i, (uint16_t)(100 + i));
    }
}

TEST_CASE(the_table_is_complete_only_when_every_index_has_arrived)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    CHECK_EQ(openyge_params_complete(&p), false);

    /* The count alone is not a table. */
    openyge_params_observe(&p, 0, 32);
    CHECK_EQ(openyge_params_complete(&p), false);

    for (uint16_t i = 1; i < 31; ++i) {
        openyge_params_observe(&p, i, (uint16_t)(100 + i));
        if (openyge_params_complete(&p)) {
            T_FAIL("complete with index %u still missing", 31);
        }
    }
    openyge_params_observe(&p, 31, 131);
    CHECK(openyge_params_complete(&p));
}

/*
 * The ESC walks its own table, so the count is not necessarily first.  Until
 * it arrives there is no way to know how many are expected.
 */
TEST_CASE(the_count_may_arrive_last)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    for (uint16_t i = 1; i < 8; ++i) {
        openyge_params_observe(&p, i, (uint16_t)(100 + i));
    }
    CHECK_EQ(openyge_params_complete(&p), false);
    CHECK_NEAR(openyge_params_progress(&p), 0.0f, 0.001f);

    openyge_params_observe(&p, 0, 8);
    CHECK(openyge_params_complete(&p));
    CHECK_NEAR(openyge_params_progress(&p), 1.0f, 0.001f);
}

/*
 * A half-read table is not the ESC's settings, and the trap is presenting it
 * as though it were.  Reading is refused outright until it is whole.
 */
TEST_CASE(a_half_read_table_refuses_to_be_read)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    openyge_params_observe(&p, 0, 32);
    openyge_params_observe(&p, 20, 14);

    uint16_t v = 0xFFFF;
    CHECK_EQ(openyge_params_get(&p, 20, &v), false);
    CHECK_EQ(v, 0xFFFF);   /* and it did not write anything either */

    fill(&p, 32);
    CHECK(openyge_params_get(&p, 20, &v));
    CHECK_EQ(v, 120);
}

TEST_CASE(an_index_past_the_count_is_not_a_parameter)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    fill(&p, 8);
    uint16_t v = 0;
    CHECK(openyge_params_get(&p, 7, &v));
    CHECK_EQ(openyge_params_get(&p, 8, &v), false);
    CHECK_EQ(openyge_params_get(&p, 63, &v), false);
}

/*
 * A write withdraws the whole table, not only the index written.  Partly-old
 * and partly-new reads as the ESC's settings and is not.
 */
TEST_CASE(a_pending_write_withdraws_the_whole_table)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    fill(&p, 16);
    CHECK(openyge_params_complete(&p));

    openyge_params_begin_writes(&p);
    CHECK_EQ(openyge_params_complete(&p), false);
    /* The count goes with it, so nothing downstream reads a stale width. */
    CHECK_EQ(p.count, 0);
    CHECK_EQ(p.seen, 0);
    uint16_t v = 0;
    CHECK_EQ(openyge_params_get(&p, 3, &v), false);

    /*
     * Values arriving during the write are stale by definition and ignored:
     * a frame already in flight carries the old value, and accepting it makes
     * the ESC look like it refused the write.
     *
     * A *whole* table is fed here, not one or two values -- otherwise the
     * table stays incomplete for want of entries rather than because the
     * entries were refused, and the assertion measures nothing.
     */
    fill(&p, 16);
    CHECK_EQ(openyge_params_complete(&p), false);
    CHECK_EQ(openyge_params_get(&p, 3, &v), false);

    openyge_params_end_writes(&p);
    CHECK_EQ(openyge_params_complete(&p), false);   /* re-read from scratch */
    fill(&p, 16);
    CHECK(openyge_params_complete(&p));
    CHECK(openyge_params_get(&p, 3, &v));
    CHECK_EQ(v, 103);
}

TEST_CASE(progress_climbs_as_the_table_fills)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    openyge_params_observe(&p, 0, 10);
    CHECK_NEAR(openyge_params_progress(&p), 0.1f, 0.001f);
    for (uint16_t i = 1; i < 5; ++i) {
        openyge_params_observe(&p, i, i);
    }
    CHECK_NEAR(openyge_params_progress(&p), 0.5f, 0.001f);
    fill(&p, 10);
    CHECK_NEAR(openyge_params_progress(&p), 1.0f, 0.001f);
}

/* A repeated index is not progress: the ESC walks its table and a frame may
 * be missed, so counting arrivals rather than distinct indices would report a
 * table complete that has a hole in it. */
TEST_CASE(the_same_index_twice_is_not_two_parameters)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    openyge_params_observe(&p, 0, 4);
    for (int i = 0; i < 20; ++i) {
        openyge_params_observe(&p, 1, 101);
    }
    CHECK_EQ(openyge_params_complete(&p), false);
    CHECK_NEAR(openyge_params_progress(&p), 0.5f, 0.001f);
}

/* The bitmap is exactly 64 wide, so a full table is where a mask built as
 * (1 << count) - 1 overflows.  Both ends of the range are checked. */
TEST_CASE(a_table_at_the_bitmaps_own_width_still_completes)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    fill(&p, OPENYGE_MAX_PARAMS);
    CHECK(openyge_params_complete(&p));
    uint16_t v = 0;
    CHECK(openyge_params_get(&p, OPENYGE_MAX_PARAMS - 1, &v));
    CHECK_EQ(v, 100 + OPENYGE_MAX_PARAMS - 1);

    /*
     * And a count the cache cannot hold is refused rather than truncated.
     * Fed a full 64 entries as well, because that is the state where the
     * bitmap is entirely set and a missing width check would call the table
     * complete -- reporting 64 parameters as though they were all 65.
     */
    openyge_params_reset(&p);
    openyge_params_observe(&p, 0, OPENYGE_MAX_PARAMS + 1);
    for (uint16_t i = 1; i < OPENYGE_MAX_PARAMS; ++i) {
        openyge_params_observe(&p, i, (uint16_t)(100 + i));
    }
    CHECK_EQ(p.seen, ~0ull);   /* every slot the cache has is filled ... */
    CHECK_EQ(openyge_params_complete(&p), false);   /* ... and it is not enough */
    CHECK_NEAR(openyge_params_progress(&p), 0.0f, 0.001f);
}

/*
 * The gear train comes free with the table, so a bench that reads it reports
 * head speed without being told anything about the model.
 */
TEST_CASE(motor_and_head_rpm_come_out_of_the_gear_train)
{
    openyge_params_t p;
    openyge_params_reset(&p);
    fill(&p, 32);
    openyge_params_observe(&p, OPENYGE_P_MOTOR_POLES, 10);
    openyge_params_observe(&p, OPENYGE_P_PINION_TEETH, 13);
    openyge_params_observe(&p, OPENYGE_P_MAIN_TEETH, 110);

    float motor = 0.0f, head = 0.0f;
    CHECK(openyge_motor_rpm(&p, 100000, &motor));
    CHECK_NEAR(motor, 20000.0f, 0.5f);          /* 10 poles -> 5 pole pairs */
    CHECK(openyge_head_rpm(&p, 100000, &head));
    CHECK_NEAR(head, 20000.0f * 13.0f / 110.0f, 0.5f);   /* ~2364 rpm */
}

TEST_CASE(rpm_is_refused_when_the_table_cannot_support_it)
{
    openyge_params_t p;
    float motor = 0.0f, head = 0.0f;

    /* Incomplete table: nothing may be derived from it. */
    openyge_params_reset(&p);
    openyge_params_observe(&p, 0, 32);
    openyge_params_observe(&p, OPENYGE_P_MOTOR_POLES, 10);
    CHECK_EQ(openyge_motor_rpm(&p, 100000, &motor), false);

    /* Complete, but a pole count that cannot be one. */
    openyge_params_reset(&p);
    fill(&p, 32);
    openyge_params_observe(&p, OPENYGE_P_MOTOR_POLES, 0);
    CHECK_EQ(openyge_motor_rpm(&p, 100000, &motor), false);

    /* Complete, sane poles, but no gear train: the motor figure stands and
     * the head figure does not. */
    openyge_params_reset(&p);
    fill(&p, 32);
    openyge_params_observe(&p, OPENYGE_P_MOTOR_POLES, 14);
    openyge_params_observe(&p, OPENYGE_P_PINION_TEETH, 0);
    openyge_params_observe(&p, OPENYGE_P_MAIN_TEETH, 0);
    CHECK(openyge_motor_rpm(&p, 70000, &motor));
    CHECK_NEAR(motor, 10000.0f, 0.5f);
    CHECK_EQ(openyge_head_rpm(&p, 70000, &head), false);
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    openyge_params_reset(NULL);
    openyge_params_observe(NULL, 0, 0);
    openyge_params_begin_writes(NULL);
    openyge_params_end_writes(NULL);
    CHECK_EQ(openyge_params_complete(NULL), false);
    CHECK_EQ(openyge_params_get(NULL, 0, NULL), false);
    CHECK_NEAR(openyge_params_progress(NULL), 0.0f, 0.001f);
    CHECK_EQ(openyge_motor_rpm(NULL, 1000, NULL), false);
    CHECK_EQ(openyge_head_rpm(NULL, 1000, NULL), false);

    /* An index the cache cannot hold must not be written through. */
    openyge_params_t p;
    openyge_params_reset(&p);
    openyge_params_observe(&p, OPENYGE_MAX_PARAMS, 1);
    openyge_params_observe(&p, 0xFFFF, 1);
    CHECK_EQ(p.seen, 0);
}

int main(void)
{
    RUN(the_table_is_complete_only_when_every_index_has_arrived);
    RUN(the_count_may_arrive_last);
    RUN(a_half_read_table_refuses_to_be_read);
    RUN(an_index_past_the_count_is_not_a_parameter);
    RUN(a_pending_write_withdraws_the_whole_table);
    RUN(progress_climbs_as_the_table_fills);
    RUN(the_same_index_twice_is_not_two_parameters);
    RUN(a_table_at_the_bitmaps_own_width_still_completes);
    RUN(motor_and_head_rpm_come_out_of_the_gear_train);
    RUN(rpm_is_refused_when_the_table_cannot_support_it);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("openyge_params");
}
