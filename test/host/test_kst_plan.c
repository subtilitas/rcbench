/*
 * KST write plans: which registers are written, in which order, and what
 * the servo holds between two writes.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_plan.h"
#include "kst_sim.h"

static kst_image_t edited(const kst_image_t *from, kst_field_id_t id,
                          unsigned raw)
{
    kst_image_t img = *from;

    if (!kst_field_edit(&img, id, (uint16_t)raw)) {
        memset(&img, 0xFF, sizeof(img));
    }
    return img;
}

/* What every plan holds, whatever it was built for. */
static bool plan_is_consistent(const kst_plan_t *plan)
{
    kst_image_t img = plan->start;
    const kst_rules_t allowed = kst_limits_image(&plan->start)
                                | kst_limits_image(&plan->target);

    if (plan->n > KST_PLAN_MAX_STEPS) {
        return false;
    }
    for (unsigned i = 0; i < plan->n; ++i) {
        const kst_write_t *w = &plan->step[i];
        kst_image_t after;
        kst_rules_t broken;

        if (w->reg == 0u || w->reg > KST_REG_MAX || w->prev != img.r[w->reg]
            || w->value == w->prev) {
            return false;
        }
        img.r[w->reg] = w->value;
        if (!kst_plan_image_after(plan, i + 1u, &after)
            || memcmp(&after, &img, sizeof(img)) != 0) {
            return false;
        }
        broken = kst_limits_image(&img) & ~allowed;
        /* Between the writes of 02 and 01 the copy differs. */
        if (w->reg == KST_REG_DUTY && i + 1u < plan->n
            && plan->step[i + 1u].reg == KST_REG_DUTY_COPY) {
            broken &= ~KST_RULE_BIT(KST_H_DUTY_COPY);
        }
        if (broken != 0u && plan->unchecked == 0u) {
            return false;
        }
    }
    if (plan->n > 0u && plan->step[plan->n - 1u].settled != 1u) {
        return false;
    }
    return memcmp(&img, &plan->target, sizeof(img)) == 0;
}

TEST_CASE(one_field_in_one_register_is_one_write)
{
    const kst_image_t start = sim_bench_image();
    const kst_image_t target = edited(&start, KST_F_BOOST, 19);
    kst_plan_t plan;
    kst_rules_t violated = 1;

    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, &violated), KST_PLAN_OK);
    CHECK(violated == 0u);
    CHECK_EQ(plan.kind, KST_PLAN_EDIT);
    CHECK_EQ(plan.unchecked, 0);
    CHECK_EQ(plan.n, 1);
    CHECK_EQ(plan.step[0].reg, 0x03);
    CHECK_EQ(plan.step[0].value, 19);
    CHECK_EQ(plan.step[0].prev, 20);
    CHECK_EQ(plan.step[0].settled, 1);
    CHECK_EQ(memcmp(&plan.start, &start, sizeof(start)), 0);
    CHECK_EQ(memcmp(&plan.target, &target, sizeof(target)), 0);
    CHECK(plan_is_consistent(&plan));
}

TEST_CASE(an_unchanged_image_is_a_plan_of_no_writes)
{
    const kst_image_t start = sim_bench_image();
    kst_plan_t plan;

    CHECK_EQ(kst_plan_edit(&start, &start, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 0);
    CHECK(plan_is_consistent(&plan));
    CHECK_EQ(kst_plan_restore(&start, &start, &plan), KST_PLAN_OK);
    CHECK_EQ(plan.n, 0);
}

TEST_CASE(a_register_is_written_whole_with_the_other_bits_as_they_are)
{
    kst_image_t start = sim_bench_image();
    kst_image_t target;
    kst_plan_t plan;

    /* Bits 7-6 of 0D have no name; this servo has them set. */
    start.r[0x0D] = 0xC7;
    target = edited(&start, KST_F_UNCONT_POS, 0x653);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 1);
    CHECK_EQ(plan.step[0].reg, 0x0D);
    CHECK_EQ(plan.step[0].value, 0xC6);

    /* Stretch shares 04 with Soft_Start, 20k_sel and spd_sel. */
    start = sim_bench_image();
    target = edited(&start, KST_F_STRETCH, 3);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 1);
    CHECK_EQ(plan.step[0].reg, 0x04);
    CHECK_EQ(plan.step[0].value, 0x4B);
}

TEST_CASE(max_duty_writes_02_then_its_copy_01)
{
    const kst_image_t start = sim_bench_image();
    const kst_image_t target = edited(&start, KST_F_DUTY, 200);
    kst_plan_t plan;

    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[0].reg, 0x02);
    CHECK_EQ(plan.step[0].value, 200);
    CHECK_EQ(plan.step[0].settled, 0);
    CHECK_EQ(plan.step[1].reg, 0x01);
    CHECK_EQ(plan.step[1].value, 200);
    CHECK_EQ(plan.step[1].settled, 1);
    CHECK(plan_is_consistent(&plan));
}

TEST_CASE(register_00_is_never_in_a_plan)
{
    const kst_image_t start = sim_bench_image();
    kst_image_t target = start;
    kst_plan_t plan;

    target.r[0x00] = 0x01;
    target.r[0x03] = 19;
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_ERR_R00);
    CHECK_EQ(plan.n, 0);
    CHECK_EQ(kst_plan_restore(&start, &target, &plan), KST_PLAN_ERR_R00);
    CHECK_EQ(plan.n, 0);
}

TEST_CASE(register_1d_is_in_no_edit)
{
    const kst_image_t start = sim_bench_image();
    kst_image_t target = start;
    kst_plan_t plan;

    target.r[0x1D] = 0x00;
    CHECK_EQ(kst_plan_edit(&start, &target, ~(kst_field_set_t)0, &plan, NULL),
             KST_PLAN_ERR_PAIRING);
    CHECK_EQ(plan.n, 0);
}

TEST_CASE(a_target_that_breaks_a_hard_rule_is_refused_with_the_rules)
{
    const kst_image_t start = sim_bench_image();
    kst_image_t target = edited(&start, KST_F_DUTY, 251);
    kst_plan_t plan;
    kst_rules_t violated = 0;

    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, &violated),
             KST_PLAN_ERR_RULES);
    CHECK(violated == KST_RULE_BIT(KST_H_DUTY_HIGH));
    CHECK_EQ(plan.n, 0);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL),
             KST_PLAN_ERR_RULES);

    /* A locked field, until the caller unlocks it. */
    target = edited(&start, KST_F_20K_SEL, 0);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, &violated),
             KST_PLAN_ERR_RULES);
    CHECK(violated == KST_RULE_BIT(KST_H_LOCKED));
    CHECK_EQ(kst_plan_edit(&start, &target, KST_FIELD_BIT(KST_F_20K_SEL),
                           &plan, &violated), KST_PLAN_OK);
    CHECK(violated == 0u);
    CHECK(plan.unlocked == KST_FIELD_BIT(KST_F_20K_SEL));
    CHECK_EQ(plan.n, 1);
    CHECK_EQ(plan.step[0].reg, 0x04);
    CHECK_EQ(plan.step[0].value, 0x08);

    /* Bits without a name: nothing unlocks them. */
    target = start;
    target.r[0x14] = 0x02;
    CHECK_EQ(kst_plan_edit(&start, &target, ~(kst_field_set_t)0, &plan,
                           &violated), KST_PLAN_ERR_RULES);
    CHECK(violated == KST_RULE_BIT(KST_H_NEVER));

    /* Soft rules do not refuse. */
    target = edited(&start, KST_F_DUTY, 250);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, &violated), KST_PLAN_OK);
    CHECK(violated == 0u);
}

TEST_CASE(missing_arguments_are_refused)
{
    const kst_image_t start = sim_bench_image();
    kst_plan_t plan;
    kst_rules_t violated = 5;
    kst_image_t img;

    memset(&plan, 0xAA, sizeof(plan));
    CHECK_EQ(kst_plan_edit(NULL, &start, 0, &plan, &violated),
             KST_PLAN_ERR_ARG);
    CHECK_EQ(plan.n, 0);
    CHECK(violated == 0u);
    CHECK_EQ(kst_plan_edit(&start, NULL, 0, &plan, NULL), KST_PLAN_ERR_ARG);
    CHECK_EQ(kst_plan_edit(&start, &start, 0, NULL, NULL), KST_PLAN_ERR_ARG);
    memset(&plan, 0xAA, sizeof(plan));
    CHECK_EQ(kst_plan_restore(NULL, &start, &plan), KST_PLAN_ERR_ARG);
    CHECK_EQ(plan.n, 0);
    CHECK_EQ(kst_plan_restore(&start, NULL, &plan), KST_PLAN_ERR_ARG);
    CHECK_EQ(kst_plan_restore(&start, &start, NULL), KST_PLAN_ERR_ARG);
    memset(&plan, 0xAA, sizeof(plan));
    CHECK_EQ(kst_plan_release_pairing(NULL, &plan), KST_PLAN_ERR_ARG);
    CHECK_EQ(plan.n, 0);
    CHECK_EQ(kst_plan_release_pairing(&start, NULL), KST_PLAN_ERR_ARG);

    CHECK_EQ(kst_plan_release_pairing(&start, &plan), KST_PLAN_OK);
    CHECK(!kst_plan_image_after(NULL, 0, &img));
    CHECK(!kst_plan_image_after(&plan, 0, NULL));
    CHECK(kst_plan_image_after(&plan, 0, &img));
    CHECK_EQ(memcmp(&img, &start, sizeof(img)), 0);
    CHECK(kst_plan_image_after(&plan, 1, &img));
    CHECK_EQ(img.r[0x1D], 0x00);
    CHECK(!kst_plan_image_after(&plan, 2, &img));
    plan.n = KST_PLAN_MAX_STEPS + 1u;
    CHECK(!kst_plan_image_after(&plan, 0, &img));
}

/* --- two-register fields ------------------------------------------------------ */

TEST_CASE(of_two_valid_orders_the_one_with_less_travel_is_taken)
{
    const kst_image_t start = sim_bench_image();
    /* Lower 0x465 to 0x500: 07 first passes 0x400, 09 first passes 0x565.
     * Both are valid; 0x565 leaves less travel to Upper. */
    kst_image_t target = edited(&start, KST_F_PULSE_LOWER, 0x500);
    kst_plan_t plan;

    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[0].reg, 0x09);
    CHECK_EQ(plan.step[0].value, 0xA5);
    CHECK_EQ(plan.step[0].settled, 0);
    CHECK_EQ(plan.step[1].reg, 0x07);
    CHECK_EQ(plan.step[1].value, 0x00);
    CHECK_EQ(plan.step[1].settled, 1);
    CHECK(plan_is_consistent(&plan));

    /* And back: 07 first passes 0x565. */
    CHECK_EQ(kst_plan_edit(&target, &start, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[0].reg, 0x07);
    CHECK_EQ(plan.step[1].reg, 0x09);
    CHECK(plan_is_consistent(&plan));

    /* Upper 0xA41 to 0x950: 08 first passes 0xA50, 09 first passes 0x941. */
    target = edited(&start, KST_F_PULSE_UPPER, 0x950);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[0].reg, 0x09);
    CHECK_EQ(plan.step[0].value, 0x94);
    CHECK_EQ(plan.step[1].reg, 0x08);
    CHECK(plan_is_consistent(&plan));
}

TEST_CASE(the_order_that_keeps_every_image_valid_is_taken)
{
    kst_image_t start = sim_bench_image();
    kst_image_t target;
    kst_plan_t plan;

    /* Lower 0x2FF to 0x300.  07 first passes 0x200, 409.6 us, below the
     * 500 us limit; 09 first passes 0x3FF. */
    CHECK(kst_field_edit(&start, KST_F_PULSE_LOWER, 0x2FF));
    target = edited(&start, KST_F_PULSE_LOWER, 0x300);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[0].reg, 0x09);
    CHECK_EQ(plan.step[0].value, 0xA3);
    CHECK_EQ(plan.step[1].reg, 0x07);
    CHECK_EQ(plan.step[1].value, 0x00);
    CHECK(plan_is_consistent(&plan));

    /* Back: 09 first passes 0x200; 07 first passes 0x3FF and is taken. */
    CHECK_EQ(kst_plan_edit(&target, &start, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[0].reg, 0x07);
    CHECK_EQ(plan.step[0].value, 0xFF);
    CHECK_EQ(plan.step[1].reg, 0x09);
    CHECK(plan_is_consistent(&plan));
}

TEST_CASE(two_fields_that_share_register_09_are_ordered_together)
{
    const kst_image_t start = sim_bench_image();
    kst_image_t target = start;
    kst_plan_t plan;

    /* Lower 0x465 to 0x380, Upper 0xA41 to 0xB10: 07, 08 and 09 change. */
    CHECK(kst_field_edit(&target, KST_F_PULSE_LOWER, 0x380));
    CHECK(kst_field_edit(&target, KST_F_PULSE_UPPER, 0xB10));
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 3);
    CHECK(plan_is_consistent(&plan));
    CHECK_EQ(plan.step[0].settled, 0);
    CHECK_EQ(plan.step[2].settled, 1);
}

TEST_CASE(where_no_order_is_valid_an_extra_write_goes_first)
{
    kst_image_t start = sim_bench_image();
    kst_image_t target;
    kst_plan_t plan;
    kst_image_t mid;

    /* Upper 0xBFF to 0xC00 with Uncontrolled Pos at 0xB20.  08 first passes
     * 0xB00, below Uncontrolled Pos; 09 first passes 0xCFF, above 2500 us.
     * With 08 at 0x35 first: 0xB35, then 0xC35 (2500.0 us), then 0xC00. */
    CHECK(kst_field_edit(&start, KST_F_PULSE_UPPER, 0xBFF));
    CHECK(kst_field_edit(&start, KST_F_UNCONT_POS, 0xB20));
    CHECK(kst_limits_image(&start) == 0u);
    target = edited(&start, KST_F_PULSE_UPPER, 0xC00);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.unchecked, 0);
    CHECK_EQ(plan.n, 3);
    CHECK_EQ(plan.step[0].reg, 0x08);
    CHECK_EQ(plan.step[0].value, 0x35);
    CHECK_EQ(plan.step[0].prev, 0xFF);
    CHECK_EQ(plan.step[0].settled, 0);
    CHECK_EQ(plan.step[1].reg, 0x09);
    CHECK_EQ(plan.step[1].value, 0xC4);
    CHECK_EQ(plan.step[1].settled, 0);
    CHECK_EQ(plan.step[2].reg, 0x08);
    CHECK_EQ(plan.step[2].value, 0x00);
    CHECK_EQ(plan.step[2].prev, 0x35);
    CHECK_EQ(plan.step[2].settled, 1);
    CHECK(plan_is_consistent(&plan));
    CHECK(kst_plan_image_after(&plan, 1, &mid));
    CHECK_EQ(kst_field_get(&mid, KST_F_PULSE_UPPER), 0xB35);
    CHECK(kst_plan_image_after(&plan, 2, &mid));
    CHECK_EQ(kst_field_get(&mid, KST_F_PULSE_UPPER), 0xC35);
}

TEST_CASE(where_no_extra_write_helps_an_edit_is_refused)
{
    kst_image_t start = sim_bench_image();
    kst_image_t target;
    kst_plan_t plan;

    /* Uncontrolled Pos at Upper itself: no value of 08 keeps 0xBxx at or
     * above it but the one it has. */
    CHECK(kst_field_edit(&start, KST_F_PULSE_UPPER, 0xBFF));
    CHECK(kst_field_edit(&start, KST_F_UNCONT_POS, 0xBFF));
    CHECK(kst_limits_image(&start) == 0u);
    target = edited(&start, KST_F_PULSE_UPPER, 0xC00);
    CHECK(kst_limits_image(&target) == 0u);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL),
             KST_PLAN_ERR_NO_PATH);
    CHECK_EQ(plan.n, 0);
    CHECK_EQ(plan.unchecked, 0);

    /* The same change in two edits has a path: Uncontrolled Pos first. */
    target = edited(&start, KST_F_UNCONT_POS, 0xB20);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
}

TEST_CASE(a_restore_with_no_valid_order_is_marked_unchecked)
{
    kst_image_t backup = sim_bench_image();
    kst_image_t current;
    kst_plan_t plan;

    CHECK(kst_field_edit(&backup, KST_F_PULSE_UPPER, 0xC00));
    CHECK(kst_field_edit(&backup, KST_F_UNCONT_POS, 0xBFF));
    current = edited(&backup, KST_F_PULSE_UPPER, 0xBFF);
    current.r[0x03] = 30;
    CHECK_EQ(kst_plan_restore(&current, &backup, &plan), KST_PLAN_OK);
    CHECK_EQ(plan.kind, KST_PLAN_RESTORE);
    CHECK_EQ(plan.unchecked, 1);
    /* Ascending addresses. */
    CHECK_EQ(plan.n, 3);
    CHECK_EQ(plan.step[0].reg, 0x03);
    CHECK_EQ(plan.step[1].reg, 0x08);
    CHECK_EQ(plan.step[2].reg, 0x09);
    CHECK(plan_is_consistent(&plan));
}

TEST_CASE(a_plan_never_passes_through_a_hard_rule_its_end_points_keep)
{
    /* Deterministic pseudo-random edits of the fields the hard rules read. */
    uint32_t seed = 12345;
    unsigned planned = 0;
    unsigned no_path = 0;
    unsigned detours = 0;
    kst_image_t start = sim_bench_image();

    for (unsigned round = 0; round < 4000u; ++round) {
        static const struct {
            kst_field_id_t id;
            uint16_t lo;
            uint16_t hi;
        } field[] = {
            { KST_F_PULSE_LOWER, 600, 1900 },
            { KST_F_PULSE_UPPER, 1850, 3150 },
            { KST_F_NEUTRAL, 2700, 4200 },
            { KST_F_UNCONT_POS, 600, 3150 },
            { KST_F_SPD, 0, 1010 },
            { KST_F_SPD_SEL, 0, 1 },
            { KST_F_DUTY, 20, 255 },
            { KST_F_STRETCH, 0, 5 },
            { KST_F_UNCONT_TIME, 4, 130 },
        };
        kst_image_t target = start;
        kst_plan_t plan;
        kst_plan_result_t res;
        unsigned writes[KST_REG_COUNT] = { 0 };
        unsigned changed = 0;

        for (unsigned k = 0; k < sizeof(field) / sizeof(field[0]); ++k) {
            seed = seed * 1664525u + 1013904223u;
            if (((seed >> 8) & 3u) != 0u) {
                continue;
            }
            seed = seed * 1664525u + 1013904223u;
            CHECK(kst_field_edit(&target, field[k].id,
                                 (uint16_t)(field[k].lo
                                            + (seed >> 12)
                                                  % (field[k].hi - field[k].lo
                                                     + 1u))));
        }
        if (kst_limits_image(&target) != 0u) {
            continue;
        }
        res = kst_plan_edit(&start, &target, 0, &plan, NULL);
        if (res == KST_PLAN_ERR_NO_PATH) {
            kst_plan_t back;

            no_path++;
            CHECK_EQ(plan.n, 0);
            /* A restore over the same way always gives a plan. */
            CHECK_EQ(kst_plan_restore(&start, &target, &back), KST_PLAN_OK);
            CHECK_EQ(back.unchecked, 1);
            CHECK(plan_is_consistent(&back));
            continue;
        }
        CHECK_EQ(res, KST_PLAN_OK);
        CHECK(plan_is_consistent(&plan));
        for (unsigned r = 0; r < KST_REG_COUNT; ++r) {
            changed += start.r[r] != target.r[r] ? 1u : 0u;
        }
        for (unsigned i = 0; i < plan.n; ++i) {
            writes[plan.step[i].reg]++;
        }
        /* Only registers that change, and at most 1 extra write a group. */
        for (unsigned r = 0; r < KST_REG_COUNT; ++r) {
            if (start.r[r] == target.r[r]) {
                CHECK_EQ(writes[r], 0);
            } else {
                CHECK(writes[r] >= 1u && writes[r] <= 2u);
            }
        }
        CHECK(plan.n <= changed + 2u);
        detours += plan.n > changed ? 1u : 0u;
        planned++;
        /* The next round starts where this one ended. */
        start = target;
    }
    CHECK(planned > 1000u);
    /* An edit with no path is the rare case. */
    CHECK(no_path * 20u < planned);
    (void)detours;
}

/* --- groups -------------------------------------------------------------------- */

TEST_CASE(positions_come_first_then_speed_then_duty_then_the_rest)
{
    const kst_image_t start = sim_bench_image();
    kst_image_t target = start;
    kst_plan_t plan;

    CHECK(kst_field_edit(&target, KST_F_RIGHT_ANGLE, 140));
    CHECK(kst_field_edit(&target, KST_F_BOOST, 10));
    CHECK(kst_field_edit(&target, KST_F_DUTY, 200));
    CHECK(kst_field_edit(&target, KST_F_SPD, 0x20A));
    CHECK(kst_field_edit(&target, KST_F_UNCONT_TIME, 30));
    CHECK(kst_field_edit(&target, KST_F_UNCONT_POS, 0x760));
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 8);
    CHECK_EQ(plan.step[0].reg, 0x0C);
    CHECK_EQ(plan.step[1].reg, 0x1C);
    CHECK_EQ(plan.step[2].reg, 0x02);
    CHECK_EQ(plan.step[3].reg, 0x01);
    CHECK_EQ(plan.step[4].reg, 0x03);
    CHECK_EQ(plan.step[5].reg, 0x05);
    CHECK_EQ(plan.step[6].reg, 0x0E);
    CHECK_EQ(plan.step[7].reg, 0x11);
    CHECK(plan_is_consistent(&plan));
}

TEST_CASE(the_speed_limit_is_never_on_with_a_speed_below_5_percent)
{
    kst_image_t start = sim_bench_image();
    kst_image_t target;
    kst_plan_t plan;

    /* SPD 0x100 to 0x0FF with spd_sel on: 1C first passes 0x000. */
    CHECK(kst_field_edit(&start, KST_F_SPD_SEL, 1));
    CHECK(kst_field_edit(&start, KST_F_SPD, 0x100));
    target = edited(&start, KST_F_SPD, 0x0FF);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 2);
    CHECK_EQ(plan.step[0].reg, 0x1B);
    CHECK_EQ(plan.step[1].reg, 0x1C);
    CHECK(plan_is_consistent(&plan));
    /* Back: 1B first passes 0x000. */
    CHECK_EQ(kst_plan_edit(&target, &start, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.step[0].reg, 0x1C);
    CHECK_EQ(plan.step[1].reg, 0x1B);
    CHECK(plan_is_consistent(&plan));

    /* Switched on together with a higher speed: the speed first, and the
     * switch as soon as the value allows it. */
    start = sim_bench_image();
    target = start;
    CHECK(kst_field_edit(&target, KST_F_SPD_SEL, 1));
    CHECK(kst_field_edit(&target, KST_F_SPD, 0x1F4));
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.n, 3);
    CHECK_EQ(plan.step[0].reg, 0x1B);
    CHECK_EQ(plan.step[1].reg, 0x04);
    CHECK_EQ(plan.step[2].reg, 0x1C);
    CHECK(plan_is_consistent(&plan));

    /* Of two valid orders the slower one: 0x1F4 to 0x2F0 passes 0x1F0. */
    start = target;
    target = edited(&start, KST_F_SPD, 0x2F0);
    CHECK_EQ(kst_plan_edit(&start, &target, 0, &plan, NULL), KST_PLAN_OK);
    CHECK_EQ(plan.step[0].reg, 0x1B);
    CHECK_EQ(plan.step[1].reg, 0x1C);
}

/* --- restore and pairing -------------------------------------------------------- */

TEST_CASE(a_restore_writes_every_register_that_differs_1d_included)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t current = backup;
    kst_plan_t plan;
    bool has_1d = false;

    current.r[0x1D] = 0x00;
    current.r[0x03] = 30;
    current.r[0x14] = 0x7F;   /* no name, no edit path */
    current.r[0x04] = 0x08;   /* 20k_sel, locked */
    CHECK(kst_field_edit(&current, KST_F_DUTY, 100));
    CHECK_EQ(kst_plan_restore(&current, &backup, &plan), KST_PLAN_OK);
    CHECK_EQ(plan.kind, KST_PLAN_RESTORE);
    CHECK_EQ(plan.unchecked, 0);
    CHECK(plan.unlocked == ~(kst_field_set_t)0);
    CHECK_EQ(plan.n, 6);
    CHECK(plan_is_consistent(&plan));
    for (unsigned i = 0; i < plan.n; ++i) {
        has_1d = has_1d || (plan.step[i].reg == 0x1D
                            && plan.step[i].value == 0x10);
    }
    CHECK(has_1d);
}

TEST_CASE(a_restore_from_an_image_that_breaks_rules_is_planned)
{
    const kst_image_t backup = sim_bench_image();
    kst_image_t current;
    kst_plan_t plan;

    /* Every register but 00 differs. */
    for (unsigned r = 0; r < KST_REG_COUNT; ++r) {
        current.r[r] = (uint8_t)(r == 0u ? backup.r[r] : backup.r[r] ^ 0xFFu);
    }
    CHECK(kst_limits_image(&current) != 0u);
    CHECK_EQ(kst_plan_restore(&current, &backup, &plan), KST_PLAN_OK);
    CHECK(plan.n >= 31u);
    CHECK(plan.n <= KST_PLAN_MAX_STEPS);
    CHECK(plan_is_consistent(&plan));
}

TEST_CASE(releasing_the_pairing_writes_00_to_1d_and_nothing_else)
{
    kst_image_t current = sim_bench_image();
    kst_plan_t plan;

    CHECK_EQ(kst_plan_release_pairing(&current, &plan), KST_PLAN_OK);
    CHECK_EQ(plan.kind, KST_PLAN_RELEASE_PAIRING);
    CHECK_EQ(plan.n, 1);
    CHECK_EQ(plan.step[0].reg, 0x1D);
    CHECK_EQ(plan.step[0].value, 0x00);
    CHECK_EQ(plan.step[0].prev, 0x10);
    CHECK_EQ(plan.step[0].settled, 1);
    CHECK(plan_is_consistent(&plan));
    /* Already released: no write. */
    current.r[0x1D] = 0x00;
    CHECK_EQ(kst_plan_release_pairing(&current, &plan), KST_PLAN_OK);
    CHECK_EQ(plan.n, 0);
}

int main(void)
{
    RUN(one_field_in_one_register_is_one_write);
    RUN(an_unchanged_image_is_a_plan_of_no_writes);
    RUN(a_register_is_written_whole_with_the_other_bits_as_they_are);
    RUN(max_duty_writes_02_then_its_copy_01);
    RUN(register_00_is_never_in_a_plan);
    RUN(register_1d_is_in_no_edit);
    RUN(a_target_that_breaks_a_hard_rule_is_refused_with_the_rules);
    RUN(missing_arguments_are_refused);
    RUN(of_two_valid_orders_the_one_with_less_travel_is_taken);
    RUN(the_order_that_keeps_every_image_valid_is_taken);
    RUN(two_fields_that_share_register_09_are_ordered_together);
    RUN(where_no_order_is_valid_an_extra_write_goes_first);
    RUN(where_no_extra_write_helps_an_edit_is_refused);
    RUN(a_restore_with_no_valid_order_is_marked_unchecked);
    RUN(a_plan_never_passes_through_a_hard_rule_its_end_points_keep);
    RUN(positions_come_first_then_speed_then_duty_then_the_rest);
    RUN(the_speed_limit_is_never_on_with_a_speed_below_5_percent);
    RUN(a_restore_writes_every_register_that_differs_1d_included);
    RUN(a_restore_from_an_image_that_breaks_rules_is_planned);
    RUN(releasing_the_pairing_writes_00_to_1d_and_nothing_else);
    return test_summary("kst_plan");
}
