/*
 * The throttle's pulse endpoints on their way to the IOmcu, and the switch
 * to ARMED that waits for them.
 *
 * The frame that switches to ARMED carries no CHAN_CFG, so an Idle pulse or
 * Full pulse edit the IOmcu has not taken would run the throttle on the
 * range it held before.  The debt (endpoints_link.c) and the arming
 * policy's verdict on it (arming_handshake_step()) are the real code here;
 * what stands in for the control task is the order it calls them in: the
 * poll of a pass pays the debt, a later pass's switch to ARMED asks it,
 * and an ENDPOINTS verdict before CLEAR ends the arm as
 * arm_write_failed() does when every exchange was answered.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdint.h>

#include "arming.h"
#include "endpoints_link.h"
#include "heartbeat.h"

#define SETTLE_MS HEARTBEAT_SETTLE_MS
#define WAIT_MS   ARMING_LINE_WAIT_MS

static endpoints_debt_t d;
static arming_t         a;
static unsigned         s_paid;   /* payments the pay step made */
static bool             s_cleared; /* the last arm_at() wrote CLEAR */

static void debt_reset(void)
{
    memset(&d, 0, sizeof(d));
    s_paid = 0u;
}

/* A poll at @p now whose control write put ARM = 0 at the far end
 * (@p far_disarmed), with a payment, if one is due, that ends in @p got. */
static endpoints_next_t poll_at(uint32_t now, bool far_disarmed,
                                endpoints_outcome_t got)
{
    const endpoints_next_t next = endpoints_take(&d, far_disarmed, now);
    if (next == ENDPOINTS_PAY) {
        ++s_paid;
        endpoints_paid(&d, got);
    }
    return next;
}

/* An arm the policy hands out at @p at, asked of a far end that trusts the
 * line. */
static void hand_out(uint32_t at)
{
    const uint32_t t0 = at - SETTLE_MS;
    arming_init(&a, t0 - 10u, SETTLE_MS);
    arming_set_line_wait(&a, WAIT_MS);
    arming_touch_seen(&a, t0);
    arming_request_arm(&a, t0);
    arming_touch_seen(&a, at);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, at), ARMING_ACT_ARM);
}

/*
 * The switch to ARMED at @p at, as service_arming() writes it: the debt is
 * asked before CLEAR and after it.  A refusal before CLEAR, with every
 * exchange answered, ends the arm as arm_write_failed() does.  Returns the
 * verdict that ended it, ARMING_HS_GO for the frame that arms.
 */
static arming_hs_t arm_at(uint32_t at, bool link_up)
{
    s_cleared = false;
    hand_out(at);
    arming_hs_facts_t f = { .link_up      = link_up,
                            .rate_settled = true };
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_HANDED, &f, at),
             ARMING_HS_GO);
    f.endpoints_owed = endpoints_owed(&d);
    const arming_hs_t prepared =
        arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at);
    if (prepared == ARMING_HS_ENDPOINTS) {
        (void)arming_write_failed(&a, true);
        return prepared;
    }
    if (prepared != ARMING_HS_GO) {
        return prepared;
    }
    s_cleared = true;
    f.endpoints_owed = endpoints_owed(&d);
    return arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at + 1u);
}

/* The bench is SAFE after a refusal: nothing armed or arming, no stop, the
 * heartbeat up, and no arm handed out again by itself.  @p before_clear:
 * the refusal came before CLEAR, so nothing was written for the arm. */
static void check_safe_and_still(uint32_t at, bool before_clear)
{
    CHECK_EQ(s_cleared, !before_clear);
    CHECK(!a.armed);
    CHECK(!a.arming);
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK(arming_heartbeat(&a, at));
    for (uint32_t t = at + 5u; t != at + 2005u; t += 50u) {
        arming_touch_seen(&a, t);
        CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    }
}

/* ---------------------------------------------------------- the debt alone */

TEST_CASE(the_debt_rests_299_ms_and_is_due_at_300)
{
    const uint32_t e = 5000u;
    CHECK_EQ(ENDPOINTS_SETTLE_MS, 300u);
    CHECK_EQ(endpoints_next(false, true, e, e + 299u), ENDPOINTS_PAID);
    CHECK_EQ(endpoints_next(false, false, e, e), ENDPOINTS_PAID);
    CHECK_EQ(endpoints_next(true, true, e, e), ENDPOINTS_WAIT_SETTLE);
    CHECK_EQ(endpoints_next(true, true, e, e + 299u), ENDPOINTS_WAIT_SETTLE);
    CHECK_EQ(endpoints_next(true, true, e, e + 300u), ENDPOINTS_PAY);
    CHECK_EQ(endpoints_next(true, true, e, e + 301u), ENDPOINTS_PAY);
    /* A far end not held SAFE waits, however long the edit has rested. */
    CHECK_EQ(endpoints_next(true, false, e, e + 299u),
             ENDPOINTS_WAIT_DISARM);
    CHECK_EQ(endpoints_next(true, false, e, e + 300000u),
             ENDPOINTS_WAIT_DISARM);
}

TEST_CASE(the_rest_runs_across_the_millisecond_wrap)
{
    const uint32_t bases[] = { 0xFFFFFFFFu - 299u, 0xFFFFFFFFu - 150u,
                               0xFFFFFFFFu, 0u };
    for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        const uint32_t e = bases[i];
        CHECK_EQ(endpoints_next(true, true, e, e + 299u),
                 ENDPOINTS_WAIT_SETTLE);
        CHECK_EQ(endpoints_next(true, true, e, e + 300u), ENDPOINTS_PAY);
        CHECK_EQ(endpoints_next(true, true, e, e + 301u), ENDPOINTS_PAY);
    }
}

TEST_CASE(only_a_missing_answer_leaves_the_debt_owed)
{
    CHECK(endpoints_owed_after(ENDPOINTS_NO_ANSWER));
    CHECK(!endpoints_owed_after(ENDPOINTS_READ_REFUSED));
    CHECK(!endpoints_owed_after(ENDPOINTS_ORDER));
    CHECK(!endpoints_owed_after(ENDPOINTS_HELD));
    CHECK(!endpoints_owed_after(ENDPOINTS_TAKEN));
    CHECK(!endpoints_owed_after(ENDPOINTS_WRITE_REFUSED));
    CHECK(endpoints_owed_after((endpoints_outcome_t)99));
}

/* A publish carries the pair, owes it, and pays it once at its rest. */
TEST_CASE(a_publish_owes_its_pair_once)
{
    debt_reset();
    CHECK(!endpoints_owed(&d));
    CHECK_EQ(poll_at(100u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAID);

    endpoints_publish(&d, 1100u, 1940u, 1000u);
    CHECK(endpoints_owed(&d));
    CHECK(!endpoints_held(&d));
    CHECK_EQ(endpoints_min(&d), 1100u);
    CHECK_EQ(endpoints_max(&d), 1940u);
    /* The ends of both settings' ranges, in one word. */
    endpoints_publish(&d, 1600u, 2400u, 1000u);
    CHECK_EQ(endpoints_min(&d), 1600u);
    CHECK_EQ(endpoints_max(&d), 2400u);
    endpoints_publish(&d, 800u, 1400u, 1000u);
    CHECK_EQ(endpoints_min(&d), 800u);
    CHECK_EQ(endpoints_max(&d), 1400u);

    CHECK_EQ(poll_at(1299u, true, ENDPOINTS_TAKEN), ENDPOINTS_WAIT_SETTLE);
    CHECK_EQ(poll_at(1300u, false, ENDPOINTS_TAKEN), ENDPOINTS_WAIT_DISARM);
    CHECK_EQ(s_paid, 0u);
    CHECK_EQ(poll_at(1300u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAY);
    CHECK_EQ(s_paid, 1u);
    CHECK(!endpoints_owed(&d));
    CHECK_EQ(poll_at(1350u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAID);
    CHECK_EQ(s_paid, 1u);

    /* Nothing to judge. */
    endpoints_publish(NULL, 1000u, 2000u, 0u);
    endpoints_link_up(NULL);
    endpoints_paid(NULL, ENDPOINTS_NO_ANSWER);
    CHECK(!endpoints_owed(NULL));
    CHECK(!endpoints_held(NULL));
    CHECK_EQ(endpoints_min(NULL), 0u);
    CHECK_EQ(endpoints_max(NULL), 0u);
    CHECK_EQ(endpoints_take(NULL, true, 0u), ENDPOINTS_WAIT_DISARM);
}

/* An edit published while a payment is on the wire stays owed, whatever
 * that payment came to, and rests from its own time. */
TEST_CASE(an_edit_during_a_payment_stays_owed)
{
    const endpoints_outcome_t outcomes[] = {
        ENDPOINTS_NO_ANSWER, ENDPOINTS_READ_REFUSED, ENDPOINTS_ORDER,
        ENDPOINTS_HELD,      ENDPOINTS_TAKEN,        ENDPOINTS_WRITE_REFUSED,
    };
    for (unsigned i = 0; i < sizeof(outcomes) / sizeof(outcomes[0]); ++i) {
        debt_reset();
        endpoints_publish(&d, 1000u, 2000u, 0u);
        CHECK_EQ(endpoints_take(&d, true, 300u), ENDPOINTS_PAY);
        CHECK(!endpoints_owed(&d));
        endpoints_publish(&d, 1050u, 2000u, 310u);
        endpoints_paid(&d, outcomes[i]);
        CHECK(endpoints_owed(&d));
        CHECK_EQ(endpoints_min(&d), 1050u);
        CHECK_EQ(endpoints_take(&d, true, 609u), ENDPOINTS_WAIT_SETTLE);
        CHECK_EQ(endpoints_take(&d, true, 610u), ENDPOINTS_PAY);
    }
}

/* A link-up owes the pair again and holds the far end SAFE until it is
 * paid; a missing answer keeps both, any answer ends both. */
TEST_CASE(a_link_up_holds_until_an_answer)
{
    debt_reset();
    endpoints_publish(&d, 1000u, 2000u, 0u);
    CHECK_EQ(poll_at(300u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAY);

    endpoints_link_up(&d);
    CHECK(endpoints_owed(&d));
    CHECK(endpoints_held(&d));
    /* The edit rested long ago: due at the first poll. */
    CHECK_EQ(poll_at(90000u, true, ENDPOINTS_NO_ANSWER), ENDPOINTS_PAY);
    CHECK(endpoints_owed(&d));
    CHECK(endpoints_held(&d));
    CHECK_EQ(poll_at(90050u, true, ENDPOINTS_HELD), ENDPOINTS_PAY);
    CHECK(!endpoints_owed(&d));
    CHECK(!endpoints_held(&d));

    const endpoints_outcome_t answers[] = {
        ENDPOINTS_READ_REFUSED, ENDPOINTS_ORDER, ENDPOINTS_TAKEN,
        ENDPOINTS_WRITE_REFUSED,
    };
    for (unsigned i = 0; i < sizeof(answers) / sizeof(answers[0]); ++i) {
        endpoints_link_up(&d);
        CHECK_EQ(poll_at(90100u, true, answers[i]), ENDPOINTS_PAY);
        CHECK(!endpoints_owed(&d));
        CHECK(!endpoints_held(&d));
    }

    /* A hold with nothing owed ends at the next look. */
    endpoints_link_up(&d);
    atomic_store(&d.owed, false);
    CHECK(endpoints_held(&d));
    CHECK_EQ(poll_at(90200u, false, ENDPOINTS_TAKEN), ENDPOINTS_PAID);
    CHECK(!endpoints_held(&d));
}

/* --------------------------------------------- the switch to ARMED on it */

/* An arm inside the 300 ms rest is refused, the bench stays SAFE and the
 * reason is the endpoints'; the hold pressed again after the payment
 * arms. */
TEST_CASE(an_arm_inside_the_rest_is_refused)
{
    const uint32_t e = 10000u;
    debt_reset();
    endpoints_publish(&d, 1100u, 1900u, e);
    for (uint32_t t = e; t < e + 300u; t += 50u) {
        CHECK_EQ(poll_at(t, true, ENDPOINTS_TAKEN), ENDPOINTS_WAIT_SETTLE);
        CHECK_EQ(arm_at(t + 5u, true), ARMING_HS_ENDPOINTS);
        check_safe_and_still(t + 5u, true);
    }
    CHECK_EQ(s_paid, 0u);
    CHECK_EQ(poll_at(e + 300u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAY);
    CHECK_EQ(arm_at(e + 305u, true), ARMING_HS_GO);
    CHECK(a.armed);
}

/* The poll before the arm ran 299, 300 or 301 ms after the edit. */
TEST_CASE(an_arm_after_a_poll_at_299_300_and_301_ms)
{
    const uint32_t bases[] = { 10000u, 0xFFFFFFFFu - 150u };
    for (unsigned b = 0; b < 2u; ++b) {
        const uint32_t e = bases[b];
        for (uint32_t dt = 299u; dt <= 301u; ++dt) {
            debt_reset();
            endpoints_publish(&d, 1000u, 2000u, e);
            (void)poll_at(e + dt, true, ENDPOINTS_TAKEN);
            const arming_hs_t got = arm_at(e + dt + 1u, true);
            if (dt < ENDPOINTS_SETTLE_MS) {
                CHECK_EQ(got, ARMING_HS_ENDPOINTS);
                CHECK(!s_cleared);
                CHECK(!a.armed);
                CHECK(endpoints_owed(&d));
            } else {
                CHECK_EQ(got, ARMING_HS_GO);
                CHECK(a.armed);
                CHECK(!endpoints_owed(&d));
            }
        }
    }
}

/* A far end that already holds the pair is the pair in force: no write,
 * and the arm goes on. */
TEST_CASE(a_page_that_holds_the_pair_lets_the_arm_go)
{
    debt_reset();
    endpoints_publish(&d, 1000u, 2000u, 0u);
    CHECK_EQ(poll_at(300u, true, ENDPOINTS_HELD), ENDPOINTS_PAY);
    CHECK_EQ(arm_at(301u, true), ARMING_HS_GO);
}

/* A write nobody answered stays owed: every arm is refused until one is
 * answered, and none after. */
TEST_CASE(an_unanswered_write_refuses_until_one_is_answered)
{
    debt_reset();
    endpoints_publish(&d, 1000u, 2000u, 0u);
    for (uint32_t t = 300u; t < 1300u; t += 50u) {
        CHECK_EQ(poll_at(t, true, ENDPOINTS_NO_ANSWER), ENDPOINTS_PAY);
        CHECK_EQ(arm_at(t + 1u, true), ARMING_HS_ENDPOINTS);
        check_safe_and_still(t + 1u, true);
    }
    CHECK_EQ(poll_at(1300u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAY);
    CHECK_EQ(arm_at(1301u, true), ARMING_HS_GO);
}

/* A refusal ends the debt with its alert: the far end keeps the pair it
 * held, and the arm is not held up by a request refused every time. */
TEST_CASE(a_refused_write_ends_the_debt)
{
    const endpoints_outcome_t refusals[] = {
        ENDPOINTS_READ_REFUSED, ENDPOINTS_ORDER, ENDPOINTS_WRITE_REFUSED,
    };
    for (unsigned i = 0; i < 3u; ++i) {
        debt_reset();
        endpoints_publish(&d, 1000u, 2000u, 0u);
        CHECK_EQ(poll_at(300u, true, refusals[i]), ENDPOINTS_PAY);
        CHECK(!endpoints_owed(&d));
        CHECK_EQ(arm_at(301u, true), ARMING_HS_GO);
    }
}

/*
 * The link lost while the pair is owed: with no link the panel's own bank
 * arms and nothing is written, the far end included.  The link that comes
 * back owes the pair again and holds the far end; the arm is refused until
 * the next poll pays it, and not after.
 */
TEST_CASE(a_link_lost_while_owed_leaves_no_stuck_refusal)
{
    debt_reset();
    endpoints_publish(&d, 1200u, 1800u, 0u);
    CHECK_EQ(poll_at(100u, true, ENDPOINTS_TAKEN), ENDPOINTS_WAIT_SETTLE);
    /* The link goes: no poll pays, and the arm is this end's alone. */
    CHECK_EQ(arm_at(400u, false), ARMING_HS_LOCAL);
    CHECK(endpoints_owed(&d));
    /* Switched to SAFE again, the link returns at 5000 ms. */
    endpoints_link_up(&d);
    CHECK_EQ(arm_at(5000u, true), ARMING_HS_ENDPOINTS);
    check_safe_and_still(5000u, true);
    CHECK_EQ(poll_at(5050u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAY);
    CHECK(!endpoints_held(&d));
    CHECK_EQ(arm_at(5051u, true), ARMING_HS_GO);

    /* A link that goes again before the answer: owed and held still, and
     * paid at the next link-up's poll. */
    endpoints_publish(&d, 1250u, 1800u, 6000u);
    endpoints_link_up(&d);
    CHECK_EQ(poll_at(6300u, true, ENDPOINTS_NO_ANSWER), ENDPOINTS_PAY);
    CHECK(endpoints_held(&d));
    CHECK_EQ(arm_at(6301u, true), ARMING_HS_ENDPOINTS);
    endpoints_link_up(&d);
    CHECK_EQ(poll_at(9000u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAY);
    CHECK_EQ(arm_at(9001u, true), ARMING_HS_GO);
}

/* An edit published while CLEAR is on the wire: the frame that arms is not
 * written. */
TEST_CASE(an_edit_during_clear_is_not_armed_over)
{
    debt_reset();
    hand_out(1000u);
    arming_hs_facts_t f = { .link_up = true, .rate_settled = true };
    f.endpoints_owed = endpoints_owed(&d);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, 1000u),
             ARMING_HS_GO);
    s_cleared = true;
    endpoints_publish(&d, 1000u, 2000u, 1000u);
    f.endpoints_owed = endpoints_owed(&d);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, 1001u),
             ARMING_HS_ENDPOINTS);
    check_safe_and_still(1001u, false);
}

/* STOP latches whatever the debt: owed, refused or in the middle of a
 * payment. */
TEST_CASE(stop_works_while_the_pair_is_owed)
{
    debt_reset();
    endpoints_publish(&d, 1000u, 2000u, 0u);
    CHECK_EQ(arm_at(150u, true), ARMING_HS_ENDPOINTS);
    arming_stop_pressed(&a);
    CHECK(a.stopped);
    CHECK_EQ(arming_stop_count(&a), 1);
    CHECK(!arming_heartbeat(&a, 151u));
    CHECK(endpoints_owed(&d));

    /* Pressed while the payment is on the wire. */
    CHECK_EQ(endpoints_take(&d, true, 300u), ENDPOINTS_PAY);
    arming_stop_pressed(&a);
    CHECK_EQ(arming_stop_count(&a), 2);
    endpoints_paid(&d, ENDPOINTS_TAKEN);
    CHECK(a.stopped);
    CHECK_EQ(arming_step(&a, 301u), ARMING_ACT_NONE);
}

/* The rest and the refusal across the millisecond wrap. */
TEST_CASE(the_refusal_holds_across_the_millisecond_wrap)
{
    const uint32_t e = 0xFFFFFFFFu - 99u;   /* wraps 100 ms after the edit */
    debt_reset();
    endpoints_publish(&d, 1000u, 2000u, e);
    CHECK_EQ(poll_at(e + 150u, true, ENDPOINTS_TAKEN), ENDPOINTS_WAIT_SETTLE);
    CHECK_EQ(arm_at(e + 151u, true), ARMING_HS_ENDPOINTS);
    check_safe_and_still(e + 151u, true);
    CHECK_EQ(poll_at(e + 299u, true, ENDPOINTS_TAKEN), ENDPOINTS_WAIT_SETTLE);
    CHECK_EQ(arm_at(e + 299u, true), ARMING_HS_ENDPOINTS);
    CHECK_EQ(poll_at(e + 300u, true, ENDPOINTS_TAKEN), ENDPOINTS_PAY);
    CHECK_EQ(arm_at(e + 300u, true), ARMING_HS_GO);
}

int main(void)
{
    RUN(the_debt_rests_299_ms_and_is_due_at_300);
    RUN(the_rest_runs_across_the_millisecond_wrap);
    RUN(only_a_missing_answer_leaves_the_debt_owed);
    RUN(a_publish_owes_its_pair_once);
    RUN(an_edit_during_a_payment_stays_owed);
    RUN(a_link_up_holds_until_an_answer);
    RUN(an_arm_inside_the_rest_is_refused);
    RUN(an_arm_after_a_poll_at_299_300_and_301_ms);
    RUN(a_page_that_holds_the_pair_lets_the_arm_go);
    RUN(an_unanswered_write_refuses_until_one_is_answered);
    RUN(a_refused_write_ends_the_debt);
    RUN(a_link_lost_while_owed_leaves_no_stuck_refusal);
    RUN(an_edit_during_clear_is_not_armed_over);
    RUN(stop_works_while_the_pair_is_owed);
    RUN(the_refusal_holds_across_the_millisecond_wrap);
    return test_summary("endpoints_link");
}
