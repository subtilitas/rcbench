/*
 * The panel's rule for an error: every output off and the bench SAFE.
 *
 * Under test: error_stop() for each of the 8 states of its three inputs --
 * the disarm only for a bench ARMED or on its way, the supply's output off
 * only when no programming port holds it, the port's cut when one does;
 * error_stop_take() acting once per change of a counter raised on another
 * task, also across its wrap from 2^32 - 1 to 0 and for a jump of more
 * than one; which of the supply link's events are errors
 * (supply_link_ev_error()); and the port's cut (port_cut_step()): at once
 * with no write, at the write's end, at ERROR_PORT_CUT_MS - 1 not yet and
 * at ERROR_PORT_CUT_MS while the write runs, across the clock's wrap, the
 * first error's time kept, a supply switched on again by the port, the cut
 * ending only once the port lets go with the output off, no write waited
 * for once it has acted, and the HITEC steps it blocks.
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
        CHECK_EQ(e.port_cut, port);
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
    CHECK(e.port_cut);
    e = error_stop_take(&w, 9u, true, true, false);
    CHECK(!e.disarm);
    /* Across the wrap. */
    error_watch_init(&w, 0xFFFFFFFFu);
    e = error_stop_take(&w, 0u, true, false, false);
    CHECK(e.disarm && !e.supply_off);
    CHECK_EQ(w.seen, 0u);
    /* No watch: nothing. */
    e = error_stop_take(NULL, 1u, true, true, true);
    CHECK(!e.disarm && !e.supply_off && !e.port_cut);
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

/* A cut asked at @p t0 with the port holding the supply, on. */
static port_cut_t asked_at(uint32_t t0)
{
    port_cut_t c = { false, false, 0u };

    port_cut_ask(&c, t0);
    CHECK(c.armed && !c.acted);
    CHECK(!port_cut_blocks(&c));
    return c;
}

TEST_CASE(an_error_with_no_write_cuts_at_once)
{
    port_cut_t c = asked_at(1000u);

    CHECK(port_cut_step(&c, true, false, true, 1000u));
    CHECK(port_cut_blocks(&c));
    /* Off: nothing more while the port holds, and the cut stands. */
    CHECK(!port_cut_step(&c, true, false, false, 1005u));
    CHECK(c.armed);
    CHECK(port_cut_blocks(&c));
    /* The port lets go with the output off: the cut ends. */
    CHECK(!port_cut_step(&c, false, false, false, 1010u));
    CHECK(!c.armed && !port_cut_blocks(&c));
    /* An operator's ON afterwards is no business of it. */
    CHECK(!port_cut_step(&c, false, false, true, 1015u));
}

TEST_CASE(an_error_during_a_write_cuts_at_its_end)
{
    port_cut_t c = asked_at(500u);

    for (uint32_t t = 500u; t < 500u + ERROR_PORT_CUT_MS; t += 997u) {
        CHECK(!port_cut_step(&c, true, true, true, t));
        CHECK(!port_cut_blocks(&c));
    }
    /* The write ends 1ms before the bound: off in that look. */
    CHECK(port_cut_step(&c, true, false, true, 500u + ERROR_PORT_CUT_MS - 1u));
    CHECK(port_cut_blocks(&c));
}

TEST_CASE(a_write_still_running_is_cut_at_the_bound)
{
    port_cut_t c = asked_at(0u);

    CHECK(!port_cut_step(&c, true, true, true, ERROR_PORT_CUT_MS - 1u));
    CHECK(port_cut_step(&c, true, true, true, ERROR_PORT_CUT_MS));
    /* Across the clock's wrap: 2^32 - 3 plus the bound. */
    c = asked_at(0xFFFFFFFDu);
    CHECK(!port_cut_step(&c, true, true, true,
                         0xFFFFFFFDu + ERROR_PORT_CUT_MS - 1u));
    CHECK(port_cut_step(&c, true, true, true,
                        0xFFFFFFFDu + ERROR_PORT_CUT_MS));
}

TEST_CASE(a_second_error_keeps_the_first_errors_time)
{
    port_cut_t c = asked_at(100u);

    port_cut_ask(&c, 100u + ERROR_PORT_CUT_MS - 10u);
    CHECK_EQ(c.t0, 100u);
    CHECK(port_cut_step(&c, true, true, true, 100u + ERROR_PORT_CUT_MS));
    /* Asked again after it acted: it stays acted. */
    port_cut_ask(&c, 100u + ERROR_PORT_CUT_MS + 1u);
    CHECK(port_cut_blocks(&c));
}

TEST_CASE(a_supply_the_port_switches_on_again_is_cut_again)
{
    port_cut_t c = asked_at(0u);

    CHECK(port_cut_step(&c, true, false, true, 0u));
    CHECK(!port_cut_step(&c, true, false, false, 5u));
    /* A KST power cycle's ON, a Hitec POWER: off again. */
    CHECK(port_cut_step(&c, true, false, true, 1200u));
    /* Once it has acted, a write is not waited for. */
    CHECK(port_cut_step(&c, true, true, true, 1300u));
    /* The port lets go with the output still on: off, then the end. */
    CHECK(port_cut_step(&c, false, false, true, 1400u));
    CHECK(c.armed);
    CHECK(!port_cut_step(&c, false, false, false, 1405u));
    CHECK(!c.armed);
}

TEST_CASE(a_cut_with_the_supply_off_waits_for_the_port)
{
    port_cut_t c = asked_at(0u);

    /* Off already, still held: it stands, and has not acted. */
    CHECK(!port_cut_step(&c, true, true, false, 0u));
    CHECK(!port_cut_step(&c, true, false, false, 20000u));
    CHECK(c.armed && !port_cut_blocks(&c));
    /* No cut asked: nothing, whatever the inputs. */
    port_cut_t idle = { false, false, 0u };
    CHECK(!port_cut_step(&idle, true, false, true, 0u));
    CHECK(!port_cut_blocks(&idle));
    CHECK(!port_cut_step(NULL, true, false, true, 0u));
    CHECK(!port_cut_blocks(NULL));
    port_cut_ask(NULL, 0u);
}

int main(void)
{
    RUN(every_state_of_the_rule);
    RUN(a_counter_acts_once_per_change);
    RUN(which_supply_events_are_errors);
    RUN(an_error_with_no_write_cuts_at_once);
    RUN(an_error_during_a_write_cuts_at_its_end);
    RUN(a_write_still_running_is_cut_at_the_bound);
    RUN(a_second_error_keeps_the_first_errors_time);
    RUN(a_supply_the_port_switches_on_again_is_cut_again);
    RUN(a_cut_with_the_supply_off_waits_for_the_port);
    return test_summary("error_stop");
}
