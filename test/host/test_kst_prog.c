/*
 * The servo programmer's state (shared/bench/kst_prog.c), end to end:
 * the programmer, the panel's link module, link dispatch, the programming
 * port, the session and the simulated servo of kst_sim.h.  The control
 * task of the panel is the function frame() here.  The rig is
 * kst_prog_fix.h.  The card operations (kst_prog_card.c) are in
 * test_kst_prog_card.c.
 *
 * Cases that need an answer the real chain gives only by accident (every
 * refusal, every result, a command nobody takes) feed the programmer by
 * hand.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_prog_fix.h"

/* --------------------------------------------------- the chain, end to end */

TEST_CASE(a_connect_enters_and_reads_the_servo)
{
    const kst_image_t sv = sim_bench_image();

    fresh_at(0u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_NONE);
    CHECK_EQ(kst_prog_message(&g_p)->id, KST_PROG_MSG_NONE);
    publish();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    publish();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_SYNC);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK(kst_prog_first_write(&g_p));

    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_ENTER);
    /* Nothing else is taken on the way. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK(kst_prog_editable(&g_p));
    CHECK(same(kst_prog_servo(&g_p), &sv));
    CHECK(same(kst_prog_draft(&g_p), &sv));
    CHECK_EQ(kst_prog_port(&g_p), CH);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);

    /* A second connect: the port is taken. */
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
}

TEST_CASE(a_coprocessor_without_the_page_is_named)
{
    fresh_at(0u);
    kst_link_came_up(&g_k, LINK_MINOR_KST - 1u);
    frames(50);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_NO_PAGE);
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_NO_PAGE);
    CHECK_EQ(kst_prog_read(&g_p),
             KST_PROG_MSG_START + KST_LINK_START_NO_PAGE);
    CHECK_EQ(g_starts, 0);
    frames(10);
    CHECK_EQ(g_starts, 0);

    kst_link_lost(&g_k);
    frames(2);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_DOWN);
    CHECK_EQ(kst_prog_leave(&g_p),
             KST_PROG_MSG_START + KST_LINK_START_DOWN);
}

TEST_CASE(the_port_says_why_a_port_is_not_taken)
{
    fresh_at(0u);
    link_up();
    /* The build has 1 port. */
    g_target = CH + 2u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH + 1u), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_PORT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    /* The port holds no pin. */
    g_target = CH + 1u;
    frame();
    g_no_pin = true;
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_PIN);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    /* No read follows an entry that was refused. */
    CHECK_EQ(g_starts, 2);
}

TEST_CASE(no_servo_on_the_port_is_a_message)
{
    fresh_at(0u);
    g_sim.powered = false;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_NO_SERVO);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK_EQ(g_starts, 1);
    /* What needs a servo read says so. */
    if (kst_prog_state(&g_p) == KST_PROG_PROGRAMMING) {
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        settle();
    }
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NEED_PORT);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_READ);
}

TEST_CASE(an_edit_inside_the_limits_changes_the_draft_only)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = kst_field_get(&sv, KST_F_BOOST);

    up();
    CHECK(boost > 1u);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, (uint16_t)(boost - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(draft(KST_F_BOOST), boost - 1u);
    CHECK(same(kst_prog_servo(&g_p), &sv));
    CHECK_EQ(kst_prog_changed(&g_p), KST_FIELD_BIT(KST_F_BOOST));
    CHECK_EQ(kst_prog_changes(&g_p), 1);
    CHECK_EQ(g_sim.n_writes, 0);

    /* Max. Duty takes its copy along and counts once. */
    edit(KST_F_DUTY, 200);
    CHECK_EQ(draft(KST_F_DUTY_COPY), 200);
    CHECK_EQ(kst_prog_changes(&g_p), 2);

    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(same(kst_prog_draft(&g_p), &sv));
}

TEST_CASE(a_value_beyond_a_hard_limit_is_not_taken)
{
    const kst_image_t sv = sim_bench_image();

    up();
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, KST_DUTY_RAW_MAX + 1u),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, KST_DUTY_RAW_MIN - 1u),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_LOW);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_PULSE_LOWER, KST_PULSE_RAW_MIN - 1u),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_LOWER_LOW);
    /* A value the field's bits do not hold. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_SOFT_START, 2),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_RULE_COUNT);
    CHECK(same(kst_prog_draft(&g_p), &sv));
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);

    /* Fields outside the editable class. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_20K_SEL, 1), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_GYRO, 1), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NODE_ADDR, 0), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY_COPY, 200),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK(same(kst_prog_draft(&g_p), &sv));
}

TEST_CASE(a_value_beyond_a_soft_limit_waits_for_the_confirmation)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    up();
    CHECK(duty + 2u <= KST_DUTY_RAW_MAX);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    CHECK((kst_prog_soft_rules(&g_p) & KST_RULE_BIT(KST_S_DUTY_RAISED))
          != 0u);
    CHECK_EQ(kst_field_get(kst_prog_soft_image(&g_p), KST_F_DUTY), duty + 1u);
    CHECK_EQ(draft(KST_F_DUTY), duty);
    kst_prog_soft_cancel(&g_p);
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);
    CHECK_EQ(draft(KST_F_DUTY), duty);
    kst_prog_soft_confirm(&g_p);     /* nothing waits: nothing happens */
    CHECK_EQ(draft(KST_F_DUTY), duty);

    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(draft(KST_F_DUTY), duty + 1u);
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);
    CHECK((kst_prog_rules(&g_p) & KST_RULE_BIT(KST_S_DUTY_RAISED)) != 0u);

    /* A rule confirmed for a value is asked again for one further from
     * the servo's. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 2u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(draft(KST_F_DUTY), duty + 2u);
    /* Back at the servo's value it is asked again on the next raise. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, duty), KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    /* Another edit drops the one that waits. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST,
                           (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);
    CHECK_EQ(draft(KST_F_DUTY), duty);
}

TEST_CASE(the_range_of_a_field_is_what_the_hard_rules_take)
{
    uint16_t lo = 0, hi = 0;

    fresh_at(0u);
    CHECK(!kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    up();
    CHECK(kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    CHECK_EQ(lo, KST_DUTY_RAW_MIN);
    CHECK_EQ(hi, KST_DUTY_RAW_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_BOOST, &lo, &hi));
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, KST_BOOST_RAW_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_STRETCH, &lo, &hi));
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, KST_STRETCH_INDEX_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_SOFT_START, &lo, &hi));
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 1);
    CHECK(kst_prog_range(&g_p, KST_F_UNCONT_TIME, &lo, &hi));
    CHECK_EQ(lo, KST_UNCONT_TIME_RAW_MIN);
    CHECK_EQ(hi, KST_UNCONT_TIME_RAW_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_NEUTRAL, &lo, &hi));
    CHECK(lo >= KST_NEUTRAL_RAW_MIN);
    CHECK(hi <= KST_NEUTRAL_RAW_MAX);
    CHECK(lo < hi);
    /* Every end is taken and the value beyond it is not. */
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t f = (kst_field_id_t)id;
        const kst_image_t before = *kst_prog_draft(&g_p);

        if (!kst_prog_range(&g_p, f, &lo, &hi)) {
            CHECK(kst_field(f)->access != KST_ACCESS_EDITABLE
                  || f == KST_F_DUTY_COPY);
            continue;
        }
        CHECK(kst_prog_edit(&g_p, f, lo) != KST_PROG_EDIT_REFUSED);
        kst_prog_soft_cancel(&g_p);
        CHECK(kst_prog_edit(&g_p, f, hi) != KST_PROG_EDIT_REFUSED);
        kst_prog_soft_cancel(&g_p);
        if (lo > 0u) {
            CHECK_EQ(kst_prog_edit(&g_p, f, (uint16_t)(lo - 1u)),
                     KST_PROG_EDIT_REFUSED);
        }
        if (hi < kst_field_raw_max(f)) {
            CHECK_EQ(kst_prog_edit(&g_p, f, (uint16_t)(hi + 1u)),
                     KST_PROG_EDIT_REFUSED);
        }
        g_p.draft = before;
    }
    CHECK(!kst_prog_range(&g_p, KST_F_LOCK, &lo, &hi));
    CHECK(!kst_prog_range(&g_p, KST_F_DUTY_COPY, &lo, &hi));
}

TEST_CASE(a_range_is_found_from_a_draft_value_the_rules_refuse)
{
    uint16_t lo = 0, hi = 0;

    up();
    /* A servo whose own Max. Duty lies above the limit: the rule it breaks
     * already does not narrow the range. */
    g_p.servo.r[KST_REG_DUTY] = 0xFFu;
    g_p.servo.r[KST_REG_DUTY_COPY] = 0xFFu;
    g_p.draft = g_p.servo;
    CHECK(kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    CHECK_EQ(lo, KST_DUTY_RAW_MIN);
    CHECK_EQ(hi, 255);
    /* The planner takes no write of it until the value is inside. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, 10), KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    kst_prog_enable(&g_p, false);
    /* A draft value outside the run the rules take. */
    g_p.servo = sim_bench_image();
    g_p.draft.r[KST_REG_DUTY] = 0xFFu;
    g_p.draft.r[KST_REG_DUTY_COPY] = 0xFFu;
    CHECK(kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    CHECK_EQ(lo, KST_DUTY_RAW_MIN);
    CHECK_EQ(hi, KST_DUTY_RAW_MAX);
}

TEST_CASE(a_step_moves_one_count_and_stops_at_the_fields_end)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = kst_field_get(&sv, KST_F_BOOST);

    up();
    CHECK_EQ(kst_prog_step(&g_p, KST_F_BOOST, -1), KST_PROG_EDIT_OK);
    CHECK_EQ(draft(KST_F_BOOST), boost - 1u);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_BOOST, 1), KST_PROG_EDIT_OK);
    CHECK_EQ(draft(KST_F_BOOST), boost);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_BOOST, 1), KST_PROG_EDIT_SOFT);
    kst_prog_soft_cancel(&g_p);

    edit(KST_F_SOFT_START, 1);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_SOFT_START, 1),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_RULE_COUNT);
    edit(KST_F_SOFT_START, 0);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_SOFT_START, -1),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(draft(KST_F_SOFT_START), 0);
}

TEST_CASE(a_write_needs_the_switch_and_writes_the_changed_fields)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 2u);
    kst_image_t now;

    up();
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NO_CHANGES);
    edit(KST_F_BOOST, boost);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_ENABLE);
    frames(20);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);

    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(kst_prog_first_write(&g_p));
    CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_WRITE);
    /* The servo's file first: nothing is in doubt before it is there. */
    CHECK(!kst_prog_in_doubt(&g_p));
    frame();
    CHECK(kst_prog_in_doubt(&g_p));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 2);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    now = servo();
    CHECK_EQ(kst_field_get(&now, KST_F_BOOST), boost);
    CHECK_EQ(now.r[KST_REG_DUTY], 200);
    CHECK_EQ(now.r[KST_REG_DUTY_COPY], 200);
    CHECK(same(kst_prog_servo(&g_p), &now));
    CHECK(same(kst_prog_draft(&g_p), &now));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_first_write(&g_p));
    /* The switch stays on for the servo that is connected. */
    CHECK(kst_prog_write_enabled(&g_p));
    CHECK_EQ(g_sim.n_bad_writes, 0);

    kst_prog_enable(&g_p, false);
    CHECK(!kst_prog_write_enabled(&g_p));
}

/* Up, the switch on, one value written. */
static void up_written(void)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(!kst_prog_first_write(&g_p));
}

TEST_CASE(the_first_write_of_every_session_is_the_first_again)
{
    /* The session is left and another entered. */
    up_written();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_first_write(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    /* And its first write ends that. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 210);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_first_write(&g_p));

    /* The link is lost between two operations. */
    up_written();
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));

    /* The IOmcu starts again between two operations. */
    up_written();
    port_fresh();
    frames(600);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
}

TEST_CASE(a_draft_the_planner_refuses_is_not_sent)
{
    up_enabled();
    /* Past the editor: a pairing value in the draft. */
    g_p.draft.r[KST_REG_NODE_ADDR] ^= 0x01u;
    CHECK(kst_prog_write(&g_p) != KST_PROG_MSG_NONE);
    g_p.draft = g_p.servo;
    g_p.draft.r[0] = 0x01u;
    CHECK(kst_prog_write(&g_p) != KST_PROG_MSG_NONE);
    g_p.draft = g_p.servo;
    g_p.draft.r[KST_REG_DUTY] = 0xFFu;
    g_p.draft.r[KST_REG_DUTY_COPY] = 0xFFu;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    frames(20);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(the_switch_goes_off_where_the_rules_say)
{
    up_enabled();
    kst_prog_screen_entry(&g_p);
    CHECK(!kst_prog_write_enabled(&g_p));

    /* STOP. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    g_stopped = true;
    frame();
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_STOPPED);
    CHECK(!kst_prog_write_enabled(&g_p));
    g_stopped = false;
    frame();

    /* A read that finds another image. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    g_sim.reg[0x03] ^= 0x01u;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_BACKUP_OTHER);
    CHECK(!kst_prog_write_enabled(&g_p));
    /* A read of the same servo leaves it. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_write_enabled(&g_p));

    /* The link. */
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true),
             KST_PROG_MSG_START + KST_LINK_START_DOWN);
}

TEST_CASE(leaving_gives_the_output_back_and_ends_the_session)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NEED_PORT);
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_PORT);

    /* And in again. */
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
}

TEST_CASE(a_verify_says_whether_the_servo_holds_what_was_read)
{
    up();
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);

    g_sim.reg[0x03] ^= 0x01u;
    g_sim.reg[0x10] ^= 0x01u;
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFY_DIFF);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 2);
    /* What is held is no longer the servo: read first. */
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    /* Two registers no write of the session went to: another servo. */
    CHECK_EQ(msg(), KST_PROG_MSG_BACKUP_OTHER);
    CHECK(!kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_servo_that_fails_the_fingerprint_is_read_only)
{
    fresh_at(0u);
    g_sim.reg[0x06] = 0x05u;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_READ_ONLY);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_READ_ONLY);
    /* Reading, keeping and comparing stay. */
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    frames(3);
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_release_of_pairing_writes_00_to_register_1d)
{
    up();
    CHECK(g_sim.reg[KST_REG_NODE_ADDR] != 0u);
    CHECK_EQ(kst_prog_release_check(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_release_check(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_EQ(g_sim.reg[KST_REG_NODE_ADDR], 0);
    CHECK_EQ(kst_prog_servo(&g_p)->r[KST_REG_NODE_ADDR], 0);
    CHECK(!kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NO_CHANGES);
}

/* ------------------------------------------------------------- the backups */

TEST_CASE(the_sessions_backup_is_restored_by_the_coprocessor)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    up_enabled();
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NO_RESTORE);
    edit(KST_F_DUTY, 200);
    edit(KST_F_SOFT_START, 1u - kst_field_get(&sv, KST_F_SOFT_START));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);

    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(plan.from, KST_PROG_FROM_SESSION);
    CHECK_EQ(plan.writes, 3);
    CHECK(!plan.unchecked);
    kst_prog_enable(&g_p, false);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_RESTORE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(same(kst_prog_servo(&g_p), &sv));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
}

TEST_CASE(the_sessions_backup_is_not_restored_to_another_servo)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    up_enabled();
    CHECK(!kst_prog_backup_other(&g_p));
    /* Servo B on the port: it differs in a field no write went to. */
    g_sim.reg[0x10] ^= 0x01u;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_BACKUP_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK(kst_prog_backup_other(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    /* With the switch on again the backup of A is still not B's. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_BACKUP_OTHER);
    memset(&plan, 0, sizeof(plan));
    plan.from = KST_PROG_FROM_SESSION;
    g_sim.n_writes = 0;
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_BACKUP_OTHER);
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
    now = servo();
    CHECK(!same(&now, &sv));
    /* An edit of B is B's own, and the backup stays A's after it. */
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(kst_prog_backup_other(&g_p));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    /* A new session reads its own backup: nothing differs, none is
     * offered, and after a write of it the restore is there. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_backup_other(&g_p));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 180);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
}

TEST_CASE(a_servo_put_back_has_its_backup_again)
{
    up_enabled();
    g_sim.reg[0x10] ^= 0x01u;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_backup_other(&g_p));
    g_sim.reg[0x10] ^= 0x01u;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_backup_other(&g_p));
}

/* A field a write of the session went to can hold a third value after a
 * write that was cut: that is the session's servo still. */
TEST_CASE(a_field_the_session_wrote_does_not_make_another_servo)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    g_sim.reg[KST_REG_DUTY] = 190;
    g_sim.reg[KST_REG_DUTY_COPY] = 190;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_backup_other(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
}

TEST_CASE(a_write_the_journal_stopped_leaves_no_field_written)
{
    /* The card takes the backup file and fails at the record. */
    up_enabled();
    g_card = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 2000u && g_fc.saves == 0; ++i) {
        frame();
    }
    CHECK_EQ(g_fc.saves, 1);
    g_card_fails = true;
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_EQ(g_p.wrote, 0);
    CHECK_EQ(g_p.held_wrote, 0);
    g_card_fails = false;
    duty_swapped_is_another_servo();

    /* The card never answers for the record: KST_PROG_STORE_MS. */
    up_enabled();
    g_card = true;
    g_card_silent = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_STORE_MS + 400u);
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_EQ(g_p.wrote, 0);
    g_card_silent = false;
    g_p.file_busy = false;
    duty_swapped_is_another_servo();
}

TEST_CASE(a_write_that_was_not_taken_or_was_refused_leaves_no_field_written)
{
    kst_prog_cmd_t cmd;

    /* The control task takes no command: the wait for it ends. */
    up_enabled();
    g_card = false;
    frame();
    g_deaf = true;
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(g_p.held_wrote != 0u);
    CHECK_EQ(g_p.wrote, 0);
    frames(KST_PROG_START_MS + 5u);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK_EQ(g_p.wrote, 0);
    CHECK_EQ(g_p.held_wrote, 0);
    g_deaf = false;
    duty_swapped_is_another_servo();

    /* The IOmcu refuses the start. */
    up_enabled();
    g_card = false;
    frame();
    g_deaf = true;
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_START);
    CHECK(g_p.wrote != 0u);
    g_answer = KST_LINK_START_BUSY;
    g_answer_id = cmd.id;
    frame();
    CHECK_EQ(msg(), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK_EQ(g_p.wrote, 0);
    g_deaf = false;
    g_answer = KST_LINK_START_OK;
    duty_swapped_is_another_servo();
}

/* A write whose command the control task took out of its queue counts from
 * then on, whatever comes of it: the answer that never arrives leaves its
 * fields written and the servo in doubt. */
TEST_CASE(a_write_that_was_taken_has_its_fields_written)
{
    kst_prog_cmd_t cmd;

    up_enabled();
    g_card = false;
    frame();
    g_deaf = true;
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(g_p.wrote, 0);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK(g_p.wrote != 0u);
    CHECK_EQ(g_p.held_wrote, 0);
    /* The control task has its pass and publishes no answer. */
    CHECK(kst_prog_gate_claim(&g_gate, cmd.id));
    frames(KST_PROG_START_MS + 5u);
    /* It cannot be taken back: the answer is waited for once more. */
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK(g_p.wrote != 0u);
    frames(KST_PROG_START_MS + 5u);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    /* An abort follows it. */
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(g_p.wrote != 0u);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_journal(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
    g_deaf = false;
    /* The duty holds a third value: the session's servo still. */
    g_sim.reg[KST_REG_DUTY] = 190;
    g_sim.reg[KST_REG_DUTY_COPY] = 190;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_backup_other(&g_p));

    /* The same with a card: backup file, record, then the command. */
    up_enabled();
    g_card = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(g_p.wrote != 0u);
    g_sim.reg[KST_REG_DUTY] = 190;
    g_sim.reg[KST_REG_DUTY_COPY] = 190;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_backup_other(&g_p));
}

/* The chain: STOP and ABORT between the start of a write and its command
 * frame drop the request, and the field it was to change still tells
 * another servo from the session's. */
TEST_CASE(a_write_stopped_or_aborted_before_its_command_leaves_no_field_written)
{
    kst_prog_cmd_t cmd;

    /* STOP: the control task aborts the module. */
    up_enabled();
    g_card = false;
    frame();
    g_deaf = true;
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK(kst_prog_gate_claim(&g_gate, cmd.id));
    g_answer = kst_link_start(&g_k, &cmd.req);
    g_answer_id = cmd.id;
    CHECK_EQ(g_answer, KST_LINK_START_OK);
    CHECK(g_p.wrote != 0u);
    kst_link_abort(&g_k);
    g_deaf = false;
    settle();
    CHECK_EQ(g_outcome.kind, KST_LINK_OUT_DROPPED);
    CHECK_EQ(g_p.wrote, 0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    duty_swapped_is_another_servo();

    /* ABORT: the model's command, taken before a frame of the write. */
    up_enabled();
    g_card = false;
    frame();
    g_deaf = true;
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK(kst_prog_gate_claim(&g_gate, cmd.id));
    g_answer = kst_link_start(&g_k, &cmd.req);
    g_answer_id = cmd.id;
    kst_prog_abort(&g_p);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK(g_p.wrote != 0u);
    kst_link_abort(&g_k);
    g_deaf = false;
    settle();
    CHECK_EQ(g_outcome.kind, KST_LINK_OUT_DROPPED);
    CHECK_EQ(g_p.wrote, 0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    duty_swapped_is_another_servo();
}

/* A write aborted after a register went keeps its fields. */
TEST_CASE(a_write_aborted_on_the_wire_has_its_fields_written)
{
    up_enabled();
    g_card = false;
    frame();
    edit(KST_F_DUTY, 200);
    edit(KST_F_BOOST, (uint16_t)(kst_field_get(kst_prog_servo(&g_p),
                                               KST_F_BOOST) - 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == 0u; ++i) {
        frame();
    }
    CHECK(g_sim.n_writes > 0u);
    kst_prog_abort(&g_p);
    settle();
    CHECK(g_p.wrote != 0u);
}

/* ------------------------------------------------------- the armed bench */

TEST_CASE(nothing_is_sent_on_a_bench_the_feed_reports_armed)
{
    kst_prog_restore_t plan;

    fresh_at(0u);
    link_up();
    g_armed = true;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NEED_DISARM);
    frames(20);
    CHECK_EQ(g_starts, 0);
    g_armed = false;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    /* Armed under a session: no operation that sends a frame starts. */
    g_armed = true;
    frame();
    g_starts = 0;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NEED_DISARM);
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_DISARM);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_DISARM);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_DISARM);
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NEED_DISARM);
    frames(20);
    CHECK_EQ(g_starts, 0);
    CHECK_EQ(g_sim.n_writes, 0);
    /* The set by the screen is the same refusal. */
    g_armed = false;
    kst_prog_bench_armed(&g_p, true);
    frame();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NEED_DISARM);
    kst_prog_bench_armed(&g_p, false);
    /* Leaving sends no frame and is taken. */
    g_armed = true;
    frame();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    g_armed = false;
}

TEST_CASE(the_bench_takes_no_arm_while_a_port_is_out_of_pwm)
{
    kst_link_readout_t rd;

    memset(&rd, 0, sizeof(rd));
    /* No IOmcu, or one with no page: nothing holds a port. */
    rd.phase = KST_LINK_DOWN;
    CHECK(!kst_prog_port_taken(&rd));
    rd.phase = KST_LINK_NO_PAGE;
    CHECK(!kst_prog_port_taken(&rd));
    /* A page not read yet is not known to be in PWM. */
    rd.phase = KST_LINK_SYNC;
    CHECK(kst_prog_port_taken(&rd));
    rd.phase = KST_LINK_READY;
    CHECK(!kst_prog_port_taken(&rd));
    rd.known = true;
    for (unsigned st = LINK_KST_PWM; st <= LINK_KST_LEAVING; ++st) {
        rd.port = (link_kst_state_t)st;
        CHECK_EQ(kst_prog_port_taken(&rd), st != LINK_KST_PWM);
    }
    /* An operation on its way, an ENTER among them, with the port still
     * read as PWM. */
    rd.port = LINK_KST_PWM;
    for (unsigned ph = KST_LINK_READY + 1u; ph <= KST_LINK_PLANNING; ++ph) {
        rd.phase = (kst_link_phase_t)ph;
        CHECK(kst_prog_port_taken(&rd));
    }
}

/* What the settings and the PROG page are held by: a session of this
 * model, whatever the readout says, or a readout that has the port taken,
 * whatever the model is in. */
TEST_CASE(the_port_is_held_by_the_session_or_by_the_readout)
{
    kst_prog_t p;
    kst_prog_feed_t f;

    kst_prog_init(&p);
    /* Nothing fed: nothing known to hold a port. */
    CHECK(!kst_prog_holds_port(&p));
    memset(&f, 0, sizeof(f));
    for (unsigned ph = KST_LINK_DOWN; ph <= KST_LINK_PLANNING; ++ph) {
        for (unsigned st = LINK_KST_PWM; st <= LINK_KST_LEAVING; ++st) {
            for (unsigned known = 0; known < 2u; ++known) {
                kst_prog_init(&p);
                f.rd.phase = (kst_link_phase_t)ph;
                f.rd.port  = (link_kst_state_t)st;
                f.rd.known = known != 0u;
                f.now_ms += 10u;
                kst_prog_feed(&p, &f);
                const kst_prog_state_t state = kst_prog_state(&p);
                const bool session = state == KST_PROG_ENTERING
                    || state == KST_PROG_PROGRAMMING
                    || state == KST_PROG_WORKING
                    || state == KST_PROG_LEAVING;
                CHECK_EQ(kst_prog_holds_port(&p),
                         session || kst_prog_port_taken(&f.rd));
            }
        }
    }
    /* A session through its states, by the rig: held from the command on,
     * with the readout still at PWM and no operation. */
    fresh_at(0u);
    link_up();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_holds_port(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    CHECK(!kst_prog_port_taken(&g_p.f.rd));
    CHECK(kst_prog_holds_port(&g_p));
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_holds_port(&g_p));
}

/* -------------------------------------------------------------- the journal */

TEST_CASE(the_journal_reaches_the_card_before_the_write_is_sent)
{
    char at_write[KST_PROG_FILE_MAX];

    up_enabled();
    g_card = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    /* The servo's file, then the journal, then the command. */
    frame();
    CHECK_EQ(g_fc.saves, 1);
    CHECK_EQ(g_file_writes, 0);
    frame();
    CHECK_EQ(g_file_writes, 1);
    CHECK_EQ(g_starts_at_file, 2);
    CHECK(strstr(g_file,
                 "journal=00F5F5144877046541A40D80530714009696000101"
                 "0000000000000A00100000\n") != NULL);
    CHECK(strstr(g_file,
                 "target=00C8C81448770465") != NULL);
    CHECK(strstr(g_file, "\nport=0\npin=0D\ncheck=") != NULL);
    /* The check is CRC-16/CCITT-FALSE of the lines before it; the value is
     * from an independent bitwise computation. */
    CHECK(strstr(g_file, "\nport=0\npin=0D\ncheck=AA87\n") != NULL);
    CHECK(strstr(g_file, "\n00=") == NULL);
    memcpy(at_write, g_file, sizeof(at_write));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    /* Verified: the journal leaves the file. */
    frames(2);
    CHECK_EQ(g_file_writes, 2);
    CHECK(strstr(g_file, "journal") == NULL);
    CHECK_STR_EQ(g_file, "RCBENCH KST 2\ncheck=2788\n");

    /* A panel that starts on the file of the write: the servo is in
     * doubt until it is read. */
    prog_fresh();
    CHECK(kst_prog_file_load(&g_p, at_write, strlen(at_write)));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_card_that_takes_no_journal_stops_the_write)
{
    up_enabled();
    g_card = true;
    g_card_fails = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 1);
    /* No write started: the next one is still the session's first. */
    CHECK(kst_prog_first_write(&g_p));

    /* A card that never answers: KST_PROG_STORE_MS. */
    g_card_fails = false;
    g_card_silent = true;
    g_p.file_busy = false;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_STORE_MS - 2u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    frames(3);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));
    /* Its late answer starts nothing. */
    kst_prog_file_done(&g_p, true);
    frames(5);
    CHECK_EQ(g_starts, 2);
    kst_prog_file_done(&g_p, true);   /* nothing on its way: ignored */
}

/* A write of the duty to 200 held at its journal: the servo's file is on
 * the card, the journal's text is taken and its answer held back.  Returns
 * the count of STARTs before the write. */
static unsigned write_at_its_journal(void)
{
    up_enabled();
    g_card = true;
    frame();
    memset(g_file, 0, sizeof(g_file));
    const unsigned starts = g_starts;

    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    g_card_silent = true;
    for (unsigned i = 0; i < 2000u && strstr(g_file, "journal=") == NULL;
         ++i) {
        frame();
    }
    CHECK(strstr(g_file, "journal=") != NULL);
    CHECK(kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(g_starts, starts);
    return starts;
}

/* The journal's answer after a refusal at the gate: nothing went, the
 * record is off the card, the values stay pending, and @p m is said. */
static void journal_refused(unsigned starts, unsigned m)
{
    g_card_silent = false;
    kst_prog_file_done(&g_p, true);
    CHECK_EQ(msg(), m);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(!kst_prog_journal(&g_p));
    settle();
    CHECK_EQ(msg(), m);
    CHECK_EQ(g_starts, starts);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_p.wrote, 0);
    CHECK_EQ(g_p.held_wrote, 0);
    CHECK(strstr(g_file, "journal") == NULL);
    CHECK_EQ(kst_prog_changes(&g_p), 1);
    CHECK(kst_prog_first_write(&g_p));
    /* A late second answer starts nothing either. */
    kst_prog_file_done(&g_p, true);
    frames(5);
    CHECK_EQ(g_starts, starts);
}

TEST_CASE(a_switch_turned_off_at_the_journal_stops_the_write)
{
    const unsigned starts = write_at_its_journal();

    /* Off at any time, the wait included; on is refused while it waits. */
    CHECK_EQ(kst_prog_enable(&g_p, false), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_enable(&g_p, true),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK(!kst_prog_write_enabled(&g_p));
    journal_refused(starts, KST_PROG_MSG_NEED_ENABLE);
    /* Switched on again, the same values go. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(g_starts > starts);
    CHECK_EQ(g_sim.reg[KST_REG_DUTY], 200);
}

TEST_CASE(arm_at_the_journal_stops_the_write)
{
    /* The feed's ARMED: the IOmcu's. */
    unsigned starts = write_at_its_journal();

    g_armed = true;
    frame();
    journal_refused(starts, KST_PROG_MSG_NEED_DISARM);

    /* The screen's ARMED. */
    starts = write_at_its_journal();
    kst_prog_bench_armed(&g_p, true);
    journal_refused(starts, KST_PROG_MSG_NEED_DISARM);
    kst_prog_bench_armed(&g_p, false);

    /* ARMED and SAFE again before the answer: the gate as it is at the
     * answer lets the write go. */
    starts = write_at_its_journal();
    g_armed = true;
    frame();
    g_armed = false;
    frame();
    g_card_silent = false;
    kst_prog_file_done(&g_p, true);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(g_starts > starts);
    CHECK_EQ(g_sim.reg[KST_REG_DUTY], 200);
}

TEST_CASE(a_switch_turned_off_at_the_servos_file_stops_the_write)
{
    up_enabled();
    g_card = true;
    frame();
    memset(g_file, 0, sizeof(g_file));
    const unsigned starts = g_starts;
    const unsigned saves = g_fc.saves;

    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_enable(&g_p, false), KST_PROG_MSG_NONE);
    settle();
    /* The servo's file is saved; its answer finds the switch off, and no
     * record of the write is made. */
    CHECK_EQ(g_fc.saves, saves + 1u);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(g_starts, starts);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(strstr(g_file, "journal") == NULL);
    CHECK_EQ(kst_prog_changes(&g_p), 1);
    CHECK(kst_prog_first_write(&g_p));
}

/* What changes, fed by hand, between the journal's text and its answer. */
enum {
    AT_NOTHING = 0,
    AT_SWITCH_OFF,
    AT_PORT_PWM,
    AT_PORT_OTHER,
    AT_PORT_BUSY,
    AT_PAGE_SYNC,
    AT_FEED_ARMED,
    AT_SCREEN_ARMED,
    AT_STOP,
    AT_COUNT,
};

TEST_CASE(the_gate_at_the_journals_answer_is_the_gate_as_it_is_then)
{
    static const unsigned said[AT_COUNT] = {
        [AT_NOTHING] = KST_PROG_MSG_NONE,
        [AT_SWITCH_OFF] = KST_PROG_MSG_NEED_ENABLE,
        [AT_PORT_PWM] = KST_PROG_MSG_NEED_PORT,
        [AT_PORT_OTHER] = KST_PROG_MSG_NEED_PORT,
        [AT_PORT_BUSY] = KST_PROG_MSG_START + KST_LINK_START_BUSY,
        [AT_PAGE_SYNC] = KST_PROG_MSG_START + KST_LINK_START_BUSY,
        [AT_FEED_ARMED] = KST_PROG_MSG_NEED_DISARM,
        [AT_SCREEN_ARMED] = KST_PROG_MSG_NEED_DISARM,
        [AT_STOP] = KST_PROG_MSG_STOPPED,
    };
    char text[KST_PROG_FILE_MAX];
    kst_prog_cmd_t cmd;

    for (unsigned at = AT_NOTHING; at < AT_COUNT; ++at) {
        by_hand();
        g_f.card = true;
        pass(1);
        while (kst_prog_file_take(&g_p, text, sizeof(text)) != 0u) {
            kst_prog_file_done(&g_p, true);
        }
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
        CHECK(strstr(text, "journal=") != NULL);
        CHECK(!kst_prog_take(&g_p, &cmd));
        switch (at) {
        case AT_SWITCH_OFF:
            (void)kst_prog_enable(&g_p, false);
            break;
        case AT_PORT_PWM:
            g_f.rd.port = LINK_KST_PWM;
            break;
        case AT_PORT_OTHER:
            g_f.rd.port_no = CH + 1u;
            break;
        case AT_PORT_BUSY:
            g_f.rd.flags |= LINK_KS_F_BUSY;
            break;
        case AT_PAGE_SYNC:
            g_f.rd.phase = KST_LINK_SYNC;
            break;
        case AT_FEED_ARMED:
            g_f.armed = true;
            break;
        case AT_SCREEN_ARMED:
            kst_prog_bench_armed(&g_p, true);
            break;
        case AT_STOP:
            g_f.stopped = true;
            break;
        default:
            break;
        }
        pass(1);
        kst_prog_file_done(&g_p, true);
        if (at == AT_NOTHING) {
            /* The write goes, the switch it was held with on. */
            CHECK(kst_prog_take(&g_p, &cmd));
            CHECK_EQ(cmd.kind, KST_PROG_CMD_START);
            CHECK_EQ(cmd.req.op, LINK_KST_OP_WRITE);
            CHECK(cmd.req.write_enabled);
            CHECK(kst_prog_journal(&g_p));
            continue;
        }
        /* No START, the record cleared and written so, the reason said. */
        CHECK(!kst_prog_take(&g_p, &cmd));
        CHECK_EQ(msg(), said[at]);
        CHECK(!kst_prog_journal(&g_p));
        CHECK_EQ(g_p.held_wrote, 0);
        CHECK(!kst_prog_writing(&g_p));
        CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
        CHECK(strstr(text, "journal") == NULL);
        kst_prog_bench_armed(&g_p, false);
    }
}

/* A write of two fields cut by a lost link after its first register;
 * with @p duty the cut leaves registers 01 and 02 apart. */
static void cut_write_of(bool duty)
{
    up_enabled();
    if (duty) {
        edit(KST_F_DUTY, 200);
    } else {
        edit(KST_F_SOFT_START,
             1u - kst_field_get(kst_prog_servo(&g_p), KST_F_SOFT_START));
    }
    edit(KST_F_BOOST, 10);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == 0u; ++i) {
        frame();
    }
    CHECK(g_sim.n_writes > 0u);
    kst_link_lost(&g_k);
    g_safe = false;
    frame();
}

static void cut_write(void)
{
    cut_write_of(true);
}

TEST_CASE(a_link_lost_in_a_write_leaves_the_servo_in_doubt)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    cut_write();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(!kst_prog_have_servo(&g_p));

    /* The IOmcu stopped the write when the link went silent, and
     * its session takes the restore only. */
    frames(200);
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    now = servo();
    CHECK(!same(&now, &sv));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    /* The read compares the servo with the image before the write. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    /* A second read says that the session is locked. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_LOCKED);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_NEED_READ);

    /* The session's own backup goes back. */
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.unchecked);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(g_sim.n_bad_writes, 0);
}

/* The cut write, then a new session on the servo as the cut left it. */
static void cut_and_come_back(bool duty)
{
    cut_write_of(duty);
    frames(200);
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK(!kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_later_session_cannot_write_to_a_servo_cut_between_01_and_02)
{
    kst_prog_restore_t plan;

    cut_and_come_back(true);
    CHECK(g_sim.reg[KST_REG_DUTY] != g_sim.reg[KST_REG_DUTY_COPY]);
    /* The fingerprint fails, and the session takes no edit. */
    CHECK(!kst_prog_editable(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_READ_ONLY);
}

TEST_CASE(a_panel_that_starts_on_a_locked_session_fetches_its_backup)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    cut_write();
    frames(200);
    /* The panel starts again: its link module and its programmer hold
     * nothing, the journal comes from the card. */
    {
        char text[KST_PROG_FILE_MAX];

        CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
        kst_link_init(&g_k);
        prog_fresh();
        g_outcomes = 0;
        g_answer_id = 0;
        CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    }
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    frames(600);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(g_fetches, 1);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    /* The port asks for a read before anything else. */
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.unchecked);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
}

TEST_CASE(a_later_session_writes_the_journals_image_back)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    cut_and_come_back(false);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    /* A read after it says no more than that it read. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.writes >= 1u);
    CHECK_EQ(plan.stay, 0);
    /* A plan is a count: the pending values take nothing of the record
     * until the restore starts, and a restore that is refused leaves
     * them alone too. */
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK_EQ(g_sim.n_bad_writes, 0);
}

TEST_CASE(a_second_cut_keeps_the_image_before_the_first_write)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;

    cut_and_come_back(false);
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    frames(3);          /* the servo's file, the journal, the command */
    kst_link_lost(&g_k);
    frame();
    CHECK(kst_prog_journal(&g_p));
    CHECK(same(&g_p.journal_img, &sv));
}

/*
 * A servo that holds the record's image or its target may be the one of
 * the interrupted write, or a second servo with the same settings.  The
 * record stands, the question is asked with the record's port, and nothing
 * is written until the operator discards the record.
 */
TEST_CASE(a_journal_whose_servo_is_unchanged_or_complete_asks)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t target;
    char text[KST_PROG_FILE_MAX];

    /* Unchanged: the write never reached the servo. */
    up();
    g_card = true;
    frame();
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH;
    g_p.journal_img = sv;
    g_p.journal_target = sv;
    g_p.journal_target.r[0x03] ^= 0x01u;
    CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
    prog_fresh();
    /* A text cut short is not whole, and asking changes nothing. */
    CHECK(kst_prog_file_whole(text, strlen(text)));
    CHECK(!kst_prog_file_whole(text, strlen(text) - 1u));
    CHECK(!kst_prog_file_whole(text, 0u));
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    CHECK(!g_p.journal);
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
    CHECK_EQ(kst_prog_journal_port(&g_p), CH);
    frame();
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_SAME);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
    CHECK_EQ(kst_prog_journal_asks(&g_p), KST_PROG_MSG_JOURNAL_SAME);
    CHECK(kst_prog_journal_held(&g_p));
    CHECK(!kst_prog_journal_other(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    /* The record stays on the card. */
    frames(2);
    CHECK(strstr(g_file, "\nport=0\n") != NULL);
    /* No write, no restore and no file reaches the servo. */
    {
        kst_prog_restore_t plan;
        const unsigned writes = g_sim.n_writes;

        CHECK(!kst_prog_write_enabled(&g_p));
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_JOURNAL_SAME);
        CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
        CHECK(!kst_prog_write_enabled(&g_p));
        CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
        CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
        CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
                 KST_PROG_MSG_JOURNAL_SAME);
        edit(KST_F_BOOST, (uint16_t)(draft(KST_F_BOOST) + 1u));
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_JOURNAL_SAME);
        settle();
        CHECK_EQ(g_sim.n_writes, writes);
        kst_prog_discard(&g_p);
        /* A read asks again. */
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_SAME);
        CHECK(kst_prog_journal_held(&g_p));
    }
    /* Discarded by the operator: the servo is written again. */
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(!kst_prog_journal_held(&g_p));
    CHECK_EQ(kst_prog_journal_asks(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    frames(2);
    CHECK_STR_EQ(g_file, "RCBENCH KST 2\ncheck=2788\n");

    /* Complete: every register of the target is on the servo. */
    target = sv;
    target.r[0x03] ^= 0x01u;
    memcpy(g_sim.reg, target.r, sizeof(target.r));
    prog_fresh();
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    frame();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DONE);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
    CHECK_EQ(kst_prog_journal_asks(&g_p), KST_PROG_MSG_JOURNAL_DONE);
    CHECK(kst_prog_journal_held(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_JOURNAL_DONE);

    /* Forgotten by the operator. */
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(!kst_prog_journal_held(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
}

/* The other value of 20k_sel, a field no edit and no restore writes. */
static void flip_20k(void)
{
    kst_image_t img = servo();

    CHECK(kst_field_set(&img, KST_F_20K_SEL,
                        1u - kst_field_get(&img, KST_F_20K_SEL)));
    memcpy(g_sim.reg, img.r, sizeof(img.r));
}

TEST_CASE(a_servo_that_is_not_the_journals_takes_no_restore_and_no_write)
{
    kst_prog_restore_t plan;
    kst_image_t before;

    /* The servo of the interrupted write, read and compared. */
    cut_and_come_back(false);
    CHECK(!kst_prog_journal_other(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    /* Another servo on the port: a locked value is not the same. */
    flip_20k();
    before = servo();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, (CH + 1u) | ((PIN + 1u) << 8));
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(kst_prog_journal_other(&g_p));
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK(kst_prog_have_servo(&g_p));
    for (unsigned from = 0; from < (unsigned)KST_PROG_FROM_COUNT; ++from) {
        CHECK(!kst_prog_restore_offered(&g_p, (kst_prog_from_t)from));
        CHECK_EQ(kst_prog_restore_plan(&g_p, (kst_prog_from_t)from, &plan),
                 KST_PROG_MSG_JOURNAL_OTHER);
        plan.from = (uint8_t)from;
        CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_JOURNAL_OTHER);
    }
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, (CH + 1u) | ((PIN + 1u) << 8));
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
    frames(50);
    {
        const kst_image_t now = servo();

        CHECK(same(&now, &before));
    }

    /* The journal's servo again: compared, and the restore is back. */
    flip_20k();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK(!kst_prog_journal_other(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    /* The other servo, and the record is discarded for it. */
    flip_20k();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_journal_other(&g_p));
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(!kst_prog_journal_other(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_editable(&g_p));
}

TEST_CASE(a_journal_of_another_port_is_not_this_servos)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    char text[KST_PROG_FILE_MAX];

    /* The journal of a write on the next port, the servo here equal
     * to its image in every register. */
    up();
    g_card = true;
    frame();
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH + 1u;
    g_p.journal_img = sv;
    g_p.journal_target = sv;
    g_p.journal_target.r[0x03] ^= 0x01u;
    CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
    prog_fresh();
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 2u);
    frame();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 2u);
    /* The record stands for the servo it names. */
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_journal_other(&g_p));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_JOURNAL_OTHER);

    /* The session ends: which servo comes next is not known. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_journal(&g_p));
    CHECK(!kst_prog_journal_other(&g_p));
}

TEST_CASE(a_restore_names_the_soft_limits_its_values_pass)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    kst_prog_restore_t plan;

    /* Max. Duty lowered by a write: the session's backup raises it. */
    up_enabled();
    edit(KST_F_DUTY, (uint16_t)(duty - 2u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(plan.soft, KST_RULE_BIT(KST_S_DUTY_RAISED));

    /* Raised by a write: the session's backup lowers it, nothing to warn
     * of. */
    up_enabled();
    edit(KST_F_DUTY, (uint16_t)(duty + 2u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(plan.soft, 0);
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_COUNT, &plan),
             KST_PROG_MSG_NO_RESTORE);
}

TEST_CASE(a_coprocessor_that_starts_again_in_a_write_leaves_the_journal)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    edit(KST_F_BOOST, 10);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == 0u; ++i) {
        frame();
    }
    port_fresh();
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_journal(&g_p));
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));

    /* Between operations a restart ends the session, and the model says
     * that the port did it. */
    up_enabled();
    port_fresh();
    frames(600);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_SESSION_ENDED);
}

TEST_CASE(a_write_the_servo_does_not_take_names_the_register)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t now;

    up_enabled();
    edit(KST_F_BOOST, 10);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 100, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_WRITE_FAILED);
    CHECK_EQ(kst_prog_message(&g_p)->reg, 0x03);
    now = servo();
    CHECK(same(&now, &sv));
    /* Nothing was written: no journal stands. */
    CHECK(!kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 1);
}

TEST_CASE(a_torn_servo_takes_the_sessions_restore_only)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    up_enabled();
    edit(KST_F_DUTY, 200);
    /* Register 01 takes the value, 02 takes nothing, and the roll-back of
     * 01 lands nowhere either. */
    sim_add_fault(&g_sim, SIM_WRITE, 0x02, 0, 100, SIM_F_WRITE_IGNORE, 0);
    sim_add_fault(&g_sim, SIM_WRITE, 0x01, 1, 100, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    now = servo();
    if (msg() != KST_PROG_MSG_SES + KST_SES_ERR_TORN) {
        /* The planner wrote 02 first: bend the other way round. */
        up_enabled();
        edit(KST_F_DUTY, 200);
        sim_add_fault(&g_sim, SIM_WRITE, 0x01, 0, 100, SIM_F_WRITE_IGNORE,
                      0);
        sim_add_fault(&g_sim, SIM_WRITE, 0x02, 1, 100, SIM_F_WRITE_IGNORE,
                      0);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
        now = servo();
    }
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_TORN);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(!same(&now, &sv));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_READ);

    /* The session's backup is offered, and its result is the operator's
     * to confirm. */
    g_sim.n_faults = 0;
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.unchecked);
    CHECK_EQ(plan.writes, 0xFF);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
    CHECK(kst_prog_editable(&g_p));
}

/* ------------------------------------------------------------ stop and abort */

TEST_CASE(a_stop_aborts_the_operation_and_asks_for_a_read)
{
    up_enabled();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    frames(60);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    /* The panel's STOP: the control task aborts and its heartbeat ends. */
    g_stopped = true;
    kst_link_abort(&g_k);
    g_safe = false;
    g_stop = true;
    frame();
    g_stop = false;
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_ABORTED);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_STOPPED);

    /* Leaving stays possible: no PD mini answers, and the port that asks
     * for a read is left without the wait. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT_DOUBT);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_STOPPED);

    /* STOP released. */
    g_stopped = false;
    g_safe = true;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_stop_before_the_command_is_taken_sends_nothing)
{
    up_enabled();
    g_card = true;
    g_card_silent = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frame();
    g_stopped = true;
    frame();
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(!kst_prog_journal(&g_p));
    frames(20);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));
}

TEST_CASE(the_operators_abort_ends_an_operation_that_runs)
{
    up();
    kst_prog_abort(&g_p);            /* nothing runs: nothing happens */
    frames(5);
    CHECK_EQ(g_aborts, 0);

    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    frames(60);
    kst_prog_abort(&g_p);
    settle();
    CHECK_EQ(g_aborts, 1);
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_ABORTED);
    /* The port asks for a read before anything else. */
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_in_doubt(&g_p));

    /* Before the control task took the command: dropped here. */
    g_deaf = true;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    kst_prog_abort(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_DROPPED);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    g_deaf = false;
    frames(5);
    CHECK_EQ(g_starts, 4);
    CHECK_EQ(g_aborts, 1);
}

/* A write whose command was taken and that the IOmcu ends with no
 * register written leaves no field written; one that reached the servo, or
 * may have, keeps them. */
TEST_CASE(a_taken_write_that_wrote_nothing_leaves_no_field_written)
{
    static const struct {
        kst_link_outcome_kind_t kind;
        uint8_t result;
        uint8_t steps;
        bool taken;
        bool nothing;
    } k[] = {
        { KST_LINK_OUT_DROPPED, 0, 0, false, true },
        { KST_LINK_OUT_REFUSED, 0, 0, false, true },
        { KST_LINK_OUT_NACK, 0, 0, false, true },
        { KST_LINK_OUT_NACK, 0, 0, true, false },
        { KST_LINK_OUT_NO_ANSWER, 0, 0, false, false },
        { KST_LINK_OUT_RESTARTED, 0, 0, false, false },
        { KST_LINK_OUT_DONE, KST_SES_ERR_BUSY, 0, true, true },
        { KST_LINK_OUT_DONE, KST_SES_ERR_FINGERPRINT, 0, true, true },
        { KST_LINK_OUT_DONE, KST_SES_ERR_WRITE_FAILED, 0, true, true },
        { KST_LINK_OUT_DONE, KST_SES_ERR_WRITE_FAILED, 1, true, false },
        { KST_LINK_OUT_DONE, KST_SES_ERR_ABORTED, 0, true, false },
        { KST_LINK_OUT_DONE, KST_SES_ERR_ABORTED, 1, true, false },
        { KST_LINK_OUT_DONE, KST_SES_OK, 1, true, false },
    };

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        kst_link_outcome_t o;

        by_hand();
        g_p.wrote_known = true;
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        CHECK_EQ(g_p.wrote, 0);
        answer(KST_LINK_START_OK);
        CHECK(g_p.wrote != 0u);
        memset(&o, 0, sizeof(o));
        o.kind = k[i].kind;
        o.op = LINK_KST_OP_WRITE;
        o.result = k[i].result;
        o.steps_done = k[i].steps;
        o.taken = k[i].taken;
        o.fail_reg = 0xFF;
        end(&o);
        if (k[i].kind == KST_LINK_OUT_RESTARTED) {
            /* The session is gone, and what it wrote with it. */
            CHECK(!g_p.wrote_known);
        } else {
            CHECK(g_p.wrote_known);
            CHECK_EQ(g_p.wrote == 0u, k[i].nothing);
        }
    }
}

/* The fields of an earlier write stand when a later one writes nothing,
 * and an operation that is no write moves nothing. */
TEST_CASE(a_write_that_wrote_nothing_takes_no_field_of_an_earlier_write)
{
    kst_link_outcome_t o;
    kst_field_set_t first;

    by_hand();
    g_p.wrote_known = true;
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_WRITE;
    o.result = KST_SES_OK;
    o.steps_done = 1;
    o.taken = true;
    end(&o);
    first = g_p.wrote;
    CHECK(first != 0u);

    /* A read that is refused at its start. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_BUSY);
    CHECK_EQ(g_p.wrote, first);
    /* A read that is dropped. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DROPPED;
    o.op = LINK_KST_OP_READ_ALL;
    end(&o);
    CHECK_EQ(g_p.wrote, first);

    /* A second write, of another field, dropped before its command. */
    g_p.servo = g_p.draft;
    g_f.image = g_p.draft;
    pass(1);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_BOOST, (uint16_t)(kst_field_get(&g_p.servo, KST_F_BOOST) - 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    CHECK(g_p.wrote != first);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DROPPED;
    o.op = LINK_KST_OP_WRITE;
    end(&o);
    CHECK_EQ(g_p.wrote, first);
}

/* The answer to a taken write never comes and the wait for it ends: the
 * fields count as written until the outcome says the request was dropped. */
TEST_CASE(a_dropped_write_whose_answer_timed_out_leaves_no_field_written)
{
    kst_link_outcome_t o;
    kst_prog_cmd_t cmd;

    by_hand();
    g_p.wrote_known = true;
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    /* No gate: a command handed out may have run. */
    pass(KST_PROG_START_MS);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(g_p.wrote != 0u);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DROPPED;
    o.op = LINK_KST_OP_WRITE;
    end(&o);
    CHECK_EQ(g_p.wrote, 0);
}

TEST_CASE(a_fetch_that_brings_nothing_is_asked_again_once_a_second)
{
    kst_prog_cmd_t cmd;

    by_hand();
    g_f.have_backup = false;
    pass(1);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_FETCH);
    pass(KST_PROG_FETCH_MS - 1u);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_FETCH);
    /* An operation goes first, and the fetch is dropped for it. */
    pass(KST_PROG_FETCH_MS);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_START);
    CHECK(!kst_prog_take(&g_p, &cmd));
    /* Across the wrap of the tick. */
    by_hand();
    g_f.have_backup = false;
    g_f.now_ms = 0xFFFFFF00u;
    pass(0);
    CHECK(kst_prog_take(&g_p, &cmd));
    pass(KST_PROG_FETCH_MS - 1u);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK(kst_prog_take(&g_p, &cmd));
    /* Nothing to fetch with the images held. */
    g_f.have_backup = true;
    pass(KST_PROG_FETCH_MS);
    CHECK(!kst_prog_take(&g_p, &cmd));
}

TEST_CASE(every_answer_of_start_is_a_message)
{
    for (unsigned a = KST_LINK_START_DOWN; a <= KST_LINK_START_NO_OP;
         ++a) {
        by_hand();
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer((kst_link_start_t)a);
        CHECK_EQ(msg(), KST_PROG_MSG_START + a);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        CHECK(msg() < KST_PROG_MSG_OUT);
    }
    /* A write that was not started leaves no journal. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_first_write(&g_p));
    answer(KST_LINK_START_BUSY);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    /* Nor is it the session's first write; one that started is, whatever
     * comes of it. */
    CHECK(kst_prog_first_write(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_first_write(&g_p));
    answer(KST_LINK_START_OK);
    CHECK(!kst_prog_first_write(&g_p));
    /* A command nobody answers within KST_PROG_START_MS is none. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    pass(KST_PROG_START_MS);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_first_write(&g_p));
}

TEST_CASE(every_refusal_of_the_port_is_a_message)
{
    for (unsigned r = LINK_KST_REF_BUSY; r <= LINK_KST_REF_LOCKED;
         ++r) {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_REFUSED;
        o.op = LINK_KST_OP_WRITE;
        o.refusal = (link_kst_refusal_t)r;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_REF + r);
        CHECK(msg() < KST_PROG_MSG_SES);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        /* Refused: nothing was written. */
        CHECK(!kst_prog_journal(&g_p));
        CHECK_EQ(kst_prog_changes(&g_p), 1);
    }
}

TEST_CASE(every_result_of_the_session_is_a_message)
{
    for (unsigned r = KST_SES_ERR_BUSY; r <= KST_SES_ERR_ABORTED; ++r) {
        kst_link_outcome_t o;
        const bool danger = r == KST_SES_ERR_LOCKED || r == KST_SES_ERR_TORN
                         || r == KST_SES_ERR_UNINTENDED
                         || r == KST_SES_ERR_VERIFY;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_WRITE;
        o.result = (kst_ses_t)r;
        o.fail_reg = 0x02;
        o.steps_done = 1;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + r);
        CHECK(msg() < KST_PROG_MSG_END);
        CHECK_EQ(kst_prog_message(&g_p)->reg, 0x02);
        CHECK_EQ(kst_prog_message(&g_p)->arg, 1);
        CHECK_EQ(kst_prog_message(&g_p)->level,
                 danger ? KST_PROG_DANGER : KST_PROG_WARN);
        /* A register was written: the servo is read before anything
         * else. */
        CHECK(kst_prog_in_doubt(&g_p));
        CHECK(kst_prog_journal(&g_p));
    }
    /* With no register written, the results that write nothing leave no
     * journal. */
    for (unsigned r = KST_SES_ERR_BUSY; r <= KST_SES_ERR_NO_PLAN; ++r) {
        kst_link_outcome_t o;
        const bool nothing = r == KST_SES_ERR_BUSY || r == KST_SES_ERR_ARG
            || r == KST_SES_ERR_NOT_IN_MODE || r == KST_SES_ERR_NO_BACKUP
            || r == KST_SES_ERR_FINGERPRINT || r == KST_SES_ERR_LOCKED
            || r == KST_SES_ERR_BAD_PLAN || r == KST_SES_ERR_UNCONFIRMED
            || r == KST_SES_ERR_CHANGED || r == KST_SES_ERR_WRITE_FAILED
            || r == KST_SES_ERR_ROLLED_BACK || r == KST_SES_ERR_NO_PLAN;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_WRITE;
        o.result = (kst_ses_t)r;
        o.fail_reg = 0xFF;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + r);
        CHECK_EQ(kst_prog_journal(&g_p), !nothing);
        /* The servo is not what was read: read again. */
        CHECK_EQ(kst_prog_in_doubt(&g_p),
                 !nothing || r == KST_SES_ERR_CHANGED);
    }
}

TEST_CASE(every_kind_of_outcome_is_a_message)
{
    static const kst_link_outcome_kind_t kinds[] = {
        KST_LINK_OUT_NACK, KST_LINK_OUT_NO_ANSWER, KST_LINK_OUT_RESTARTED,
        KST_LINK_OUT_DROPPED,
    };

    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = kinds[i];
        o.op = LINK_KST_OP_READ_ALL;
        o.nack = 5;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_OUT + (unsigned)kinds[i]);
        CHECK(msg() < KST_PROG_MSG_REF);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_message(&g_p)->arg,
                 kinds[i] == KST_LINK_OUT_NACK ? 5 : 0);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        /* A restart ends the session. */
        CHECK_EQ(kst_prog_have_servo(&g_p),
                 kinds[i] != KST_LINK_OUT_RESTARTED);
    }
    /* In a write: a frame refused or a request dropped wrote nothing; no
     * answer and a restart leave the journal. */
    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        kst_link_outcome_t o;
        const bool open = kinds[i] == KST_LINK_OUT_NO_ANSWER
                       || kinds[i] == KST_LINK_OUT_RESTARTED;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = kinds[i];
        o.op = LINK_KST_OP_WRITE;
        end(&o);
        CHECK_EQ(kst_prog_journal(&g_p), open);
        CHECK_EQ(kst_prog_in_doubt(&g_p), open);
        CHECK_EQ(msg(), open ? (unsigned)KST_PROG_MSG_JOURNAL
                             : KST_PROG_MSG_OUT + (unsigned)kinds[i]);
    }
    /* A frame refused after the command was acknowledged: the write may
     * have run.  The record stays and a read comes first. */
    {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_NACK;
        o.op = LINK_KST_OP_WRITE;
        o.nack = 5;
        o.taken = true;
        end(&o);
        CHECK(kst_prog_journal(&g_p));
        CHECK(kst_prog_in_doubt(&g_p));
        CHECK(!kst_prog_editable(&g_p));
        CHECK_EQ(msg(), KST_PROG_MSG_OUT + (unsigned)KST_LINK_OUT_NACK);
        CHECK_EQ(kst_prog_message(&g_p)->arg, 5);
    }
    /* A read that says it succeeded and brought no image. */
    {
        kst_link_outcome_t o;

        by_hand();
        g_f.have_image = false;
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_READ_ALL;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_READ);
        /* And a write that did: read before anything else. */
        by_hand();
        g_f.have_image = false;
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        o.op = LINK_KST_OP_WRITE;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK(kst_prog_in_doubt(&g_p));
    }
    /* A read that fails leaves what was read before in doubt. */
    {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_READ_ALL;
        o.result = KST_SES_ERR_READ;
        o.fail_reg = 0x07;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_READ);
        CHECK_EQ(kst_prog_message(&g_p)->reg, 0x07);
        CHECK(kst_prog_in_doubt(&g_p));
    }
    /* An outcome nobody waits for is not this programmer's. */
    {
        kst_link_outcome_t o;

        by_hand();
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_READ_ALL;
        o.result = KST_SES_ERR_READ;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    }
}

static void answer_never_comes(uint32_t t0)
{
    by_hand();
    g_f.now_ms = t0;
    pass(0);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    pass(KST_PROG_START_MS - 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    /* The command is withdrawn. */
    {
        kst_prog_cmd_t cmd;

        CHECK(!kst_prog_take(&g_p, &cmd));
    }
}

static void outcome_never_comes(uint32_t t0)
{
    kst_prog_cmd_t cmd;

    by_hand();
    g_f.now_ms = t0;
    pass(0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    pass(KST_PROG_RUN_MS - 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
}

/* A read or a verify with no outcome: what was read is not trusted. */
static void read_never_ends(uint32_t t0, bool verify)
{
    kst_prog_cmd_t cmd;
    kst_link_outcome_t o;

    by_hand();
    g_f.now_ms = t0;
    pass(0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(kst_prog_editable(&g_p));
    CHECK_EQ(verify ? kst_prog_verify(&g_p) : kst_prog_read(&g_p),
             KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    pass(KST_PROG_RUN_MS - 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_READ);

    /* A read that ends is what clears it. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    o.fail_reg = 0xFF;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_editable(&g_p));
}

/* A read or a verify the link gave up on may have run on the IOmcu:
 * the image here is not the one there. */
static void read_gets_no_answer(bool verify)
{
    kst_link_outcome_t o;

    by_hand();
    CHECK(kst_prog_editable(&g_p));
    CHECK_EQ(verify ? kst_prog_verify(&g_p) : kst_prog_read(&g_p),
             KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_NO_ANSWER;
    o.op = verify ? LINK_KST_OP_VERIFY : LINK_KST_OP_READ_ALL;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    o.fail_reg = 0xFF;
    end(&o);
    CHECK(kst_prog_editable(&g_p));
}

TEST_CASE(a_read_the_link_gave_up_on_asks_for_a_read)
{
    kst_link_outcome_t o;

    read_gets_no_answer(false);
    read_gets_no_answer(true);
    /* A frame the page refused ran nothing: the image stands. */
    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_NACK;
    o.op = LINK_KST_OP_READ_ALL;
    end(&o);
    CHECK(kst_prog_editable(&g_p));
}

TEST_CASE(a_file_the_card_did_not_take_is_written_again)
{
    char text[KST_PROG_FILE_MAX];

    /* The record of a write is discarded and the card keeps the old
     * file: without another write the next start finds the record. */
    by_hand();
    g_f.now_ms = 0xFFFFF000u;            /* the wait crosses the wrap */
    g_f.card = true;
    pass(0);
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH;
    g_p.journal_img = g_p.servo;
    g_p.journal_target = g_p.servo;
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    kst_prog_file_done(&g_p, false);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    pass(KST_PROG_FILE_RETRY_MS - 1u);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    pass(1);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    CHECK(strstr(text, "journal") == NULL);
    /* It fails again, and is written again. */
    kst_prog_file_done(&g_p, false);
    pass(KST_PROG_FILE_RETRY_MS - 1u);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    pass(1);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    /* The card takes it: that is the end of it. */
    kst_prog_file_done(&g_p, true);
    pass(2u * KST_PROG_FILE_RETRY_MS);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);

    /* A card that is gone waits for one. */
    g_p.journal = true;
    g_p.journal_open = true;
    kst_prog_forget(&g_p);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    kst_prog_file_done(&g_p, false);
    g_f.card = false;
    pass(KST_PROG_FILE_RETRY_MS);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    g_f.card = true;
    pass(1);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
}

TEST_CASE(a_plan_that_ends_without_a_plan_wrote_nothing)
{
    static const uint8_t results[] = {
        LINK_KST_RESULT_NO_PATH, LINK_KST_RESULT_TOO_COMPLEX,
    };

    for (unsigned i = 0; i < sizeof(results); ++i) {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        /* The port plans. */
        g_f.rd.phase = KST_LINK_PLANNING;
        g_f.rd.flags |= LINK_KS_F_BUSY;
        pass(1);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
        CHECK(kst_prog_planning(&g_p));
        g_f.rd.phase = KST_LINK_READY;
        g_f.rd.flags &= (uint16_t)~LINK_KS_F_BUSY;
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_WRITE;
        o.taken = true;
        o.result = results[i];
        o.fail_reg = 0xFF;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + (unsigned)results[i]);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK(!kst_prog_planning(&g_p));
        /* The servo is as it was read: no record, no read owed, the
         * pending value kept, and the next write is taken. */
        CHECK(!kst_prog_journal(&g_p));
        CHECK(!kst_prog_in_doubt(&g_p));
        CHECK(kst_prog_editable(&g_p));
        CHECK_EQ(kst_prog_changes(&g_p), 1);
        CHECK(kst_prog_write_enabled(&g_p));
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    }
}

TEST_CASE(a_port_that_reports_busy_is_not_timed_out)
{
    kst_prog_cmd_t cmd;

    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    g_f.rd.phase = KST_LINK_PLANNING;
    g_f.rd.flags |= LINK_KS_F_BUSY;
    pass(KST_PROG_RUN_MS);
    pass(KST_PROG_RUN_MS);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK(!kst_prog_take(&g_p, &cmd));
    /* Busy gone and no outcome: the time runs from the last busy feed. */
    g_f.rd.phase = KST_LINK_RUNNING;
    g_f.rd.flags &= (uint16_t)~LINK_KS_F_BUSY;
    pass(KST_PROG_RUN_MS - 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK(kst_prog_in_doubt(&g_p));
}

/* ------------------------------------------- the seams between the tasks */

/* The end of a write given up arrives while the read asked for since waits:
 * it carries the write's number and is not the read's. */
TEST_CASE(a_late_outcome_of_another_operation_ends_none)
{
    kst_prog_cmd_t cmd;
    kst_link_outcome_t o;
    uint32_t old;

    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    old = g_f.answer_id;
    pass(KST_PROG_RUN_MS);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);

    /* The read the message asks for, not handed over yet. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_WRITE;
    o.id = old;
    o.result = KST_SES_ERR_ABORTED;
    o.steps_done = 1;
    end(&o);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);

    /* And handed over and answered: the old end is still not its end. */
    answer(KST_LINK_START_OK);
    CHECK(g_f.answer_id != old);
    end(&o);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);

    /* The read's own end is. */
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    end(&o);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);

    /* With nothing asked for, the late end of a write is taken. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    pass(KST_PROG_RUN_MS);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_WRITE;
    o.result = KST_SES_ERR_ABORTED;
    o.steps_done = 1;
    end(&o);
    CHECK(msg() != KST_PROG_MSG_TIMEOUT);
}

/* The card's answer to the text of a write given up is not the answer to
 * the text of the write that waits: that write starts on its own text. */
TEST_CASE(the_answer_to_an_older_file_releases_no_write)
{
    char text[KST_PROG_FILE_MAX];
    kst_prog_cmd_t cmd;
    unsigned writes;

    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    writes = g_sim.n_writes;

    /* The card takes the text of a second write and does not answer. */
    g_card_silent = true;
    edit(KST_F_DUTY, 210);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_STORE_MS + 2u);
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_EQ(g_sim.n_writes, writes);

    /* A third write; the card answers the second one's text now. */
    g_deaf = true;
    edit(KST_F_DUTY, 220);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    kst_prog_file_done(&g_p, true);
    CHECK(!kst_prog_take(&g_p, &cmd));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);

    /* The third write's text goes, and its answer starts the write. */
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    CHECK(!kst_prog_take(&g_p, &cmd));
    kst_prog_file_done(&g_p, true);
    g_card_silent = false;
    g_deaf = false;
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 220);
}

/* An operation the port reports as running and this model awaits no longer
 * is aborted by ABORT all the same. */
TEST_CASE(abort_reaches_an_operation_the_model_awaits_no_longer)
{
    kst_prog_cmd_t cmd;

    by_hand();
    kst_prog_abort(&g_p);
    CHECK(!kst_prog_take(&g_p, &cmd));

    g_f.rd.phase = KST_LINK_RUNNING;
    g_f.rd.flags |= LINK_KS_F_BUSY;
    pass(1);
    kst_prog_abort(&g_p);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);

    /* A command that waits to be handed over is not replaced by it. */
    g_f.rd.phase = KST_LINK_READY;
    g_f.rd.flags &= (uint16_t)~LINK_KS_F_BUSY;
    pass(1);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    kst_prog_abort(&g_p);
    CHECK(!kst_prog_take(&g_p, &cmd));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
}

/* A port in PWM that no command of the model put there: the session is
 * gone, and the model says so. */
TEST_CASE(a_session_the_port_ends_is_said)
{
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    g_f.rd.port = LINK_KST_PWM;
    g_f.rd.flags = 0;
    g_f.have_image = false;
    g_f.have_backup = false;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_SESSION_ENDED);

    /* The supply switched off under a session, end to end. */
    fresh_at(0u);
    g_rail = true;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    g_sup_on = false;
    frames(3000);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_SESSION_ENDED);

    /* A leave the operator asked for says its own line. */
    up();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(msg() != KST_PROG_MSG_SESSION_ENDED);
}

/* A result, a refusal or an outcome kind the tables do not hold is said
 * with its number, and is no other message's. */
TEST_CASE(a_code_the_panel_does_not_know_is_said_with_its_number)
{
    kst_link_outcome_t o;

    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = (uint8_t)(KST_PROG_MSG_END - KST_PROG_MSG_SES);
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_UNKNOWN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_PROG_MSG_END - KST_PROG_MSG_SES);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);

    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = 0xFFu;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_UNKNOWN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 0xFF);

    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_REFUSED;
    o.op = LINK_KST_OP_READ_ALL;
    o.refusal = (link_kst_refusal_t)(KST_PROG_MSG_SES - KST_PROG_MSG_REF);
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_UNKNOWN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_PROG_MSG_SES - KST_PROG_MSG_REF);

    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = (kst_link_outcome_kind_t)(KST_PROG_MSG_REF - KST_PROG_MSG_OUT);
    o.op = LINK_KST_OP_READ_ALL;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_UNKNOWN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_PROG_MSG_REF - KST_PROG_MSG_OUT);
}

/* A port that reports busy without end is given up KST_PROG_RUN_MAX_MS
 * after the operation started, across the tick wrap too. */
static void busy_without_end(uint32_t t0)
{
    kst_prog_cmd_t cmd;

    by_hand();
    g_f.now_ms = t0;
    pass(0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    g_f.rd.phase = KST_LINK_RUNNING;
    g_f.rd.flags |= LINK_KS_F_BUSY;
    for (uint32_t t = 1000u; t < KST_PROG_RUN_MAX_MS; t += 1000u) {
        pass(1000u);
    }
    pass(999u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_journal(&g_p));
}

TEST_CASE(a_port_busy_without_end_is_given_up)
{
    busy_without_end(1000u);
    busy_without_end(0xFFFFFFFFu - KST_PROG_RUN_MAX_MS / 2u);
}

/* The port counts as taken while the page refuses and its last status had
 * the port out of PWM. */
TEST_CASE(a_page_that_refuses_keeps_the_port_as_it_was_last_read)
{
    kst_link_readout_t rd;

    memset(&rd, 0, sizeof(rd));
    rd.phase = KST_LINK_NO_PAGE;
    CHECK(!kst_prog_port_taken(&rd));
    rd.was_held = true;
    CHECK(kst_prog_port_taken(&rd));
    rd.phase = KST_LINK_DOWN;
    CHECK(!kst_prog_port_taken(&rd));
}

TEST_CASE(a_command_nobody_answers_is_given_up)
{
    answer_never_comes(1000u);
    outcome_never_comes(1000u);
    read_never_ends(1000u, false);
    read_never_ends(1000u, true);
}

TEST_CASE(the_waits_hold_across_the_tick_wrap)
{
    answer_never_comes(0xFFFFFFFFu - KST_PROG_START_MS / 2u);
    outcome_never_comes(0xFFFFFFFFu - KST_PROG_RUN_MS / 2u);
    outcome_never_comes(0xFFFFFFFFu);
}

TEST_CASE(the_command_counter_skips_0_when_it_wraps)
{
    kst_prog_cmd_t cmd;

    by_hand();
    g_p.next_id = 0xFFFFFFFFu;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.id, 1);
    /* An answer to an older command is not this one's. */
    g_f.answer_id = 0xFFFFFFFFu;
    g_f.answer = KST_LINK_START_BUSY;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
}

TEST_CASE(the_state_follows_the_port)
{
    by_hand();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    g_f.rd.flags |= LINK_KS_F_BUSY;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    g_f.rd.flags &= (uint16_t)~LINK_KS_F_BUSY;
    g_f.rd.port = LINK_KST_STOPPING;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    g_f.rd.port = LINK_KST_LOW;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    g_f.rd.port = LINK_KST_RAIL_OFF;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    g_f.rd.port = LINK_KST_RAIL_WAIT;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    CHECK_EQ(kst_prog_leave(&g_p),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    g_f.rd.phase = KST_LINK_SYNC;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_SYNC);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    g_f.rd.phase = KST_LINK_READY;
    g_f.rd.known = false;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_SYNC);

    /* The flags of the port that put the servo in doubt. */
    by_hand();
    CHECK(!kst_prog_in_doubt(&g_p));
    g_f.rd.flags |= LINK_KS_F_MUST_READ;
    pass(1);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_READ);
    g_f.rd.flags = (uint16_t)((g_f.rd.flags & ~LINK_KS_F_MUST_READ)
                              | LINK_KS_F_LOCKED);
    pass(1);
    CHECK(kst_prog_in_doubt(&g_p));

    /* A link lost with a command not yet taken: nothing was sent. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    g_f.rd.phase = KST_LINK_DOWN;
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER);
    CHECK(!kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    /* And with the operation on its way, when the module says nothing. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    g_f.rd.phase = KST_LINK_DOWN;
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK(kst_prog_journal(&g_p));
}

/* ----------------------------------------------------------------- the file */

static size_t text_of(char *out, bool journal)
{
    kst_prog_t p;

    kst_prog_init(&p);
    p.journal = journal;
    p.journal_port = LINK_PG_PORTS_MAX - 1u;
    p.journal_pin = KST_PROG_PIN_LAST;
    p.journal_img = sim_bench_image();
    p.journal_img.r[0x05] = 0xABu;
    p.journal_target = sim_bench_image();
    p.journal_target.r[0x1F] = 0xCDu;
    return kst_prog_file_text(&p, out, KST_PROG_FILE_MAX);
}

TEST_CASE(the_file_text_is_read_back_as_it_was_written)
{
    char text[KST_PROG_FILE_MAX], again[KST_PROG_FILE_MAX];
    kst_prog_t p;

    for (unsigned v = 0; v < 2u; ++v) {
        const bool journal = v != 0u;
        const size_t len = text_of(text, journal);

        CHECK(len != 0u);
        CHECK(len < KST_PROG_FILE_MAX);
        CHECK_EQ(len, strlen(text));
        kst_prog_init(&p);
        CHECK(kst_prog_file_load(&p, text, len));
        CHECK_EQ(kst_prog_journal(&p), journal);
        CHECK_EQ(kst_prog_in_doubt(&p), journal);
        CHECK_EQ(kst_prog_message(&p)->id,
                 journal ? KST_PROG_MSG_JOURNAL : KST_PROG_MSG_NONE);
        if (journal) {
            CHECK_EQ(p.journal_port, LINK_PG_PORTS_MAX - 1u);
            CHECK_EQ(kst_prog_journal_pin(&p), KST_PROG_PIN_LAST);
            CHECK_EQ(kst_prog_message(&p)->arg,
                     LINK_PG_PORTS_MAX | ((KST_PROG_PIN_LAST + 1u) << 8));
            CHECK_EQ(p.journal_img.r[0x05], 0xAB);
            CHECK_EQ(p.journal_target.r[0x1F], 0xCD);
        }
        CHECK_EQ(kst_prog_file_text(&p, again, sizeof(again)), len);
        CHECK_STR_EQ(again, text);
    }
    /* A buffer too small takes nothing. */
    CHECK_EQ(kst_prog_file_text(&p, again, KST_PROG_FILE_MAX - 1u), 0);
    /* The longest text fits. */
    CHECK(text_of(text, true) + 1u <= KST_PROG_FILE_MAX);
}

/* The images of the texts below; each check value is from an independent
 * bitwise CRC-16/CCITT-FALSE. */
#define REC_IMAGES \
    "journal=00F5F5144877046541A40D805307140096960001010000000000000A00100000\n" \
    "target=00C8C8144877046541A40D805307140096960001010000000000000A00100000\n"

/* A record a panel of the file's version 1 wrote: no pin line.  It stands,
 * and its messages name the port. */
TEST_CASE(a_record_of_version_1_stands_and_names_its_port)
{
    static const char v1[] =
        "RCBENCH KST 1\n" REC_IMAGES "port=2\ncheck=A30F\n";
    char again[KST_PROG_FILE_MAX];
    kst_prog_t p;

    kst_prog_init(&p);
    CHECK(kst_prog_file_whole(v1, strlen(v1)));
    CHECK(kst_prog_file_load(&p, v1, strlen(v1)));
    CHECK(kst_prog_journal(&p));
    CHECK_EQ(kst_prog_message(&p)->id, KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_journal_port(&p), 2u);
    CHECK_EQ(kst_prog_journal_pin(&p), KST_PROG_NO_PIN);
    CHECK_EQ(kst_prog_message(&p)->arg, 3u);
    CHECK_EQ(kst_prog_journal_arg(&p), 3u);
    /* Written again, it is a text of version 2 with no pin known. */
    CHECK(kst_prog_file_text(&p, again, sizeof(again)) != 0u);
    CHECK_STR_EQ(again, "RCBENCH KST 2\n" REC_IMAGES
                        "port=2\npin=FF\ncheck=602A\n");
    kst_prog_init(&p);
    CHECK(kst_prog_file_load(&p, again, strlen(again)));
    CHECK_EQ(kst_prog_journal_pin(&p), KST_PROG_NO_PIN);
}

/* The pin line: GP0 to GP47 named, FF and any pin above GP47 not known --
 * the record stands by its port.  A line out of its place, a digit that is
 * none and a version not known make the text not whole. */
TEST_CASE(the_record_names_a_pin_of_the_coprocessor_only)
{
    static const struct {
        const char *text;
        bool whole;
        uint8_t pin;
    } k[] = {
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npin=00\ncheck=A87D\n",
          true, 0u },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npin=2F\ncheck=64E2\n",
          true, KST_PROG_PIN_LAST },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npin=30\ncheck=F12D\n",
          true, KST_PROG_NO_PIN },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npin=FF\ncheck=602A\n",
          true, KST_PROG_NO_PIN },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\ncheck=57D3\n",
          false, 0 },
        { "RCBENCH KST 1\n" REC_IMAGES "port=2\npin=0D\ncheck=2DE9\n",
          false, 0 },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npin=0G\ncheck=39B3\n",
          false, 0 },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npin=ZZ\ncheck=1036\n",
          false, 0 },
        { "RCBENCH KST 3\n" REC_IMAGES "port=2\npin=0D\ncheck=53E7\n",
          false, 0 },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npon=0D\ncheck=E101\n",
          false, 0 },
        { "RCBENCH KST 2\n" REC_IMAGES "port=2\npin=0D;check=4A92\n",
          false, 0 },
    };

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        kst_prog_t p;

        kst_prog_init(&p);
        CHECK_EQ(kst_prog_file_whole(k[i].text, strlen(k[i].text)),
                 k[i].whole);
        CHECK_EQ(kst_prog_file_load(&p, k[i].text, strlen(k[i].text)),
                 k[i].whole);
        CHECK_EQ(kst_prog_journal(&p), k[i].whole);
        if (!k[i].whole) {
            CHECK_EQ(kst_prog_message(&p)->id, KST_PROG_MSG_FILE_BAD);
            continue;
        }
        CHECK_EQ(kst_prog_journal_port(&p), 2u);
        CHECK_EQ(kst_prog_journal_pin(&p), k[i].pin);
        CHECK_EQ(kst_prog_message(&p)->arg,
                 k[i].pin == KST_PROG_NO_PIN
                     ? 3u : 3u | ((k[i].pin + 1u) << 8));
    }
}

/* The pin is the one of the port the write went through, as the feed has
 * it at the write; a port the binding gives no pin records none. */
TEST_CASE(the_journal_records_the_pin_of_its_port_at_the_write)
{
    up_enabled();
    g_card = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(2);
    CHECK(kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_journal_port(&g_p), CH);
    CHECK_EQ(kst_prog_journal_pin(&g_p), PIN);
    CHECK_EQ(kst_prog_journal_arg(&g_p), (CH + 1u) | ((PIN + 1u) << 8));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);

    CHECK(!kst_prog_journal(&g_p));

    /* The panel with no pin known for the port: the next write records
     * none, though the one before recorded GP13. */
    g_pin_unknown = true;
    frame();
    edit(KST_F_DUTY, 210);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(2);
    CHECK(kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_journal_pin(&g_p), KST_PROG_NO_PIN);
    CHECK_EQ(kst_prog_journal_arg(&g_p), CH + 1u);
    CHECK(strstr(g_file, "\nport=0\npin=FF\ncheck=") != NULL);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
}

/* The bounds of the pin the feed gives: GP47 is recorded, GP48 and above
 * are not a pin of the IOmcu and the record stands by its port. */
TEST_CASE(the_journal_records_a_pin_up_to_gp47_only)
{
    static const struct { int pin; uint8_t want; const char *line; } k[] = {
        { 0,  0,                 "\nport=0\npin=00\ncheck=" },
        { 47, 47,                "\nport=0\npin=2F\ncheck=" },
        { 48, KST_PROG_NO_PIN,   "\nport=0\npin=FF\ncheck=" },
    };

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        up_enabled();
        g_pin_value = k[i].pin;
        g_card = true;
        frame();
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        frames(2);
        CHECK(kst_prog_journal(&g_p));
        CHECK_EQ(kst_prog_journal_pin(&g_p), k[i].want);
        CHECK(strstr(g_file, k[i].line) != NULL);
        settle();
    }
    g_pin_value = -1;
}

TEST_CASE(a_file_that_is_not_whole_changes_nothing)
{
    char good[KST_PROG_FILE_MAX], text[KST_PROG_FILE_MAX + 8u];
    const size_t len = text_of(good, true);
    kst_prog_t p;
    unsigned taken = 0;

    /* Every single character changed, every length but the whole. */
    for (size_t i = 0; i < len; ++i) {
        memcpy(text, good, len + 1u);
        text[i] = text[i] == 'x' ? 'y' : 'x';
        kst_prog_init(&p);
        if (kst_prog_file_load(&p, text, len)) {
            taken++;
        } else {
            CHECK_EQ(kst_prog_message(&p)->id, KST_PROG_MSG_FILE_BAD);
            CHECK(!kst_prog_journal(&p));
        }
    }
    CHECK_EQ(taken, 0);
    for (size_t n = 0; n < len; ++n) {
        kst_prog_init(&p);
        CHECK(!kst_prog_file_load(&p, good, n));
    }
    /* A text with a valid check and a body that is none. */
    {
        static const char *const bodies[] = {
            "RCBENCH KST 1\n00=00\n",                 /* a backup cut short */
            "RCBENCH KST 1\njunk\n",
            "RCBENCH KST 1\n00=0g\n01=00\n",
            "RCBENCH KST 3\n",
        };

        for (unsigned i = 0; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
            kst_prog_t w;
            size_t n;

            /* The check of the body, by the writer's own arithmetic. */
            kst_prog_init(&w);
            n = kst_prog_file_text(&w, text, KST_PROG_FILE_MAX);
            CHECK(n != 0u);
            (void)n;
            kst_prog_init(&p);
            CHECK(!kst_prog_file_load(&p, bodies[i], strlen(bodies[i])));
        }
    }
    /* Too long. */
    memset(text, 'A', sizeof(text));
    kst_prog_init(&p);
    CHECK(!kst_prog_file_load(&p, text, sizeof(text)));
    /* A text that is not whole leaves what is held. */
    kst_prog_init(&p);
    CHECK(kst_prog_file_load(&p, good, len));
    CHECK(!kst_prog_file_load(&p, good, len - 1u));
    CHECK(kst_prog_journal(&p));
}

TEST_CASE(the_file_is_written_once_per_change_and_only_to_a_card)
{
    char text[KST_PROG_FILE_MAX];

    by_hand();
    g_f.card = true;
    kst_prog_feed(&g_p, &g_f);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH;
    g_p.journal_img = g_p.servo;
    g_p.journal_target = g_p.servo;
    kst_prog_forget(&g_p);
    /* A buffer too small takes nothing. */
    CHECK_EQ(kst_prog_file_take(&g_p, text, 10), 0);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    /* One write at a time. */
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    kst_prog_file_done(&g_p, true);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
}

/* ------------------------------------------- the port, the target, the supply */

TEST_CASE(a_connect_needs_the_port_as_the_testers_target)
{
    fresh_at(0u);
    link_up();
    /* The SERVO screen drives the servo outputs. */
    g_target = 0u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NEED_TARGET);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_TARGET);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(g_starts, 0);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    /* Another port than the one asked for. */
    g_target = CH + 2u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NEED_TARGET);
    CHECK_EQ(g_starts, 0);
    /* STOP is said before the target. */
    g_stopped = true;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_STOPPED);
    g_stopped = false;
    g_target = CH + 1u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    /* The target moves under a held port: nothing is taken away. */
    g_target = 0u;
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);
}

TEST_CASE(a_connect_sets_the_supply_it_was_given)
{
    fresh_at(0u);
    g_rail = true;
    link_up();
    CHECK_EQ(kst_prog_supply_mv(&g_p), KST_PROG_SUPPLY_MV);
    CHECK_EQ(kst_prog_supply_ma(&g_p), KST_PROG_SUPPLY_MA);
    CHECK_EQ(kst_prog_supply_mv(&g_p), 5000);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(g_sup_on);
    CHECK_EQ(g_sup_mv, 5000);
    CHECK_EQ(g_sup_ma, 1000);

    /* Another voltage and limit, at the port's ends. */
    fresh_at(0u);
    g_rail = true;
    link_up();
    kst_prog_supply_set(&g_p, KST_PORT_SUPPLY_MAX_MV, PDMINI_I_MAX_MA);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(g_sup_mv, KST_PORT_SUPPLY_MAX_MV);
    CHECK_EQ(g_sup_ma, PDMINI_I_MAX_MA);

    fresh_at(0u);
    g_rail = true;
    link_up();
    kst_prog_supply_set(&g_p, KST_PORT_SUPPLY_MIN_MV, PDMINI_I_MIN_MA);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(g_sup_mv, KST_PORT_SUPPLY_MIN_MV);
    CHECK_EQ(g_sup_ma, PDMINI_I_MIN_MA);

    /* Above a limit of the supply nothing is asked; at it, and with no
     * limit, it is. */
    for (int which = 0; which < 2; ++which) {
        fresh_at(0u);
        g_rail = true;
        link_up();
        kst_prog_supply_set(&g_p, 6000u, 1000u);
        kst_prog_supply_caps(&g_p, which == 0 ? 5900u : 0u,
                             which == 0 ? 0u : 999u);
        CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_SUPPLY_CAPS);
        CHECK_EQ(msg(), KST_PROG_MSG_SUPPLY_CAPS);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
        CHECK(!kst_prog_holds_port(&g_p));
        settle();
        CHECK_EQ(g_sup_mv, 0u);
        kst_prog_supply_caps(&g_p, 6000u, 1000u);
        CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(g_sup_mv, 6000u);
        CHECK_EQ(g_sup_ma, 1000u);
    }

    /* Under the port's lowest voltage nothing is set, and the refusal is
     * the port's by name. */
    fresh_at(0u);
    g_rail = true;
    link_up();
    kst_prog_supply_set(&g_p, KST_PORT_SUPPLY_MIN_MV - 100u, 1000u);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_LOW);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(g_sup_mv, 0u);
}

/* The floor is the panel's too: an IOmcu built before the port's
 * refusal takes the ENTER and leaves an output that is on where it is. */
TEST_CASE(an_output_on_under_the_floor_is_refused_before_the_port_is_asked)
{
    static const struct {
        bool     read;
        uint16_t set_mv;
        uint16_t floor_mv;
        bool     refused;
    } c[] = {
        { true,  KST_PORT_SUPPLY_MIN_MV - 1u, KST_PORT_SUPPLY_MIN_MV, true  },
        { true,  KST_PORT_SUPPLY_MIN_MV,      KST_PORT_SUPPLY_MIN_MV, false },
        { true,  0u,                          KST_PORT_SUPPLY_MIN_MV, true  },
        { false, 0u,                          KST_PORT_SUPPLY_MIN_MV, false },
        { true,  KST_PORT_SUPPLY_MIN_MV - 1u, 0u,                     false },
    };

    for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        fresh_at(0u);
        link_up();
        kst_prog_supply_floor(&g_p, c[i].floor_mv);
        g_set_read = c[i].read;
        g_set_mv = c[i].set_mv;
        settle();
        if (c[i].refused) {
            const unsigned sent = g_sim.n_frames;

            CHECK_EQ(kst_prog_connect(&g_p, CH),
                     KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_LOW);
            CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_LOW);
            CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
            CHECK(!kst_prog_holds_port(&g_p));
            settle();
            CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
            CHECK_EQ(g_sim.n_frames, sent);
            CHECK_EQ(g_sup_mv, 0u);
        } else {
            CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
        }
    }
    g_set_read = false;
    g_set_mv = 0;
}

TEST_CASE(a_supply_outside_the_ports_range_is_refused_by_name)
{
    static const uint16_t bad[][2] = {
        { KST_PORT_SUPPLY_MAX_MV + 1u, 1000u },
        { PDMINI_V_MIN_MV - 1u, 1000u },
        { 5000u, PDMINI_I_MAX_MA + 1u },
        { 5000u, PDMINI_I_MIN_MA - 1u },
    };

    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        fresh_at(0u);
        g_rail = true;
        link_up();
        kst_prog_supply_set(&g_p, bad[i][0], bad[i][1]);
        CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_RANGE);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
        CHECK(!g_sup_on);
        /* No read is chained to it. */
        CHECK_EQ(g_starts, 1);
    }
    /* With no PD mini the supply is not touched and nothing is refused. */
    fresh_at(0u);
    link_up();
    kst_prog_supply_set(&g_p, KST_PORT_SUPPLY_MAX_MV + 1u, 1000u);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!g_sup_on);
}

TEST_CASE(a_supply_that_does_not_switch_on_ends_the_connect_with_result_32)
{
    fresh_at(0u);
    g_rail = true;
    g_rail_stuck = true;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK_EQ(g_starts, 1);
}

TEST_CASE(a_leave_goes_back_to_pwm_and_switches_no_supply)
{
    /* No PD mini answers: the port's command changes its state only. */
    fresh_at(0u);
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    /* Nothing else starts under it. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
}

TEST_CASE(a_leave_with_a_pd_mini_keeps_the_supply_on)
{
    int starts;

    fresh_at(0u);
    g_rail = true;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(g_sup_on);
    starts = g_starts;
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    settle();
    /* 1 command, and it is no supply cycle. */
    CHECK_EQ(g_starts, starts + 1);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(g_sup_on);
}

/* Frames from the leave to the port in PWM. */
static unsigned leave_takes(void)
{
    unsigned n = 0;

    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    while (n < 20000u && kst_prog_state(&g_p) != KST_PROG_PWM) {
        /* Nothing else starts while the port waits. */
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
        frame();
        n++;
    }
    return n;
}

TEST_CASE(the_wait_before_pwm_is_the_leaves_wait)
{
    static const uint16_t waits[] = { 0u, 100u, 10000u };

    for (unsigned i = 0; i < sizeof(waits) / sizeof(waits[0]); ++i) {
        kst_prog_cmd_t cmd;
        unsigned n;

        /* The request carries the value. */
        by_hand();
        CHECK(kst_prog_wait_set(&g_p, waits[i]));
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        CHECK(kst_prog_take(&g_p, &cmd));
        CHECK_EQ(cmd.kind, KST_PROG_CMD_START);
        CHECK_EQ(cmd.req.op, LINK_KST_OP_LEAVE);
        CHECK_EQ(cmd.req.port, CH);
        CHECK_EQ(cmd.req.leave_ms, waits[i]);

        /* And the port waits that long: 1 frame is 1ms, and the link's
         * exchanges come on top. */
        up();
        CHECK(kst_prog_wait_set(&g_p, waits[i]));
        n = leave_takes();
        CHECK(n >= waits[i]);
        CHECK(n <= waits[i] + 400u);
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
        CHECK(!kst_prog_have_servo(&g_p));
    }
}

TEST_CASE(a_wait_above_the_range_is_not_sent)
{
    kst_link_request_t r;
    kst_prog_cmd_t cmd;

    /* The model takes no such value. */
    up();
    CHECK(kst_prog_wait_set(&g_p, 100u));
    CHECK(!kst_prog_wait_set(&g_p, 10001u));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 100u);
    /* The link module sends nothing for one. */
    memset(&r, 0, sizeof(r));
    r.op = LINK_KST_OP_LEAVE;
    r.port = CH;
    r.leave_ms = 10001u;
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_ARG);
    r.leave_ms = 10000u;
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_OK);
    settle();
    /* And its answer is a message that keeps the session. */
    by_hand();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_ARG);
    CHECK_EQ(msg(), KST_PROG_MSG_START + KST_LINK_START_ARG);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_take(&g_p, &cmd));
}

TEST_CASE(an_abort_in_the_wait_keeps_the_session)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK(kst_prog_wait_set(&g_p, 5000u));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    frames(500);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_LEAVE);
    CHECK_EQ(g_p.f.rd.port, LINK_KST_LEAVING);
    /* Nothing starts under the wait. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    kst_prog_abort(&g_p);
    settle();
    CHECK_EQ(g_aborts, 1);
    CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_CUT);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    /* The session is as the leave found it: no read is owed. */
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 1);
    CHECK_EQ(g_p.f.rd.flags & LINK_KS_F_MUST_READ, 0);
    /* A new leave is taken at once. */
    CHECK(kst_prog_wait_set(&g_p, 0u));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
}

TEST_CASE(a_disarm_in_the_wait_keeps_the_port_and_asks_for_a_read)
{
    up_enabled();
    CHECK(kst_prog_wait_set(&g_p, 5000u));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    frames(500);
    CHECK_EQ(g_p.f.rd.port, LINK_KST_LEAVING);
    g_stop = true;
    frame();
    g_stop = false;
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_CUT);
    /* The port is not in PWM: the tester does not have it. */
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK((g_p.f.rd.flags & LINK_KS_F_MUST_READ) != 0u);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    /* The read the port asks for, then the leave with its wait. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_wait_set(&g_p, 100u));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
}

TEST_CASE(a_supply_that_goes_off_in_the_wait_ends_in_pwm_with_it_off)
{
    fresh_at(0u);
    g_rail = true;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK(g_sup_on);
    CHECK(kst_prog_wait_set(&g_p, 5000u));
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    frames(500);
    CHECK_EQ(g_p.f.rd.port, LINK_KST_LEAVING);
    /* The panel's STOP switches the PD mini off. */
    g_sup_on = false;
    frames(200);
    /* Not in PWM while the supply's off time runs. */
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    CHECK_EQ(g_p.f.rd.port, LINK_KST_RAIL_OFF);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_CUT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!g_sup_on);
}

TEST_CASE(a_coprocessor_without_leave_is_named_and_cycles_no_supply)
{
    for (uint16_t minor = 13u; minor < LINK_MINOR_KST_LEAVE; ++minor) {
        int starts;

        /* No PD mini answers: the port's older command changes its state
         * only, and the end says that no wait was applied. */
        fresh_at(0u);
        kst_link_came_up(&g_k, minor);
        settle();
        CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_READ);
        starts = g_starts;
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(g_starts, starts + 2);
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT_NO_WAIT);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
        CHECK(!kst_prog_have_servo(&g_p));

        /* A PD mini answers: that command would cycle the supply.  It is
         * not sent, and the session stays. */
        fresh_at(0u);
        g_rail = true;
        kst_link_came_up(&g_k, minor);
        settle();
        CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
        settle();
        CHECK(g_sup_on);
        starts = g_starts;
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(g_starts, starts + 1);
        CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_SUPPLY);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        CHECK(kst_prog_have_servo(&g_p));
        CHECK(g_sup_on);
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_READ);
    }
    /* A page not read when the answer comes is taken as one with a PD
     * mini: nothing is sent. */
    {
        kst_prog_cmd_t cmd;

        by_hand();
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        g_f.rd.known = false;
        answer(KST_LINK_START_NO_OP);
        CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_SUPPLY);
        CHECK(!kst_prog_take(&g_p, &cmd));
    }
}

TEST_CASE(a_leave_the_port_refuses_keeps_the_session)
{
    static const uint8_t refs[] = {
        LINK_KST_REF_BUSY, LINK_KST_REF_PORT, LINK_KST_REF_UNSAFE,
        LINK_KST_REF_NOT_PROGRAMMING, LINK_KST_REF_MUST_READ,
        LINK_KST_REF_LEAVE_MS, LINK_KST_REF_LOCKED,
    };

    for (unsigned i = 0; i < sizeof(refs); ++i) {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_REFUSED;
        o.op = LINK_KST_OP_LEAVE;
        o.refusal = (link_kst_refusal_t)refs[i];
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_REF + (unsigned)refs[i]);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        CHECK(kst_prog_have_servo(&g_p));
        CHECK_EQ(kst_prog_changes(&g_p), 1);
    }
}

TEST_CASE(a_port_in_doubt_is_left_by_leave_only_where_a_pd_mini_answers)
{
    static const uint16_t doubts[] = {
        LINK_KS_F_MUST_READ, LINK_KS_F_LOCKED,
    };

    for (unsigned i = 0; i < 2u; ++i) {
        kst_prog_cmd_t cmd;
        kst_link_outcome_t o;

        /* A PD mini answers: the command is LEAVE, which the port refuses
         * and which switches nothing. */
        by_hand();
        g_f.rd.flags |= (uint16_t)(doubts[i] | LINK_KS_F_RAIL);
        pass(1);
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        CHECK(kst_prog_take(&g_p, &cmd));
        CHECK_EQ(cmd.req.op, LINK_KST_OP_LEAVE);

        /* None answers: STOP would leave the port where it is.  The
         * command that changes the port's state only is sent. */
        by_hand();
        g_f.rd.flags |= doubts[i];
        pass(1);
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        CHECK(kst_prog_take(&g_p, &cmd));
        CHECK_EQ(cmd.req.op, LINK_KST_OP_POWER_CYCLED);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
        g_f.answer_id = cmd.id;
        g_f.answer = KST_LINK_START_OK;
        pass(1);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_POWER_CYCLED;
        o.taken = true;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT_DOUBT);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK(!kst_prog_have_servo(&g_p));
    }
}

TEST_CASE(the_wait_before_pwm_keeps_its_limits)
{
    fresh_at(0u);
    CHECK_EQ(kst_prog_wait_ms(&g_p), KST_PROG_WAIT_MS);
    CHECK_EQ(KST_PROG_WAIT_MS, 100u);
    CHECK(kst_prog_wait_set(&g_p, 0u));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 0u);
    CHECK(kst_prog_wait_set(&g_p, KST_PROG_WAIT_MAX_MS));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 10000u);
    /* 1ms above the range changes nothing, and nothing wraps. */
    CHECK(!kst_prog_wait_set(&g_p, KST_PROG_WAIT_MAX_MS + 1u));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 10000u);
    CHECK(!kst_prog_wait_set(&g_p, 65536u + 5u));
    CHECK(!kst_prog_wait_set(&g_p, UINT32_MAX));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 10000u);
    CHECK(kst_prog_wait_set(&g_p, 250u));
    /* A link that comes and goes keeps it. */
    link_up();
    CHECK_EQ(kst_prog_wait_ms(&g_p), 250u);
}

/* ------------------------------------- what an operation leaves behind */

/* What a write that ended with @p r wrote when the IOmcu counted no
 * step done.  A result added to kst_ses_t does not compile here until it
 * has a row. */
typedef enum { LEFT_OK, LEFT_NOTHING, LEFT_DOUBT } left_t;

static left_t left_by(unsigned r)
{
    if (r > (unsigned)KST_SES_ERR_NO_PLAN) {
        return (r == LINK_KST_RESULT_NO_PATH
                || r == LINK_KST_RESULT_TOO_COMPLEX) ? LEFT_NOTHING
                                                     : LEFT_DOUBT;
    }
    switch ((kst_ses_t)r) {
    case KST_SES_OK:
        return LEFT_OK;
    case KST_SES_ERR_BUSY:
    case KST_SES_ERR_ARG:
    case KST_SES_ERR_NOT_IN_MODE:
    case KST_SES_ERR_NO_BACKUP:
    case KST_SES_ERR_FINGERPRINT:
    case KST_SES_ERR_LOCKED:
    case KST_SES_ERR_BAD_PLAN:
    case KST_SES_ERR_UNCONFIRMED:
    case KST_SES_ERR_CHANGED:
    case KST_SES_ERR_NO_PLAN:
    case KST_SES_ERR_WRITE_FAILED:
    case KST_SES_ERR_ROLLED_BACK:
        return LEFT_NOTHING;
    case KST_SES_BUSY:
    case KST_SES_ERR_NO_SERVO:
    case KST_SES_ERR_READ:
    case KST_SES_ERR_TORN:
    case KST_SES_ERR_UNINTENDED:
    case KST_SES_ERR_VERIFY:
    case KST_SES_ERR_DRIVER:
    case KST_SES_ERR_ABORTED:
        return LEFT_DOUBT;
    }
    return LEFT_DOUBT;
}

/* Every kind of outcome, every result of the session and of the page, with
 * and without a step counted done, taken or not, for each operation that
 * writes: the fields written are those of before exactly when no register
 * was written, and the servo is in doubt in every other case. */
TEST_CASE(every_outcome_of_a_write_says_what_it_wrote)
{
    static const kst_link_outcome_kind_t kinds[] = {
        KST_LINK_OUT_DONE, KST_LINK_OUT_REFUSED, KST_LINK_OUT_NACK,
        KST_LINK_OUT_NO_ANSWER, KST_LINK_OUT_RESTARTED, KST_LINK_OUT_DROPPED,
    };
    static const link_kst_op_t ops[] = {
        LINK_KST_OP_WRITE, LINK_KST_OP_RESTORE, LINK_KST_OP_RELEASE,
    };
    unsigned classed = 0;

    /* The table above ends where the session's results end. */
    CHECK_EQ(KST_SES_ERR_NO_PLAN, 20);
    CHECK_EQ(KST_LINK_OUT_DROPPED, 6);
    for (unsigned r = 0; r < 64u; ++r) {
        const left_t l = left_by(r);

        CHECK_EQ(kst_prog_result_class(r),
                 l == LEFT_OK ? KST_PROG_RES_OK
                 : l == LEFT_NOTHING ? KST_PROG_RES_CLEAN
                                     : KST_PROG_RES_DOUBT);
        classed += (l != LEFT_DOUBT) ? 1u : 0u;
    }
    CHECK_EQ(classed, 15);
    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        for (unsigned r = 0; r < 40u; ++r) {
            for (unsigned c = 0; c < 12u; ++c) {
                const uint8_t steps = (uint8_t)(c & 1u);
                const bool taken = (c & 2u) != 0u;
                const link_kst_op_t op = ops[c / 4u];
                const bool done = kinds[i] == KST_LINK_OUT_DONE;
                kst_link_outcome_t o;
                bool nothing;

                if (!done && r != 0u) {
                    continue;       /* the result is the DONE outcome's */
                }
                switch (kinds[i]) {
                case KST_LINK_OUT_DONE:
                    nothing = steps == 0u && left_by(r) == LEFT_NOTHING;
                    break;
                case KST_LINK_OUT_REFUSED:
                case KST_LINK_OUT_DROPPED:
                    nothing = true;
                    break;
                case KST_LINK_OUT_NACK:
                    nothing = !taken;
                    break;
                default:
                    nothing = false;
                    break;
                }
                by_hand();
                g_p.wrote_known = true;
                CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
                edit(KST_F_DUTY, 200);
                CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
                CHECK_EQ(g_p.wrote, 0);
                answer(KST_LINK_START_OK);
                CHECK(g_p.wrote != 0u);
                CHECK(kst_prog_journal(&g_p));
                /* The same end for the three operations that write. */
                g_p.op = (uint8_t)op;
                memset(&o, 0, sizeof(o));
                o.kind = kinds[i];
                o.op = op;
                o.result = (uint8_t)r;
                o.steps_done = steps;
                o.taken = taken;
                o.fail_reg = 0xFF;
                end(&o);
                if (kinds[i] == KST_LINK_OUT_RESTARTED) {
                    /* The session is gone, and what it wrote with it. */
                    CHECK(!g_p.wrote_known);
                    CHECK(kst_prog_journal(&g_p));
                    continue;
                }
                CHECK(g_p.wrote_known);
                if (done && r == KST_SES_OK) {
                    CHECK(g_p.wrote != 0u);
                    CHECK(!kst_prog_journal(&g_p));
                    continue;
                }
                CHECK_EQ(g_p.wrote == 0u, nothing);
                CHECK_EQ(kst_prog_journal(&g_p), !nothing);
                CHECK_EQ(kst_prog_in_doubt(&g_p),
                         !nothing || (done && r == KST_SES_ERR_CHANGED));
                CHECK_EQ(kst_prog_editable(&g_p),
                         nothing && !(done && r == KST_SES_ERR_CHANGED));
                /* The pending value is there to be written again. */
                CHECK_EQ(draft(KST_F_DUTY), 200);
            }
        }
    }
}

/* --------------------------- a frame refused after the command was taken */

typedef enum {
    NK_READ, NK_VERIFY, NK_WRITE, NK_RESTORE, NK_RELEASE, NK_ENTER, NK_LEAVE,
    NK_COUNT
} nk_t;

/* The session up to the operation @p k, and the operation asked for. */
static void nk_begin(nk_t k)
{
    kst_prog_restore_t plan;

    if (k == NK_ENTER) {
        fresh_at(0u);
        link_up();
        CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
        return;
    }
    up_enabled();
    switch (k) {
    case NK_READ:
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        break;
    case NK_VERIFY:
        CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
        break;
    case NK_WRITE:
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        break;
    case NK_RESTORE:
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
                 KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
        break;
    case NK_RELEASE:
        CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NONE);
        break;
    default:
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        break;
    }
}

/*
 * Each operation, with each of its exchanges in turn answered by a NACK.
 * A NACK after the command was acknowledged ends the operation with no
 * result read: the image held is not the servo's any more, and nothing but
 * a read is taken until one succeeds.
 */
TEST_CASE(a_nack_after_the_command_leaves_the_image_unusable)
{
    for (unsigned k = 0; k < (unsigned)NK_COUNT; ++k) {
        unsigned taken = 0;
        unsigned n;

        for (n = 0; n < 4000u; ++n) {
            unsigned before;
            bool held;

            nk_begin((nk_t)k);
            before = g_outcomes;
            held = kst_prog_have_servo(&g_p);
            g_nacked = false;
            g_nack_at = (int)n;
            settle();
            g_nack_at = -1;
            if (!g_nacked) {
                break;          /* the operation has no exchange n */
            }
            if (g_outcomes == before
                || g_outcome.kind != KST_LINK_OUT_NACK || !g_outcome.taken) {
                /* The command itself, refused: nothing ran.  Or a status
                 * read: the page is gone, and the session with it. */
                if (kst_prog_state(&g_p) == KST_PROG_NO_PAGE) {
                    CHECK(!kst_prog_editable(&g_p));
                    CHECK(kst_prog_write_check(&g_p) != KST_PROG_MSG_NONE);
                }
                continue;
            }
            taken++;
            CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_NACK);
            if (!held && kst_prog_state(&g_p) != KST_PROG_PROGRAMMING) {
                continue;       /* an ENTER that did not get there */
            }
            if (kst_prog_state(&g_p) != KST_PROG_PROGRAMMING) {
                /* A LEAVE: the port is out of the mode or on its way. */
                CHECK(!kst_prog_editable(&g_p));
                continue;
            }
            CHECK(!kst_prog_editable(&g_p));
            if (held) {
                CHECK(kst_prog_in_doubt(&g_p));
                CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, 10),
                         KST_PROG_EDIT_REFUSED);
                CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_NEED_READ);
                CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
                CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_READ);
                CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
                CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
            } else {
                CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
            }
            frames(50);
            CHECK_EQ(g_fc.saves, k == NK_WRITE || k == NK_RESTORE
                                 || k == NK_RELEASE ? 1 : 0);
            /* A read that succeeds gives the image back. */
            CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
            settle();
            CHECK(kst_prog_have_servo(&g_p));
            CHECK(!g_p.need_read);
            {
                const kst_image_t now = servo();

                CHECK(same(kst_prog_servo(&g_p), &now));
            }
            CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
        }
        CHECK(n > 2u);
        CHECK(n < 4000u);
        CHECK(taken > 2u);
    }
}

/* ---------------------------------------- a restore that is never sent */

/* The record the card holds names @p target. */
static bool text_has_target(const kst_image_t *target)
{
    kst_prog_t t;

    kst_prog_init(&t);
    return g_file_len != 0u && kst_prog_file_load(&t, g_file, g_file_len)
        && t.journal && same(&t.journal_target, target);
}

typedef enum {
    RS_FILE_FAILS, RS_FILE_SILENT,
    RS_NOT_HANDED, RS_STALLED, RS_ABORT, RS_LINK_LOST, RS_COUNT
} rs_t;

/*
 * A confirmed restore from the journal's record that ends before its
 * command goes: at the journal's file, at the start.  The pending values,
 * the prompt that was open and the record are as they were before RESTORE
 * was confirmed.  No file of the servo is written first: the servo may be
 * half written, and the record holds it as found.
 */
TEST_CASE(a_restore_that_never_starts_leaves_the_pending_values)
{
    for (unsigned s = 0; s < (unsigned)RS_COUNT; ++s) {
        kst_prog_restore_t plan;
        kst_image_t before, record;
        unsigned starts, writes, changes, saves, files;

        cut_and_come_back(false);
        /* One value pending, and a prompt open for another. */
        edit(KST_F_BOOST, (uint16_t)(draft(KST_F_BOOST) + 1u));
        CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY,
                               (uint16_t)(draft(KST_F_DUTY) + 2u)),
                 KST_PROG_EDIT_SOFT);
        CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
                 KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        CHECK(g_p.soft);
        before = *kst_prog_draft(&g_p);
        record = g_p.journal_target;
        changes = kst_prog_changes(&g_p);
        starts = g_starts;
        writes = g_sim.n_writes;
        saves = g_fc.saves;
        files = g_file_writes;
        CHECK(!kst_prog_save_first(&g_p));

        switch ((rs_t)s) {
        case RS_FILE_FAILS:
            g_card_fails = true;
            break;
        case RS_FILE_SILENT:
            g_card_silent = true;
            break;
        case RS_NOT_HANDED:
            g_deaf = true;
            break;
        default:
            g_stall = true;
            break;
        }
        CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
        /* The restore's values are the pending ones while it waits. */
        CHECK(!same(kst_prog_draft(&g_p), &before));
        CHECK(!g_p.soft);
        if (s == (unsigned)RS_ABORT) {
            frames(20);
            kst_prog_abort(&g_p);
            frame();
        } else if (s == (unsigned)RS_LINK_LOST) {
            frames(20);
            kst_link_lost(&g_k);
            g_safe = false;
            frame();
        } else {
            frames(KST_PROG_CARD_MS + KST_PROG_START_MS + 100u);
        }
        CHECK(kst_prog_state(&g_p) != KST_PROG_WORKING);
        switch ((rs_t)s) {
        case RS_FILE_FAILS:
        case RS_FILE_SILENT:
            CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
            CHECK_EQ(g_fc.saves, saves);
            CHECK(g_file_writes > files);
            break;
        case RS_NOT_HANDED:
        case RS_STALLED:
            CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
            CHECK_EQ(g_fc.saves, saves);
            break;
        case RS_ABORT:
            CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_DROPPED);
            break;
        default:
            CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER);
            break;
        }
        /* Nothing was sent, and the queue holds nothing that is. */
        g_deaf = false;
        g_stall = false;
        frames(100);
        if (g_card_silent) {
            /* The card's answer, long after it was given up. */
            kst_prog_file_done(&g_p, false);
        }
        g_card_fails = false;
        g_card_silent = false;
        frames(KST_PROG_STORE_MS + 2100u);
        CHECK_EQ(g_starts, starts);
        CHECK_EQ(g_sim.n_writes, writes);
        /* As before the confirm. */
        CHECK(kst_prog_journal_held(&g_p));
        CHECK(same(&g_p.journal_target, &record));
        CHECK(text_has_target(&record));
        if (s != (unsigned)RS_LINK_LOST) {
            CHECK(same(kst_prog_draft(&g_p), &before));
            CHECK_EQ(kst_prog_changes(&g_p), changes);
            CHECK(g_p.soft);
            CHECK(!g_p.soft_load);
            CHECK(!kst_prog_in_doubt(&g_p));
            CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
            /* The prompt takes its answer. */
            kst_prog_soft_confirm(&g_p);
            CHECK_EQ(kst_prog_changes(&g_p), changes + 1u);
        }
    }
}

/* The same for the two writes that send no pending value: the prompt that
 * was open is open again. */
TEST_CASE(a_release_or_a_session_restore_that_never_starts_keeps_the_prompt)
{
    for (unsigned s = 0; s < 2u; ++s) {
        kst_prog_restore_t plan;
        kst_image_t before;
        unsigned starts;

        up_enabled();
        if (s == 1u) {
            edit(KST_F_BOOST, (uint16_t)(draft(KST_F_BOOST) + 1u));
            CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
            settle();
            CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        }
        edit(KST_F_BOOST, (uint16_t)(draft(KST_F_BOOST) + 1u));
        CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY,
                               (uint16_t)(draft(KST_F_DUTY) + 2u)),
                 KST_PROG_EDIT_SOFT);
        before = *kst_prog_draft(&g_p);
        starts = g_starts;
        g_card_fails = true;
        if (s == 0u) {
            CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NONE);
        } else {
            CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION,
                                           &plan), KST_PROG_MSG_NONE);
            CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
        }
        CHECK(!g_p.soft);
        frames(20);
        CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
        CHECK_EQ(g_starts, starts);
        CHECK(g_p.soft);
        CHECK(same(kst_prog_draft(&g_p), &before));
        g_card_fails = false;
    }
}

/* ----------------------------------------- the servo a write belongs to */

/*
 * The session's backup goes to the servo it was read from.  Another servo
 * plugged in between differs in registers no write of the session
 * changed: the restore ends before its first write, the write switch is
 * off, and a read comes before anything else.  Read or not, no later
 * restore writes to it either.
 */
TEST_CASE(a_session_restore_does_not_reach_a_swapped_servo)
{
    static const uint8_t other[] = { 0x1E, 0x1F, 0x15, 0x1D, 0x0E };
    kst_prog_restore_t plan;
    kst_image_t b, now;
    unsigned writes;

    up_enabled();
    edit(KST_F_DEAD_BAND, (uint16_t)(draft(KST_F_DEAD_BAND) - 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    /* The second servo. */
    for (size_t i = 0; i < sizeof(other); ++i) {
        g_sim.reg[other[i]] ^= 0x21u;
    }
    b = servo();
    writes = g_sim.n_writes;
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_CHANGED);
    CHECK_EQ(g_sim.n_writes, writes);
    now = servo();
    CHECK(same(&now, &b));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NEED_READ);
    /* The plan made before is not taken either. */
    (void)kst_prog_enable(&g_p, true);
    (void)kst_prog_restore(&g_p, &plan);
    settle();
    CHECK_EQ(g_sim.n_writes, writes);
    /* Read: it is another servo, and the backup is not written to it. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_write_enabled(&g_p));
    for (unsigned again = 0; again < 2u; ++again) {
        (void)kst_prog_enable(&g_p, true);
        if (kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan)
            == KST_PROG_MSG_NONE) {
            (void)kst_prog_restore(&g_p, &plan);
        }
        settle();
        CHECK_EQ(g_sim.n_writes, writes);
        now = servo();
        CHECK(same(&now, &b));
        (void)kst_prog_read(&g_p);
        settle();
    }
}

/*
 * A write that ends with the servo in doubt switches the write off, and
 * the read that follows does not switch it on.  The session's backup and
 * its file stay the ones of the servo as found: no file of the servo as
 * the write left it takes their place, and the session's restore brings
 * the servo back.
 */
TEST_CASE(a_write_in_doubt_ends_the_write_switch_and_keeps_the_backup)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    char name[32];
    unsigned saves;
    kst_image_t now;

    up_enabled();
    edit(KST_F_SOFT_START,
         1u - kst_field_get(kst_prog_servo(&g_p), KST_F_SOFT_START));
    edit(KST_F_BOOST, (uint16_t)(draft(KST_F_BOOST) - 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == 0u; ++i) {
        frame();
    }
    CHECK(g_sim.n_writes > 0u);
    kst_prog_abort(&g_p);
    settle();
    now = servo();
    CHECK(!same(&now, &sv));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(kst_prog_backed_up(&g_p));
    snprintf(name, sizeof(name), "%s", kst_prog_backup_name(&g_p));
    CHECK(name[0] != '\0');
    saves = g_fc.saves;

    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), name);
    CHECK(!kst_prog_save_first(&g_p));
    /* The session's restore brings the servo back as found, and files
     * nothing on its way. */
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK_EQ(g_fc.saves, saves);
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), name);
    for (unsigned i = 0; i < g_fc.n; ++i) {
        CHECK(same(&g_fc.file[i].img, &sv));
    }
}

/*
 * A locked session takes its restore with no read before it.  The servo
 * is not known then, and the restore is recorded all the same: cut, the
 * record names the backup as what the servo is to hold.
 */
TEST_CASE(a_session_restore_without_a_read_is_recorded)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    unsigned writes;

    up_enabled();
    edit(KST_F_DUTY, 200);
    sim_add_fault(&g_sim, SIM_WRITE, 0x02, 0, 100, SIM_F_WRITE_IGNORE, 0);
    sim_add_fault(&g_sim, SIM_WRITE, 0x01, 1, 100, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    if (msg() != KST_PROG_MSG_SES + KST_SES_ERR_TORN) {
        /* The planner wrote 02 first: bend the other way round. */
        up_enabled();
        edit(KST_F_DUTY, 200);
        sim_add_fault(&g_sim, SIM_WRITE, 0x01, 0, 100, SIM_F_WRITE_IGNORE,
                      0);
        sim_add_fault(&g_sim, SIM_WRITE, 0x02, 1, 100, SIM_F_WRITE_IGNORE,
                      0);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
    }
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_TORN);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    g_sim.n_faults = 0;
    /* With no record of the write left, the restore makes its own. */
    kst_prog_forget(&g_p);
    frames(5);
    CHECK(!kst_prog_journal_held(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    writes = g_sim.n_writes;
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == writes; ++i) {
        frame();
    }
    CHECK(g_sim.n_writes > writes);
    kst_link_lost(&g_k);
    g_safe = false;
    frame();
    CHECK(kst_prog_journal_held(&g_p));
    CHECK(kst_prog_journal(&g_p));
    CHECK(same(&g_p.journal_img, &sv));
    CHECK(same(&g_p.journal_target, &sv));
    frames(5);
    CHECK(strstr(g_file, "\nport=0\n") != NULL);
}

/* The same when the write is complete on the servo and its end is not
 * read: the record asks, and discarded, the backup is still the servo as
 * found. */
TEST_CASE(a_write_whose_end_is_not_read_keeps_the_backup)
{
    const kst_image_t sv = sim_bench_image();
    char name[32];
    unsigned n, saves;
    bool found = false;

    for (n = 0; n < 4000u && !found; ++n) {
        up_enabled();
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        g_nacked = false;
        g_nack_at = (int)n;
        settle();
        g_nack_at = -1;
        if (!g_nacked) {
            break;
        }
        found = g_outcome.kind == KST_LINK_OUT_NACK && g_outcome.taken
            && kst_prog_state(&g_p) == KST_PROG_PROGRAMMING
            && servo().r[KST_REG_DUTY] == 200u;
    }
    CHECK(found);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    snprintf(name, sizeof(name), "%s", kst_prog_backup_name(&g_p));
    CHECK(name[0] != '\0');
    saves = g_fc.saves;
    /* The session's restore waits for the read: what the servo holds is
     * not known. */
    {
        kst_prog_restore_t plan;

        CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
        CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
                 KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_enable(&g_p, false), KST_PROG_MSG_NONE);
    }
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DONE);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), name);
    CHECK(!kst_prog_save_first(&g_p));
    kst_prog_forget(&g_p);
    CHECK(!kst_prog_save_first(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 210);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, saves);
    for (unsigned i = 0; i < g_fc.n; ++i) {
        CHECK(same(&g_fc.file[i].img, &sv));
    }
}

/*
 * The record of a servo of the same type with other settings is not this
 * servo's: a register that holds neither the record's value nor the
 * write's target says so, and nothing of the record reaches the servo.
 */
TEST_CASE(a_record_of_a_servo_with_other_settings_is_not_restored)
{
    const kst_image_t b = sim_bench_image();
    kst_image_t a = b, target, now;
    kst_prog_restore_t plan;
    char text[KST_PROG_FILE_MAX];
    unsigned writes;

    CHECK(kst_field_edit(&a, KST_F_NEUTRAL,
                         (uint16_t)(kst_field_get(&b, KST_F_NEUTRAL) + 40u)));
    CHECK(kst_field_edit(&a, KST_F_REVERSION,
                         (uint16_t)(1u - kst_field_get(&b, KST_F_REVERSION))));
    target = a;
    CHECK(kst_field_edit(&target, KST_F_BOOST, 10));
    up();
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH;
    g_p.journal_img = a;
    g_p.journal_target = target;
    CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
    fresh_at(1000);
    up();
    prog_fresh();
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    frame();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
    CHECK(kst_prog_journal_other(&g_p));
    CHECK_EQ(kst_prog_journal_asks(&g_p), KST_PROG_MSG_NONE);
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    writes = g_sim.n_writes;
    (void)kst_prog_enable(&g_p, true);
    CHECK(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan)
          != KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(g_sim.n_writes, writes);
    now = servo();
    CHECK(same(&now, &b));
    CHECK_EQ(g_fc.n, 0);
}

/* The servo of the write, cut between two registers of one field or in
 * the middle of its plan, is still this record's. */
TEST_CASE(a_servo_cut_in_its_write_fits_its_record)
{
    for (unsigned duty = 0; duty < 2u; ++duty) {
        cut_and_come_back(duty != 0u);
        CHECK(!kst_prog_journal_other(&g_p));
        CHECK(kst_prog_journal_held(&g_p));
        CHECK_EQ(kst_prog_journal_asks(&g_p), KST_PROG_MSG_NONE);
        CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    }
}

/* A restore from the record writes no file of the half-written servo. */
TEST_CASE(a_restore_from_the_record_files_no_half_written_servo)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t torn, now;
    int files;

    cut_and_come_back(false);
    torn = servo();
    CHECK(!same(&torn, &sv));
    files = g_fc.n;
    CHECK(!kst_prog_save_first(&g_p));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    frames(50);
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK_EQ(g_fc.n, files);
    for (unsigned i = 0; i < g_fc.n; ++i) {
        CHECK(!same(&g_fc.file[i].img, &torn));
    }
}

/* A record made while no card was in reaches the card that comes. */
TEST_CASE(a_record_reaches_a_card_put_in_later)
{
    char text[KST_PROG_FILE_MAX];
    unsigned writes;

    up_enabled();
    g_card = false;
    frames(3);
    edit(KST_F_SOFT_START,
         1u - kst_field_get(kst_prog_servo(&g_p), KST_F_SOFT_START));
    edit(KST_F_BOOST, 10);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == 0u; ++i) {
        frame();
    }
    CHECK(g_sim.n_writes > 0u);
    kst_link_lost(&g_k);
    g_safe = false;
    frame();
    CHECK(kst_prog_journal_held(&g_p));
    frames(100);
    writes = g_file_writes;
    g_card = true;
    frames(100);
    CHECK(g_file_writes > writes);
    CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
    CHECK_STR_EQ(g_file, text);
    CHECK(strstr(g_file, "\nport=") != NULL);
}

/* A record's port is one of the bench's. */
TEST_CASE(a_record_with_a_port_the_bench_has_not_is_refused)
{
    char text[KST_PROG_FILE_MAX];

    for (unsigned port = 0; port < 16u; ++port) {
        kst_prog_t w, p;
        size_t len;

        kst_prog_init(&w);
        w.journal = true;
        w.journal_port = (uint8_t)port;
        w.journal_img = sim_bench_image();
        w.journal_target = sim_bench_image();
        len = kst_prog_file_text(&w, text, sizeof(text));
        CHECK(len != 0u);
        kst_prog_init(&p);
        if (port < LINK_PG_PORTS_MAX) {
            CHECK(kst_prog_file_load(&p, text, len));
            CHECK_EQ(kst_prog_journal_port(&p), port);
        } else {
            CHECK(!kst_prog_file_load(&p, text, len));
            CHECK_EQ(kst_prog_message(&p)->id, KST_PROG_MSG_FILE_BAD);
            CHECK(!kst_prog_journal_held(&p));
        }
    }
}

/* ------------------------------------------- the gate of the two tasks */

TEST_CASE(a_pass_is_taken_once_and_by_its_number_only)
{
    kst_prog_gate_t g;

    kst_prog_gate_init(&g);
    CHECK(!kst_prog_gate_claim(&g, 0));
    CHECK(!kst_prog_gate_claim(&g, 1));
    CHECK(!kst_prog_gate_revoke(&g, 1));
    kst_prog_gate_offer(&g, 5);
    CHECK(!kst_prog_gate_claim(&g, 4));
    CHECK(!kst_prog_gate_claim(&g, 0));
    CHECK(!kst_prog_gate_revoke(&g, 6));
    /* The link's task first: the command is carried out. */
    CHECK(kst_prog_gate_claim(&g, 5));
    CHECK(!kst_prog_gate_claim(&g, 5));
    CHECK(!kst_prog_gate_revoke(&g, 5));
    /* The model first: it never is. */
    kst_prog_gate_offer(&g, 6);
    CHECK(kst_prog_gate_revoke(&g, 6));
    CHECK(!kst_prog_gate_claim(&g, 6));
    CHECK(!kst_prog_gate_revoke(&g, 6));
    /* A later offer ends the earlier one. */
    kst_prog_gate_offer(&g, 7);
    kst_prog_gate_offer(&g, 8);
    CHECK(!kst_prog_gate_claim(&g, 7));
    CHECK(kst_prog_gate_claim(&g, 8));
    /* A pass of the number 0 is none. */
    kst_prog_gate_offer(&g, 0);
    CHECK(!kst_prog_gate_claim(&g, 0));
    CHECK(!kst_prog_gate_revoke(&g, 0));
}

/*
 * The control task takes nothing out of its queue for longer than the wait
 * for an answer, then all of it.  The START that timed out is not carried
 * out: no kst_link_start(), no frame to the servo.  For a write and for a
 * read, given up by the timeout, by the operator and by the link.
 */
TEST_CASE(a_start_that_timed_out_in_the_queue_is_never_carried_out)
{
    for (unsigned c = 0; c < 8u; ++c) {
        const bool write = (c & 1u) != 0u;
        const unsigned how = c / 2u;    /* timeout, abort, link, next op */
        const kst_image_t sv = sim_bench_image();
        unsigned starts, frames_sent, aborts;
        kst_image_t now;

        up_enabled();
        if (write) {
            edit(KST_F_DUTY, 200);
            CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        } else {
            CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        }
        g_stall = true;
        starts = g_starts;
        aborts = g_aborts;
        frames(10);             /* the card's files, the command queued */
        CHECK_EQ(g_qn, 1);
        CHECK_EQ(g_q[0].kind, KST_PROG_CMD_START);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
        frames_sent = g_sim.n_frames;
        if (how == 1u) {
            kst_prog_abort(&g_p);
            frame();
            CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_DROPPED);
        } else if (how == 2u) {
            kst_link_lost(&g_k);
            g_safe = false;
            frame();
            CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER);
            g_safe = true;
            kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
            frames(50);
        } else {
            frames(KST_PROG_START_MS - 20u);
            CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
            frames(40);
            CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
            frames(3000);       /* and it stalls on */
        }
        CHECK(kst_prog_state(&g_p) != KST_PROG_WORKING);
        /* Nothing was sent: the servo is as it was read. */
        CHECK(!kst_prog_journal(&g_p));
        CHECK_EQ(g_p.wrote, 0);
        if (how != 2u) {
            CHECK(!kst_prog_in_doubt(&g_p));
            CHECK(kst_prog_editable(&g_p));
        }
        if (how == 3u) {
            /* The next operation is asked for while the queue stands. */
            CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
            frames(5);
            CHECK_EQ(g_qn, 2);
        }
        /* The queue moves again. */
        g_stall = false;
        frames(200);
        settle();
        CHECK_EQ(g_dropped, 1);
        CHECK_EQ(g_aborts, aborts);
        now = servo();
        CHECK(same(&now, &sv));
        CHECK_EQ(g_sim.n_writes, 0);
        if (how == 3u) {
            CHECK_EQ(g_starts, starts + 1u);
            CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);
        } else if (how != 2u) {
            CHECK_EQ(g_starts, starts);
            CHECK_EQ(g_sim.n_frames, frames_sent);
        } else {
            CHECK_EQ(g_starts, starts);
        }
        if (how == 0u && write) {
            /* The value is pending still, and the write goes when asked
             * again. */
            CHECK_EQ(draft(KST_F_DUTY), 200);
            CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
            CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
            settle();
            CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
            CHECK_EQ(g_sim.reg[KST_REG_DUTY], 200);
        }
    }
}

/* With no gate between the tasks the same START may have run: the servo is
 * in doubt and a read is asked for. */
TEST_CASE(without_a_gate_an_unanswered_start_leaves_the_servo_in_doubt)
{
    kst_prog_cmd_t cmd;

    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    pass(KST_PROG_START_MS);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
}

/* --------------------------------- a record from a card mounted late */

static size_t record_text(char *text, uint8_t port, const kst_image_t *img,
                          const kst_image_t *target)
{
    kst_prog_t t;

    kst_prog_init(&t);
    t.journal = true;
    t.journal_open = true;
    t.journal_port = port;
    t.journal_img = *img;
    t.journal_target = *target;
    return kst_prog_file_text(&t, text, KST_PROG_FILE_MAX);
}

static bool record_is(const char *text)
{
    char now[KST_PROG_FILE_MAX];

    return kst_prog_file_text(&g_p, now, sizeof(now)) != 0u
        && strcmp(now, text) == 0;
}

/*
 * The record of an interrupted write reaches the model before the servo is
 * read (the card was in at the start) or after it (the card is mounted
 * under the session).  Either way it is held against the servo read: the
 * servo's own record is settled by a read, and the record of another port
 * or of another servo stands, takes no restore and lets no write through.
 */
TEST_CASE(a_record_is_held_against_the_servo_whenever_the_card_comes)
{
    for (unsigned c = 0; c < 6u; ++c) {
        const unsigned who = c / 2u;    /* this servo, a port, a servo */
        const bool late = (c & 1u) != 0u;
        kst_image_t img = sim_bench_image();
        kst_image_t target;
        kst_prog_restore_t plan;
        char text[KST_PROG_FILE_MAX];
        size_t len;

        up();
        if (who == 2u) {
            CHECK(kst_field_set(&img, KST_F_20K_SEL,
                                1u - kst_field_get(&img, KST_F_20K_SEL)));
        }
        target = img;
        CHECK(kst_field_edit(&target, KST_F_BOOST,
                             (uint16_t)(kst_field_get(&img, KST_F_BOOST)
                                        + 1u)));
        len = record_text(text, (uint8_t)(who == 1u ? CH + 1u : CH), &img,
                          &target);
        CHECK(len != 0u);
        /* The panel starts under the session the IOmcu keeps. */
        prog_fresh();
        if (!late) {
            CHECK(kst_prog_file_load(&g_p, text, len));
            CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
            frame();
            CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
            settle();
        } else {
            frame();
            CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
            settle();
            CHECK_EQ(msg(), KST_PROG_MSG_READ);
            CHECK(kst_prog_editable(&g_p));
            CHECK(kst_prog_file_load(&g_p, text, len));
            if (who == 0u) {
                /* This servo's: what it holds now is not known to be what
                 * was read before the record came. */
                CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
                CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
                CHECK(kst_prog_in_doubt(&g_p));
                CHECK(!kst_prog_editable(&g_p));
                CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, 10),
                         KST_PROG_EDIT_REFUSED);
                CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
                CHECK(record_is(text));
                CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
                settle();
            }
        }
        if (who == 0u) {
            /* The servo holds the record's image: the record stands and
             * asks whether this is the servo. */
            CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_SAME);
            CHECK_EQ(kst_prog_journal_asks(&g_p), KST_PROG_MSG_JOURNAL_SAME);
            CHECK(kst_prog_journal_held(&g_p));
            CHECK(!kst_prog_journal_other(&g_p));
            CHECK(!kst_prog_in_doubt(&g_p));
            CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_JOURNAL_SAME);
            continue;
        }
        CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
        CHECK_EQ(kst_prog_message(&g_p)->arg, (who == 1u ? CH + 2u : CH + 1u));
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
        CHECK(kst_prog_journal(&g_p));
        CHECK(kst_prog_journal_other(&g_p));
        CHECK(kst_prog_in_doubt(&g_p));
        CHECK(!kst_prog_editable(&g_p));
        CHECK(!kst_prog_write_enabled(&g_p));
        for (unsigned from = 0; from < (unsigned)KST_PROG_FROM_COUNT; ++from) {
            CHECK(!kst_prog_restore_offered(&g_p, (kst_prog_from_t)from));
            CHECK_EQ(kst_prog_restore_plan(&g_p, (kst_prog_from_t)from,
                                           &plan),
                     KST_PROG_MSG_JOURNAL_OTHER);
            plan.from = (uint8_t)from;
            CHECK_EQ(kst_prog_restore(&g_p, &plan),
                     KST_PROG_MSG_JOURNAL_OTHER);
        }
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
        CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
        frames(50);
        CHECK_EQ(g_sim.n_writes, 0);
        /* The record is the one the card gave, to the byte. */
        CHECK(record_is(text));
        /* A read changes nothing of that. */
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
        CHECK(record_is(text));
    }
}

/*
 * A locked session takes its restore with no servo read.  Such a restore
 * has no image to tell whose record the model holds: it makes no record of
 * its own, rewrites none and clears none.  The image read at its end says
 * whose the record is.
 */
TEST_CASE(a_restore_with_no_servo_read_leaves_the_record_it_did_not_make)
{
    static const kst_link_outcome_kind_t kinds[] = {
        KST_LINK_OUT_DONE, KST_LINK_OUT_DROPPED, KST_LINK_OUT_REFUSED,
        KST_LINK_OUT_NO_ANSWER,
    };

    for (unsigned c = 0; c < 3u * sizeof(kinds) / sizeof(kinds[0]); ++c) {
        const unsigned who = c % 3u;    /* this servo, a port, a servo */
        const kst_link_outcome_kind_t kind = kinds[c / 3u];
        kst_image_t img = sim_bench_image();
        kst_image_t target;
        kst_prog_restore_t plan;
        kst_link_outcome_t o;
        char text[KST_PROG_FILE_MAX];
        char out[KST_PROG_FILE_MAX];
        size_t len;

        by_hand();
        g_p.have_servo = false;
        g_f.have_image = false;
        g_f.card = true;
        g_f.rd.flags = LINK_KS_F_IN_MODE | LINK_KS_F_BACKUP | LINK_KS_F_LOCKED;
        pass(1);
        while (kst_prog_take(&g_p, &(kst_prog_cmd_t){ 0 })) {
        }
        if (who == 2u) {
            CHECK(kst_field_set(&img, KST_F_20K_SEL,
                                1u - kst_field_get(&img, KST_F_20K_SEL)));
        }
        target = img;
        CHECK(kst_field_edit(&target, KST_F_BOOST,
                             (uint16_t)(kst_field_get(&img, KST_F_BOOST)
                                        + 1u)));
        len = record_text(text, (uint8_t)(who == 1u ? CH + 1u : CH), &img,
                          &target);
        CHECK(kst_prog_file_load(&g_p, text, len));
        CHECK(kst_prog_journal(&g_p));
        CHECK(!kst_prog_journal_other(&g_p));

        CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
        CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
                 KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
        /* No file of the record goes to the card, and the command does. */
        CHECK_EQ(kst_prog_file_take(&g_p, out, sizeof(out)), 0);
        CHECK(record_is(text));
        answer(KST_LINK_START_OK);
        CHECK(record_is(text));

        memset(&o, 0, sizeof(o));
        o.kind = kind;
        o.op = LINK_KST_OP_RESTORE;
        o.result = KST_SES_OK;
        o.steps_done = 3;
        o.taken = true;
        o.fail_reg = 0xFF;
        if (kind == KST_LINK_OUT_DONE) {
            /* The servo holds the session's backup, and it is read. */
            g_f.have_image = true;
            g_f.image = g_f.backup;
            g_f.rd.flags = LINK_KS_F_IN_MODE | LINK_KS_F_IMAGE
                         | LINK_KS_F_BACKUP | LINK_KS_F_FP_OK;
        }
        end(&o);
        if (kind != KST_LINK_OUT_DONE) {
            /* As it was. */
            CHECK(kst_prog_journal(&g_p));
            CHECK(record_is(text));
            continue;
        }
        CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
        CHECK(kst_prog_have_servo(&g_p));
        if (who == 0u) {
            /* This servo's, and it holds the record's image again. */
            CHECK(!kst_prog_journal(&g_p));
            CHECK(!kst_prog_in_doubt(&g_p));
        } else {
            CHECK(kst_prog_journal(&g_p));
            CHECK(kst_prog_journal_other(&g_p));
            CHECK(record_is(text));
            CHECK(!kst_prog_write_enabled(&g_p));
            CHECK_EQ(kst_prog_file_take(&g_p, out, sizeof(out)), 0);
        }
    }
}

/* ----------------------------------- soft rules against the session's image */

/*
 * The session's restore on a locked session with no servo read records the
 * pin its port has at the restore: GP13 as the feed gives it.  A record
 * discarded before leaves no pin behind, and a binding that moved the port
 * since is the one recorded; a port with no pin records none.
 */
TEST_CASE(a_locked_restore_with_no_servo_read_records_the_pin_of_its_port)
{
    static const struct {
        bool discarded;     /* a record of GP13 discarded first */
        bool known;         /* the feed has a pin for the port  */
        uint8_t pin;        /* and which                        */
        uint8_t want;
        const char *line;
    } k[] = {
        { false, true,  13u, 13u,             "\nport=0\npin=0D\ncheck=" },
        { true,  true,  9u,  9u,              "\nport=0\npin=09\ncheck=" },
        { true,  false, 13u, KST_PROG_NO_PIN, "\nport=0\npin=FF\ncheck=" },
    };

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        kst_prog_restore_t plan;
        char out[KST_PROG_FILE_MAX];
        size_t n;

        by_hand();
        g_p.have_servo = false;
        g_f.have_image = false;
        g_f.card = true;
        g_f.port_pin[CH] = 13u;
        g_f.port_pin_known = (uint8_t)(1u << CH);
        g_f.rd.flags = LINK_KS_F_IN_MODE | LINK_KS_F_BACKUP | LINK_KS_F_LOCKED;
        pass(1);
        while (kst_prog_take(&g_p, &(kst_prog_cmd_t){ 0 })) {
        }
        if (k[i].discarded) {
            kst_prog_t rec;
            char text[KST_PROG_FILE_MAX];
            size_t len;

            kst_prog_init(&rec);
            rec.journal = true;
            rec.journal_open = true;
            rec.journal_port = CH;
            rec.journal_pin = 13u;
            rec.journal_img = sim_bench_image();
            rec.journal_target = rec.journal_img;
            len = kst_prog_file_text(&rec, text, sizeof(text));
            CHECK(len != 0u);
            CHECK(kst_prog_file_load(&g_p, text, len));
            CHECK_EQ(kst_prog_journal_pin(&g_p), 13u);
            kst_prog_forget(&g_p);
            CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
            CHECK(!kst_prog_journal(&g_p));
            CHECK_EQ(kst_prog_journal_pin(&g_p), KST_PROG_NO_PIN);
            /* The card takes the text with no record. */
            CHECK(kst_prog_file_take(&g_p, out, sizeof(out)) != 0u);
            kst_prog_file_done(&g_p, true);
            g_f.port_pin[CH] = k[i].pin;
            g_f.port_pin_known = k[i].known ? (uint8_t)(1u << CH) : 0u;
            pass(1);
        }
        CHECK(!kst_prog_journal(&g_p));
        CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
        CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
                 KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
        CHECK(kst_prog_journal(&g_p));
        CHECK_EQ(kst_prog_journal_pin(&g_p), k[i].want);
        n = kst_prog_file_take(&g_p, out, sizeof(out));
        CHECK(n > 0u);
        out[n < sizeof(out) ? n : sizeof(out) - 1u] = 0;
        CHECK(strstr(out, k[i].line) != NULL);
    }
}

TEST_CASE(a_confirmed_soft_limit_is_asked_again_further_out)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    const uint16_t ang = kst_field_get(&sv, KST_F_LEFT_ANGLE);

    up();
    CHECK(duty + 5u <= KST_DUTY_RAW_MAX);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 3u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_confirm(&g_p);
    /* Up to the value confirmed nothing is asked. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 3u)),
             KST_PROG_EDIT_OK);
    /* One count beyond it the rule is asked again, and a cancel leaves
     * the value confirmed where it was. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 4u)),
             KST_PROG_EDIT_SOFT);
    CHECK_EQ(kst_prog_soft_rules(&g_p), KST_RULE_BIT(KST_S_DUTY_RAISED));
    kst_prog_soft_cancel(&g_p);
    CHECK_EQ(draft(KST_F_DUTY), duty + 3u);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 5u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 4u)),
             KST_PROG_EDIT_OK);
    /* Another field's value moves freely below its rule and is asked at
     * every count above the servo's. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_LEFT_ANGLE, (uint16_t)(ang - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_LEFT_ANGLE, (uint16_t)(ang + 1u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_LEFT_ANGLE, 255),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_cancel(&g_p);
    /* DISCARD: no value counts as confirmed. */
    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_cancel(&g_p);
}

TEST_CASE(a_value_on_the_other_side_of_the_servos_is_asked_again)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t n0 = kst_field_get(&sv, KST_F_NEUTRAL);

    /* 1 count of Neutral is 0.2us: 110 counts are 22us. */
    up();
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NEUTRAL, (uint16_t)(n0 + 110u)),
             KST_PROG_EDIT_SOFT);
    CHECK((kst_prog_soft_rules(&g_p) & KST_RULE_BIT(KST_S_NEUTRAL_MOVED))
          != 0u);
    kst_prog_soft_confirm(&g_p);
    /* Nearer on the other side: the rule stands broken and is asked. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NEUTRAL, (uint16_t)(n0 - 105u)),
             KST_PROG_EDIT_SOFT);
    CHECK((kst_prog_soft_rules(&g_p) & KST_RULE_BIT(KST_S_NEUTRAL_MOVED))
          != 0u);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NEUTRAL, (uint16_t)(n0 - 104u)),
             KST_PROG_EDIT_OK);
}

TEST_CASE(the_soft_limits_compare_with_the_servo_as_the_session_found_it)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t n0 = kst_field_get(&sv, KST_F_NEUTRAL);
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    /* Neutral moved by 16us, under the 20us of the rule, and written. */
    up_enabled();
    CHECK(same(kst_prog_soft_base(&g_p), &sv));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NEUTRAL, (uint16_t)(n0 + 80u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_field_get(kst_prog_servo(&g_p), KST_F_NEUTRAL), n0 + 80u);
    /* The base is the servo as found, not as last read. */
    CHECK(same(kst_prog_soft_base(&g_p), &sv));
    /* 16us more are 32us from where the session began: asked. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NEUTRAL, (uint16_t)(n0 + 160u)),
             KST_PROG_EDIT_SOFT);
    CHECK_EQ(kst_prog_soft_rules(&g_p), KST_RULE_BIT(KST_S_NEUTRAL_MOVED));
    kst_prog_soft_cancel(&g_p);
    /* Back towards the servo as found: nothing to ask. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NEUTRAL, (uint16_t)(n0 + 40u)),
             KST_PROG_EDIT_OK);
    kst_prog_discard(&g_p);

    /* A raise written under its confirmation: what the servo holds counts
     * as confirmed, one count more is asked. */
    edit(KST_F_DUTY, (uint16_t)(duty + 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST,
                           (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 2u)),
             KST_PROG_EDIT_SOFT);
    CHECK_EQ(kst_prog_soft_rules(&g_p), KST_RULE_BIT(KST_S_DUTY_RAISED));
    kst_prog_soft_cancel(&g_p);
    /* The servo as found is no raise. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, duty), KST_PROG_EDIT_OK);
}

TEST_CASE(another_servo_is_compared_with_its_own_image)
{
    const kst_image_t sv = sim_bench_image();

    up_enabled();
    edit(KST_F_BOOST, (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    g_sim.reg[KST_REG_DUTY] = 190;
    g_sim.reg[KST_REG_DUTY_COPY] = 190;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_backup_other(&g_p));
    CHECK(same(kst_prog_soft_base(&g_p), kst_prog_servo(&g_p)));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 191), KST_PROG_EDIT_SOFT);
    kst_prog_soft_cancel(&g_p);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 189), KST_PROG_EDIT_OK);
}

TEST_CASE(the_fields_at_fault_for_a_soft_limit_are_named)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    kst_prog_t none;

    kst_prog_init(&none);
    CHECK_EQ(kst_prog_soft_fields(&none, &sv), 0);
    up();
    CHECK_EQ(kst_prog_soft_fields(&g_p, kst_prog_draft(&g_p)), 0);
    edit(KST_F_DUTY, (uint16_t)(duty + 1u));
    edit(KST_F_BOOST, (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u));
    /* Boost lowered breaks nothing; Max. Duty raised does. */
    CHECK_EQ(kst_prog_soft_fields(&g_p, kst_prog_draft(&g_p)),
             KST_FIELD_BIT(KST_F_DUTY));
    /* Of the edit that waits, as of the draft. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_LEFT_ANGLE,
                           (uint16_t)(kst_field_get(&sv, KST_F_LEFT_ANGLE)
                                      + 1u)),
             KST_PROG_EDIT_SOFT);
    CHECK_EQ(kst_prog_soft_fields(&g_p, kst_prog_soft_image(&g_p)),
             KST_FIELD_BIT(KST_F_DUTY) | KST_FIELD_BIT(KST_F_LEFT_ANGLE));
    kst_prog_soft_cancel(&g_p);
}

/* ------------------------------------------------- messages that went stale */

TEST_CASE(a_message_goes_when_what_it_says_no_longer_holds)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = kst_field_get(&sv, KST_F_BOOST);
    uint32_t seq;

    up();
    edit(KST_F_BOOST, (uint16_t)(boost - 1u));
    /* The switch is off: said, and gone once it is on. */
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    seq = kst_prog_message(&g_p)->seq;
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    CHECK(kst_prog_message(&g_p)->seq != seq);
    /* Nothing to clear: the count stays. */
    seq = kst_prog_message(&g_p)->seq;
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_message(&g_p)->seq, seq);

    /* "n values written" beside a new edit. */
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, (uint16_t)(boost - 2u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);

    /* An edit that is refused says why, and DISCARD clears it. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_R06, 1), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    kst_prog_discard(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);

    /* An edit taken through its confirmation clears as well. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_R06, 1), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY,
                           (uint16_t)(kst_field_get(&sv, KST_F_DUTY) + 1u)),
             KST_PROG_EDIT_SOFT);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
}

TEST_CASE(a_message_of_the_danger_level_stays_over_an_edit)
{
    const kst_image_t sv = sim_bench_image();

    by_hand();
    g_p.msg.id = (uint8_t)(KST_PROG_MSG_SES + KST_SES_ERR_VERIFY);
    g_p.msg.level = KST_PROG_DANGER;
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST,
                           (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_VERIFY);
    kst_prog_discard(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_VERIFY);
}

/* ------------------------------------------------------- a write that runs */

TEST_CASE(a_write_that_runs_is_no_interrupted_write)
{
    const kst_image_t sv = sim_bench_image();
    unsigned seen = 0;

    up_enabled();
    CHECK(!kst_prog_writing(&g_p));
    CHECK_EQ(kst_prog_write_steps(&g_p), 0);
    /* Nothing runs and no record is held: FORGET says nothing. */
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    edit(KST_F_BOOST, (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u));
    edit(KST_F_NEUTRAL, (uint16_t)(kst_field_get(&sv, KST_F_NEUTRAL) + 5u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 60000u && !at_rest(); ++i) {
        frame();
        if (kst_prog_state(&g_p) != KST_PROG_WORKING) {
            continue;
        }
        seen++;
        CHECK(kst_prog_writing(&g_p));
        /* Boost is 1 register, Neutral here 1 of its 2. */
        CHECK_EQ(kst_prog_write_steps(&g_p), 2);
        if (seen == 5u) {
            /* The record of this write is held and is not one to
             * discard. */
            CHECK(kst_prog_journal(&g_p));
            kst_prog_forget(&g_p);
            CHECK_EQ(msg(), KST_PROG_MSG_START + KST_LINK_START_BUSY);
            CHECK(kst_prog_journal(&g_p));
        }
    }
    CHECK(seen > 5u);
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(!kst_prog_writing(&g_p));
    CHECK_EQ(kst_prog_write_steps(&g_p), 0);
    CHECK(!kst_prog_journal(&g_p));

    /* A read is no write. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK(!kst_prog_writing(&g_p));
    settle();
}

TEST_CASE(a_restore_counts_its_registers_where_its_order_is_checked)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;

    up_enabled();
    edit(KST_F_BOOST, (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write_steps(&g_p), 1);
    settle();
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK(!plan.unchecked);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    CHECK(kst_prog_writing(&g_p));
    CHECK_EQ(kst_prog_write_steps(&g_p), plan.writes);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    /* A release is planned by the IOmcu alone. */
    if (kst_prog_release(&g_p) == KST_PROG_MSG_NONE) {
        CHECK(kst_prog_writing(&g_p));
        CHECK_EQ(kst_prog_write_steps(&g_p), 0);
        settle();
    }
}

/* -------------------------------- a servo that breaks a hard rule as found */

TEST_CASE(a_servo_that_breaks_a_hard_limit_is_written_once_the_value_is_mended)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = kst_field_get(&sv, KST_F_BOOST);

    fresh_at(0u);
    g_sim.reg[0x10] = 0;               /* Left Angle 0 */
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    /* Another field is edited: the write names the rule the servo breaks,
     * and nothing is sent. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, (uint16_t)(boost - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_LEFT_ZERO);
    CHECK_EQ(g_sim.n_writes, 0);
    /* The value mended, the write goes with both. */
    edit(KST_F_LEFT_ANGLE, 100);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_sim.reg[0x10], 100);
    CHECK_EQ(g_sim.reg[0x03], boost - 1u);
}

int main(void)
{
    RUN(a_connect_enters_and_reads_the_servo);
    RUN(a_coprocessor_without_the_page_is_named);
    RUN(the_port_says_why_a_port_is_not_taken);
    RUN(no_servo_on_the_port_is_a_message);
    RUN(a_connect_needs_the_port_as_the_testers_target);
    RUN(a_connect_sets_the_supply_it_was_given);
    RUN(an_output_on_under_the_floor_is_refused_before_the_port_is_asked);
    RUN(a_supply_outside_the_ports_range_is_refused_by_name);
    RUN(a_supply_that_does_not_switch_on_ends_the_connect_with_result_32);
    RUN(a_leave_goes_back_to_pwm_and_switches_no_supply);
    RUN(a_leave_with_a_pd_mini_keeps_the_supply_on);
    RUN(the_wait_before_pwm_is_the_leaves_wait);
    RUN(a_wait_above_the_range_is_not_sent);
    RUN(an_abort_in_the_wait_keeps_the_session);
    RUN(a_disarm_in_the_wait_keeps_the_port_and_asks_for_a_read);
    RUN(a_supply_that_goes_off_in_the_wait_ends_in_pwm_with_it_off);
    RUN(a_coprocessor_without_leave_is_named_and_cycles_no_supply);
    RUN(a_leave_the_port_refuses_keeps_the_session);
    RUN(a_port_in_doubt_is_left_by_leave_only_where_a_pd_mini_answers);
    RUN(the_wait_before_pwm_keeps_its_limits);
    RUN(an_edit_inside_the_limits_changes_the_draft_only);
    RUN(a_value_beyond_a_hard_limit_is_not_taken);
    RUN(a_value_beyond_a_soft_limit_waits_for_the_confirmation);
    RUN(the_range_of_a_field_is_what_the_hard_rules_take);
    RUN(a_range_is_found_from_a_draft_value_the_rules_refuse);
    RUN(a_step_moves_one_count_and_stops_at_the_fields_end);
    RUN(a_write_needs_the_switch_and_writes_the_changed_fields);
    RUN(the_first_write_of_every_session_is_the_first_again);
    RUN(a_draft_the_planner_refuses_is_not_sent);
    RUN(the_switch_goes_off_where_the_rules_say);
    RUN(leaving_gives_the_output_back_and_ends_the_session);
    RUN(a_verify_says_whether_the_servo_holds_what_was_read);
    RUN(a_servo_that_fails_the_fingerprint_is_read_only);
    RUN(a_release_of_pairing_writes_00_to_register_1d);
    RUN(the_sessions_backup_is_restored_by_the_coprocessor);
    RUN(the_sessions_backup_is_not_restored_to_another_servo);
    RUN(a_servo_put_back_has_its_backup_again);
    RUN(a_field_the_session_wrote_does_not_make_another_servo);
    RUN(a_write_the_journal_stopped_leaves_no_field_written);
    RUN(a_write_that_was_not_taken_or_was_refused_leaves_no_field_written);
    RUN(a_write_that_was_taken_has_its_fields_written);
    RUN(a_taken_write_that_wrote_nothing_leaves_no_field_written);
    RUN(a_write_that_wrote_nothing_takes_no_field_of_an_earlier_write);
    RUN(a_dropped_write_whose_answer_timed_out_leaves_no_field_written);
    RUN(a_write_stopped_or_aborted_before_its_command_leaves_no_field_written);
    RUN(a_write_aborted_on_the_wire_has_its_fields_written);
    RUN(nothing_is_sent_on_a_bench_the_feed_reports_armed);
    RUN(the_bench_takes_no_arm_while_a_port_is_out_of_pwm);
    RUN(the_port_is_held_by_the_session_or_by_the_readout);
    RUN(the_journal_reaches_the_card_before_the_write_is_sent);
    RUN(a_card_that_takes_no_journal_stops_the_write);
    RUN(a_switch_turned_off_at_the_journal_stops_the_write);
    RUN(arm_at_the_journal_stops_the_write);
    RUN(a_switch_turned_off_at_the_servos_file_stops_the_write);
    RUN(the_gate_at_the_journals_answer_is_the_gate_as_it_is_then);
    RUN(a_link_lost_in_a_write_leaves_the_servo_in_doubt);
    RUN(a_later_session_cannot_write_to_a_servo_cut_between_01_and_02);
    RUN(a_panel_that_starts_on_a_locked_session_fetches_its_backup);
    RUN(a_fetch_that_brings_nothing_is_asked_again_once_a_second);
    RUN(a_later_session_writes_the_journals_image_back);
    RUN(a_second_cut_keeps_the_image_before_the_first_write);
    RUN(a_journal_whose_servo_is_unchanged_or_complete_asks);
    RUN(a_servo_that_is_not_the_journals_takes_no_restore_and_no_write);
    RUN(a_journal_of_another_port_is_not_this_servos);
    RUN(a_restore_names_the_soft_limits_its_values_pass);
    RUN(a_coprocessor_that_starts_again_in_a_write_leaves_the_journal);
    RUN(a_write_the_servo_does_not_take_names_the_register);
    RUN(a_torn_servo_takes_the_sessions_restore_only);
    RUN(a_stop_aborts_the_operation_and_asks_for_a_read);
    RUN(a_stop_before_the_command_is_taken_sends_nothing);
    RUN(the_operators_abort_ends_an_operation_that_runs);
    RUN(every_answer_of_start_is_a_message);
    RUN(every_refusal_of_the_port_is_a_message);
    RUN(every_result_of_the_session_is_a_message);
    RUN(every_kind_of_outcome_is_a_message);
    RUN(a_read_the_link_gave_up_on_asks_for_a_read);
    RUN(a_file_the_card_did_not_take_is_written_again);
    RUN(a_plan_that_ends_without_a_plan_wrote_nothing);
    RUN(a_port_that_reports_busy_is_not_timed_out);
    RUN(a_command_nobody_answers_is_given_up);
    RUN(a_late_outcome_of_another_operation_ends_none);
    RUN(the_answer_to_an_older_file_releases_no_write);
    RUN(abort_reaches_an_operation_the_model_awaits_no_longer);
    RUN(a_session_the_port_ends_is_said);
    RUN(a_code_the_panel_does_not_know_is_said_with_its_number);
    RUN(a_port_busy_without_end_is_given_up);
    RUN(a_page_that_refuses_keeps_the_port_as_it_was_last_read);
    RUN(the_waits_hold_across_the_tick_wrap);
    RUN(the_command_counter_skips_0_when_it_wraps);
    RUN(the_state_follows_the_port);
    RUN(the_file_text_is_read_back_as_it_was_written);
    RUN(a_file_that_is_not_whole_changes_nothing);
    RUN(a_record_of_version_1_stands_and_names_its_port);
    RUN(the_record_names_a_pin_of_the_coprocessor_only);
    RUN(the_journal_records_the_pin_of_its_port_at_the_write);
    RUN(the_journal_records_a_pin_up_to_gp47_only);
    RUN(the_file_is_written_once_per_change_and_only_to_a_card);
    RUN(every_outcome_of_a_write_says_what_it_wrote);
    RUN(a_nack_after_the_command_leaves_the_image_unusable);
    RUN(a_restore_that_never_starts_leaves_the_pending_values);
    RUN(a_release_or_a_session_restore_that_never_starts_keeps_the_prompt);
    RUN(a_session_restore_does_not_reach_a_swapped_servo);
    RUN(a_write_in_doubt_ends_the_write_switch_and_keeps_the_backup);
    RUN(a_session_restore_without_a_read_is_recorded);
    RUN(a_write_whose_end_is_not_read_keeps_the_backup);
    RUN(a_record_of_a_servo_with_other_settings_is_not_restored);
    RUN(a_servo_cut_in_its_write_fits_its_record);
    RUN(a_restore_from_the_record_files_no_half_written_servo);
    RUN(a_record_reaches_a_card_put_in_later);
    RUN(a_record_with_a_port_the_bench_has_not_is_refused);
    RUN(a_pass_is_taken_once_and_by_its_number_only);
    RUN(a_start_that_timed_out_in_the_queue_is_never_carried_out);
    RUN(without_a_gate_an_unanswered_start_leaves_the_servo_in_doubt);
    RUN(a_record_is_held_against_the_servo_whenever_the_card_comes);
    RUN(a_restore_with_no_servo_read_leaves_the_record_it_did_not_make);
    RUN(a_locked_restore_with_no_servo_read_records_the_pin_of_its_port);
    RUN(a_confirmed_soft_limit_is_asked_again_further_out);
    RUN(a_value_on_the_other_side_of_the_servos_is_asked_again);
    RUN(the_soft_limits_compare_with_the_servo_as_the_session_found_it);
    RUN(another_servo_is_compared_with_its_own_image);
    RUN(the_fields_at_fault_for_a_soft_limit_are_named);
    RUN(a_message_goes_when_what_it_says_no_longer_holds);
    RUN(a_message_of_the_danger_level_stays_over_an_edit);
    RUN(a_write_that_runs_is_no_interrupted_write);
    RUN(a_restore_counts_its_registers_where_its_order_is_checked);
    RUN(a_servo_that_breaks_a_hard_limit_is_written_once_the_value_is_mended);
    return test_summary("kst_prog");
}
