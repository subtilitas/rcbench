/*
 * The panel's rule for an error: every output off and the bench SAFE.
 *
 * Under test: error_stop() for each of the 8 states of its three inputs --
 * the disarm only for a bench ARMED or on its way, the supply's output off
 * only when no programming port holds it; error_stop_take() acting once per
 * change of a counter raised on another task, also across its wrap from
 * 2^32 - 1 to 0 and for a jump of more than one; and which of the supply
 * link's events are errors (supply_link_ev_error()).
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "error_stop.h"
#include "supply_link.h"

TEST_CASE(every_state_of_the_rule)
{
    for (int k = 0; k < 8; ++k) {
        const bool live = (k & 1) != 0;
        const bool on   = (k & 2) != 0;
        const bool port = (k & 4) != 0;
        const error_stop_t e = error_stop(live, on, port);
        CHECK_EQ(e.disarm, live);
        CHECK_EQ(e.supply_off, on && !port);
    }
}

TEST_CASE(a_counter_acts_once_per_change)
{
    error_watch_t w;
    error_watch_init(&w, 5u);
    error_stop_t e = error_stop_take(&w, 5u, true, true, false);
    CHECK(!e.disarm && !e.supply_off);                /* no change */
    e = error_stop_take(&w, 6u, true, true, false);
    CHECK(e.disarm && e.supply_off);
    e = error_stop_take(&w, 6u, true, true, false);
    CHECK(!e.disarm && !e.supply_off);                /* taken once */
    e = error_stop_take(&w, 9u, false, true, true);   /* three at once */
    CHECK(!e.disarm && !e.supply_off);                /* SAFE, port holds */
    e = error_stop_take(&w, 9u, true, true, false);
    CHECK(!e.disarm);
    /* Across the wrap. */
    error_watch_init(&w, 0xFFFFFFFFu);
    e = error_stop_take(&w, 0u, true, false, false);
    CHECK(e.disarm && !e.supply_off);
    CHECK_EQ(w.seen, 0u);
    /* No watch: nothing. */
    e = error_stop_take(NULL, 1u, true, true, false);
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

int main(void)
{
    RUN(every_state_of_the_rule);
    RUN(a_counter_acts_once_per_change);
    RUN(which_supply_events_are_errors);
    return test_summary("error_stop");
}
