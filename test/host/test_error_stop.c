/*
 * The panel's rule for an error: every output off and the bench SAFE.
 *
 * Under test: error_stop() for each of the 16 states of its four inputs --
 * the disarm only for a bench ARMED or on its way; with an IOmcu of link
 * 4.31 the supply's output off on every error, with an older one only when
 * no programming port holds it; error_stop_take() acting once per change
 * of a counter raised on another task, also across its wrap from 2^32 - 1
 * to 0 and for a jump of more than one; which of the supply link's events
 * are errors (supply_link_ev_error()); which KST outcomes
 * (kst_link_outcome_fault()) and Hitec session results
 * (hitec_prog_ses_fault()) are session faults, counted by the Hitec gate;
 * and the supply link's OFF for an error (supply_link_cut()): written
 * alone, before anything else, with the page read off, again after no
 * answer, once after an answer, and dropped with the link.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "error_stop.h"
#include "hitec_prog.h"
#include "kst_link.h"
#include "supply_link.h"

TEST_CASE(every_state_of_the_rule)
{
    for (int k = 0; k < 16; ++k) {
        const bool live   = (k & 1) != 0;
        const bool on     = (k & 2) != 0;
        const bool port   = (k & 4) != 0;
        const bool defers = (k & 8) != 0;
        const error_stop_t e = error_stop(live, on, port, defers);
        CHECK_EQ(e.disarm, live);
        /* A 4.30 IOmcu: a port that holds the supply keeps it.  A 4.31
         * one: off, whoever holds it and whatever the panel reads. */
        CHECK_EQ(e.supply_off, defers || (on && !port));
    }
}

TEST_CASE(a_counter_acts_once_per_change)
{
    error_watch_t w;
    error_watch_init(&w, 5u);
    error_stop_t e = error_stop_take(&w, 5u, true, true, false, false);
    CHECK(!e.disarm && !e.supply_off);                /* no change */
    e = error_stop_take(&w, 6u, true, true, false, false);
    CHECK(e.disarm && e.supply_off);
    e = error_stop_take(&w, 6u, true, true, false, false);
    CHECK(!e.disarm && !e.supply_off);                /* taken once */
    e = error_stop_take(&w, 9u, false, true, true, false); /* three at once */
    CHECK(!e.disarm && !e.supply_off);           /* SAFE, port holds, 4.30 */
    e = error_stop_take(&w, 9u, true, true, false, false);
    CHECK(!e.disarm);
    e = error_stop_take(&w, 10u, false, false, true, true);
    CHECK(!e.disarm && e.supply_off);           /* 4.31: off, port or not */
    e = error_stop_take(&w, 10u, false, false, true, true);
    CHECK(!e.disarm);
    /* Across the wrap. */
    error_watch_init(&w, 0xFFFFFFFFu);
    e = error_stop_take(&w, 0u, true, false, false, false);
    CHECK(e.disarm && !e.supply_off);
    CHECK_EQ(w.seen, 0u);
    /* No watch: nothing. */
    e = error_stop_take(NULL, 1u, true, true, false, true);
    CHECK(!e.disarm && !e.supply_off);
    error_watch_init(NULL, 1u);
}

TEST_CASE(which_supply_events_are_errors)
{
    CHECK(supply_link_ev_error(SUPPLY_LINK_EV_ON_REFUSED));
    CHECK(supply_link_ev_error(SUPPLY_LINK_EV_ON_LOST));
    CHECK(supply_link_ev_error(SUPPLY_LINK_EV_STUCK));
    CHECK(supply_link_ev_error(SUPPLY_LINK_EV_SET_STUCK));
    CHECK(supply_link_ev_error(SUPPLY_LINK_EV_TRIPPED));
    CHECK(supply_link_ev_error(SUPPLY_LINK_EV_SAGGED));
    CHECK(!supply_link_ev_error(0u));
    CHECK(!supply_link_ev_error(SUPPLY_LINK_EV_WIRING_REFUSED));
    CHECK(!supply_link_ev_error(SUPPLY_LINK_EV_WIRING_LIVE));
    CHECK(!supply_link_ev_error(SUPPLY_LINK_EV_BAUD_FOUND));
    CHECK(supply_link_ev_error(SUPPLY_LINK_EV_BAUD_FOUND
                               | SUPPLY_LINK_EV_TRIPPED));
}

TEST_CASE(which_kst_outcomes_are_faults)
{
    static const unsigned k_fault[] = {
        KST_SES_ERR_NO_SERVO, KST_SES_ERR_READ, KST_SES_ERR_CHANGED,
        KST_SES_ERR_WRITE_FAILED, KST_SES_ERR_ROLLED_BACK, KST_SES_ERR_TORN,
        KST_SES_ERR_UNINTENDED, KST_SES_ERR_VERIFY, KST_SES_ERR_DRIVER,
        LINK_KST_RESULT_SUPPLY,
    };
    kst_link_outcome_t o;
    unsigned faults = 0;

    for (unsigned r = 0; r < 256u; ++r) {
        bool want = false;

        for (unsigned i = 0; i < sizeof(k_fault) / sizeof(k_fault[0]); ++i) {
            want = want || k_fault[i] == r;
        }
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.result = (uint8_t)r;
        CHECK_EQ(kst_link_outcome_fault(&o), want);
        faults += want ? 1u : 0u;
        /* The same result in any other kind of outcome is none. */
        o.kind = KST_LINK_OUT_REFUSED;
        CHECK(!kst_link_outcome_fault(&o));
    }
    CHECK_EQ(faults, 10u);
    CHECK(!kst_link_outcome_fault(NULL));
}

TEST_CASE(which_hitec_results_are_faults_and_the_gate_counts_them)
{
    static const unsigned k_fault[] = {
        HITEC_SES_ERR_NO_SERVO, HITEC_SES_ERR_ENTRY, HITEC_SES_ERR_READ,
        HITEC_SES_ERR_WRITE, HITEC_SES_ERR_VERIFY, HITEC_SES_ERR_STEP,
    };
    unsigned faults = 0;

    for (unsigned r = 0; r < 256u; ++r) {
        bool want = false;

        for (unsigned i = 0; i < sizeof(k_fault) / sizeof(k_fault[0]); ++i) {
            want = want || k_fault[i] == r;
        }
        CHECK_EQ(hitec_prog_ses_fault(r), want);
        faults += want ? 1u : 0u;
    }
    CHECK_EQ(faults, 6u);
    CHECK(!hitec_prog_ses_fault(HITEC_SES_ERR_ABORTED));
    CHECK(!hitec_prog_ses_fault(HITEC_SES_ERR_CUT));
    CHECK(!hitec_prog_ses_fault(HITEC_SES_ERR_LINK));
    CHECK(!hitec_prog_ses_fault(HITEC_SES_OK));
    CHECK_EQ(hitec_prog_gate_faults(NULL), 0u);
}

/* The page as the link reads it: wired, output off, no port. */
static void link_up_off(supply_link_t *s)
{
    uint16_t regs[LINK_SP_COUNT];
    const supply_wiring_t w = { true, 8, 9, 1u };
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t out[LINK_SP_FLAGS];

    supply_link_init(s);
    supply_link_wire(s, &w);
    /* Who holds the page; then the OFF owed at the start, the wiring and the
     * set points. */
    memset(regs, 0, sizeof(regs));
    supply_link_read(s, regs, 0u);
    for (int k = 0; k < 3; ++k) {
        if (supply_link_next(s, &off, &n, out) == SUPPLY_LINK_W_NONE) {
            break;
        }
        supply_link_written(s, SUPPLY_LINK_ACK);
        memset(regs, 0, sizeof(regs));
        regs[LINK_SP_ENABLE] = 1u;
        regs[LINK_SP_TX_PIN] = 8u;
        regs[LINK_SP_RX_PIN] = 9u;
        regs[LINK_SP_BAUD] = 1u;
        regs[LINK_SP_SET_MV] = s->page_mv;
        regs[LINK_SP_SET_MA] = s->page_ma;
        regs[LINK_SP_FLAGS] = LINK_SP_ONLINE;
        supply_link_read(s, regs, 0u);
    }
    CHECK(supply_link_settled(s));
    CHECK_EQ(supply_link_next(s, &off, &n, out), SUPPLY_LINK_W_NONE);
}

TEST_CASE(an_error_writes_the_off_with_the_page_read_off)
{
    supply_link_t s;
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t out[LINK_SP_FLAGS];

    link_up_off(&s);
    supply_link_cut(&s);
    CHECK(!supply_link_settled(&s));
    /* Alone, OUTPUT 0, and again after no answer. */
    CHECK_EQ(supply_link_next(&s, &off, &n, out), SUPPLY_LINK_W_OFF);
    CHECK_EQ(off, LINK_SP_OUTPUT);
    CHECK_EQ(n, 1u);
    CHECK_EQ(out[0], 0u);
    supply_link_written(&s, SUPPLY_LINK_NO_ANSWER);
    CHECK_EQ(supply_link_next(&s, &off, &n, out), SUPPLY_LINK_W_OFF);
    /* Answered: once. */
    supply_link_written(&s, SUPPLY_LINK_ACK);
    CHECK_EQ(supply_link_next(&s, &off, &n, out), SUPPLY_LINK_W_NONE);
    CHECK(supply_link_settled(&s));
    /* A refusal ends it too. */
    supply_link_cut(&s);
    CHECK_EQ(supply_link_next(&s, &off, &n, out), SUPPLY_LINK_W_OFF);
    supply_link_written(&s, (int)LINK_NACK_BAD_VALUE);
    CHECK_EQ(supply_link_next(&s, &off, &n, out), SUPPLY_LINK_W_NONE);
    /* An ON asked is off with it; the link lost drops it. */
    supply_link_command(&s, true, 5000u, 1000u);
    supply_link_cut(&s);
    CHECK(!s.on);
    supply_link_lost(&s);
    CHECK(!s.off_owed);
    supply_link_cut(NULL);
}

int main(void)
{
    RUN(every_state_of_the_rule);
    RUN(a_counter_acts_once_per_change);
    RUN(which_supply_events_are_errors);
    RUN(which_kst_outcomes_are_faults);
    RUN(which_hitec_results_are_faults_and_the_gate_counts_them);
    RUN(an_error_writes_the_off_with_the_page_read_off);
    return test_summary("error_stop");
}
