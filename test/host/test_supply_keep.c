/*
 * A programming port's ON that an error leaves to the port
 * (supply_link_keep(), for an IOmcu older than link 4.31): the panel's half
 * of the SUPPLY link page asks what the page held at its last read, writes
 * no OFF over it and does not refuse it again until the port lets go; and
 * nothing is left to a port the link cannot name.  Whether a port holds
 * the PD mini for the error rule (supply_link_port_holds()): the page's
 * flag as last read and a session with a port; whether a session keeps the
 * panel's ON and set points off it (supply_link_session_blocks()), on a
 * board whose ports run from the PD mini and on one with their own supply.
 * A panel restarted inside an ESC_WIRE hold: its wiring is taken, and the next
 * session's ON is not refused or switched off.
 * Against the IOmcu's half of the page.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "pdmini.h"
#include "supply_link.h"
#include "supply_page.h"

static outputs_t     o;
static supply_page_t pg;    /* the IOmcu's end */
static pdmini_t      drv;   /* its driver, told what to say */
static supply_link_t sl;    /* the panel's end */
static uint32_t      now;
static unsigned      writes;

static const supply_wiring_t k_wired = { true, 8, 9, 1u };

static void fresh(void)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, (uint64_t)1u << 3);
    supply_page_init(&pg);
    pdmini_init(&drv, NULL, 0u);
    supply_link_init(&sl);
    now = 1000u;
    writes = 0u;
}

/* The IOmcu's pass, then a read of its page by the panel. */
static void far_step_and_read(bool beat)
{
    supply_page_step(&pg, beat, supply_page_enabled(&pg) ? &drv : NULL);
    /* A wiring change its state read cleared, taken as the IOmcu
     * takes it. */
    uint16_t w[4];
    if (supply_page_wire_ready(&pg, w)) {
        supply_page_follow(&pg, supply_page_enabled(&pg) ? &drv : NULL);
        (void)supply_page_wire_write(&pg, &o);
    }
    uint16_t regs[LINK_SP_COUNT];
    supply_page_read(&pg, 0u, LINK_SP_COUNT, regs);
    supply_link_read(&sl, regs, now);
}

/* Every write owed, as the control task makes them, and a read after each. */
static void pump(bool beat)
{
    for (int k = 0; k < 8; ++k) {
        uint8_t off = 0u;
        uint8_t n = 0u;
        uint16_t regs[LINK_SP_FLAGS];
        if (supply_link_next(&sl, &off, &n, regs) == SUPPLY_LINK_W_NONE) {
            far_step_and_read(beat);
            if (supply_link_next(&sl, &off, &n, regs) == SUPPLY_LINK_W_NONE) {
                return;
            }
        }
        ++writes;
        /* What the driver says now, as the IOmcu takes it first. */
        supply_page_follow(&pg, supply_page_enabled(&pg) ? &drv : NULL);
        const uint8_t nack = supply_page_write(&pg, off, n, regs, &o, beat);
        supply_link_written(&sl, nack == 0u ? SUPPLY_LINK_ACK : (int)nack);
        far_step_and_read(beat);
        now += 50u;
    }
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0u;
    supply_page_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

/* A port's ON on a page whose wiring the settings' refused: refused, as
 * the panel's own would be.  Left to the port (an IOmcu older than 4.31),
 * no OFF is written over it and it is not refused again while the port
 * holds the page; once the port lets go it is refused and switched off.
 * Not left to it, the OFF is written at once. */
TEST_CASE(an_on_left_to_a_port_is_not_cut_until_it_lets_go)
{
    const supply_wiring_t bad = { true, 3, 9, 1u };   /* 3 is reserved */
    bool on = false;
    uint16_t mv = 0u;
    uint16_t ma = 0u;

    for (int keep = 0; keep < 2; ++keep) {
        fresh();
        supply_link_wire(&sl, &k_wired);
        pump(true);
        supply_link_wire(&sl, &bad);
        pump(true);
        CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_WIRING_REFUSED);
        CHECK(!supply_link_keep(&sl));             /* no port holds it */
        supply_page_hold(&pg, true);
        CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
        pump(true);
        CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
        CHECK(!sl.on);
        CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
        (void)supply_link_adopted(&sl, &on, &mv, &ma);
        const unsigned before = writes;
        if (keep != 0) {
            CHECK(supply_link_keep(&sl));
            CHECK(sl.on);
            CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
            CHECK(on);
            CHECK_EQ(mv, 5000u);
            CHECK_EQ(ma, 1000u);
            for (int i = 0; i < 4; ++i) {
                pump(true);
                now += 100u;
            }
            CHECK_EQ(writes, before);
            CHECK_EQ(supply_link_events(&sl), 0u);
            CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
            /* The port lets go: the settings' refusal stands again. */
            supply_page_hold(&pg, false);
            pump(true);
            CHECK(!sl.kept);
            CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
            pump(true);
        } else {
            pump(true);
        }
        CHECK(writes > before);
        CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
        CHECK(!sl.on);
    }
}

/* Nothing is left to a port that the link cannot name: NULL, no read, no
 * command known, an OFF owed by a cut. */
TEST_CASE(a_keep_needs_a_held_page_and_no_cut)
{
    CHECK(!supply_link_keep(NULL));
    fresh();
    sl.port_held = true;
    CHECK(!supply_link_keep(&sl));                 /* no read */
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 5000u, 1000u);
    pump(true);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    supply_page_hold(&pg, true);
    pump(true);
    CHECK(supply_link_port_held(&sl));
    sl.commanded = false;
    CHECK(!supply_link_keep(&sl));                 /* nothing known */
    sl.commanded = true;
    supply_link_cut(&sl);
    CHECK(!supply_link_keep(&sl));                 /* a cut owed */
    CHECK(!sl.kept);
    sl.off_owed = false;
    /* What this end held before goes: the page's command as last read,
     * with nothing owed against it. */
    sl.on = false;
    sl.page_on = false;
    sl.mv = 1234u;
    sl.ma = 321u;
    sl.page_mv = 0xFFFFu;
    sl.page_ma = 0xFFFFu;
    CHECK(supply_link_keep(&sl));
    CHECK(sl.kept);
    bool on = false;
    uint16_t mv = 0u;
    uint16_t ma = 0u;
    CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
    CHECK(on);
    CHECK_EQ(mv, 5000u);
    CHECK_EQ(ma, 1000u);
    const unsigned before = writes;
    pump(true);
    CHECK_EQ(writes, before);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    /* Gone with the link. */
    supply_link_lost(&sl);
    CHECK(!sl.kept);
}

/* The PD mini a port holds for the error rule: the page's flag as last
 * read, a programmer's session with a port, the PD mini in use.  A hold
 * counts from the read that shows it to the read that shows it gone, or to
 * the end of the session, whichever comes first; an unanswered read keeps
 * the last answer, a lost link clears it, and the model is never held. */
TEST_CASE(a_port_holds_the_pd_mini_as_the_page_and_the_session_say)
{
    fresh();
    CHECK(!supply_link_port_holds(&sl, true, true));    /* before a read */
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 7400u, 1500u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK(!supply_link_port_holds(&sl, true, true));    /* flag clear */
    supply_page_hold(&pg, true);
    CHECK(!supply_link_port_holds(&sl, true, true));    /* not read yet */
    far_step_and_read(true);
    CHECK(supply_link_port_holds(&sl, true, true));
    CHECK(!supply_link_port_holds(&sl, true, false));   /* session ended */
    CHECK(!supply_link_port_holds(&sl, false, true));   /* the model */
    CHECK(!supply_link_port_holds(NULL, true, true));
    supply_link_read(&sl, NULL, now);                   /* unanswered */
    CHECK(supply_link_port_holds(&sl, true, true));
    supply_page_hold(&pg, false);
    CHECK(supply_link_port_holds(&sl, true, true));     /* not read yet */
    CHECK(!supply_link_port_holds(&sl, true, false));   /* but ended */
    far_step_and_read(true);
    CHECK(!supply_link_port_holds(&sl, true, true));
    supply_page_hold(&pg, true);
    far_step_and_read(true);
    CHECK(supply_link_port_holds(&sl, true, true));
    supply_link_lost(&sl);
    CHECK(!supply_link_port_holds(&sl, true, true));
}

/* A session keeps the panel's ON and set points off the PD mini only where
 * the ports run from it: never without a session, never on a board with
 * the ports' own supply. */
TEST_CASE(a_session_blocks_the_pd_mini_only_where_the_ports_use_it)
{
    CHECK(supply_link_session_blocks(true, false));
    CHECK(!supply_link_session_blocks(true, true));     /* the carrier */
    CHECK(!supply_link_session_blocks(false, false));
    CHECK(!supply_link_session_blocks(false, true));
}

/*
 * A panel restarted inside an ESC_WIRE hold: its OFF stands to the port's let-go,
 * and the wiring it writes at its start is taken, so the next session's ON is the
 * port's and no OFF of the panel's follows it.
 */
TEST_CASE(a_panel_restarted_in_a_hold_leaves_the_next_session_alone)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(true);
    supply_page_wire_freeze(&pg, true);
    supply_page_hold(&pg, true);
    far_step_and_read(true);
    supply_link_init(&sl);
    supply_link_wire(&sl, &k_wired);
    pump(true);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), 0u);
    CHECK(!supply_page_port_set(&pg, true, 5000u, 1000u, true));
    /* CLOSE, then OPEN and ENTER. */
    supply_page_wire_freeze(&pg, false);
    supply_page_hold(&pg, false);
    pump(true);
    supply_page_wire_freeze(&pg, true);
    supply_page_hold(&pg, true);
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    const unsigned before = writes;

    pump(true);
    pump(true);
    pump(true);
    CHECK_EQ(writes, before);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK_EQ(supply_link_events(&sl), 0u);
}

int main(void)
{
    RUN(an_on_left_to_a_port_is_not_cut_until_it_lets_go);
    RUN(a_keep_needs_a_held_page_and_no_cut);
    RUN(a_port_holds_the_pd_mini_as_the_page_and_the_session_say);
    RUN(a_session_blocks_the_pd_mini_only_where_the_ports_use_it);
    RUN(a_panel_restarted_in_a_hold_leaves_the_next_session_alone);
    return test_summary("supply_keep");
}
