/*
 * The PROG_SUPPLY page: the programming ports' own supply on a board that
 * switches one with a pin (shared/outputs/prog_supply.c, protocol 4.32).
 *
 * Under test: the page at start reads off with its pin and voltage, and a
 * board without the switch reads nothing fitted and refuses ON; the
 * panel's ON wants a live heartbeat (NOT_ARMED) and a value of 0 or 1, and
 * past OUTPUT the page is read only; a lost heartbeat or a silent link
 * switches the pin off in the same step; under a port's hold the panel's
 * ON is refused and its OFF taken, held back from the pin while the port
 * writes, to SUPPLY_PAGE_WRITE_WAIT_MS after the OFF to the millisecond,
 * across the wrap of the clock at 2^32ms, and the port's ON refused until
 * it lets go; an OFF that finds a port's frame on the wire waits for the
 * frame's end, SUPPLY_PAGE_OFF_WAIT_MS at most; the port's view; a port
 * gets the supply only after the panel's PANEL 1, which the heartbeat
 * lost clears.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "prog_supply.h"

#define PIN 21u

static prog_supply_t pg;
static uint32_t      g_ms;

/* Register @p i as the page reads it. */
static uint16_t reg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    prog_supply_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

/* The panel writes OUTPUT = @p v. */
static uint8_t panel(uint16_t v, bool beat)
{
    return prog_supply_write(&pg, LINK_PS_OUTPUT, 1u, &v, beat);
}

/* One pass at g_ms: what the pin drives. */
static bool pass(bool beat, bool frame_out)
{
    prog_supply_port_frame(&pg, frame_out, g_ms);
    return prog_supply_step(&pg, beat);
}

/* The panel writes PANEL = @p v. */
static uint8_t declare(uint16_t v, bool beat)
{
    return prog_supply_write(&pg, LINK_PS_PANEL, 1u, &v, beat);
}

/* The page at its start, the panel's PANEL 1 written. */
static void fresh(void)
{
    prog_supply_init(&pg, PIN, PROG_SUPPLY_CARRIER_MV);
    g_ms = 1000u;
    CHECK_EQ(declare(1u, true), 0u);
}

TEST_CASE(the_page_starts_off_with_its_pin_and_voltage)
{
    fresh();
    CHECK(!pass(true, false));
    CHECK_EQ(reg(LINK_PS_OUTPUT), 0u);
    CHECK_EQ(reg(LINK_PS_FLAGS), LINK_PS_FITTED);
    CHECK_EQ(reg(LINK_PS_PIN), PIN);
    CHECK_EQ(reg(LINK_PS_MV), 5000u);
    CHECK_EQ(reg(LINK_PS_COUNT), 0u);           /* past the page: 0 */
    CHECK_EQ(panel(1u, true), 0u);
    CHECK_EQ(reg(LINK_PS_OUTPUT), 1u);
    CHECK(pass(true, false));
    CHECK_EQ(reg(LINK_PS_FLAGS), LINK_PS_FITTED | LINK_PS_ON);
    CHECK_EQ(panel(0u, true), 0u);
    CHECK(!pass(true, false));

    /* A board without the switch: nothing fitted, ON refused, OFF taken. */
    prog_supply_init(&pg, LINK_PS_NO_PIN, PROG_SUPPLY_CARRIER_MV);
    CHECK(!prog_supply_fitted(&pg));
    CHECK_EQ(reg(LINK_PS_FLAGS), 0u);
    CHECK_EQ(reg(LINK_PS_PIN), LINK_PS_NO_PIN);
    CHECK_EQ(reg(LINK_PS_MV), 0u);
    CHECK_EQ(panel(1u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(panel(0u, true), 0u);
    CHECK(!prog_supply_port_set(&pg, true, true));
    CHECK(!pass(true, false));
    supply_port_view_t v;
    prog_supply_port_view(&pg, &v);
    CHECK(!v.online);

    /* NULL everywhere is nothing. */
    prog_supply_init(NULL, PIN, 5000u);
    CHECK(!prog_supply_fitted(NULL));
    CHECK(!prog_supply_step(NULL, true));
    CHECK(!prog_supply_port_set(NULL, true, true));
    CHECK_EQ(prog_supply_write(NULL, 0u, 1u, (const uint16_t[]){ 1u }, true),
             LINK_NACK_BAD_RANGE);
    uint16_t out[2] = { 7u, 7u };
    prog_supply_read(NULL, 0u, 2u, out);
    CHECK_EQ(out[0], 0u);
    prog_supply_read(&pg, 0u, 1u, NULL);
    prog_supply_port_view(NULL, &v);
    CHECK(!v.online);
    prog_supply_port_view(&pg, NULL);
    prog_supply_hold(NULL, true);
    prog_supply_port_writes(NULL, true);
    prog_supply_port_frame(NULL, true, 0u);
}

TEST_CASE(the_panels_write_is_judged_at_its_limits)
{
    fresh();
    /* ON without a heartbeat: NOT_ARMED, nothing moves. */
    CHECK_EQ(panel(1u, false), LINK_NACK_NOT_ARMED);
    CHECK_EQ(reg(LINK_PS_OUTPUT), 0u);
    /* A value over 1. */
    CHECK_EQ(panel(2u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(panel(0xFFFFu, true), LINK_NACK_BAD_VALUE);
    /* Past OUTPUT: read only; past the page: range. */
    const uint16_t two[2] = { 1u, 0u };
    CHECK_EQ(prog_supply_write(&pg, LINK_PS_OUTPUT, 2u, two, true),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(prog_supply_write(&pg, LINK_PS_FLAGS, 1u, two, true),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(prog_supply_write(&pg, LINK_PS_MV, 2u, two, true),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(prog_supply_write(&pg, LINK_PS_PANEL, 2u, two, true),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(prog_supply_write(&pg, LINK_PS_OUTPUT, 1u, NULL, true),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(prog_supply_write(&pg, LINK_PS_OUTPUT, 0u, two, true), 0u);
    CHECK_EQ(reg(LINK_PS_OUTPUT), 0u);
    /* OFF without a heartbeat is taken. */
    CHECK_EQ(panel(1u, true), 0u);
    CHECK_EQ(panel(0u, false), 0u);
    CHECK_EQ(reg(LINK_PS_OUTPUT), 0u);
}

TEST_CASE(a_lost_heartbeat_or_link_switches_it_off_in_the_step)
{
    fresh();
    CHECK_EQ(panel(1u, true), 0u);
    CHECK(pass(true, false));
    CHECK(!pass(false, false));
    CHECK_EQ(reg(LINK_PS_OUTPUT), 0u);
    /* And stays off with the heartbeat back: nothing asks it on. */
    CHECK(!pass(true, false));
    /* A port's ON the same, after the panel's word again: off at the next
     * step without a heartbeat. */
    CHECK_EQ(declare(1u, true), 0u);
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, false));
    CHECK(!pass(false, false));
    CHECK(!prog_supply_port_set(&pg, true, false));
}

TEST_CASE(a_held_supply_takes_the_panels_off_and_waits_for_a_write)
{
    fresh();
    prog_supply_hold(&pg, true);
    CHECK_EQ(reg(LINK_PS_FLAGS) & LINK_PS_PORT, LINK_PS_PORT);
    /* The panel's ON under the hold: refused. */
    CHECK_EQ(panel(1u, true), LINK_NACK_BAD_VALUE);
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, false));
    supply_port_view_t v;
    prog_supply_port_view(&pg, &v);
    CHECK(v.online && v.asked_on && v.on && !v.off);
    CHECK_EQ(v.set_mv, 5000u);
    CHECK_EQ(v.vin_mv, 0u);

    /* An OFF while the port writes: OUTPUT reads 0, the pin stays on. */
    prog_supply_port_writes(&pg, true);
    const uint32_t t0 = g_ms;
    CHECK_EQ(panel(0u, true), 0u);
    CHECK_EQ(reg(LINK_PS_OUTPUT), 0u);
    CHECK(pass(true, false));
    CHECK_EQ(reg(LINK_PS_FLAGS) & LINK_PS_HELD, LINK_PS_HELD);
    prog_supply_port_view(&pg, &v);
    CHECK(v.asked_on && v.on);
    /* A second OFF does not restart the bound. */
    g_ms = t0 + 5000u;
    CHECK_EQ(panel(0u, true), 0u);
    /* One ms before the bound: on.  At it: off. */
    g_ms = t0 + SUPPLY_PAGE_WRITE_WAIT_MS - 1u;
    CHECK(pass(true, false));
    g_ms = t0 + SUPPLY_PAGE_WRITE_WAIT_MS;
    CHECK(!pass(true, false));
    CHECK_EQ(reg(LINK_PS_FLAGS) & LINK_PS_HELD, 0u);
    /* The port's ON after the panel's OFF: refused until it lets go. */
    CHECK(!prog_supply_port_set(&pg, true, true));
    prog_supply_port_writes(&pg, false);
    CHECK(!prog_supply_port_set(&pg, true, true));
    prog_supply_hold(&pg, false);
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, false));
}

TEST_CASE(a_held_off_ends_at_the_writes_end_the_ports_off_or_a_lost_beat)
{
    /* The write ends: off at that step. */
    fresh();
    prog_supply_hold(&pg, true);
    CHECK(prog_supply_port_set(&pg, true, true));
    prog_supply_port_writes(&pg, true);
    CHECK(pass(true, false));
    CHECK_EQ(panel(0u, true), 0u);
    CHECK(pass(true, false));
    prog_supply_port_writes(&pg, false);
    CHECK(!pass(true, false));

    /* The port's own OFF. */
    fresh();
    prog_supply_hold(&pg, true);
    CHECK(prog_supply_port_set(&pg, true, true));
    prog_supply_port_writes(&pg, true);
    CHECK_EQ(panel(0u, true), 0u);
    CHECK(pass(true, false));
    CHECK(prog_supply_port_set(&pg, false, true));
    CHECK(!pass(true, false));

    /* The heartbeat lost. */
    fresh();
    prog_supply_hold(&pg, true);
    CHECK(prog_supply_port_set(&pg, true, true));
    prog_supply_port_writes(&pg, true);
    CHECK_EQ(panel(0u, true), 0u);
    CHECK(pass(true, false));
    CHECK(!pass(false, false));

    /* The port lets go. */
    fresh();
    prog_supply_hold(&pg, true);
    CHECK(prog_supply_port_set(&pg, true, true));
    prog_supply_port_writes(&pg, true);
    CHECK_EQ(panel(0u, true), 0u);
    CHECK(pass(true, false));
    prog_supply_hold(&pg, false);
    CHECK(!pass(true, false));

    /* No write running: the OFF is at once. */
    fresh();
    prog_supply_hold(&pg, true);
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, false));
    CHECK_EQ(panel(0u, true), 0u);
    CHECK(!pass(true, false));
}

TEST_CASE(the_write_bound_holds_across_the_clocks_wrap)
{
    fresh();
    g_ms = 0xFFFFFFFFu - 100u;
    prog_supply_hold(&pg, true);
    CHECK(prog_supply_port_set(&pg, true, true));
    prog_supply_port_writes(&pg, true);
    CHECK(pass(true, false));
    const uint32_t t0 = g_ms;
    CHECK_EQ(panel(0u, true), 0u);
    g_ms = t0 + SUPPLY_PAGE_WRITE_WAIT_MS - 1u;    /* past the wrap */
    CHECK(g_ms < t0);
    CHECK(pass(true, false));
    g_ms = t0 + SUPPLY_PAGE_WRITE_WAIT_MS;
    CHECK(!pass(true, false));
}

TEST_CASE(an_off_waits_for_a_ports_frame_to_end)
{
    fresh();
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, true));
    /* OFF with a frame on the wire: the pin stays on until it ends. */
    CHECK(prog_supply_port_set(&pg, false, true));
    const uint32_t t0 = g_ms;
    CHECK(pass(true, true));
    g_ms = t0 + 5u;
    CHECK(pass(true, true));
    g_ms = t0 + 6u;
    CHECK(!pass(true, false));               /* the frame ended */

    /* A frame that does not end: off at the bound, to the ms. */
    fresh();
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, true));
    CHECK(prog_supply_port_set(&pg, false, true));
    const uint32_t t1 = g_ms;
    CHECK(pass(true, true));
    g_ms = t1 + SUPPLY_PAGE_OFF_WAIT_MS - 1u;
    CHECK(pass(true, true));
    g_ms = t1 + SUPPLY_PAGE_OFF_WAIT_MS;
    CHECK(!pass(true, true));

    /* The heartbeat lost with a frame out waits the same. */
    fresh();
    CHECK_EQ(panel(1u, true), 0u);
    CHECK(pass(true, true));
    CHECK(pass(false, true));
    CHECK(!pass(false, false));
}

TEST_CASE(a_port_gets_the_supply_only_after_the_panels_word)
{
    prog_supply_init(&pg, PIN, PROG_SUPPLY_CARRIER_MV);
    g_ms = 1000u;
    supply_port_view_t v;
    /* A panel before 4.32 writes no PANEL: the port's ON is refused and the
     * supply is offline to it; the panel's own OFF is taken. */
    CHECK_EQ(reg(LINK_PS_PANEL), 0u);
    CHECK(!prog_supply_port_set(&pg, true, true));
    prog_supply_port_view(&pg, &v);
    CHECK(!v.online);
    CHECK(!pass(true, false));
    CHECK_EQ(panel(0u, true), 0u);
    /* Only 1, only with a live heartbeat. */
    CHECK_EQ(declare(0u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(declare(2u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(declare(1u, false), LINK_NACK_NOT_ARMED);
    CHECK_EQ(reg(LINK_PS_PANEL), 0u);
    CHECK_EQ(declare(1u, true), 0u);
    CHECK_EQ(reg(LINK_PS_PANEL), 1u);
    prog_supply_port_view(&pg, &v);
    CHECK(v.online);
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, false));
    /* The heartbeat lost: off, and the word gone with it. */
    CHECK(!pass(false, false));
    CHECK_EQ(reg(LINK_PS_PANEL), 0u);
    CHECK(!prog_supply_port_set(&pg, true, true));
    prog_supply_port_view(&pg, &v);
    CHECK(!v.online);
    CHECK_EQ(declare(1u, true), 0u);
    CHECK(prog_supply_port_set(&pg, true, true));
    CHECK(pass(true, false));
}

int main(void)
{
    RUN(the_page_starts_off_with_its_pin_and_voltage);
    RUN(the_panels_write_is_judged_at_its_limits);
    RUN(a_lost_heartbeat_or_link_switches_it_off_in_the_step);
    RUN(a_held_supply_takes_the_panels_off_and_waits_for_a_write);
    RUN(a_held_off_ends_at_the_writes_end_the_ports_off_or_a_lost_beat);
    RUN(the_write_bound_holds_across_the_clocks_wrap);
    RUN(an_off_waits_for_a_ports_frame_to_end);
    RUN(a_port_gets_the_supply_only_after_the_panels_word);
    return test_summary("prog_supply");
}
