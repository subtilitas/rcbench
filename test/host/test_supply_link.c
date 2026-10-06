/*
 * The panel's half of the SUPPLY link page, against the coprocessor's half.
 *
 * Under test: an OFF first, alone; the wiring only once a read shows the
 * page's output off and nothing on it that may be on; a refused wiring not
 * written again until it changes; an ON refused, or let go at the far end,
 * reported once and not asked for again; a coprocessor that started again
 * between two reads written again from the start; the readings, stale ones
 * and a module the page does not drive taken as not answering.
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

static outputs_t     o;
static supply_page_t pg;    /* the coprocessor's end */
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

/* The coprocessor's pass, then a read of its page by the panel. */
static void far_step_and_read(bool beat)
{
    supply_page_step(&pg, beat, supply_page_enabled(&pg) ? &drv : NULL);
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
    supply_link_written(&sl, SUPPLY_LINK_ACK);
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
    supply_link_wire(&sl, &bad);
    pump(true);
    CHECK_EQ(supply_link_events(&sl), SUPPLY_LINK_EV_WIRING_REFUSED);
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

/* A coprocessor that started again between two reads holds nothing: its
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

int main(void)
{
    RUN(an_off_then_the_wiring_then_the_command);
    RUN(the_wiring_waits_for_the_output_to_be_off);
    RUN(refused_wiring_is_not_written_again);
    RUN(an_unwired_supply_refuses_an_on);
    RUN(an_on_without_a_heartbeat_is_refused);
    RUN(an_on_the_far_end_let_go_is_lost);
    RUN(a_restarted_far_end_is_written_again);
    RUN(unanswered_writes_and_a_lost_link);
    RUN(the_state_is_what_the_page_says);
    RUN(stuck_is_reported_once);
    RUN(a_found_rate_is_reported_once);
    return test_summary("supply_link");
}
