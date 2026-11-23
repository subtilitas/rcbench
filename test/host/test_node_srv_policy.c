/**
 * @file
 * @brief The servo node's safety decisions
 *        (firmware/node/main/node_srv_policy.c).
 *
 * The failures this is about: a heartbeat that edges without a panel, one
 * that keeps edging past the keep-alive timeout, a STOP or a BLE disconnect
 * that leaves an output driven, a throttle held past its age, an IOmcu loss
 * that leaves the node ARMED, an ARM taken without its preconditions, and
 * every timeout breaking at the 32-bit ms clock's wrap.  Each timeout is
 * held at its last millisecond and at its first.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "heartbeat.h"
#include "link_pages.h"
#include "node_srv_policy.h"
#include "out_bind.h"

/** @brief The board the node takes is board 3's number in out_bind.h. */
_Static_assert(NODE_SRV_IOMCU_BOARD == (unsigned)OUTBIND_BOARD_RSN,
               "the node takes board 3");

/** @brief A start that wraps the clock 1000ms in. */
#define T_WRAP (0xFFFFFFFFu - 999u)

/**
 * @brief A node at @p t0 with the IOmcu answered (board 3, 4.41), the
 *        all-off writes done, BLE up and a first KEEPALIVE.
 * @param p  The state.
 * @param t0 The clock.
 */
static void linked_at(node_srv_pol_t *p, uint32_t t0)
{
    node_srv_out_t o;
    node_srv_pol_init(p, t0);
    node_srv_pol_answer(p, t0);
    CHECK(node_srv_pol_identity(p, 4u, 41u, 3u));
    node_srv_pol_step(p, t0, &o);
    node_srv_pol_off_done(p, o.off_gen);
    node_srv_pol_ble(p, t0, true);
    node_srv_pol_keepalive(p, t0, false);
    (void)node_srv_pol_events(p);
}

/**
 * @brief linked_at(), then an ARM of @p roles taken and the line trusted and
 *        the arming frame acknowledged at @p t0 + 100ms.
 * @param p     The state.
 * @param t0    The clock.
 * @param roles The ARM value.
 * @return The clock after it.
 */
static uint32_t armed_at(node_srv_pol_t *p, uint32_t t0, uint16_t roles)
{
    node_srv_out_t o;
    linked_at(p, t0);
    CHECK_EQ(node_srv_pol_arm(p, t0, roles), NODE_SRV_ARM_OK);
    const uint32_t t = t0 + HEARTBEAT_SETTLE_MS;
    node_srv_pol_answer(p, t);
    node_srv_pol_keepalive(p, t, false);
    node_srv_pol_status(p, t, 0u);
    node_srv_pol_step(p, t, &o);
    CHECK(o.arm_write);
    node_srv_pol_arm_done(p, t, true);
    node_srv_pol_step(p, t, &o);
    CHECK_EQ(p->arm, NODE_SRV_ARMED);
    CHECK_EQ(o.arm, roles);
    (void)node_srv_pol_events(p);
    return t;
}

/** @brief Step at @p now_ms and return the output. */
static node_srv_out_t step(node_srv_pol_t *p, uint32_t now_ms)
{
    node_srv_out_t o;
    node_srv_pol_step(p, now_ms, &o);
    return o;
}

/* --------------------------------------------------------------- STANDALONE */

TEST_CASE(standalone_drives_nothing_for_a_minute)
{
    node_srv_pol_t p;
    node_srv_pol_init(&p, 0u);
    node_srv_pol_ble(&p, 0u, true);
    (void)node_srv_pol_identity(&p, 4u, 41u, 3u);
    for (uint32_t t = 0u; t <= 60000u; t += 5u) {
        node_srv_pol_answer(&p, t);
        const node_srv_out_t o = step(&p, t);
        CHECK(!o.beat);
        CHECK(!o.arm_write);
        CHECK_EQ(o.arm, 0u);
        CHECK_EQ(o.throttle, 0u);
        if (o.off) {
            node_srv_pol_off_done(&p, o.off_gen);
        }
        if (t % 1000u == 0u) {
            CHECK_EQ(node_srv_pol_arm(&p, t, LINK_ARM_ALL), NODE_SRV_ARM_NO_PANEL);
            node_srv_pol_throttle(&p, t, 5000u);
        }
    }
    CHECK_EQ(p.panel, NODE_SRV_STANDALONE);
}

TEST_CASE(the_all_off_writes_are_owed_from_the_start)
{
    node_srv_pol_t p;
    node_srv_pol_init(&p, 100u);
    node_srv_out_t o = step(&p, 100u);
    CHECK(o.off);
    node_srv_pol_off_done(&p, o.off_gen);
    o = step(&p, 101u);
    CHECK(!o.off);
}

TEST_CASE(a_stop_in_standalone_latches_and_owes_the_all_off_writes)
{
    node_srv_pol_t p;
    node_srv_pol_init(&p, 0u);
    node_srv_out_t o = step(&p, 0u);
    node_srv_pol_off_done(&p, o.off_gen);
    node_srv_pol_stop(&p, 10u);
    o = step(&p, 10u);
    CHECK_EQ(p.panel, NODE_SRV_STOPPED);
    CHECK(o.off);
    CHECK(!o.beat);
    CHECK_EQ(node_srv_pol_events(&p) & NODE_SRV_EV_STOP, NODE_SRV_EV_STOP);
}

/* --------------------------------------------------------- the keep-alive */

TEST_CASE(the_beat_starts_on_a_keepalive_with_the_iomcu_answering)
{
    node_srv_pol_t p;
    node_srv_pol_init(&p, 0u);
    node_srv_pol_ble(&p, 0u, true);
    node_srv_pol_keepalive(&p, 0u, false);
    CHECK(!step(&p, 0u).beat);           /* no answer from the IOmcu yet */
    node_srv_pol_answer(&p, 1u);
    CHECK(!step(&p, 1u).beat);           /* IDENTITY not read */
    CHECK(!node_srv_pol_identity(&p, 4u, 40u, 3u));
    CHECK(!step(&p, 2u).beat);           /* 4.40 */
    CHECK(!node_srv_pol_identity(&p, 4u, 41u, 1u));
    CHECK(!step(&p, 3u).beat);           /* board 1 */
    CHECK(!node_srv_pol_identity(&p, 5u, 41u, 3u));
    CHECK(!step(&p, 4u).beat);           /* major 5 */
    CHECK(node_srv_pol_identity(&p, 4u, 42u, 3u));
    CHECK(step(&p, 5u).beat);
    CHECK_EQ(p.panel, NODE_SRV_LINKED);
}

/** @brief T_ka at 299 and 300ms from @p t0. */
static void ka_at_299_and_300(uint32_t t0)
{
    node_srv_pol_t p;
    linked_at(&p, t0);
    node_srv_pol_answer(&p, t0 + 299u);
    node_srv_out_t o = step(&p, t0 + 299u);
    CHECK(o.beat);
    CHECK(!o.off);
    CHECK_EQ(p.panel, NODE_SRV_LINKED);
    node_srv_pol_answer(&p, t0 + 300u);
    o = step(&p, t0 + 300u);
    CHECK(!o.beat);
    CHECK(o.off);
    CHECK_EQ(p.panel, NODE_SRV_PANEL_LOST);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_KA_LOST);
}

TEST_CASE(the_keepalive_ages_out_at_300ms_and_not_at_299ms)
{
    ka_at_299_and_300(1000u);
}

TEST_CASE(the_keepalive_age_holds_across_the_wrap)
{
    ka_at_299_and_300(T_WRAP);
    ka_at_299_and_300(0xFFFFFFFFu - 299u);   /* 300ms lands on 0 */
    ka_at_299_and_300(0xFFFFFFFFu);
}

TEST_CASE(a_keepalive_lost_while_armed_disarms_and_latches)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 5000u, LINK_ARM_SURFACE);
    node_srv_pol_answer(&p, t + 299u);
    CHECK_EQ(step(&p, t + 299u).arm, LINK_ARM_SURFACE);
    node_srv_pol_answer(&p, t + 300u);
    const node_srv_out_t o = step(&p, t + 300u);
    CHECK_EQ(o.arm, 0u);
    CHECK(o.off);
    CHECK(o.ctl_now);
    CHECK(!o.beat);
    CHECK_EQ(p.arm, NODE_SRV_SAFE);
}

TEST_CASE(a_latch_holds_through_returning_keepalives_until_an_arm)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    node_srv_pol_answer(&p, 300u);
    node_srv_out_t o = step(&p, 300u);
    node_srv_pol_off_done(&p, o.off_gen);
    for (uint32_t t = 350u; t < 5000u; t += 50u) {
        node_srv_pol_answer(&p, t);
        node_srv_pol_keepalive(&p, t, false);
        o = step(&p, t);
        CHECK(!o.beat);
        CHECK_EQ(p.panel, NODE_SRV_PANEL_LOST);
    }
    /* No second event and no second owing while the latch stands. */
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_KA_LOST);
    CHECK(!o.off);
    CHECK_EQ(node_srv_pol_arm(&p, 4950u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    CHECK(step(&p, 4951u).beat);
    CHECK_EQ(p.panel, NODE_SRV_LINKED);
}

/* ------------------------------------------------------------------ STOP */

TEST_CASE(a_stop_op_while_armed_is_all_off_at_once)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_ALL);
    node_srv_pol_stop(&p, t + 1u);
    const node_srv_out_t o = step(&p, t + 1u);
    CHECK(!o.beat);
    CHECK(o.off);
    CHECK_EQ(o.arm, 0u);
    CHECK_EQ(p.panel, NODE_SRV_STOPPED);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_STOP);
}

TEST_CASE(the_stop_flag_latches_once_and_refuses_arm_while_it_comes)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_ALL);
    for (uint32_t k = 0u; k < 5u; ++k) {
        node_srv_pol_keepalive(&p, t + 50u * k, true);
        node_srv_pol_answer(&p, t + 50u * k);
        const node_srv_out_t o = step(&p, t + 50u * k);
        CHECK(!o.beat);
        CHECK_EQ(o.arm, 0u);
        node_srv_pol_off_done(&p, o.off_gen);
        CHECK_EQ(node_srv_pol_arm(&p, t + 50u * k, LINK_ARM_ALL), NODE_SRV_ARM_STOP);
    }
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_STOP);
    /* The flag clears: the latch stays until the ARM. */
    node_srv_pol_keepalive(&p, t + 300u, false);
    node_srv_pol_answer(&p, t + 300u);
    CHECK(!step(&p, t + 300u).beat);
    CHECK_EQ(node_srv_pol_arm(&p, t + 300u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    CHECK(step(&p, t + 301u).beat);
}

TEST_CASE(a_ble_disconnect_is_a_panel_loss_at_once)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_THROTTLE);
    node_srv_pol_ble(&p, t + 1u, false);
    const node_srv_out_t o = step(&p, t + 1u);
    CHECK(!o.beat);
    CHECK(o.off);
    CHECK_EQ(o.arm, 0u);
    CHECK_EQ(p.panel, NODE_SRV_PANEL_LOST);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_BLE_LOST);
    /* Back, with a keep-alive from before the drop only: no ARM. */
    node_srv_pol_ble(&p, t + 2u, true);
    CHECK_EQ(node_srv_pol_arm(&p, t + 2u, LINK_ARM_ALL), NODE_SRV_ARM_NO_PANEL);
}

/* -------------------------------------------------------------- THROTTLE */

/** @brief THROTTLE age at 149 and 150ms from @p t0. */
static void throttle_at_149_and_150(uint32_t t0)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, t0, LINK_ARM_THROTTLE);
    node_srv_pol_throttle(&p, t, 4200u);
    node_srv_pol_keepalive(&p, t + 100u, false);
    node_srv_pol_answer(&p, t + 149u);
    node_srv_out_t o = step(&p, t + 149u);
    CHECK_EQ(o.throttle, 4200u);
    node_srv_pol_answer(&p, t + 150u);
    o = step(&p, t + 150u);
    CHECK_EQ(o.throttle, 0u);
    /* The bench stays ARMED until T_ka. */
    CHECK_EQ(o.arm, LINK_ARM_THROTTLE);
    CHECK(o.beat);
    /* A fresh THROTTLE is written again. */
    node_srv_pol_throttle(&p, t + 160u, 10001u);
    CHECK_EQ(step(&p, t + 160u).throttle, LINK_THROTTLE_MAX);
}

TEST_CASE(a_throttle_older_than_150ms_is_written_as_0_not_at_149ms)
{
    throttle_at_149_and_150(0u);
}

TEST_CASE(the_throttle_age_holds_across_the_wrap)
{
    throttle_at_149_and_150(0xFFFFFFFFu - 100u - 120u);
    throttle_at_149_and_150(0xFFFFFFFFu - 100u - 149u);
}

TEST_CASE(a_throttle_while_safe_is_not_kept)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    node_srv_pol_throttle(&p, 1u, 3000u);
    CHECK_EQ(step(&p, 2u).throttle, 0u);
    CHECK_EQ(node_srv_pol_arm(&p, 3u, LINK_ARM_THROTTLE), NODE_SRV_ARM_OK);
    node_srv_pol_answer(&p, 103u);
    node_srv_pol_status(&p, 103u, 0u);
    (void)step(&p, 103u);
    node_srv_pol_arm_done(&p, 103u, true);
    CHECK_EQ(step(&p, 103u).throttle, 0u);
}

/* ---------------------------------------------------------- the IOmcu lost */

/** @brief IOmcu lost at 999 and 1000ms from @p t0. */
static void iomcu_at_999_and_1000(uint32_t t0)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, t0, LINK_ARM_ALL);
    for (uint32_t k = 50u; k < 1000u; k += 50u) {
        node_srv_pol_keepalive(&p, t + k, false);
    }
    node_srv_out_t o = step(&p, t + 999u);
    CHECK(o.beat);
    CHECK_EQ(o.arm, LINK_ARM_ALL);
    CHECK(!p.iomcu_lost);
    o = step(&p, t + 1000u);
    CHECK(p.iomcu_lost);
    CHECK(!o.beat);
    CHECK(o.off);
    CHECK_EQ(o.arm, 0u);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_IOMCU_LOST);
    CHECK_EQ(node_srv_pol_arm(&p, t + 1000u, LINK_ARM_ALL), NODE_SRV_ARM_NO_IOMCU);
}

TEST_CASE(the_iomcu_is_lost_at_1000ms_without_an_answer_not_at_999ms)
{
    iomcu_at_999_and_1000(0u);
}

TEST_CASE(the_iomcu_loss_holds_across_the_wrap)
{
    iomcu_at_999_and_1000(T_WRAP);
    iomcu_at_999_and_1000(0xFFFFFFFFu - 1000u - 100u + 1u);
}

TEST_CASE(a_returning_iomcu_gets_the_all_off_writes_and_its_identity_read)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    node_srv_pol_keepalive(&p, 990u, false);
    CHECK(step(&p, 1000u).off);
    CHECK(p.iomcu_lost);
    CHECK_EQ(p.panel, NODE_SRV_LINKED);
    node_srv_pol_keepalive(&p, 1010u, false);
    node_srv_pol_answer(&p, 1020u);
    node_srv_out_t o = step(&p, 1020u);
    CHECK(!p.iomcu_lost);
    CHECK(!p.identity_ok);
    CHECK(!o.beat);
    CHECK(o.off);
    CHECK(node_srv_pol_identity(&p, 4u, 41u, 3u));
    node_srv_pol_off_done(&p, o.off_gen);
    o = step(&p, 1021u);
    CHECK(o.beat);
    CHECK(!o.off);
}

TEST_CASE(run_pulses_once_at_3000ms_of_silence)
{
    node_srv_pol_t p;
    node_srv_pol_init(&p, 0u);
    node_srv_pol_answer(&p, 500u);
    CHECK(!step(&p, 3499u).run_pulse);
    CHECK(step(&p, 3500u).run_pulse);
    for (uint32_t t = 3501u; t < 20000u; t += 7u) {
        CHECK(!step(&p, t).run_pulse);
    }
    /* An answer starts a new silence. */
    node_srv_pol_answer(&p, 20000u);
    CHECK(!step(&p, 22999u).run_pulse);
    CHECK(step(&p, 23000u).run_pulse);
}

TEST_CASE(run_pulses_at_3000ms_without_a_first_answer_and_across_the_wrap)
{
    node_srv_pol_t p;
    node_srv_pol_init(&p, T_WRAP);
    CHECK(!step(&p, T_WRAP + 2999u).run_pulse);
    CHECK(step(&p, T_WRAP + 3000u).run_pulse);
}

TEST_CASE(the_iomcu_refusing_the_arm_in_force_disarms)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_ALL);
    node_srv_pol_refused(&p);
    const node_srv_out_t o = step(&p, t + 1u);
    CHECK_EQ(o.arm, 0u);
    CHECK(o.off);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_ARM_FAILED);
}

TEST_CASE(another_board_found_while_armed_disarms)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_ALL);
    CHECK(!node_srv_pol_identity(&p, 4u, 41u, 2u));
    const node_srv_out_t o = step(&p, t + 1u);
    CHECK(!o.beat);
    CHECK_EQ(o.arm, 0u);
    CHECK(o.off);
}

/* ---------------------------------------------------------------- ARM */

TEST_CASE(arm_preconditions_each_refuse)
{
    node_srv_pol_t p;
    /* Roles. */
    linked_at(&p, 0u);
    CHECK_EQ(node_srv_pol_arm(&p, 1u, 0u), NODE_SRV_ARM_ROLES);
    CHECK_EQ(node_srv_pol_arm(&p, 1u, 0x0008u), NODE_SRV_ARM_ROLES);
    CHECK_EQ(node_srv_pol_arm(&p, 1u, LINK_ARM_ALL | LINK_ARM_SURFACE),
             NODE_SRV_ARM_ROLES);
    /* No BLE. */
    linked_at(&p, 0u);
    node_srv_pol_ble(&p, 1u, false);
    node_srv_pol_ble(&p, 2u, true);
    node_srv_pol_keepalive(&p, 2u, false);
    node_srv_pol_ble(&p, 3u, false);
    CHECK_EQ(node_srv_pol_arm(&p, 3u, LINK_ARM_ALL), NODE_SRV_ARM_NO_PANEL);
    /* KEEPALIVE at 299 and 300ms. */
    linked_at(&p, 0u);
    node_srv_pol_answer(&p, 299u);
    CHECK_EQ(node_srv_pol_arm(&p, 300u, LINK_ARM_ALL), NODE_SRV_ARM_NO_PANEL);
    linked_at(&p, 0u);
    node_srv_pol_answer(&p, 299u);
    CHECK_EQ(node_srv_pol_arm(&p, 299u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    /* The IOmcu's last answer at 999 and 1000ms. */
    linked_at(&p, 0u);
    node_srv_pol_keepalive(&p, 1000u, false);
    CHECK_EQ(node_srv_pol_arm(&p, 1000u, LINK_ARM_ALL), NODE_SRV_ARM_NO_IOMCU);
    linked_at(&p, 0u);
    node_srv_pol_keepalive(&p, 999u, false);
    CHECK_EQ(node_srv_pol_arm(&p, 999u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    /* IDENTITY not taken. */
    linked_at(&p, 0u);
    (void)node_srv_pol_identity(&p, 4u, 40u, 3u);
    CHECK_EQ(node_srv_pol_arm(&p, 1u, LINK_ARM_ALL), NODE_SRV_ARM_NO_IOMCU);
    /* The all-off writes not answered. */
    linked_at(&p, 0u);
    node_srv_pol_stop(&p, 1u);
    node_srv_pol_keepalive(&p, 2u, false);
    CHECK_EQ(node_srv_pol_arm(&p, 2u, LINK_ARM_ALL), NODE_SRV_ARM_OFF_OWED);
    node_srv_out_t o = step(&p, 3u);
    node_srv_pol_off_done(&p, o.off_gen);
    CHECK_EQ(node_srv_pol_arm(&p, 3u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    /* A second ARM while one waits; another roles' while armed. */
    CHECK_EQ(node_srv_pol_arm(&p, 4u, LINK_ARM_ALL), NODE_SRV_ARM_BUSY);
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_SURFACE);
    CHECK_EQ(node_srv_pol_arm(&p, t, LINK_ARM_SURFACE), NODE_SRV_ARM_OK);
    CHECK_EQ(node_srv_pol_arm(&p, t, LINK_ARM_THROTTLE), NODE_SRV_ARM_BUSY);
}

TEST_CASE(the_arm_waits_for_the_settle_and_a_status_without_the_heartbeat_fault)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    CHECK_EQ(node_srv_pol_arm(&p, 10u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    node_srv_out_t o = step(&p, 10u);
    CHECK(o.beat);
    CHECK(o.status_fast);
    CHECK(!o.arm_write);
    CHECK_EQ(o.arm, 0u);
    /* A clear STATUS before the settle does not count. */
    node_srv_pol_status(&p, 10u + HEARTBEAT_SETTLE_MS - 1u, 0u);
    CHECK(!step(&p, 10u + HEARTBEAT_SETTLE_MS - 1u).arm_write);
    /* The fault still set after it does not either. */
    node_srv_pol_status(&p, 10u + HEARTBEAT_SETTLE_MS, LINK_FAULT_HEARTBEAT);
    CHECK(!step(&p, 10u + HEARTBEAT_SETTLE_MS).arm_write);
    node_srv_pol_answer(&p, 160u);
    node_srv_pol_keepalive(&p, 160u, false);
    node_srv_pol_status(&p, 160u, 0u);
    o = step(&p, 160u);
    CHECK(o.arm_write);
    CHECK_EQ(o.arm, 0u);
    node_srv_pol_arm_done(&p, 161u, true);
    o = step(&p, 161u);
    CHECK_EQ(o.arm, LINK_ARM_ALL);
    CHECK(!o.status_fast);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_ARMED);
}

TEST_CASE(an_arm_whose_line_is_not_trusted_by_300ms_is_given_up)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    CHECK_EQ(node_srv_pol_arm(&p, 0u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    for (uint32_t t = 50u; t < 300u; t += 50u) {
        node_srv_pol_answer(&p, t);
        node_srv_pol_keepalive(&p, t, false);
        node_srv_pol_status(&p, t, LINK_FAULT_HEARTBEAT);
        CHECK_EQ(step(&p, t).arm, 0u);
    }
    node_srv_pol_answer(&p, 299u);
    CHECK_EQ(p.arm, NODE_SRV_ARM_WAIT);
    (void)step(&p, 299u);
    CHECK_EQ(p.arm, NODE_SRV_ARM_WAIT);
    (void)step(&p, 300u);
    CHECK_EQ(p.arm, NODE_SRV_SAFE);
    /* A clear STATUS at 300ms comes too late. */
    node_srv_pol_status(&p, 300u, 0u);
    CHECK(!step(&p, 300u).arm_write);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_ARM_FAILED);
}

TEST_CASE(a_refused_arming_frame_is_safe)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    CHECK_EQ(node_srv_pol_arm(&p, 0u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    node_srv_pol_status(&p, 100u, 0u);
    CHECK(step(&p, 100u).arm_write);
    node_srv_pol_arm_done(&p, 101u, false);
    const node_srv_out_t o = step(&p, 101u);
    CHECK_EQ(o.arm, 0u);
    CHECK(!o.arm_write);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_ARM_FAILED);
}

TEST_CASE(a_stop_while_the_arming_frame_is_on_its_way_wins)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    CHECK_EQ(node_srv_pol_arm(&p, 0u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    node_srv_pol_status(&p, 100u, 0u);
    CHECK(step(&p, 100u).arm_write);
    node_srv_pol_stop(&p, 101u);
    node_srv_pol_arm_done(&p, 102u, true);
    const node_srv_out_t o = step(&p, 102u);
    CHECK_EQ(o.arm, 0u);
    CHECK(o.off);
    CHECK(!o.beat);
    CHECK_EQ(node_srv_pol_events(&p), NODE_SRV_EV_STOP);
}

TEST_CASE(all_off_writes_owed_again_on_their_way_stay_owed)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    node_srv_pol_stop(&p, 1u);
    const node_srv_out_t first = step(&p, 1u);
    CHECK(first.off);
    /* A second cause while the first writes run: a BLE drop cannot latch
     * again (STOPPED), an IOmcu loss can. */
    CHECK(step(&p, 1001u).off);
    CHECK(p.iomcu_lost);
    node_srv_pol_off_done(&p, first.off_gen);
    const node_srv_out_t o = step(&p, 1002u);
    CHECK(o.off);
    CHECK(o.off_gen != first.off_gen);
}

TEST_CASE(safe_writes_control_at_once_and_keeps_the_beat)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_ALL);
    node_srv_pol_safe(&p);
    node_srv_out_t o = step(&p, t + 1u);
    CHECK(o.ctl_now);
    CHECK_EQ(o.arm, 0u);
    CHECK(o.beat);
    CHECK(!o.off);
    node_srv_pol_ctl_done(&p);
    o = step(&p, t + 2u);
    CHECK(!o.ctl_now);
    CHECK_EQ(p.panel, NODE_SRV_LINKED);
}

/* ------------------------------------------- an exchange given up on the way */

/** @brief How the switch to ARMED is cut. */
typedef enum { CUT_SAFE, CUT_STOP_FLAG, CUT_BLE, CUT_KA } cut_t;

/**
 * @brief Cut the switch to ARMED @p how at @p t.
 * @param p   The state.
 * @param t   The clock.
 * @param how The cut.
 */
static void cut(node_srv_pol_t *p, uint32_t t, cut_t how)
{
    switch (how) {
    case CUT_SAFE:      node_srv_pol_safe(p); break;
    case CUT_STOP_FLAG: node_srv_pol_keepalive(p, t, true); break;
    case CUT_BLE:       node_srv_pol_ble(p, t, false); break;
    case CUT_KA:        break;   /* the step at t + 300ms after the last */
    }
}

/**
 * @brief A cut during CLEAR's wait (@p at_write) or during the settle: CLEAR
 *        and the arming frame are given up, an ACK arriving after it arms
 *        nothing, and CONTROL then carries ARM 0.
 */
static void cut_arming(cut_t how, bool at_write)
{
    node_srv_pol_t p;
    linked_at(&p, 0u);
    CHECK_EQ(node_srv_pol_arm(&p, 0u, LINK_ARM_ALL), NODE_SRV_ARM_OK);
    node_srv_out_t o;
    uint32_t t = 50u;
    if (at_write) {
        node_srv_pol_answer(&p, 100u);
        node_srv_pol_status(&p, 100u, 0u);
        o = step(&p, 100u);
        CHECK(o.arm_write);
        CHECK(node_srv_pol_keep(&p, &o, NODE_SRV_X_ARMING, 0u));
        t = 101u;
    }
    cut(&p, t, how);
    if (how == CUT_KA) {
        t = 300u;   /* the KEEPALIVE came at 0 */
    }
    node_srv_pol_answer(&p, t);
    o = step(&p, t);
    CHECK(!node_srv_pol_keep(&p, &o, NODE_SRV_X_ARMING, 0u));
    CHECK(!o.arm_write);
    CHECK_EQ(o.arm, 0u);
    /* An ACK of CLEAR or ARM arriving now arms nothing. */
    node_srv_pol_arm_done(&p, t, true);
    o = step(&p, t);
    CHECK_EQ(p.arm, NODE_SRV_SAFE);
    CHECK_EQ(o.arm, 0u);
    CHECK_EQ(node_srv_pol_events(&p) & NODE_SRV_EV_ARMED, 0u);
    /* What follows is not held up: the all-off writes for a STOP or a
     * loss, CONTROL's ARM 0 for a SAFE. */
    if (how == CUT_SAFE) {
        CHECK(o.ctl_now);
        CHECK(node_srv_pol_keep(&p, &o, NODE_SRV_X_CONTROL, 0u));
    } else {
        CHECK(o.off);
        CHECK(node_srv_pol_keep(&p, &o, NODE_SRV_X_OFF, 0u));
        CHECK(!node_srv_pol_keep(&p, &o, NODE_SRV_X_READ, 0u));
        CHECK(!o.beat);
    }
    /* A later STATUS cannot bring the arming frame back. */
    node_srv_pol_status(&p, t + 1u, 0u);
    CHECK(!step(&p, t + 1u).arm_write);
}

TEST_CASE(safe_stop_ble_or_keepalive_loss_during_clear_arm_nothing)
{
    cut_arming(CUT_SAFE, true);
    cut_arming(CUT_STOP_FLAG, true);
    cut_arming(CUT_BLE, true);
    cut_arming(CUT_KA, true);
}

TEST_CASE(safe_stop_ble_or_keepalive_loss_during_the_settle_arm_nothing)
{
    cut_arming(CUT_SAFE, false);
    cut_arming(CUT_STOP_FLAG, false);
    cut_arming(CUT_BLE, false);
    cut_arming(CUT_KA, false);
}

TEST_CASE(a_control_write_carrying_arm_is_given_up_after_safe)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_SURFACE);
    node_srv_out_t o = step(&p, t);
    CHECK(node_srv_pol_keep(&p, &o, NODE_SRV_X_CONTROL, LINK_ARM_SURFACE));
    CHECK(!node_srv_pol_keep(&p, &o, NODE_SRV_X_CONTROL, LINK_ARM_THROTTLE));
    node_srv_pol_safe(&p);
    o = step(&p, t + 1u);
    CHECK(!node_srv_pol_keep(&p, &o, NODE_SRV_X_CONTROL, LINK_ARM_SURFACE));
    CHECK(node_srv_pol_keep(&p, &o, NODE_SRV_X_CONTROL, 0u));
    CHECK(node_srv_pol_keep(&p, &o, NODE_SRV_X_READ, 0u));
}

TEST_CASE(the_all_off_writes_are_never_given_up_and_end_every_other_exchange)
{
    node_srv_pol_t p;
    const uint32_t t = armed_at(&p, 0u, LINK_ARM_ALL);
    node_srv_pol_stop(&p, t);
    const node_srv_out_t o = step(&p, t);
    CHECK(node_srv_pol_keep(&p, &o, NODE_SRV_X_OFF, 0u));
    CHECK(!node_srv_pol_keep(&p, &o, NODE_SRV_X_READ, 0u));
    CHECK(!node_srv_pol_keep(&p, &o, NODE_SRV_X_CONTROL, 0u));
    CHECK(!node_srv_pol_keep(&p, &o, NODE_SRV_X_ARMING, 0u));
}

int main(void)
{
    RUN(standalone_drives_nothing_for_a_minute);
    RUN(the_all_off_writes_are_owed_from_the_start);
    RUN(a_stop_in_standalone_latches_and_owes_the_all_off_writes);
    RUN(the_beat_starts_on_a_keepalive_with_the_iomcu_answering);
    RUN(the_keepalive_ages_out_at_300ms_and_not_at_299ms);
    RUN(the_keepalive_age_holds_across_the_wrap);
    RUN(a_keepalive_lost_while_armed_disarms_and_latches);
    RUN(a_latch_holds_through_returning_keepalives_until_an_arm);
    RUN(a_stop_op_while_armed_is_all_off_at_once);
    RUN(the_stop_flag_latches_once_and_refuses_arm_while_it_comes);
    RUN(a_ble_disconnect_is_a_panel_loss_at_once);
    RUN(a_throttle_older_than_150ms_is_written_as_0_not_at_149ms);
    RUN(the_throttle_age_holds_across_the_wrap);
    RUN(a_throttle_while_safe_is_not_kept);
    RUN(the_iomcu_is_lost_at_1000ms_without_an_answer_not_at_999ms);
    RUN(the_iomcu_loss_holds_across_the_wrap);
    RUN(a_returning_iomcu_gets_the_all_off_writes_and_its_identity_read);
    RUN(run_pulses_once_at_3000ms_of_silence);
    RUN(run_pulses_at_3000ms_without_a_first_answer_and_across_the_wrap);
    RUN(the_iomcu_refusing_the_arm_in_force_disarms);
    RUN(another_board_found_while_armed_disarms);
    RUN(arm_preconditions_each_refuse);
    RUN(the_arm_waits_for_the_settle_and_a_status_without_the_heartbeat_fault);
    RUN(an_arm_whose_line_is_not_trusted_by_300ms_is_given_up);
    RUN(a_refused_arming_frame_is_safe);
    RUN(a_stop_while_the_arming_frame_is_on_its_way_wins);
    RUN(all_off_writes_owed_again_on_their_way_stay_owed);
    RUN(safe_writes_control_at_once_and_keeps_the_beat);
    RUN(safe_stop_ble_or_keepalive_loss_during_clear_arm_nothing);
    RUN(safe_stop_ble_or_keepalive_loss_during_the_settle_arm_nothing);
    RUN(a_control_write_carrying_arm_is_given_up_after_safe);
    RUN(the_all_off_writes_are_never_given_up_and_end_every_other_exchange);
    return test_summary("node_srv_policy");
}
