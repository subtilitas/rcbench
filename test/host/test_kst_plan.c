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

/* --- the planner's output, pinned ------------------------------------------- */

/*
 * A session runs a plan only when it equals the planner's own, write for
 * write.  The cases below hold the planner's output over a set of inputs
 * to one digest each: FNV-1a, 64 bits, over the result, the broken rules
 * and every member of the plan, the unused writes included.  A change of
 * the planner that changes one byte of one plan fails here.
 */
typedef struct {
    uint64_t digest;
    unsigned calls;
    unsigned ok;
    unsigned extra;      /* plans with a write to a third value */
    unsigned no_path;
    unsigned unchecked;
    unsigned refused;    /* a rule, register 00 or register 1D */
    unsigned unsound;    /* checked plans plan_is_consistent() rejects */
    unsigned longest;    /* most writes in one plan */
} pin_t;

static void pin_begin(pin_t *p)
{
    memset(p, 0, sizeof(*p));
    p->digest = UINT64_C(0xCBF29CE484222325);
}

static void pin_byte(pin_t *p, unsigned b)
{
    p->digest = (p->digest ^ (b & 0xFFu)) * UINT64_C(0x100000001B3);
}

static void pin_u64(pin_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8u; ++i) {
        pin_byte(p, (unsigned)(v >> (8u * i)));
    }
}

static void pin_image(pin_t *p, const kst_image_t *img)
{
    for (unsigned i = 0; i < KST_REG_COUNT; ++i) {
        pin_byte(p, img->r[i]);
    }
}

static unsigned registers_that_differ(const kst_plan_t *plan)
{
    unsigned n = 0;

    for (unsigned i = 0; i < KST_REG_COUNT; ++i) {
        n += plan->start.r[i] != plan->target.r[i] ? 1u : 0u;
    }
    return n;
}

static void pin_plan(pin_t *p, kst_plan_result_t r, kst_rules_t violated,
                     const kst_plan_t *plan)
{
    p->calls++;
    pin_byte(p, (unsigned)r);
    pin_u64(p, violated);
    pin_image(p, &plan->start);
    pin_image(p, &plan->target);
    pin_u64(p, plan->unlocked);
    pin_byte(p, plan->kind);
    pin_byte(p, plan->n);
    pin_byte(p, plan->unchecked);
    for (unsigned i = 0; i < KST_PLAN_MAX_STEPS; ++i) {
        pin_byte(p, plan->step[i].reg);
        pin_byte(p, plan->step[i].value);
        pin_byte(p, plan->step[i].prev);
        pin_byte(p, plan->step[i].settled);
    }
    if (r == KST_PLAN_OK) {
        p->ok++;
        p->extra += plan->n > registers_that_differ(plan) ? 1u : 0u;
        p->unchecked += plan->unchecked;
        p->unsound += plan_is_consistent(plan) ? 0u : 1u;
        p->longest = plan->n > p->longest ? plan->n : p->longest;
    } else if (r == KST_PLAN_ERR_NO_PATH) {
        p->no_path++;
    } else {
        p->refused++;
    }
}

/* The plan is filled with a pattern first: what the planner leaves in it
 * does not depend on what it held. */
static void pin_edit(pin_t *p, const kst_image_t *start,
                     const kst_image_t *target, kst_field_set_t unlocked)
{
    kst_plan_t plan;
    kst_rules_t violated = ~(kst_rules_t)0;
    kst_plan_result_t r;

    memset(&plan, 0xA5, sizeof(plan));
    r = kst_plan_edit(start, target, unlocked, &plan, &violated);
    pin_plan(p, r, violated, &plan);
}

static void pin_restore(pin_t *p, const kst_image_t *current,
                        const kst_image_t *backup)
{
    kst_plan_t plan;
    kst_plan_result_t r;

    memset(&plan, 0xA5, sizeof(plan));
    r = kst_plan_restore(current, backup, &plan);
    pin_plan(p, r, 0, &plan);
}

static void pin_release(pin_t *p, const kst_image_t *current)
{
    kst_plan_t plan;
    kst_plan_result_t r;

    memset(&plan, 0xA5, sizeof(plan));
    r = kst_plan_release_pairing(current, &plan);
    pin_plan(p, r, 0, &plan);
}

/* Both directions as an edit with every lock lifted and as a restore, and
 * the way there as an edit with no lock lifted. */
static void pin_pair(pin_t *p, const kst_image_t *a, const kst_image_t *b)
{
    pin_edit(p, a, b, ~(kst_field_set_t)0);
    pin_edit(p, b, a, ~(kst_field_set_t)0);
    pin_edit(p, a, b, 0);
    pin_restore(p, a, b);
    pin_restore(p, b, a);
}

static void pin_report(const pin_t *p)
{
    printf("      digest 0x%016llX calls %u ok %u extra %u no_path %u "
           "unchecked %u refused %u longest %u unsound %u\n",
           (unsigned long long)p->digest, p->calls, p->ok, p->extra,
           p->no_path, p->unchecked, p->refused, p->longest, p->unsound);
}

/* The digest, and the tallies that say what the set of inputs reached. */
#define CHECK_PIN(p, want_digest, want_calls, want_ok, want_extra,            \
                  want_no_path, want_unchecked, want_refused, want_longest)   \
    do {                                                                      \
        const pin_t *_p = (p);                                                \
        if (_p->digest != (want_digest) || _p->calls != (want_calls)          \
            || _p->ok != (want_ok) || _p->extra != (want_extra)               \
            || _p->no_path != (want_no_path)                                  \
            || _p->unchecked != (want_unchecked)                              \
            || _p->refused != (want_refused)                                  \
            || _p->longest != (want_longest) || _p->unsound != 0u) {          \
            T_FAIL("the plans differ from the pinned ones");                  \
            pin_report(_p);                                                   \
        }                                                                     \
    } while (0)

/* Start images: the bench image; Upper one below a change of its high
 * part with Uncontrolled Pos close under it; Lower on a change of its
 * high part with Uncontrolled Pos on it; the speed limit on. */
#define PIN_STARTS 4u

static kst_image_t pin_start(unsigned which)
{
    kst_image_t img = sim_bench_image();

    if (which == 1u) {
        (void)kst_field_set(&img, KST_F_PULSE_UPPER, 0xBFF);
        (void)kst_field_set(&img, KST_F_UNCONT_POS, 0xB20);
    } else if (which == 2u) {
        (void)kst_field_set(&img, KST_F_PULSE_LOWER, 0x300);
        (void)kst_field_set(&img, KST_F_UNCONT_POS, 0x300);
    } else if (which == 3u) {
        (void)kst_field_set(&img, KST_F_SPD_SEL, 1);
        (void)kst_field_set(&img, KST_F_SPD, 0x100);
    }
    return img;
}

/* Every raw value a field's bits hold, the limits of every field among
 * them, from each of the 4 start images. */
TEST_CASE(the_plan_to_every_value_of_every_field_is_pinned)
{
    pin_t p;

    pin_begin(&p);
    for (unsigned s = 0; s < PIN_STARTS; ++s) {
        const kst_image_t start = pin_start(s);

        for (unsigned f = 0; f < KST_F_COUNT; ++f) {
            const kst_field_id_t id = (kst_field_id_t)f;
            const unsigned max = kst_field_raw_max(id);

            for (unsigned v = 0; v <= max; ++v) {
                kst_image_t target = start;

                CHECK(kst_field_set(&target, id, (uint16_t)v));
                pin_edit(&p, &start, &target, ~(kst_field_set_t)0);
                pin_edit(&p, &start, &target, 0);
                pin_restore(&p, &start, &target);
            }
        }
    }
    CHECK_PIN(&p, UINT64_C(0x50CE74E2DCB32004), 408816u, 213452u, 96u,
              286u, 143u, 195078u, 3u);
}

/* A field in two registers changes its high part: values on both sides of
 * every such step, in both directions, alone and with Uncontrolled Pos
 * one below, at and one above each of the two values. */
TEST_CASE(the_plans_across_every_high_part_step_are_pinned)
{
    static const unsigned k_below[] = { 0x01, 0x80, 0xFF, 0x100 };
    static const unsigned k_above[] = { 0x00, 0x7F, 0xFE };
    pin_t p;

    pin_begin(&p);
    for (unsigned f = 0; f < KST_F_COUNT; ++f) {
        const kst_field_id_t id = (kst_field_id_t)f;
        const unsigned max = kst_field_raw_max(id);

        if (kst_field(id)->hi_bits == 0u) {
            continue;
        }
        for (unsigned step = 0x100; step <= max; step += 0x100u) {
            for (unsigned i = 0; i < 12u; ++i) {
                const unsigned from = step - k_below[i / 3u];
                const unsigned to = step + k_above[i % 3u];
                kst_image_t a = sim_bench_image();
                kst_image_t b;

                CHECK(kst_field_set(&a, id, (uint16_t)from));
                b = a;
                CHECK(kst_field_set(&b, id, (uint16_t)to));
                pin_pair(&p, &a, &b);
                for (unsigned k = 0; k < 6u && id != KST_F_UNCONT_POS; ++k) {
                    const unsigned at = ((k < 3u ? from : to) + k % 3u - 1u)
                                        & 0xFFFu;
                    kst_image_t c = a;
                    kst_image_t d = b;

                    CHECK(kst_field_set(&c, KST_F_UNCONT_POS, (uint16_t)at));
                    CHECK(kst_field_set(&d, KST_F_UNCONT_POS, (uint16_t)at));
                    pin_pair(&p, &c, &d);
                }
            }
        }
    }
    CHECK_PIN(&p, UINT64_C(0x7E9895BEF1B730A2), 40380u, 19500u, 0u,
              30u, 20u, 20850u, 2u);
}

TEST_CASE(the_plans_to_every_duty_are_pinned)
{
    pin_t p;

    pin_begin(&p);
    for (unsigned s = 0; s < 2u; ++s) {
        kst_image_t start = sim_bench_image();

        /* The second start holds a copy that differs. */
        start.r[KST_REG_DUTY_COPY] = (uint8_t)(start.r[KST_REG_DUTY] - s);
        for (unsigned v = 0; v < 256u; ++v) {
            kst_image_t both = start;
            kst_image_t duty = start;
            kst_image_t copy = start;

            both.r[KST_REG_DUTY] = (uint8_t)v;
            both.r[KST_REG_DUTY_COPY] = (uint8_t)v;
            duty.r[KST_REG_DUTY] = (uint8_t)v;
            copy.r[KST_REG_DUTY_COPY] = (uint8_t)v;
            pin_pair(&p, &start, &both);
            pin_pair(&p, &start, &duty);
            pin_pair(&p, &start, &copy);
        }
    }
    CHECK_PIN(&p, UINT64_C(0x5514EDBA50FA4C78), 7680u, 4748u, 0u,
              0u, 0u, 2932u, 2u);
}

TEST_CASE(the_plans_about_register_1d_and_register_00_are_pinned)
{
    const kst_image_t bench = sim_bench_image();
    pin_t p;

    pin_begin(&p);
    for (unsigned v = 0; v < 256u; ++v) {
        kst_image_t img = bench;

        img.r[KST_REG_NODE_ADDR] = (uint8_t)v;
        pin_release(&p, &img);
        pin_pair(&p, &bench, &img);
        img = bench;
        img.r[0] = (uint8_t)v;
        pin_release(&p, &img);
        pin_pair(&p, &bench, &img);
    }
    CHECK_PIN(&p, UINT64_C(0xB0609F9CD21942A1), 3072u, 1032u, 0u,
              0u, 0u, 2040u, 1u);
}

/* An end point steps across a change of its high part with Uncontrolled
 * Pos at each of the 256 values beside it: an extra write where one
 * helps, no path where none does, a restore that is not checked. */
TEST_CASE(the_plans_with_an_extra_write_and_with_no_path_are_pinned)
{
    pin_t p;

    pin_begin(&p);
    for (unsigned lo = 0; lo < 256u; ++lo) {
        kst_image_t a = sim_bench_image();
        kst_image_t b;

        CHECK(kst_field_set(&a, KST_F_PULSE_UPPER, 0xBFF));
        CHECK(kst_field_set(&a, KST_F_UNCONT_POS, (uint16_t)(0xB00u + lo)));
        b = a;
        CHECK(kst_field_set(&b, KST_F_PULSE_UPPER, 0xC00));
        pin_pair(&p, &a, &b);
        /* With more registers of the group and one outside it. */
        CHECK(kst_field_set(&b, KST_F_NEUTRAL, 0xD0E));
        b.r[0x03] = 30;
        pin_pair(&p, &a, &b);

        a = sim_bench_image();
        CHECK(kst_field_set(&a, KST_F_PULSE_LOWER, 0x300));
        CHECK(kst_field_set(&a, KST_F_UNCONT_POS, (uint16_t)(0x300u + lo)));
        b = a;
        CHECK(kst_field_set(&b, KST_F_PULSE_LOWER, 0x2FF));
        pin_pair(&p, &a, &b);
        CHECK(kst_field_set(&b, KST_F_PULSE_UPPER, 0xA40));
        pin_pair(&p, &a, &b);
    }
    CHECK_PIN(&p, UINT64_C(0xF076F733837BBBF3), 5120u, 3230u, 1950u,
              1890u, 1260u, 0u, 5u);
}

/* --- seeded random pairs ------------------------------------------------------ */

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static unsigned rng_below(uint32_t *state, unsigned n)
{
    return (unsigned)(rng_next(state) % n);
}

/* 1 to 6 changes to @p from.  Most keep a field inside its limits and put
 * the position fields close to each other, which is where the order of
 * the writes matters; some set a field or a register to any value. */
static kst_image_t rng_image(uint32_t *state, const kst_image_t *from)
{
    kst_image_t img = *from;
    const unsigned changes = 1u + rng_below(state, 6);

    for (unsigned i = 0; i < changes; ++i) {
        const unsigned lower = kst_field_get(&img, KST_F_PULSE_LOWER);
        const unsigned upper = kst_field_get(&img, KST_F_PULSE_UPPER);
        const unsigned span = upper > lower ? upper - lower : 0u;
        const unsigned kind = rng_below(state, 10);
        kst_field_id_t id = KST_F_SPD_SEL;
        unsigned raw = rng_below(state, 2);

        if (kind == 0u) {
            id = KST_F_PULSE_LOWER;
            raw = KST_PULSE_RAW_MIN + rng_below(state, 900);
        } else if (kind == 1u) {
            id = KST_F_PULSE_UPPER;
            raw = KST_PULSE_RAW_MAX - rng_below(state, 900);
        } else if (kind == 2u) {
            id = KST_F_NEUTRAL;
            raw = KST_NEUTRAL_RAW_MIN
                  + rng_below(state,
                              KST_NEUTRAL_RAW_MAX - KST_NEUTRAL_RAW_MIN + 1u);
        } else if (kind == 3u) {
            id = KST_F_UNCONT_POS;
            raw = lower + rng_below(state, span + 1u);
        } else if (kind == 4u) {
            id = KST_F_UNCONT_POS;
            raw = rng_below(state, 2) != 0u ? lower + rng_below(state, 3)
                                            : upper - rng_below(state, 3);
        } else if (kind == 5u) {
            id = KST_F_SPD;
            raw = rng_below(state, KST_SPD_RAW_MAX + 1u);
        } else if (kind == 6u) {
            id = KST_F_DUTY;
            raw = KST_DUTY_RAW_MIN
                  + rng_below(state, KST_DUTY_RAW_MAX - KST_DUTY_RAW_MIN + 1u);
            img.r[KST_REG_DUTY_COPY] = (uint8_t)raw;
        } else if (kind == 7u) {
            id = (kst_field_id_t)(1u + rng_below(state, KST_F_COUNT - 1u));
            raw = rng_below(state, kst_field_raw_max(id) + 1u);
        } else if (kind == 8u) {
            img.r[1u + rng_below(state, KST_REG_MAX)] =
                (uint8_t)rng_below(state, 256);
            continue;
        }
        (void)kst_field_set(&img, id,
                            (uint16_t)(raw & kst_field_raw_max(id)));
    }
    return img;
}

TEST_CASE(the_plans_of_4000_seeded_random_pairs_are_pinned)
{
    const kst_image_t bench = sim_bench_image();
    uint32_t state = 0x4B535431u;
    pin_t p;

    pin_begin(&p);
    for (unsigned i = 0; i < 4000u; ++i) {
        const kst_image_t a = rng_image(&state, &bench);
        const kst_image_t b = rng_image(&state, &a);

        pin_pair(&p, &a, &b);
    }
    CHECK_PIN(&p, UINT64_C(0x1B3A7F08A858CBE4), 20000u, 14317u, 50u,
              144u, 194u, 5539u, 11u);
}

/* Any byte in registers 01 to 1F, 3 of 4 registers the same in both
 * images: the planner starts from and ends at images that break rules. */
TEST_CASE(the_plans_between_2000_seeded_random_images_are_pinned)
{
    uint32_t state = 0x52535432u;
    pin_t p;

    pin_begin(&p);
    for (unsigned i = 0; i < 2000u; ++i) {
        kst_image_t a = sim_bench_image();
        kst_image_t b = a;

        for (unsigned reg = 1; reg <= KST_REG_MAX; ++reg) {
            a.r[reg] = (uint8_t)rng_below(&state, 256);
            b.r[reg] = rng_below(&state, 4) != 0u
                           ? a.r[reg] : (uint8_t)rng_below(&state, 256);
        }
        pin_pair(&p, &a, &b);
    }
    CHECK_PIN(&p, UINT64_C(0xE0AC45EDA4BDBEDD), 10000u, 4000u, 0u,
              0u, 0u, 6000u, 16u);
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
    RUN(the_plan_to_every_value_of_every_field_is_pinned);
    RUN(the_plans_across_every_high_part_step_are_pinned);
    RUN(the_plans_to_every_duty_are_pinned);
    RUN(the_plans_about_register_1d_and_register_00_are_pinned);
    RUN(the_plans_with_an_extra_write_and_with_no_path_are_pinned);
    RUN(the_plans_of_4000_seeded_random_pairs_are_pinned);
    RUN(the_plans_between_2000_seeded_random_images_are_pinned);
    return test_summary("kst_plan");
}
