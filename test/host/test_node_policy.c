/*
 * The node's battery and sleep decisions (firmware/node/main/node_policy.c).
 *
 * The failures this is about: a node that sleeps while the panel holds a
 * link or a pack is on the XT60, one that never sleeps, a single low reading
 * that switches a node off, a board on USB alone (no cell) taken for an
 * empty cell, and an idle count that breaks at the ms clock's wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "node_policy.h"
#include "rcnode.h"

/** Inputs at @p now_ms with nothing holding the node and a full cell. */
static node_inputs_t idle_at(uint32_t now_ms)
{
    node_inputs_t in = { now_ms, false, false, false, 4000u };
    return in;
}

TEST_CASE(an_idle_node_sleeps_after_120s_and_not_a_ms_before)
{
    node_policy_t p;
    node_policy_init(&p, 1000u);
    node_inputs_t in = idle_at(1000u + NODE_IDLE_MS - 1u);
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    in.now_ms = 1000u + NODE_IDLE_MS;
    CHECK_EQ(node_policy_step(&p, &in), NODE_SLEEP);
}

TEST_CASE(a_link_a_pack_or_the_console_restart_the_idle_count)
{
    bool *holds[3];
    node_inputs_t in = idle_at(0u);
    holds[0] = &in.link;
    holds[1] = &in.pack;
    holds[2] = &in.stay;
    for (unsigned k = 0; k < 3u; ++k) {
        node_policy_t p;
        node_policy_init(&p, 0u);
        in = idle_at(50000u);
        *holds[k] = true;
        CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
        in = idle_at(50000u + NODE_IDLE_MS - 1u);
        CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
        in.now_ms = 50000u + NODE_IDLE_MS;
        CHECK_EQ(node_policy_step(&p, &in), NODE_SLEEP);
    }
}

TEST_CASE(the_idle_count_holds_across_the_ms_clock_wrap)
{
    node_policy_t p;
    node_policy_init(&p, 0xFFFFFFFFu - 1000u);
    node_inputs_t in = idle_at(NODE_IDLE_MS - 1002u);   /* 119999ms on */
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    in.now_ms = NODE_IDLE_MS - 1001u;                     /* 120000ms on */
    CHECK_EQ(node_policy_step(&p, &in), NODE_SLEEP);
}

TEST_CASE(three_low_readings_in_a_row_switch_off_and_a_good_one_restarts_the_count)
{
    node_policy_t p;
    node_policy_init(&p, 0u);
    node_inputs_t in = idle_at(0u);
    in.link = true;
    in.bat_mv = RCNODE_BAT_OFF_MV - 1u;
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    in.bat_mv = RCNODE_BAT_OFF_MV;               /* 3.300V is not under */
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    in.bat_mv = RCNODE_BAT_OFF_MV - 1u;
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    CHECK_EQ(node_policy_step(&p, &in), NODE_OFF);  /* a link does not hold it */
    CHECK_EQ(node_policy_step(&p, &in), NODE_OFF);
}

TEST_CASE(no_cell_on_usb_alone_switches_nothing_off)
{
    node_policy_t p;
    node_policy_init(&p, 0u);
    node_inputs_t in = idle_at(0u);
    in.stay = true;
    in.bat_mv = NODE_BAT_NONE_MV - 1u;
    for (unsigned k = 0; k < 10u; ++k) {
        CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    }
    in.bat_mv = NODE_BAT_NONE_MV;                /* 1.000V is a cell, and empty */
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    CHECK_EQ(node_policy_step(&p, &in), NODE_RUN);
    CHECK_EQ(node_policy_step(&p, &in), NODE_OFF);
}

TEST_CASE(a_timer_wake_stays_up_for_a_pack_of_5v_and_sleeps_below)
{
    CHECK_EQ(node_policy_wake(true, RCNODE_PACK_UV, 3900u), NODE_RUN);
    CHECK_EQ(node_policy_wake(true, RCNODE_PACK_UV - 1, 3900u), NODE_SLEEP);
    CHECK_EQ(node_policy_wake(false, 22000000, 3900u), NODE_SLEEP);
    CHECK_EQ(node_policy_wake(true, 22000000, RCNODE_BAT_OFF_MV - 1u), NODE_OFF);
    CHECK_EQ(node_policy_wake(true, 22000000, RCNODE_BAT_OFF_MV), NODE_RUN);
    CHECK_EQ(node_policy_wake(true, 22000000, 0u), NODE_RUN);   /* no cell: USB */
}

TEST_CASE(the_divider_doubles_the_pin_and_clamps)
{
    CHECK_EQ(node_bat_mv(0u), 0);
    CHECK_EQ(node_bat_mv(2100u), 4200);
    CHECK_EQ(node_bat_mv(1750u), 3500);
    CHECK_EQ(node_bat_mv(32767u), 65534);
    CHECK_EQ(node_bat_mv(32768u), 65535);
    CHECK_EQ(node_bat_mv(0xFFFFFFFFu), 65535);
}

TEST_CASE(the_battery_words_change_at_1v_and_3_5v)
{
    CHECK_EQ(node_bat_flags(0u), RCNODE_ST_BAT_NONE);
    CHECK_EQ(node_bat_flags(NODE_BAT_NONE_MV - 1u), RCNODE_ST_BAT_NONE);
    CHECK_EQ(node_bat_flags(NODE_BAT_NONE_MV), RCNODE_ST_BAT_LOW);
    CHECK_EQ(node_bat_flags(RCNODE_BAT_LOW_MV - 1u), RCNODE_ST_BAT_LOW);
    CHECK_EQ(node_bat_flags(RCNODE_BAT_LOW_MV), 0);
    CHECK_EQ(node_bat_flags(4200u), 0);
}

int main(void)
{
    RUN(an_idle_node_sleeps_after_120s_and_not_a_ms_before);
    RUN(a_link_a_pack_or_the_console_restart_the_idle_count);
    RUN(the_idle_count_holds_across_the_ms_clock_wrap);
    RUN(three_low_readings_in_a_row_switch_off_and_a_good_one_restarts_the_count);
    RUN(no_cell_on_usb_alone_switches_nothing_off);
    RUN(a_timer_wake_stays_up_for_a_pack_of_5v_and_sleeps_below);
    RUN(the_divider_doubles_the_pin_and_clamps);
    RUN(the_battery_words_change_at_1v_and_3_5v);
    return test_summary("node_policy");
}
