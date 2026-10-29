/*
 * The panel's half of the SUPPLY link page, against the IOmcu's half.
 *
 * Under test: an OFF first, alone; the wiring only once a read shows the
 * page's output off and nothing on it that may be on; a refused wiring not
 * written again until it changes; an ON refused, or let go at the far end,
 * reported once and not asked for again; an IOmcu that started again
 * between two reads written again from the start; the readings, stale ones
 * and a module the page does not drive taken as not answering; a wiring
 * change the far end holds for a state read waited for, and its refusal
 * said; an output the far end switched off for a sagging input; an edit
 * followed between two writes keeping an ON off the old pins; a reading
 * count that stands for SUPPLY_LINK_BLIND_MS behind a page that answers
 * taken as readings stopped, across the tick wrap; the supply free of a
 * programming port and its output off only on a read that shows both.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "pdmini.h"
#include "supply_link.h"
#include "supply_page.h"
#include "tick_wrap.h"

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

TEST_CASE(an_off_then_the_wiring_then_the_command)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 12000u, 1500u);
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t regs[LINK_SP_FLAGS];
    /* Nothing on the page known: an OFF before the ON asked for. */
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_OFF);
    CHECK_EQ(off, LINK_SP_OUTPUT);
    CHECK_EQ(n, 1u);
    CHECK_EQ(regs[0], 0u);
    supply_link_written(&sl, SUPPLY_LINK_ACK);
    /* The wiring waits for a read that shows the output off. */
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_NONE);
    far_step_and_read(true);
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_WIRING);
    CHECK_EQ(off, LINK_SP_ENABLE);
    CHECK_EQ(n, 4u);
    CHECK_EQ(supply_page_write(&pg, off, n, regs, &o, true), 0u);
    supply_link_written(&sl, SUPPLY_LINK_ACK);
    /* Acknowledged, and the page's once a read shows it held. */
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_NONE);
    far_step_and_read(true);
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_COMMAND);
    CHECK_EQ(off, LINK_SP_OUTPUT);
    CHECK_EQ(n, 3u);
    CHECK_EQ(regs[0], 1u);
    CHECK_EQ(regs[1], 12000u);
    CHECK_EQ(regs[2], 1500u);
    supply_link_written(&sl, SUPPLY_LINK_ACK);
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_NONE);

    /* The same against the far end, in one pump. */
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 9000u, 800u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_ENABLE), 1u);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);
    CHECK_EQ(reg(LINK_SP_RX_PIN), 9u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 9000u);
    CHECK_EQ(supply_link_events(&sl), 0u);
    CHECK(supply_link_settled(&sl));
    const unsigned before = writes;
    pump(true);
    CHECK_EQ(writes, before);                   /* nothing owed: nothing sent */
    supply_link_command(&sl, true, 9100u, 800u);
    CHECK(!supply_link_settled(&sl));
    CHECK(!supply_link_settled(NULL));
}

/* An OFF goes before a wiring change, and the wiring waits while the far
 * end says the output may still be on. */
TEST_CASE(the_wiring_waits_for_the_output_to_be_off)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    drv.st.output = true;                       /* the module came on */
    const supply_wiring_t moved = { true, 10, 11, 1u };
    supply_link_wire(&sl, &moved);
    supply_link_command(&sl, false, 5000u, 500u);
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t regs[LINK_SP_FLAGS];
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_OFF);
    supply_link_written(&sl, (int)supply_page_write(&pg, off, n, regs, &o,
                                                    true));
    far_step_and_read(true);                    /* still on: LIVE */
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_NONE);
    drv.st.output = false;                      /* off now */
    far_step_and_read(true);
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_WIRING);
    supply_link_written(&sl, (int)supply_page_write(&pg, off, n, regs, &o,
                                                    true));
    CHECK_EQ(reg(LINK_SP_TX_PIN), 10u);
    CHECK_EQ(supply_link_events(&sl), 0u);
}

/* Pins the far end will not take are refused once and not written again
 * until the settings change; an ON meanwhile is refused here. */
TEST_CASE(refused_wiring_is_not_written_again)
{
    fresh();
    const supply_wiring_t bad = { true, 3, 9, 1u };   /* 3 is reserved */
    int tx = 0;
    int rx = 0;
    /* No read yet: not known. */
    CHECK(!supply_link_held_pins(&sl, &tx, &rx));
    CHECK_EQ(tx, -1);
    CHECK_EQ(rx, -1);
    supply_link_wire(&sl, &bad);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_WIRING_REFUSED);
    /* Refused, the page holding nothing: known, no pins. */
    CHECK(supply_link_held_pins(&sl, &tx, &rx));
    CHECK_EQ(tx, -1);
    CHECK_EQ(rx, -1);
    CHECK(!supply_link_held_pins(NULL, &tx, &rx));
    CHECK_EQ(tx, -1);
    CHECK(supply_link_held_pins(&sl, NULL, NULL));
    const unsigned before = writes;
    pump(true);
    CHECK_EQ(writes, before);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
    CHECK(!sl.on);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    supply_link_wire(&sl, &k_wired);
    pump(true);
    CHECK_EQ(reg(LINK_SP_ENABLE), 1u);
    CHECK_EQ(supply_link_events(&sl), 0u);

    /* Good wiring replaced by refused wiring: no ON to the old pins. */
    supply_link_wire(&sl, &bad);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_WIRING_REFUSED);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
}

/* The bench's walk: the PD mini on TX GP19 and RX GP18, the sensor bus on
 * GP16 and GP17.  RX stepped to 17, which the sensor bus holds: refused,
 * once, and the refusal stands; RX back to 18: the page holds the wiring
 * asked once a read shows it, and the refusal is over.  One refused write
 * in all. */
TEST_CASE(a_refused_pin_stepped_back_is_over_once_the_page_holds_it)
{
    fresh();
    outputs_reserve_pins(&o, ((uint64_t)1u << 3) | ((uint64_t)1u << 16)
                                 | ((uint64_t)1u << 17));
    const supply_wiring_t bench = { true, 19, 18, SUPPLY_LINK_BAUD_AUTO };
    supply_link_wire(&sl, &bench);
    pump(true);
    CHECK_EQ(reg(LINK_SP_RX_PIN), 18u);
    CHECK(!supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_REFUSED));
    drv.answered    = true;                 /* the module has answered */
    drv.state_known = true;
    unsigned nacks = 0u;
    const supply_wiring_t rx17 = { true, 19, 17, SUPPLY_LINK_BAUD_AUTO };
    supply_link_wire(&sl, &rx17);
    for (int k = 0; k < 8; ++k) {
        uint8_t off = 0u;
        uint8_t n = 0u;
        uint16_t regs[LINK_SP_FLAGS];
        if (supply_link_next(&sl, &off, &n, regs) == SUPPLY_LINK_W_NONE) {
            far_step_and_read(true);
            continue;
        }
        const uint8_t nack = supply_page_write(&pg, off, n, regs, &o, true);
        nacks += (nack != 0u) ? 1u : 0u;
        supply_link_written(&sl, nack == 0u ? SUPPLY_LINK_ACK : (int)nack);
    }
    CHECK_EQ(nacks, 1u);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_WIRING_REFUSED);
    CHECK(supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_REFUSED));
    CHECK_EQ(reg(LINK_SP_RX_PIN), 18u);     /* the page kept its wiring */
    /* Back to 18: the page holds it still, so the refusal is over at once
     * and nothing is written. */
    supply_link_wire(&sl, &bench);
    CHECK(!supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_REFUSED));
    const unsigned before = writes;
    pump(true);
    CHECK_EQ(writes, before);
    CHECK(!supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_REFUSED));
    CHECK(!supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_LIVE));
    CHECK_EQ(supply_link_events(&sl), 0u);
    /* Other events are left to the band; a lost link leaves it standing. */
    CHECK(supply_link_event_stands(&sl, SUPPLY_LINK_EV_ON_REFUSED));
    supply_link_lost(&sl);
    CHECK(supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_REFUSED));
    CHECK(!supply_link_event_stands(NULL, SUPPLY_LINK_EV_WIRING_REFUSED));
}

/* A wiring change refused on its state read, for an output that may be on:
 * the refusal stands until other pins are written and held. */
TEST_CASE(a_wiring_refused_on_its_read_stands_until_new_pins_are_held)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(true);
    drv.answered    = true;
    drv.state_known = true;
    const supply_wiring_t moved = { true, 10, 11, 1u };
    supply_link_wire(&sl, &moved);
    pump(true);
    drv.check = (uint8_t)PDMINI_CHECK_NOT_OFF;
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_WIRING_LIVE);
    CHECK(supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_LIVE));
    const supply_wiring_t again = { true, 12, 13, 1u };
    supply_link_wire(&sl, &again);
    pump(true);                             /* written, held for a read */
    CHECK(supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_LIVE));
    drv.check = (uint8_t)PDMINI_CHECK_OFF;
    pump(true);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 12u);
    CHECK(!supply_link_event_stands(&sl, SUPPLY_LINK_EV_WIRING_LIVE));
}

/* Disabled, or both pins on one, is written as disabled; an ON to it is
 * refused here. */
TEST_CASE(an_unwired_supply_refuses_an_on)
{
    fresh();
    const supply_wiring_t one_pin = { true, 8, 8, 1u };
    supply_link_wire(&sl, &one_pin);
    pump(true);
    CHECK_EQ(reg(LINK_SP_ENABLE), 0u);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
}

/* No heartbeat at the far end: the ON is refused, reported once, and not
 * asked for again. */
TEST_CASE(an_on_without_a_heartbeat_is_refused)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(false);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(false);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
    CHECK(!sl.on);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    pump(false);
    CHECK_EQ(supply_link_events(&sl), 0u);
}

/* An ON refused while the far end reports its failsafe names the
 * failsafe, the one cause only an ARM clears; any other refusal says
 * refused.  The module's trip comes first, then an ON let go. */
TEST_CASE(an_on_refused_in_the_far_ends_failsafe_names_it)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(false);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(false);
    const uint16_t ev = supply_link_events(&sl);
    CHECK_EQ(ev, SUPPLY_LINK_EV_ON_REFUSED);
    CHECK_EQ(supply_link_on_ended(ev, LINK_FAULT_HEARTBEAT),
             SUPPLY_LINK_SAY_FAILSAFE);
    CHECK_EQ(supply_link_on_ended(ev, LINK_FAULT_LINK_SILENT),
             SUPPLY_LINK_SAY_FAILSAFE);
    CHECK_EQ(supply_link_on_ended(ev, LINK_FAULT_LINK_SILENT
                                          | LINK_FAULT_OVERCURRENT),
             SUPPLY_LINK_SAY_FAILSAFE);
    CHECK_EQ(supply_link_on_ended(ev, 0u), SUPPLY_LINK_SAY_REFUSED);
    /* Every other fault bit is no failsafe. */
    for (unsigned b = 0; b < 16u; ++b) {
        const uint16_t f = (uint16_t)(1u << b);
        if ((f & (LINK_FAULT_LINK_SILENT | LINK_FAULT_HEARTBEAT)) == 0u) {
            CHECK_EQ(supply_link_on_ended(ev, f), SUPPLY_LINK_SAY_REFUSED);
        }
    }
    CHECK_EQ(supply_link_on_ended(SUPPLY_LINK_EV_ON_REFUSED
                                      | SUPPLY_LINK_EV_ON_LOST,
                                  LINK_FAULT_HEARTBEAT),
             SUPPLY_LINK_SAY_LOST);
    CHECK_EQ(supply_link_on_ended(SUPPLY_LINK_EV_ON_LOST
                                      | SUPPLY_LINK_EV_TRIPPED, 0u),
             SUPPLY_LINK_SAY_TRIPPED);
    CHECK_EQ(supply_link_on_ended(0xFFFFu & ~(unsigned)(
                                      SUPPLY_LINK_EV_ON_REFUSED
                                      | SUPPLY_LINK_EV_ON_LOST
                                      | SUPPLY_LINK_EV_TRIPPED),
                                  0xFFFFu),
             SUPPLY_LINK_SAY_NONE);
}

/* An ON the far end let go -- its heartbeat stopped -- is lost, reported
 * once, and not switched back on. */
TEST_CASE(an_on_the_far_end_let_go_is_lost)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    far_step_and_read(false);                   /* the heartbeat stopped */
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_LOST);
    CHECK(!sl.on);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(supply_link_events(&sl), 0u);
}

/* A programming port commands the supply at the far end: what the page
 * then holds is what the panel asks, nothing is written back, no ON is
 * reported lost, and the caller is handed the command once. */
TEST_CASE(a_programming_ports_command_is_adopted_and_not_fought)
{
    bool on = false;
    uint16_t mv = 0u;
    uint16_t ma = 0u;

    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 7400u, 1500u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK(!supply_link_adopted(&sl, &on, &mv, &ma));
    CHECK(!supply_link_port_held(&sl));
    CHECK(!supply_link_port_held(NULL));
    /* The port takes the supply and lowers it for an entry. */
    supply_page_hold(&pg, true);
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    const unsigned before = writes;
    pump(true);
    CHECK(supply_link_port_held(&sl));
    CHECK_EQ(writes, before);
    CHECK_EQ(supply_link_events(&sl), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 5000u);
    CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
    CHECK(on);
    CHECK_EQ(mv, 5000u);
    CHECK_EQ(ma, 1000u);
    CHECK(!supply_link_adopted(&sl, &on, &mv, &ma));     /* once */
    /* It switches the supply off for a power cycle: no ON is lost. */
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, true));
    pump(true);
    CHECK_EQ(writes, before);
    CHECK_EQ(supply_link_events(&sl), 0u);
    CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
    CHECK(!on);
    CHECK_EQ(mv, 5000u);
    /* And on again, and lets go in the same pass: the read after the hold
     * ends is still the port's. */
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    supply_page_hold(&pg, false);
    pump(true);
    CHECK_EQ(writes, before);
    CHECK_EQ(supply_link_events(&sl), 0u);
    CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
    CHECK(on);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK(!supply_link_port_held(&sl));
    /* Not held any more: an ON the far end lets go is lost as ever. */
    far_step_and_read(false);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_LOST);
    CHECK(!supply_link_adopted(&sl, &on, &mv, &ma));
    /* Nothing without all four. */
    CHECK(!supply_link_adopted(NULL, &on, &mv, &ma));
    CHECK(!supply_link_adopted(&sl, NULL, &mv, &ma));
    CHECK(!supply_link_adopted(&sl, &on, NULL, &ma));
    CHECK(!supply_link_adopted(&sl, &on, &mv, NULL));
}

/* The supply is the port's until a read shows no port holding it and the
 * output off, asked and read: a Hitec LEAVE leaves it on, and the panel's
 * OFF that follows reads off only once the module does. */
TEST_CASE(the_supply_is_free_and_off_only_with_no_port_and_the_output_off)
{
    CHECK(!supply_link_port_free_off(NULL));
    fresh();
    CHECK(!supply_link_port_free_off(&sl));             /* no read yet */
    supply_link_wire(&sl, &k_wired);
    pump(true);
    CHECK(supply_link_port_free_off(&sl));
    supply_link_command(&sl, true, 7400u, 1500u);
    drv.st.output = true;
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    CHECK(!supply_link_port_free_off(&sl));
    /* The port takes it; its cycle's off time is still the port's. */
    supply_page_hold(&pg, true);
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    pump(true);
    CHECK(!supply_link_port_free_off(&sl));
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, true));
    drv.st.output = false;
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK(!supply_link_port_free_off(&sl));
    /* On again and let go, as a classic LEAVE ends: on, not free. */
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    supply_page_hold(&pg, false);
    drv.st.output = true;
    pump(true);
    CHECK(!supply_link_port_held(&sl));
    CHECK(!supply_link_port_free_off(&sl));
    /* The panel's OFF: asked off, the module still on. */
    supply_link_command(&sl, false, 5000u, 1000u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK(!supply_link_port_free_off(&sl));
    /* Read off, with an OFF still owed to the module: possibly on. */
    drv.st.output = false;
    drv.off_owed = true;
    far_step_and_read(true);
    CHECK(!supply_link_port_free_off(&sl));
    drv.off_owed = false;
    far_step_and_read(true);
    CHECK(supply_link_port_free_off(&sl));
    /* A port that takes it again, output off: not free. */
    supply_page_hold(&pg, true);
    far_step_and_read(true);
    CHECK(!supply_link_port_free_off(&sl));
}

/* What the panel asks while the port holds the supply is written as ever:
 * an OFF is taken, an ON is refused and said as any refused ON. */
TEST_CASE(a_command_under_a_programming_port_is_off_or_refused)
{
    bool on = false;
    uint16_t mv = 0u;
    uint16_t ma = 0u;

    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, false, 5000u, 1000u);
    pump(true);
    supply_page_hold(&pg, true);
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    pump(true);
    CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
    CHECK(on);
    /* The operator switches it off. */
    supply_link_command(&sl, false, 5000u, 1000u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK(!supply_link_adopted(&sl, &on, &mv, &ma));
    /* And on again, which the page refuses under the port. */
    supply_link_command(&sl, true, 5000u, 1000u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
    CHECK(!sl.on);
    /* After the operator's OFF the port's own ON is refused until it lets
     * go (4.31); a new hold takes it. */
    CHECK(!supply_page_port_set(&pg, true, 5000u, 1000u, true));
    supply_page_hold(&pg, false);
    supply_page_hold(&pg, true);
    /* A lost link forgets a command taken and not handed out. */
    CHECK(supply_page_port_set(&pg, true, 5000u, 1000u, true));
    far_step_and_read(true);
    supply_link_lost(&sl);
    CHECK(!supply_link_adopted(&sl, &on, &mv, &ma));
}

/* Set points the page refuses under a programming port leave the output
 * on: the page's values are adopted, nothing is said, no OFF is written.
 * The same when the port took the supply after this end's last read. */
TEST_CASE(set_points_refused_under_a_port_leave_the_output_on)
{
    bool on = false;
    uint16_t mv = 0u;
    uint16_t ma = 0u;

    for (int seen = 0; seen < 2; ++seen) {
        fresh();
        supply_link_wire(&sl, &k_wired);
        supply_link_command(&sl, true, 5000u, 1000u);
        pump(true);
        CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
        supply_page_hold(&pg, true);
        if (seen != 0) {
            far_step_and_read(true);        /* the bit is read first */
        }
        /* A raise, a lowering, a current limit: each refused, each time
         * the output stays and this end holds the page's values again. */
        static const uint16_t k_ask[3][2] = {
            { 8000u, 1000u }, { 4000u, 1000u }, { 5000u, 3000u },
        };
        for (unsigned k = 0; k < 3u; ++k) {
            supply_link_command(&sl, true, k_ask[k][0], k_ask[k][1]);
            pump(true);
            CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
            CHECK_EQ(reg(LINK_SP_SET_MV), 5000u);
            CHECK_EQ(reg(LINK_SP_SET_MA), 1000u);
            CHECK_EQ(supply_link_events(&sl), 0u);
            CHECK(sl.on);
            CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
            CHECK(on);
            CHECK_EQ(mv, 5000u);
            CHECK_EQ(ma, 1000u);
            CHECK(supply_link_settled(&sl));
        }
        /* Between the refusal and its read nothing is written but an OFF. */
        supply_link_command(&sl, true, 8000u, 1000u);
        uint8_t off = 0u;
        uint8_t n = 0u;
        uint16_t regs[LINK_SP_FLAGS];
        CHECK_EQ(supply_link_next(&sl, &off, &n, regs),
                 SUPPLY_LINK_W_COMMAND);
        supply_link_written(&sl, (int)LINK_NACK_BAD_VALUE);
        CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_NONE);
        supply_link_command(&sl, false, 8000u, 1000u);
        CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_OFF);
        supply_link_written(&sl, SUPPLY_LINK_NO_ANSWER);
        supply_link_command(&sl, true, 8000u, 1000u);
        pump(true);
        CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
        CHECK_EQ(supply_link_events(&sl), 0u);
        CHECK(supply_link_adopted(&sl, &on, &mv, &ma));
        CHECK_EQ(mv, 5000u);
    }

    /* No port: set points refused on an output that is on end the ON as
     * any refused ON does, said once, after the read. */
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 5000u, 1000u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    supply_link_command(&sl, true, 6000u, 1000u);
    pump(false);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
    CHECK(!sl.on);
    CHECK(!supply_link_adopted(&sl, &on, &mv, &ma));
    /* A lost link forgets a refusal not judged. */
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 5000u, 1000u);
    pump(true);
    supply_link_command(&sl, true, 6000u, 1000u);
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t regs[LINK_SP_FLAGS];
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_COMMAND);
    supply_link_written(&sl, (int)LINK_NACK_BAD_VALUE);
    CHECK(sl.judge);
    supply_link_lost(&sl);
    CHECK(!sl.judge);
}

/* An IOmcu that started again between two reads holds nothing: its
 * page is written again from the start, and the ON it held is lost. */
TEST_CASE(a_restarted_far_end_is_written_again)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    supply_page_init(&pg);                      /* started again */
    pdmini_init(&drv, NULL, 0u);
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_LOST);
    pump(true);
    CHECK_EQ(reg(LINK_SP_ENABLE), 1u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK_EQ(reg(LINK_SP_SET_MV), 5000u);

    /* And set points changed under it are put back. */
    const uint16_t other[3] = { 0u, 7000u, 500u };
    CHECK_EQ(supply_page_write(&pg, LINK_SP_OUTPUT, 3u, other, &o, true), 0u);
    far_step_and_read(true);
    pump(true);
    CHECK_EQ(reg(LINK_SP_SET_MV), 5000u);
}

/* A write or a read that is not answered is tried again; the link going
 * makes everything owed and ends the panel's ON. */
TEST_CASE(unanswered_writes_and_a_lost_link)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t regs[LINK_SP_FLAGS];
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_OFF);
    supply_link_written(&sl, SUPPLY_LINK_NO_ANSWER);
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_OFF);
    supply_link_read(&sl, NULL, now);           /* a read not answered */
    CHECK(!supply_link_read_due(&sl, now + 50u));
    CHECK(supply_link_read_due(&sl, now + SUPPLY_LINK_READ_MS));

    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    supply_link_lost(&sl);
    CHECK(!sl.on);
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_OFF);

    /* Set points over the page's bounds are brought to them. */
    supply_link_command(&sl, false, 25000u, 5000u);
    CHECK_EQ(sl.mv, 20000u);
    CHECK_EQ(sl.ma, 3000u);
    supply_link_command(&sl, false, 0u, 10u);
    CHECK_EQ(sl.mv, 1000u);
    CHECK_EQ(sl.ma, 50u);
    /* And set points refused with the output off are not asked again. */
    supply_link_written(&sl, SUPPLY_LINK_ACK);
    supply_link_read(&sl, NULL, now);
    sl.read_any = true;
    sl.wired = true;
    sl.page = k_wired;
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_COMMAND);
    supply_link_written(&sl, (int)LINK_NACK_BAD_VALUE);
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_NONE);
    CHECK_EQ(supply_link_events(&sl), 0u);
}

/* The readings, the mode and the set points read back; stale, or from a
 * page that drives nothing, is not answering. */
TEST_CASE(the_state_is_what_the_page_says)
{
    fresh();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    supply_link_state(&sl, now, &st);
    CHECK(!st.online);
    CHECK_EQ(st.ok, 0u);

    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 9000u, 800u);
    pump(true);
    drv.st.online = true;
    drv.st.output = true;
    drv.st.mode   = PDMINI_MODE_CC;
    drv.st.v_mv   = 8990u;
    drv.st.i_ma   = 800u;
    drv.st.set_mv = 9000u;
    drv.st.set_ma = 800u;
    far_step_and_read(true);
    st.output = true;
    supply_link_state(&sl, now, &st);
    CHECK(st.online);
    CHECK(st.output);                           /* the caller's, left */
    CHECK_EQ(st.mode, SUPPLY_MODE_CC);
    CHECK_EQ(st.ok, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    CHECK_NEAR(st.v, 8.99f, 0.001f);
    CHECK_NEAR(st.p, 7.192f, 0.001f);
    CHECK_NEAR(st.set_v, 9.0f, 0.001f);
    CHECK_NEAR(st.set_i, 0.8f, 0.001f);

    /* Which reading it is, and when the panel had it: the module's count,
     * and the time of the read that brought it. */
    drv.st.samples = 70001u;                    /* 4465 mod 65536 */
    now += 100u;
    far_step_and_read(true);
    supply_link_state(&sl, now + 30u, &st);
    CHECK_EQ(st.samples, (uint16_t)(70001u & 0xFFFFu));
    CHECK_EQ(st.taken_ms, now);

    /* A display read on the IOmcu that is slow or fails leaves
     * SAMPLES where it was, and the current on the page with it: later
     * page reads are the same reading, stamped when it first came. */
    const uint32_t first = now;
    drv.st.i_ma = 1234u;                        /* not a reading: no count */
    for (int k = 0; k < 5; ++k) {
        now += 100u;
        far_step_and_read(true);
    }
    supply_link_state(&sl, now, &st);
    CHECK(st.online);                           /* the page answers */
    CHECK_EQ(st.samples, (uint16_t)(70001u & 0xFFFFu));
    CHECK_EQ(st.taken_ms, first);
    CHECK_EQ(st.ok, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    /* Past SUPPLY_LINK_STALE_MS with the count standing still, the page
     * still answering and ONLINE: the values are the last ones and no
     * longer a reading, so no total or trip takes them as new. */
    for (int k = 0; k < 11; ++k) {
        now += 100u;
        far_step_and_read(true);
    }
    supply_link_state(&sl, now, &st);
    CHECK(st.online);
    CHECK((uint32_t)(now - first) >= SUPPLY_LINK_STALE_MS);
    CHECK_EQ(st.ok, 0u);
    CHECK_EQ(st.taken_ms, first);
    /* And three readings between two page reads: the count says so. */
    drv.st.samples = 70004u;
    now += 100u;
    far_step_and_read(true);
    supply_link_state(&sl, now, &st);
    CHECK_EQ((uint16_t)(st.samples - (uint16_t)(70001u & 0xFFFFu)), 3u);
    CHECK_EQ(st.taken_ms, now);

    drv.st.mode = PDMINI_MODE_NORMAL;
    far_step_and_read(true);
    supply_link_state(&sl, now, &st);
    CHECK_EQ(st.mode, SUPPLY_MODE_CV);
    drv.st.output = false;                      /* the module reads off */
    far_step_and_read(true);
    supply_link_state(&sl, now, &st);
    CHECK_EQ(st.mode, SUPPLY_MODE_OFF);

    supply_link_state(&sl, now + SUPPLY_LINK_STALE_MS, &st);
    CHECK(!st.online);
    CHECK_EQ(st.ok, 0u);
    drv.st.online = false;
    far_step_and_read(true);
    supply_link_state(&sl, now, &st);
    CHECK(!st.online);
}

/* The module stuck, or its set points, is reported once as it starts. */
TEST_CASE(stuck_is_reported_once)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(true);
    drv.st.stuck = true;
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_STUCK);
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), 0u);
    drv.st.set_stuck = true;
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_SET_STUCK);
    /* And the module switching its output off ends the ON here. */
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    drv.st.tripped = true;
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_TRIPPED);
    CHECK(!sl.on);
    CHECK_EQ(supply_link_next(NULL, NULL, NULL, NULL), SUPPLY_LINK_W_NONE);
    CHECK_EQ(supply_link_events(NULL), 0u);
}

/* With AUTO, the rate the far end found is reported once. */
TEST_CASE(a_found_rate_is_reported_once)
{
    fresh();
    const supply_wiring_t automatic = { true, 8, 9, SUPPLY_LINK_BAUD_AUTO };
    supply_link_wire(&sl, &automatic);
    pump(true);
    CHECK_EQ(reg(LINK_SP_BAUD), SUPPLY_LINK_BAUD_AUTO);
    CHECK_EQ(supply_link_events(&sl), 0u);
    drv.answered = true;
    ++drv.who_failed;
    (void)supply_page_rate(&pg, &drv);
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_BAUD_FOUND);
    CHECK_EQ(sl.regs[LINK_SP_BAUD_FOUND], 1u);
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), 0u);
    /* New wiring scans again; the same rate found is said again. */
    const supply_wiring_t moved = { true, 10, 11, SUPPLY_LINK_BAUD_AUTO };
    supply_link_wire(&sl, &moved);
    pdmini_init(&drv, NULL, 0u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_BAUD_FOUND), SUPPLY_LINK_BAUD_AUTO);
    (void)supply_link_events(&sl);
    drv.answered = true;
    (void)supply_page_rate(&pg, &drv);
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_BAUD_FOUND);
}

/* A restart asked with the output on: the OFF first, then RESET, alone,
 * and the far end passes it to its driver. */
TEST_CASE(a_restart_follows_the_off)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    supply_link_command(&sl, false, 5000u, 500u);
    supply_link_reset(&sl);
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t regs[LINK_SP_FLAGS];
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_OFF);
    supply_link_written(&sl, (int)supply_page_write(&pg, off, n, regs, &o,
                                                    true));
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_RESET);
    CHECK_EQ(off, LINK_SP_RESET);
    CHECK_EQ(n, 1u);
    supply_link_written(&sl, (int)supply_page_write(&pg, off, n, regs, &o,
                                                    true));
    far_step_and_read(true);
    CHECK(drv.reset_owed);
    CHECK(supply_link_next(&sl, &off, &n, regs) != SUPPLY_LINK_W_RESET);
}

/* The far end holds a wiring change for a state read: nothing more is
 * written while it waits -- an ON asked meanwhile neither -- and the
 * wiring is the page's once a read shows it held. */
TEST_CASE(a_wiring_change_held_for_a_read_is_waited_for)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(true);
    CHECK(supply_link_settled(&sl));
    drv.answered    = true;                     /* a module has answered */
    drv.state_known = true;                     /* and was read off */
    const supply_wiring_t moved = { true, 10, 11, 1u };
    supply_link_wire(&sl, &moved);
    pump(true);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_WAIT) != 0u);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);
    CHECK_EQ(drv.check, (uint8_t)PDMINI_CHECK_ASKED);
    CHECK(!sl.wired);
    const unsigned before = writes;
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    pump(true);
    CHECK_EQ(writes, before);                   /* not written again */
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);          /* nor an ON */
    drv.check = (uint8_t)PDMINI_CHECK_OFF;      /* the read shows it off */
    pump(true);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 10u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);          /* and the ON follows */
    CHECK_EQ(supply_link_events(&sl), 0u);
}

/* The read shows the output on, or fails: the change is refused, said once
 * as a refusal for an output that may be on, and not written again until
 * the settings change; an ON meanwhile is refused here. */
TEST_CASE(a_wiring_change_refused_on_its_read_is_said)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(true);
    drv.answered    = true;
    drv.state_known = true;
    const supply_wiring_t moved = { true, 10, 11, 1u };
    supply_link_wire(&sl, &moved);
    pump(true);
    drv.check = (uint8_t)PDMINI_CHECK_NOT_OFF;
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_WIRING_LIVE);
    CHECK(sl.refused);
    CHECK_EQ(reg(LINK_SP_TX_PIN), 8u);
    /* Refused: the page keeps the wiring it held, by its last read. */
    int tx = -1;
    int rx = -1;
    CHECK(supply_link_held_pins(&sl, &tx, &rx));
    CHECK_EQ(tx, 8);
    CHECK_EQ(rx, (int)reg(LINK_SP_RX_PIN));
    CHECK(rx >= 0);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_REFUSED) != 0u);
    const unsigned before = writes;
    pump(true);
    CHECK_EQ(writes, before);
    supply_link_command(&sl, true, 5000u, 500u);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_ON_REFUSED);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    /* The settings change: written again, and the refusal clears. */
    const supply_wiring_t again = { true, 12, 13, 1u };
    supply_link_wire(&sl, &again);
    pump(true);
    CHECK_EQ(reg(LINK_SP_FLAGS) & LINK_SP_WIRE_REFUSED, 0u);
    CHECK((reg(LINK_SP_FLAGS) & LINK_SP_WIRE_WAIT) != 0u);
    /* The link lost: nothing on the page is known. */
    supply_link_lost(&sl);
    CHECK(!supply_link_held_pins(&sl, &tx, &rx));
    CHECK_EQ(tx, -1);
}

/* The far end switched the output off for a sagging input: the ON is over
 * here, said once with the input and set point of that read, and the OFF
 * written. */
TEST_CASE(an_output_switched_off_for_a_sag_is_said_with_its_numbers)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 6000u, 1000u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 1u);
    drv.st.online = true;
    drv.st.sagged = true;
    drv.st.vin_mv = 6180u;
    drv.st.set_mv = 6000u;
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_SAGGED);
    CHECK(!sl.on);
    uint16_t vin = 0u;
    uint16_t set = 0u;
    supply_link_sag(&sl, &vin, &set);
    CHECK_EQ(vin, 6180u);
    CHECK_EQ(set, 6000u);
    drv.st.vin_mv = 12000u;                     /* a later reading */
    far_step_and_read(true);
    CHECK_EQ(supply_link_events(&sl), 0u);      /* said once */
    supply_link_sag(&sl, &vin, &set);
    CHECK_EQ(vin, 6180u);
    pump(true);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    supply_link_sag(NULL, &vin, &set);
    supply_link_sag(&sl, NULL, &set);
}

/* An edit published between two writes, followed straight before the
 * second: the ON taken before it does not reach the page with the old
 * pins.  Not followed, it would. */
TEST_CASE(an_edit_followed_before_a_write_keeps_an_on_off_the_old_pins)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    pump(true);
    supply_link_command(&sl, true, 5000u, 500u);
    uint8_t off = 0u;
    uint8_t n = 0u;
    uint16_t regs[LINK_SP_FLAGS];
    CHECK_EQ(supply_link_next(&sl, &off, &n, regs), SUPPLY_LINK_W_COMMAND);
    CHECK_EQ(regs[0], 1u);                      /* the ON, unfollowed */
    /* What the follow does on an edit: the ON off, the new pins asked. */
    const supply_wiring_t moved = { true, 10, 11, 1u };
    supply_link_command(&sl, false, 5000u, 500u);
    supply_link_wire(&sl, &moved);
    const supply_link_write_t w = supply_link_next(&sl, &off, &n, regs);
    CHECK(w != SUPPLY_LINK_W_COMMAND || regs[0] == 0u);
    CHECK_EQ(w, SUPPLY_LINK_W_WIRING);
    CHECK_EQ(regs[1], 10u);
}

/* The edit count and the word, each a move; neither, none. */
TEST_CASE(a_wiring_moves_on_its_count_or_its_word)
{
    supply_link_follow_t f = { 0 };
    CHECK(supply_link_wiring_moved(&f, 1u, 0x1000000u));   /* first look */
    CHECK(!supply_link_wiring_moved(&f, 1u, 0x1000000u));
    CHECK(supply_link_wiring_moved(&f, 2u, 0x1000000u));   /* counted, word
                                                              not yet stored,
                                                              or undone */
    CHECK(supply_link_wiring_moved(&f, 2u, 0x1090A01u));   /* the word */
    CHECK(!supply_link_wiring_moved(&f, 2u, 0x1090A01u));
    CHECK(!supply_link_wiring_moved(NULL, 3u, 0u));
}

/* A read is asked for again SUPPLY_LINK_READ_MS after the last one was
 * asked for, wherever the clock is. */
static void a_read_is_due_at_the_interval(uint32_t t0)
{
    fresh();
    CHECK(supply_link_read_due(&sl, t0));       /* none asked for yet */
    supply_link_read(&sl, NULL, t0);            /* asked, not answered */
    CHECK(!supply_link_read_due(&sl, t0 + 1u));
    CHECK(!supply_link_read_due(&sl, t0 + SUPPLY_LINK_READ_MS - 1u));
    CHECK(supply_link_read_due(&sl, t0 + SUPPLY_LINK_READ_MS));
}

TEST_CASE(a_read_is_due_at_its_interval_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(a_read_is_due_at_the_interval,
                                  SUPPLY_LINK_READ_MS / 2u);
}

/* The supply answers for SUPPLY_LINK_STALE_MS less 1ms after a read and
 * is not answering at SUPPLY_LINK_STALE_MS, wherever the clock is. */
static void a_read_is_fresh_until_the_stale_time(uint32_t t0)
{
    fresh();
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 9000u, 800u);
    pump(true);
    drv.st.online = true;
    drv.st.output = true;
    drv.st.v_mv   = 8990u;
    drv.st.i_ma   = 800u;
    now = t0;
    drv.st.samples = (uint16_t)(drv.st.samples + 1u);   /* a new reading */
    far_step_and_read(true);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    supply_link_state(&sl, t0 + 1u, &st);
    CHECK(st.online);
    supply_link_state(&sl, t0 + SUPPLY_LINK_STALE_MS - 1u, &st);
    CHECK(st.online);
    CHECK_EQ(st.ok, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    supply_link_state(&sl, t0 + SUPPLY_LINK_STALE_MS, &st);
    CHECK(!st.online);
    CHECK_EQ(st.ok, 0u);
}

TEST_CASE(a_read_goes_stale_at_its_time_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(a_read_is_fresh_until_the_stale_time,
                                  SUPPLY_LINK_STALE_MS / 2u);
}

/* The page answers and the module reads ONLINE, but its reading count
 * stands: not stopped 1ms before SUPPLY_LINK_BLIND_MS, stopped at it and
 * after it, wherever the clock is; a count that moves again starts the
 * time over. */
static void readings_stop_at_the_blind_time(uint32_t t0)
{
    fresh();
    CHECK(!supply_link_blind(&sl, t0 + 10u * SUPPLY_LINK_BLIND_MS));
    CHECK(!supply_link_blind(NULL, t0));
    supply_link_wire(&sl, &k_wired);
    supply_link_command(&sl, true, 9000u, 800u);
    pump(true);
    drv.st.online = true;
    drv.st.output = true;
    now = t0;
    drv.st.samples = drv.st.samples + 1u;       /* a new reading */
    far_step_and_read(true);
    CHECK(!supply_link_blind(&sl, t0));
    CHECK(!supply_link_blind(&sl, t0 + 1u));
    /* The page read every 100ms, the count standing.  The last reading
     * is a reading 1ms before SUPPLY_LINK_STALE_MS and none at it, with
     * the page read 100ms before both. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    while ((uint32_t)(now - t0) < SUPPLY_LINK_BLIND_MS - 100u) {
        now += 100u;
        far_step_and_read(true);
        if ((uint32_t)(now - t0) == SUPPLY_LINK_STALE_MS - 100u) {
            supply_link_state(&sl, t0 + SUPPLY_LINK_STALE_MS - 1u, &st);
            CHECK(st.online);
            CHECK_EQ(st.ok, SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
            supply_link_state(&sl, t0 + SUPPLY_LINK_STALE_MS, &st);
            CHECK(st.online);
            CHECK_EQ(st.ok, 0u);
        }
    }
    supply_link_state(&sl, t0 + SUPPLY_LINK_BLIND_MS - 1u, &st);
    CHECK(st.online);                           /* the page answers */
    CHECK_EQ(st.ok, 0u);                        /* no reading to trip on */
    CHECK(!supply_link_blind(&sl, t0 + SUPPLY_LINK_BLIND_MS - 1u));
    CHECK(supply_link_blind(&sl, t0 + SUPPLY_LINK_BLIND_MS));
    CHECK(supply_link_blind(&sl, t0 + SUPPLY_LINK_BLIND_MS + 1u));
    supply_link_state(&sl, t0 + SUPPLY_LINK_BLIND_MS, &st);
    CHECK(st.online);
    /* The count moves again: the time starts over from that read. */
    now = t0 + SUPPLY_LINK_BLIND_MS;
    drv.st.samples = drv.st.samples + 1u;
    far_step_and_read(true);
    CHECK(!supply_link_blind(&sl, now));
    CHECK(!supply_link_blind(&sl, now + SUPPLY_LINK_BLIND_MS - 1u));
    CHECK(supply_link_blind(&sl, now + SUPPLY_LINK_BLIND_MS));
}

TEST_CASE(readings_stop_at_their_time_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(readings_stop_at_the_blind_time,
                                  SUPPLY_LINK_BLIND_MS / 2u);
}

int main(void)
{
    RUN(an_off_then_the_wiring_then_the_command);
    RUN(the_wiring_waits_for_the_output_to_be_off);
    RUN(refused_wiring_is_not_written_again);
    RUN(a_refused_pin_stepped_back_is_over_once_the_page_holds_it);
    RUN(a_wiring_refused_on_its_read_stands_until_new_pins_are_held);
    RUN(an_unwired_supply_refuses_an_on);
    RUN(an_on_without_a_heartbeat_is_refused);
    RUN(an_on_refused_in_the_far_ends_failsafe_names_it);
    RUN(an_on_the_far_end_let_go_is_lost);
    RUN(a_programming_ports_command_is_adopted_and_not_fought);
    RUN(the_supply_is_free_and_off_only_with_no_port_and_the_output_off);
    RUN(a_command_under_a_programming_port_is_off_or_refused);
    RUN(set_points_refused_under_a_port_leave_the_output_on);
    RUN(a_restarted_far_end_is_written_again);
    RUN(unanswered_writes_and_a_lost_link);
    RUN(the_state_is_what_the_page_says);
    RUN(stuck_is_reported_once);
    RUN(a_found_rate_is_reported_once);
    RUN(a_restart_follows_the_off);
    RUN(a_wiring_change_held_for_a_read_is_waited_for);
    RUN(a_wiring_change_refused_on_its_read_is_said);
    RUN(an_output_switched_off_for_a_sag_is_said_with_its_numbers);
    RUN(an_edit_followed_before_a_write_keeps_an_on_off_the_old_pins);
    RUN(a_wiring_moves_on_its_count_or_its_word);
    RUN(a_read_is_due_at_its_interval_across_the_tick_wrap);
    RUN(a_read_goes_stale_at_its_time_across_the_tick_wrap);
    RUN(readings_stop_at_their_time_across_the_tick_wrap);
    return test_summary("supply_link");
}
