/*
 * The SUPPLY page's hold of the panel's OFF for a programming port's write
 * (shared/outputs/supply_page.c, protocol 4.31).
 *
 * Under test: an OFF the panel writes while a port holds the page and
 * writes to the servo's memory reads 0 at once and keeps the driver and the
 * port's view on until the write ends; with no write, or no port, it
 * reaches the driver at once; the hold ends at SUPPLY_PAGE_WRITE_WAIT_MS
 * after the OFF to the millisecond, also across the wrap of the clock at
 * 2^32ms, and a second OFF does not restart it; a lost heartbeat or a
 * silent link, the port's own OFF and the port letting go each end it at
 * once; after the panel's OFF of a held page the port's ON is refused until
 * the port lets go, also when the OFF came with the output already off, and
 * an OFF with no port holding refuses nothing later.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "pdmini.h"
#include "supply_page.h"

#define MV 5000u
#define MA 1000u

static outputs_t     o;
static supply_page_t pg;
static pdmini_t      drv;
static uint32_t      g_ms;

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xFFFFu;

    supply_page_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

/* The panel's frame of OUTPUT, SET_MV and SET_MA, heartbeat live. */
static uint8_t panel(uint16_t on)
{
    const uint16_t r[3] = { on, MV, MA };

    return supply_page_write(&pg, LINK_SP_OUTPUT, 3u, r, &o, true);
}

/* One pass at g_ms as the IOmcu runs it: the port's frame and write, the
 * step, the module reading back what it was asked; then the clock. */
static void pass_as(bool writing, bool beat)
{
    supply_page_port_frame(&pg, false, g_ms);
    supply_page_port_writes(&pg, writing);
    supply_page_step(&pg, beat, &drv);
    drv.st.output = drv.want_output;
    drv.st.set_mv = drv.want_mv;
    drv.st.set_ma = drv.want_ma;
    g_ms++;
}

static void pass(bool writing)
{
    pass_as(writing, true);
}

static supply_port_view_t view(void)
{
    supply_port_view_t v;

    supply_page_port_view(&pg, &v);
    return v;
}

/* Wired and on at MV and MA from the panel at @p t0, a port holding the
 * page, the module read on. */
static void held_on_at(uint32_t t0, bool writing)
{
    static const uint16_t k_wiring[4] = { 1u, 8u, 9u, 1u };

    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, (uint64_t)1u << 3);
    supply_page_init(&pg);
    pdmini_init(&drv, NULL, 0u);
    drv.answered = true;
    drv.state_known = true;
    drv.st.online = true;
    g_ms = t0;
    CHECK_EQ(supply_page_write(&pg, LINK_SP_ENABLE, 4u, k_wiring, &o, true),
             0u);
    CHECK_EQ(panel(1u), 0u);
    supply_page_hold(&pg, true);
    pass(writing);
    pass(writing);
    CHECK(drv.want_output);
    CHECK(view().on);
}

TEST_CASE(an_off_during_a_write_waits_for_the_writes_end)
{
    held_on_at(1000u, true);
    CHECK_EQ(panel(0u), 0u);
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    CHECK(supply_page_off_held(&pg));
    for (unsigned i = 0; i < 3000u; ++i) {
        pass(true);
        CHECK(drv.want_output);
        CHECK_EQ(drv.want_mv, MV);
        CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
        /* The port sees its supply as it was: it does not cut itself. */
        CHECK(view().asked_on);
        CHECK(view().on);
    }
    /* The write ends: off in that pass, and the port sees it asked off. */
    pass(false);
    CHECK(!drv.want_output);
    CHECK(!supply_page_off_held(&pg));
    CHECK(!view().asked_on);
    /* A write after it does not bring it back. */
    pass(true);
    CHECK(!drv.want_output);
}

TEST_CASE(an_off_with_no_write_or_no_port_is_at_once)
{
    held_on_at(50u, false);
    CHECK_EQ(panel(0u), 0u);
    CHECK(!supply_page_off_held(&pg));
    pass(false);
    CHECK(!drv.want_output);
    CHECK(!view().asked_on);

    /* No port holds the page: a write some port reports holds nothing. */
    held_on_at(50u, true);
    supply_page_hold(&pg, false);
    CHECK_EQ(panel(0u), 0u);
    CHECK(!supply_page_off_held(&pg));
    pass(true);
    CHECK(!drv.want_output);
}

/* The bound, to the millisecond, from the pass clock of the OFF; the
 * second start puts the clock's wrap at 2^32ms inside the wait. */
TEST_CASE(a_held_off_reaches_the_module_at_the_bound)
{
    static const uint32_t k_t0[2] = { 1000u, 0xFFFFFFFFu - 4000u };

    for (unsigned w = 0; w < 2u; ++w) {
        uint32_t off_at;

        held_on_at(k_t0[w], true);
        off_at = g_ms - 1u;           /* the clock of the last pass */
        CHECK_EQ(panel(0u), 0u);
        while ((uint32_t)(g_ms - off_at) < SUPPLY_PAGE_WRITE_WAIT_MS) {
            pass(true);
            CHECK(drv.want_output);
            if ((uint32_t)(g_ms - off_at) == 5000u) {
                /* A second OFF keeps the first one's time. */
                CHECK_EQ(panel(0u), 0u);
            }
        }
        CHECK_EQ((uint32_t)(g_ms - off_at), SUPPLY_PAGE_WRITE_WAIT_MS);
        pass(true);
        CHECK(!drv.want_output);
        CHECK(!view().asked_on);
    }
}

TEST_CASE(a_lost_heartbeat_or_link_during_a_held_off_cuts_at_once)
{
    held_on_at(10u, true);
    CHECK_EQ(panel(0u), 0u);
    pass(true);
    CHECK(drv.want_output);
    /* safety_gate_supply_ok() false: the heartbeat or the link gone. */
    pass_as(true, false);
    CHECK(!drv.want_output);
    CHECK(!supply_page_off_held(&pg));
    CHECK(!view().asked_on);
    /* Back: the hold is not taken up again. */
    pass(true);
    CHECK(!drv.want_output);
}

TEST_CASE(the_ports_own_off_or_letting_go_ends_a_held_off)
{
    held_on_at(10u, true);
    CHECK_EQ(panel(0u), 0u);
    pass(true);
    CHECK(drv.want_output);
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, true));
    CHECK(!view().asked_on);
    pass(true);
    CHECK(!drv.want_output);

    held_on_at(10u, true);
    CHECK_EQ(panel(0u), 0u);
    pass(true);
    supply_page_hold(&pg, false);
    pass(true);
    CHECK(!drv.want_output);
}

/* After the panel's OFF the port's ON is refused until it lets go: a KST
 * power cycle's ON, a Hitec POWER's. */
TEST_CASE(the_panels_off_refuses_the_ports_on_until_it_lets_go)
{
    held_on_at(10u, false);
    CHECK_EQ(panel(0u), 0u);
    pass(false);
    CHECK(!supply_page_port_set(&pg, true, MV, MA, true));
    CHECK_EQ(reg(LINK_SP_OUTPUT), 0u);
    pass(false);
    CHECK(!drv.want_output);
    /* An OFF stays taken. */
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, true));
    /* Let go, and held again by the next session: it takes the ON. */
    supply_page_hold(&pg, false);
    supply_page_hold(&pg, true);
    CHECK(supply_page_port_set(&pg, true, MV, MA, true));
    pass(false);
    CHECK(drv.want_output);

    /* The port has the output off itself, in its power cycle's off time,
     * when the panel's OFF comes: its ON afterwards is refused too. */
    held_on_at(10u, false);
    CHECK(supply_page_port_set(&pg, false, 0u, 0u, true));
    pass(false);
    CHECK(!drv.want_output);
    CHECK_EQ(panel(0u), 0u);
    CHECK(!supply_page_port_set(&pg, true, MV, MA, true));
    pass(false);
    CHECK(!drv.want_output);

    /* An OFF while no port holds the page refuses no later port. */
    held_on_at(10u, false);
    supply_page_hold(&pg, false);
    CHECK_EQ(panel(0u), 0u);
    supply_page_hold(&pg, true);
    CHECK(supply_page_port_set(&pg, true, MV, MA, true));

    supply_page_port_writes(NULL, true);
    CHECK(!supply_page_off_held(NULL));
}

int main(void)
{
    RUN(an_off_during_a_write_waits_for_the_writes_end);
    RUN(an_off_with_no_write_or_no_port_is_at_once);
    RUN(a_held_off_reaches_the_module_at_the_bound);
    RUN(a_lost_heartbeat_or_link_during_a_held_off_cuts_at_once);
    RUN(the_ports_own_off_or_letting_go_ends_a_held_off);
    RUN(the_panels_off_refuses_the_ports_on_until_it_lets_go);
    return test_summary("supply_hold");
}
